/*****************************************************************************\
 *  job_submit_all_partitions.c - Decides between using two shared storage tiers while scheduling the
 *  jobs, based on the scheduling mechanism proposed in http://dx.doi.org/10.1109/CCGRID.2019.00046.
 *****************************************************************************
 *  Copyright (c) 2019-2020 Technische Universitaet Darmstadt, Darmstadt, Germany
 *  Authors: Varsha Dubay and Hamid Mohammadi Fard, contact: fard@cs.tu-darmstadt.de
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

#include <argp.h>
#include <stdlib.h>
#include <libgen.h>
#include <sys/statvfs.h>
#include <setjmp.h>

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
#define FOO_EXCEPTION (1)
#define BAR_EXCEPTION (2)
#define BAZ_EXCEPTION (3)

const char plugin_name[]       	= "Job submit all_partitions plugin";
const char plugin_type[]       	= "job_submit/all_partitions";
const uint32_t plugin_version   = SLURM_VERSION_NUMBER;

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
#define MAX_DIGITS_NUMBER 100

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

/* 
 * processing job submission command line arguments
 * input:  
 * 	  - job_descriptor structure contains job arguments array
 *        - job_arguments structure,
 * output: void
 */
extern void _read_job_arguments(struct job_descriptor *job_desc, struct arguments *job_arguments) {
	info("_read_job_arguments start");
	argp_parse(&argp, job_desc->argc, job_desc->argv, 0, 0, job_arguments);
	info("_read_job_arguments end");
}

/* 
 * returning available storage space for the input path,
 * if statvfs can't be made for the input path, then returning zero
 * input:   char* with storage path,
 * output:  integer variable contains storage free space
 */
extern int _get_storage_free_space(const char *path) {
	info("_get_storage_free_space start");
	int storage_free_size = 0;
	struct statvfs stat;
	
	// trying to get statvfs structure, if not successful then return 0
	if (0 != statvfs(path, &stat)) {
		info("_get_storage_free_space end");
		return storage_free_size;
	}
	
	// calculating free space block_size * free_block_counter
	storage_free_size = stat.f_bsize * stat.f_bavail / KBYTES_PER_GBYTES;
	info("_get_storage_free_space end");
	return storage_free_size;	
}

/*
 *  setting new paths for job work_dir, std_err and std_out attributes
 *  input:  
 *  	   - job_descriptor structure contains work_dir, std_err, std_out attributes,
 *  	   - char* with the new working directory
 *  output: void
 */
extern void _set_job_working_dir(struct job_descriptor *job_desc, const char *work_dir) {
	info("_set_job_working_dir start");
/*	int i, job_id_int;
	char *job_name2, *job_name, *new_job_stderr, *new_job_stdout;
	char job_id_str[MAX_DIGITS_NUMBER];
	char *anchor = "SLURM_JOB_NAME=";
	for (i = 0; job_desc->environment[i] != 0; i++) {
		if (NULL != (job_name = strstr(job_desc->environment[i], anchor))) {
			job_name += strlen(anchor);
			break;
		}
	}
	
	job_name2 = job_desc->name;
	info("job_name2: %s", job_name2);

	job_id_int = get_next_job_id(true);
	sprintf(job_id_str, "%d", job_id_int);

	new_job_stderr = (char*) xmalloc ((strlen(work_dir) + strlen(job_name) + strlen(job_id_str) + 7) * sizeof(char));
	strcpy(new_job_stderr, work_dir);
	strcat(new_job_stderr, "/");
	strcat(new_job_stderr, job_name);
	strcat(new_job_stderr, ".%J.err");
	info("job_stderr: %s", new_job_stderr);

	new_job_stdout = (char*) xmalloc ((strlen(work_dir) + strlen(job_name) + strlen(job_id_str) + 7) * sizeof(char));
	strcpy(new_job_stdout, work_dir);
	strcat(new_job_stdout, "/");
	strcat(new_job_stdout, job_name);
	strcat(new_job_stdout, ".%J.out");
	info("job_stdout: %s", new_job_stdout);
	
	job_desc->work_dir = xstrdup(work_dir);
	job_desc->std_err = xstrdup(new_job_stderr);
	job_desc->std_out = xstrdup(new_job_stdout);

	xfree(new_job_stderr);
	xfree(new_job_stdout);
	info("_set_job_working_dir end");
*/
        char *new_job_stderr = (char*) xmalloc ((strlen(basename(job_desc->std_err)) + strlen(work_dir) + 2) * sizeof(char));
        char *new_job_stdout = (char*) xmalloc ((strlen(basename(job_desc->std_out)) + strlen(work_dir) + 2) * sizeof(char));

	// making new std_err absolute filename
        strcpy(new_job_stderr, work_dir);
        strcat(new_job_stderr, "/");
        strcat(new_job_stderr, basename(job_desc->std_err));

	// making new std_out absolute filename
        strcpy(new_job_stdout, work_dir);
        strcat(new_job_stdout, "/");
        strcat(new_job_stdout, basename(job_desc->std_out));
	
	// saving new job attributes
	xfree(job_desc->work_dir);
	xfree(job_desc->std_err);
	xfree(job_desc->std_out);
	
	job_desc->work_dir = xstrdup(work_dir);
	job_desc->std_err = xstrdup(new_job_stderr);
	job_desc->std_out = xstrdup(new_job_stdout);

	xfree(new_job_stderr);
	xfree(new_job_stdout);
	info("_set_job_working_dir end"); 
}

