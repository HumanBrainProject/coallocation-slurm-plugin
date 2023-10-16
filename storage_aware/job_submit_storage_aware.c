/*****************************************************************************\
 *  job_submit_storage_aware.c - Decides between two shared storage tiers
 *  at submission time, based on the scheduling mechanism proposed in
 *  http://dx.doi.org/10.1109/CCGRID.2019.00046.
 *****************************************************************************
 *  Copyright (c) 2019-2021 Technical University of Darmstadt, Darmstadt, Germany
 *  Authors: Taylan Özden, Varsha Dubay and Hamid Mohammadi Fard
 *
 *  This job submission plugin is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
\*****************************************************************************/

#include <stdlib.h>
#include <math.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  SLURM uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "auth" for SLURM authentication) and <method> is a
 * description of how this plugin satisfies that application.  SLURM will
 * only load authentication plugins if the plugin_type string has a prefix
 * of "auth/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */

const char plugin_name[] = "Job submit storage_aware plugin";
const char plugin_type[] = "job_submit/storage_aware";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

#define EMPTY 0

// specifications of the low-performance storage
// adjust to reflect system parameters
#define LPS_PATH "/home/vagrant/lps"
#define LPS_BANDWIDTH 12

// specifications of the high-performance storage
// adjust to reflect system parameters
#define HPS_PATH "/home/vagrant/hps"
#define HPS_BANDWIDTH 192
#define HPS_SPACE 5120

static pthread_mutex_t pthread_lock = PTHREAD_MUTEX_INITIALIZER;

/**
 * Allocates the specified storage tier
 * Note: This function currently suggests a storage tier by setting the environment variable "SLURM_STORAGE_TIER" and
 * needs to be adjusted on the productive system
 * @param job_desc (IN/OUT) the job description
 * @param hps (IN) whether the job should be allocated on the HPS
 * @param expected_hps_wait_time (IN) the expected HPS waiting time in seconds (disregarded when hps == false)
 */
static void _allocate_storage_tier(job_desc_msg_t* job_desc, bool hps, time_t expected_hps_wait_time) {
	info("_allocate_storage_tier start");
	job_desc->environment = (char**) xrealloc(job_desc->environment, (job_desc->env_size + 2) * sizeof(char*));
	if (hps) {
		job_desc->environment[job_desc->env_size] = xstrdup_printf("%s=%s", "SLURM_STORAGE_TIER", HPS_PATH);
		// when submitting to HPS, adjust time limit to reflect higher bandwidth (but at least one minute)
		double time_limit = ceil((double) job_desc->time_limit / ((double) HPS_BANDWIDTH / (double) LPS_BANDWIDTH));
		job_desc->time_limit = MAX(time_limit + (expected_hps_wait_time / 60.0), 1);
	} else {
		job_desc->environment[job_desc->env_size] = xstrdup_printf("%s=%s", "SLURM_STORAGE_TIER", LPS_PATH);
		// remove burst buffer specification if LPS is used
		xfree(job_desc->burst_buffer);
	}
	job_desc->env_size++;
	info("_allocate_storage_tier end");
}

/**
 * Builds a list of running, pending and suspended jobs
 * @param job_list (OUT) the List object to be filled
 * @return the job_info_msg_t pointer to call slurm_free_job_info_msg when the list is no longer needed
 */
static job_info_msg_t* _build_job_list(List job_list) {
	info("_build_job_list start");
	job_info_msg_t* job_info_msg = NULL;
	slurm_load_jobs((time_t) NULL, &job_info_msg, SHOW_ALL);

	// processing through jobs array and save to list only running jobs
	job_info_t* job_info;
	for (int i = 0; i < job_info_msg->record_count; i++) {
		if (job_info_msg->job_array[i].job_state == JOB_RUNNING ||
			job_info_msg->job_array[i].job_state == JOB_PENDING ||
			job_info_msg->job_array[i].job_state == JOB_SUSPENDED) {
			job_info = job_info_msg->job_array + i;
			list_append(job_list, (void*) job_info);
		}
	}
	info("_build_job_list end");
	return job_info_msg;
}

/**
 * Extracts burst buffer information from a given string
 * @param burst_buffer (IN) the burst buffer specification
 * @param capacity (OUT) the extracted required capacity
 * @param io (OUT) the extracted required intermediate data (read/write access)
 */
static void _extract_bb_info(char* burst_buffer, uint32_t* capacity, uint32_t* io) {

	info("_extract_bb_info start");

	char* burst_buffer_spec;
	char* token;
	char* context;
	char* ptr;

	burst_buffer_spec = xstrdup(burst_buffer);

	token = strtok_r(burst_buffer_spec, " ", &context);
	ptr = xstrstr(token, "capacity=");
	*capacity = strtoul(ptr + strlen("capacity="), NULL, 10);
	token = strtok_r(NULL, " ", &context);
	ptr = xstrstr(token, "io=");
	*io = strtoul(ptr + strlen("io="), NULL, 10);

	xfree(burst_buffer_spec);

	info("_extract_bb_info end");
}

/**
 * Estimates the overall waiting time by accumulating time limits of submitted jobs with a simplistic approach
 * @return the estimated maximum overall waiting time in seconds
 */
