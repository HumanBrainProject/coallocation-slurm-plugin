/*****************************************************************************\
 *  job_submit_all_partitions.c - Set storage partition to job by submit request
 *  to all partitions in a cluster.
 *****************************************************************************
 *  Copyright (c) 2019-2020 Technische Universitaet Darmstadt, Darmstadt, Germany
 *  Author: Hamid Mohammadi Fard, email: fard@cs.tu-darmstadt.de
 *
 *  This source code is a modified version of the source code of job_submit_all_partitions.c
 *  which has been publihed in 31 JAN 2020, by the following URL:
 *  https://github.com/SchedMD/slurm/blob/master/src/plugins/job_submit/all_partitions/job_submit_all_partitions.c
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

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/statvfs.h>
#include <argp.h>
#include <libgen.h>
#include <time.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/job_scheduler.h"

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
const char plugin_name[]       	= "Job submit all_partitions plugin";
const char plugin_type[]       	= "job_submit/all_partitions";
//const uint32_t plugin_version   = SLURM_VERSION_NUMBER;

#define EMPTY_QUEUE 0
#define KBYTES_PER_GBYTES ( 1024 * 1024 )
#define WAIT_MIN 0
#define WAIT_MAX 120
#define DEFAULT_LPS_SPEED 80
#define DEFAULT_HPS_SPEED 500
#define DEFAULT_JOB_SPACE 2000
#define DEFAULT_LPS_PATH "/tmp"
#define DEFAULT_HPS_PATH "/tmp"
#define HPS_PARTITION_NAME "hps"
#define LPS_PARTITION_NAME "lps"

// defining string constants for storages names
static pthread_mutex_t pthread_lock = PTHREAD_MUTEX_INITIALIZER;

// argparse processing helper variables
static char doc[] = "Some documentation string, useless in this plugin...";
static char args_doc[] = "ARG1 ARG2 ARG3 ARG4 ARG5 ARG6";

// defining all command line arguments in this structure
static struct argp_option options[] = {
        { "lps-path", 'l', "LPS_PATH", 0, "Path to low speed storage", 0 },
        { "hps-path", 'h', "HPS_PATH", 0, "Path to high speed storage", 0 },
        { "lps-speed", 's', "LPS_SPEED", 0, "Low speed storage speed", 0 },
        { "hps-speed", 'f', "HPS_SPEED", 0, "High speed storage speed", 0  },
        { "wait-time", 'w', "WAIT_TIME", 0, "Wait time for high speed storage", 0  },
        { "job-space", 'S', "JOB_SPACE", 0, "Storage space consumable by a job", 0 },
        { 0 }
};

// structure to store job submission command line arguments
struct arguments {
        char *lps_path;
        char *hps_path;
        double lps_speed;
        double hps_speed;
        int wait_time;
        int job_space;
};

// processing submission command line arguments
static error_t parse_opt(int key, char *arg, struct argp_state *state) {
        struct arguments *arguments = state->input;
	
        switch (key) {
                case 'l':
                        arguments->lps_path = arg;
                        break;
                case 'h':
                        arguments->hps_path = arg;
                        break;
                case 's':
                        arguments->lps_speed = atof(arg);
                        break;
                case 'f':
                        arguments->hps_speed = atof(arg);
                        break;
                case 'w':
                        arguments->wait_time = atoi(arg);
                        break;
                case 'S':
                        arguments->job_space = atoi(arg);
                        break;
                default:
                        return ARGP_ERR_UNKNOWN;
        }
        return 0;
}

// the main argparse structure
static struct argp argp = { options, parse_opt, args_doc, doc };


extern void _read_job_arguments(struct job_descriptor *job_desc, struct arguments *job_arguments) {
	argp_parse(&argp, job_desc->argc, job_desc->argv, 0, 0, job_arguments);
}


extern int _get_storage_free_space(const char *path) {
	int storage_free_size = 0;
	struct statvfs stat;
	
	// trying to get statvfs structure, if not successful then return 0
	if (0 != statvfs(path, &stat)) {
		return storage_free_size;
	}
	
	// calculating free space block_size * free_block_counter
	storage_free_size = stat.f_bsize * stat.f_bavail / KBYTES_PER_GBYTES;
	return storage_free_size;	
}

/*
 *  setting new paths for job work_dir, std_err and std_out attributes
 */