/* 
 * setting the new job partition
 * input:
 *        - job_descriptor structure contains partition attribute 
 *        - char* with the new partition name
 * output: void
 */
extern void _set_job_partition(struct job_descriptor *job_desc, const char *partition) {
	info("_set_job_partition start");
	job_desc->partition = xstrdup(partition);
	info("_set_job_partition end");
}

/* 
 * setting job submission parameters to the job argv array
 * we need to do this to save job arguments for subsequent job
 * processing during hps storage free space calculation
 * input:
 *        - job_descriptor structure, contains argc and argv arrray
 *        - job_arguments structure
 * output: void
 */
extern void _set_job_arguments(struct job_descriptor *job_desc, struct arguments job_arguments) {
	info("_set_job_arguments start");
	char *buf;
	char num[MAX_DIGITS_NUMBER];
	buf = (char*) xmalloc ((strlen(job_desc->argv[0]) + 1) * sizeof(char));
	strcpy(buf, job_desc->argv[0]);
	// resizing job argv array
	job_desc->argc = 8;
	job_desc->argv = xmalloc (job_desc->argc * sizeof(char*));
	// restoring submission command to argv array from the local variable
	job_desc->argv[0] = xstrdup(buf);
	
	// setting lps_path attribute to the job argv array
	buf = (char*) xrealloc (buf, (strlen("--lps-path=") + strlen(job_arguments.lps_path) + 1) * sizeof(char));
        strcpy(buf, "--lps-path=");
	strcat(buf, job_arguments.lps_path);
	job_desc->argv[1] = xstrdup(buf);

	// setting hps_path attribute to the job argv array
	buf = (char*) xrealloc (buf, (strlen("--hps-path=") + strlen(job_arguments.hps_path) + 1) * sizeof(char));
        strcpy(buf, "--hps-path=");
	strcat(buf, job_arguments.hps_path);
	job_desc->argv[2] = xstrdup(buf);

	// setting lps_spee attribute to the job argv array
	buf = (char*) xrealloc (buf, MAX_DIGITS_NUMBER * sizeof(char));
        strcpy(buf, "--lps-speed=");
 	sprintf(num, "%f", job_arguments.lps_speed);
	strcat(buf, num);
	job_desc->argv[3] = xstrdup(buf);

	// setting hps_speed attribute to the job argv array
	buf = (char*) xrealloc (buf, MAX_DIGITS_NUMBER * sizeof(char));
        strcpy(buf, "--hps-speed=");
 	sprintf(num, "%f", job_arguments.hps_speed);
	strcat(buf, num);
	job_desc->argv[4] = xstrdup(buf);
	
	// setting wait_tim attribute to the job argv array
	buf = (char*) xrealloc (buf, MAX_DIGITS_NUMBER * sizeof(char));
        strcpy(buf, "--wait-time=");
 	sprintf(num, "%d", job_arguments.wait_time);
	strcat(buf, num);
	job_desc->argv[5] = xstrdup(buf);

	// setting job_space attribute to the job argv array
	buf = (char*) xrealloc (buf, MAX_DIGITS_NUMBER * sizeof(char));
        strcpy(buf, "--job-space=");
 	sprintf(num, "%d", job_arguments.job_space);
	strcat(buf, num);
	job_desc->argv[6] = xstrdup(buf);
	info("_set_job_arguments end");
	xfree(buf);
}

/*
 * building job list by getting all jobs from the SLURM and
 * filter it by a job status
 * input:  job_list - empty list for running and pending jobs,
 * output: void
 */
extern void _build_job_list(List job_list) {
	info("_build_job_list start");
	int i;
	job_info_t *job_info;
	uint16_t show_flags = 0;
	show_flags |= SHOW_ALL;
	job_info_msg_t *job_ptr = NULL;
	slurm_load_jobs((time_t) NULL, &job_ptr, show_flags);
	
	// processing through jobs array and save to list only running jobs
	for (i = 0; i < job_ptr->record_count; i++) {
		if (JOB_PENDING == job_ptr->job_array[i].job_state ||
			JOB_RUNNING == job_ptr->job_array[i].job_state ||
			JOB_SUSPENDED == job_ptr->job_array[i].job_state) {
			job_info = job_ptr->job_array + i;
			list_append(job_list, (void *) job_info);
		}
	}
	info("_build_job_list end");
}

