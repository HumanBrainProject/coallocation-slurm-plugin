#!/bin/bash
#
#SBATCH --job-name=sample_job
#SBATCH --output=sample_job_%j.out
#SBATCH --error=sample_job_%j.err
#SBATCH --ntasks=1
#SBATCH --time=05:00
#SBATCH --mem-per-cpu=512

dd if=/dev/zero of="testfile_$SLURM_JOBID.dat" bs=1M count=1536
rm "testfile_$SLURM_JOBID.dat"

#end of job script