extern void _set_job_working_dir(struct job_descriptor *job_desc, const char *work_dir) {
        char *new_job_stderr = (char*) malloc (strlen(basename(job_desc->std_err)) + strlen(work_dir) + 1);
        char *new_job_stdout = (char*) malloc (strlen(basename(job_desc->std_out)) + strlen(work_dir) + 1);

	// making new std_err absolute filename
        strcpy(new_job_stderr, work_dir);
        strcat(new_job_stderr, "/");
        strcat(new_job_stderr, basename(job_desc->std_err));

	// making new std_out absolute filename
        strcpy(new_job_stdout, work_dir);
        strcat(new_job_stdout, "/");
        strcat(new_job_stdout, basename(job_desc->std_out));
	
	// saving new job attributes
	job_desc->work_dir = xstrdup(work_dir);
	job_desc->std_err = xstrdup(new_job_stderr);
	job_desc->std_out = xstrdup(new_job_stdout);

	free(new_job_stderr);
	free(new_job_stdout);
}

/* 
 * setting the new job partition
 */
extern void _set_job_partition(struct job_descriptor *job_desc, const char *partition) {
	job_desc->partition = xstrdup(partition);
}

/* 
 * setting job submission parameters to the job argv array
 * we need to do this to save job arguments for subsequent job
 * processing during hps storage free space calculation
 */
extern void _set_job_arguments(struct job_descriptor *job_desc, struct arguments job_arguments) {
	char *buf;
	char num[100];
	char *command;
	// saving submission command to local variable
	command = xstrdup(job_desc->argv[0]);
	// resizing job argv array
	job_desc->argc = 8;
	job_desc->argv = calloc(job_desc->argc, sizeof(char*));
	// restoring submission command to argv array from the local variable
	job_desc->argv[0] = xstrdup(command);
	
	// setting lps_path attribute to the job argv array
	buf = (char*) calloc (strlen("--lps-path=") + strlen(job_arguments.lps_path) + 1, sizeof(char));
        strcpy(buf, "--lps-path=");
	strcat(buf, job_arguments.lps_path);
	job_desc->argv[1] = xstrdup(buf);
	free(buf);

	// setting hps_path attribute to the job argv array
	buf = (char*) calloc (strlen("--hps-path=") + strlen(job_arguments.hps_path) + 1, sizeof(char));
        strcpy(buf, "--hps-path=");
	strcat(buf, job_arguments.hps_path);
	job_desc->argv[2] = xstrdup(buf);
	free(buf);

	// setting lps_speed attribute to the job argv array
	buf = (char*) calloc(255, sizeof(char));
        strcpy(buf, "--lps-speed=");
 	sprintf(num, "%f", job_arguments.lps_speed);
	strcat(buf, num);
	job_desc->argv[3] = xstrdup(buf);
	free(buf);

	// setting hps_speed attribute to the job argv array
	buf = (char*) calloc(255, sizeof(char));
        strcpy(buf, "--hps-speed=");
 	sprintf(num, "%f", job_arguments.hps_speed);
	strcat(buf, num);
	job_desc->argv[4] = xstrdup(buf);
	free(buf);
	
	// setting wait_time attribute to the job argv array
	buf = (char*) calloc(255, sizeof(char));
        strcpy(buf, "--wait-time=");
 	sprintf(num, "%d", job_arguments.wait_time);
	strcat(buf, num);
	job_desc->argv[5] = xstrdup(buf);
	free(buf);

	// setting job_space attribute to the job argv array
	buf = (char*) calloc(255, sizeof(char));
        strcpy(buf, "--job_space=");
 	sprintf(num, "%d", job_arguments.job_space);
	strcat(buf, num);
	job_desc->argv[6] = xstrdup(buf);
	free(buf);
}


/*
 * extracting job_space value from the job command line
 */
extern uint32_t _get_job_storage_space(char *command) {
	uint32_t job_space_int = 0;
	char *job_space_str;
	char *anchor = "--job_space=";
	
	if (NULL != (job_space_str = strstr(command, anchor))) {
		job_space_int = atoi(job_space_str + strlen(anchor));
	}
	return job_space_int;
}