/*
 * generating random wait time, if the user didn't input
 * wait_time as a submission argument
 * input:  none,
 * output: uint32_t value with a random value, to simulate job
 *         resources waiting time for the "hps" storage
 */
extern uint32_t _generate_wait_time() {
	info("_generate_wait_time end");
	uint32_t wait_time = 0;
	// generating seed
	srand(time(NULL));
	wait_time = (rand() % (WAIT_MAX - WAIT_MIN + 1)) + WAIT_MIN;
	info("_generate_wait_time end");
	return wait_time;
}

/*
 * extracting job_space value from the job command line
 * input:  char* with job command line,
 * output: uint32_t value with storage space consumed by a job
 */
extern uint32_t _get_job_storage_space(char *command) {
	info("_get_job_storage_space start");
	uint32_t job_space_int = 0;
	char *job_space_str;
	char *anchor = "--job-space=";
	
	if (NULL != (job_space_str = strstr(command, anchor))) {
		job_space_int = atoi(job_space_str + strlen(anchor));
	}
	info("_get_job_storage_space end");
	return job_space_int;
}

/*
 * copy submit file to the selected storage path
 * input:
 *        - job_descriptor structure contains submit filename
 *        - char* with the new working directory
 * output: void
*/
extern void _copy_submit_file_to_storage(struct job_descriptor *job_desc, char *path) {
	info("_copy_submit_file_to_storage start");
        int c;
        FILE *input_fd;
        FILE *output_fd;

	char *new_submit_filename = (char*) xmalloc ((strlen(basename(job_desc->argv[0])) + strlen(path) + 2) * sizeof(char));
	strcpy(new_submit_filename, path);
	strcat(new_submit_filename, "/");
	strcat(new_submit_filename, basename(job_desc->argv[0]));
                
        input_fd = fopen(job_desc->argv[0], "r");
        if (input_fd == NULL) {
                info("can't open input file %s, %d", job_desc->argv[0], errno);
		xfree(new_submit_filename);
		return;
	}

        output_fd = fopen(new_submit_filename, "w");
        if (output_fd == NULL) {
		info("can't open output file %s, %d", new_submit_filename, errno);
		fclose(input_fd);
		xfree(new_submit_filename);
		return;
        }

        while((c = fgetc(input_fd)) != EOF) {
	        fputc(c, output_fd);
        }	
        
	fclose(input_fd);
        fclose(output_fd);
	xfree(new_submit_filename);
	info("_copy_submit_file_to_storage end");
}

extern int init(void) {
	return SLURM_SUCCESS;
}

extern int fini(void) {
	return SLURM_SUCCESS;
}

/*
 * submiting a new job
 * input: 
 *        - job_rescriptor structure, contains all information about a job
 *        - uint32_t - uid of the job owner,
 *        - char** - pointer to error message
 * output: int value with return status of the function
 */
extern int job_submit(struct job_descriptor *job_desc,
		      uint32_t submit_uid,
		      char **err_msg) {
        info("job_submit start");
	slurm_mutex_lock(&pthread_lock);

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
	// calculating job estimated time
	job_lps_time = job_arguments.job_space / job_arguments.lps_speed;
	job_hps_time = job_arguments.job_space / job_arguments.hps_speed + job_arguments.wait_time;
        info("lps estimated job time: %d", job_lps_time);
        info("hps estimated job time: %d", job_hps_time);

	// decide where to submit job dependent from queue availability
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
	} else {
		// cycling through queue and calculating available
		// space in the hps storage
		while ((job_info = (job_info_t *) list_pop(job_list))) {
			if (JOB_RUNNING == job_info->job_state &&
				0 == strcmp(job_info->partition, HPS_PARTITION_NAME)) {
				hps_storage_free_space -= _get_job_storage_space(job_info->command);
			}
		}
		
		if (job_arguments.job_space < hps_storage_free_space) {
			if (job_lps_time > job_hps_time) {
        			info("\tsubmit the job to hps");
				_copy_submit_file_to_storage(job_desc, job_arguments.hps_path);
				_set_job_working_dir(job_desc, job_arguments.hps_path);
				_set_job_partition(job_desc, HPS_PARTITION_NAME);
			} else {
        			info("\tsubmit the job to lps");
				_copy_submit_file_to_storage(job_desc, job_arguments.lps_path);
				_set_job_working_dir(job_desc, job_arguments.lps_path);
				_set_job_partition(job_desc, LPS_PARTITION_NAME);
			}
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
        info("job_submit end");
	return SLURM_SUCCESS;
}

extern int job_modify(struct job_descriptor *job_desc,
		      struct job_record *job_ptr, uint32_t submit_uid) {
	return SLURM_SUCCESS;
}