static double _get_wait_time() {

	info("_get_wait_time start");
	time_t now = time(NULL);

	List job_list = list_create(NULL);
	job_info_msg_t* job_info_msg = _build_job_list(job_list);

	// if no jobs are in the queue, return 0 seconds
	if (slurm_list_count(job_list) == EMPTY) {
		slurm_free_job_info_msg(job_info_msg);
		FREE_NULL_LIST(job_list);
		return 0;
	}

	job_info_t* job_info;
	time_t latest = 0;
	// find latest end time which is already estimated
	while ((job_info = (job_info_t*) list_pop(job_list))) {
		latest = MAX(job_info->end_time, latest);
	}
	slurm_free_job_info_msg(job_info_msg);
	FREE_NULL_LIST(job_list);

	job_list = list_create(NULL);
	job_info_msg = _build_job_list(job_list);
	// find jobs without end time estimation
	while ((job_info = (job_info_t*) list_pop(job_list))) {
		if (job_info->end_time == 0) {
			// add wall time of current job to latest end time
			latest = latest + job_info->time_limit * 60;
		}
	}
	slurm_free_job_info_msg(job_info_msg);
	FREE_NULL_LIST(job_list);

	info("_get_wait_time end");
	// return difference to current time in seconds
	return difftime(latest, now);
}

/**
 * Estimates the HPS waiting by accumulating time limits of submitted jobs on the HPS with a simplistic approach
 * @param requested_hps_space the requested HPS storage space to consider when submitting the job on the HPS
 * @return the estimated maximum HPS waiting time in seconds
 */
static double _get_hps_wait_time(uint32_t requested_hps_space) {

	info("_get_hps_wait_time start");
	if (requested_hps_space > HPS_SPACE) {
		return UINT32_MAX;
	}

	List job_list = list_create(NULL);
	job_info_msg_t* job_info_msg = _build_job_list(job_list);

	// if no jobs are in the queue, return 0 seconds
	if (slurm_list_count(job_list) == EMPTY) {
		slurm_free_job_info_msg(job_info_msg);
		FREE_NULL_LIST(job_list);
		return 0;
	}

	job_info_t* job_info;
	uint32_t remaining_hps_space = HPS_SPACE;
	uint32_t hps_wait_time = 0;
	uint32_t job_space;
	uint32_t io;

	time_t now = time(NULL);
	// accumulate time limits of jobs allocated on the HPS
	while ((job_info = (job_info_t*) list_pop(job_list))) {
		if (job_info->burst_buffer) {
			_extract_bb_info(job_info->burst_buffer, &job_space, &io);
			remaining_hps_space = remaining_hps_space >= job_space ? remaining_hps_space - job_space : 0;
			if (job_info->job_state == JOB_RUNNING) {
				hps_wait_time += difftime(job_info->end_time, now);
			} else {
				hps_wait_time += job_info->time_limit * 60;
			}
		}
	}
	slurm_free_job_info_msg(job_info_msg);
	FREE_NULL_LIST(job_list);

	info("_get_hps_wait_time end");
	// return 0 if the remaining HPS space is sufficient, expected wait time otherwise
	return requested_hps_space < remaining_hps_space ? 0 : hps_wait_time;
}

extern int init(void) {
	return SLURM_SUCCESS;
}

extern int job_submit(job_desc_msg_t* job_desc, uint32_t submit_uid, char** err_msg) {

	info("job_submit start");
	slurm_mutex_lock(&pthread_lock);

	if (!job_desc->burst_buffer) {
		info("No burst buffer specification provided");
		info("Submitting job to LPS");
		_allocate_storage_tier(job_desc, false, UINT32_MAX);
		slurm_mutex_unlock(&pthread_lock);
		return SLURM_SUCCESS;
	}

	uint32_t lps_time = 0;
	uint32_t hps_time = 0;

	uint32_t job_space;
	uint32_t io;

	_extract_bb_info(job_desc->burst_buffer, &job_space, &io);

	// currently, the expected overall wait time can be disregarded as it is added to both estimated times (LPS and HPS)
	// however, the estimated time is considered nonetheless as its calculation is subject to future modifications
	uint32_t expected_wait_time = _get_wait_time();
	info("Expected wait time (overall): %u seconds", expected_wait_time);
	uint32_t expected_hps_wait_time = _get_hps_wait_time(job_space);
	info("Expected HPS wait time: %u seconds", expected_hps_wait_time);


	lps_time = expected_wait_time + (job_desc->time_limit * 60) + ((double) io / LPS_BANDWIDTH);
	info("Estimated job time (LPS): %u", lps_time);

	if (expected_hps_wait_time == UINT32_MAX) {
		hps_time = UINT32_MAX;
	} else {
		hps_time = expected_wait_time + expected_hps_wait_time + (job_desc->time_limit * 60) +
				   ((double) io / HPS_BANDWIDTH);
	}
	info("Estimated job time (HPS): %u", hps_time);

	if (hps_time < lps_time) {
		info("HPS time reasonable");
		info("Submitting job to HPS");
		_allocate_storage_tier(job_desc, true, expected_hps_wait_time);
	} else {
		info("HPS time unreasonable");
		info("Submitting job to LPS");
		_allocate_storage_tier(job_desc, false, UINT32_MAX);
	}

	slurm_mutex_unlock(&pthread_lock);
	info("job_submit end");
	return SLURM_SUCCESS;
}

extern int job_modify(job_desc_msg_t* job_desc, struct job_record* job_ptr, uint32_t submit_uid) {
	// currently not implemented
	return SLURM_SUCCESS;
}

extern int fini(void) {
	return SLURM_SUCCESS;
}