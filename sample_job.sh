#!/bin/bash
#
#SBATCH --job-name=sample_job
#SBATCH --output=sample_job_%j.out
#SBATCH --ntasks=1
#SBATCH --time=05:00
#SBATCH --mem-per-cpu=512

dd if=/dev/zero of="$SLURM_STORAGE_TIER/testfile_$SLURM_JOBID.dat" bs=1M count=1024
rm "$SLURM_STORAGE_TIER/testfile_$SLURM_JOBID.dat"

#end of job script