/*
 * copy submit file to the selected storage path
*/
extern void _copy_submit_file_to_storage(struct job_descriptor *job_desc, char *path) {
        int c;
        FILE *input_fd;
        FILE *output_fd;

	char *new_submit_filename = (char*) calloc(strlen(basename(job_desc->argv[0])) + strlen(path) + 2, sizeof(char));
	strcpy(new_submit_filename, path);
	strcat(new_submit_filename, "/");
	strcat(new_submit_filename, basename(job_desc->argv[0]));
                
        input_fd = fopen(job_desc->argv[0], "r");
        if (input_fd == NULL) {
                info("can't open input file %s, %d", job_desc->argv[0], errno);
		return;
	}

        output_fd = fopen(new_submit_filename, "w");
        if (output_fd == NULL) {
		info("can't open output file %s, %d", new_submit_filename, errno);
		fclose(input_fd);
		return;
        }

        while((c = fgetc(input_fd)) != EOF) {
	        fputc(c, output_fd);
        }	
        
	fclose(input_fd);
        fclose(output_fd);
	free(new_submit_filename);
}

extern int init(void) {
	return SLURM_SUCCESS;
}

extern int fini(void) {
	return SLURM_SUCCESS;
}

/*
 * submiting a new job
 */
extern int job_submit(struct job_descriptor *job_desc,
		      uint32_t submit_uid,
		      char **err_msg) {
        info("start job_submission");
	List job_list;
	int job_lps_time = 0;
	int job_hps_time = 0;	
	int hps_storage_free_space = 0;	
	job_info_t *job_info;

	// defining default values to command line job arguments
        struct arguments job_arguments = { DEFAULT_LPS_PATH,
			DEFAULT_HPS_PATH,
			DEFAULT_LPS_SPEED,
			DEFAULT_HPS_SPEED,
			_generate_wait_time(),
			DEFAULT_JOB_SPACE };
	
	slurm_mutex_lock(&pthread_lock);

	// reading and merging command line job arguments with default values
	_read_job_arguments(job_desc, &job_arguments);

	// printing job command line arguments to slurmctld.log file
        info("lps_path: %s", job_arguments.lps_path);
        info("hps_path: %s", job_arguments.hps_path);
        info("lps_speed: %f", job_arguments.lps_speed);
        info("hps_speed: %f", job_arguments.hps_speed);
        info("wait_time: %d", job_arguments.wait_time);
        info("job_space: %d", job_arguments.job_space);

	// saving job command line arguments to the job argv array
	_set_job_arguments(job_desc, job_arguments);

	// creating empty list for job queue
	job_list = list_create(NULL);
	// fill list with jobs
	_build_job_list(job_list);

	// getting free space currently available on the hps storage
	hps_storage_free_space = _get_storage_free_space(job_arguments.hps_path);

	
	// free space on the hps storage and job estimated time in
	// hps and lps storages
	if (EMPTY_QUEUE == slurm_list_count(job_list)) {
		if (job_arguments.job_space < hps_storage_free_space) {
        		info("\tsubmit the job to hps");
			_copy_submit_file_to_storage(job_desc, job_arguments.hps_path);
			_set_job_working_dir(job_desc, job_arguments.hps_path);
			_set_job_partition(job_desc, HPS_PARTITION_NAME);
        		info("\thps storage free space: %d", hps_storage_free_space - job_arguments.job_space);
		} else {
        		info("\tsubmit the job to lps");
			_copy_submit_file_to_storage(job_desc, job_arguments.lps_path);
			_set_job_working_dir(job_desc, job_arguments.lps_path);
			_set_job_partition(job_desc, LPS_PARTITION_NAME);
		}
	} 
	
	FREE_NULL_LIST(job_list);
	slurm_mutex_unlock(&pthread_lock);
	return SLURM_SUCCESS;
}

extern int job_modify(struct job_descriptor *job_desc,
		      struct job_record *job_ptr, uint32_t submit_uid) {
	return SLURM_SUCCESS;
}
