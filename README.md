How-to-use instructions
=======================
To use our plugin in a multi-tiered storage cluster, we describe how to install and use our plugin, as follows. We have considered two storage tiers, called low performance storage (lps) and high performance storage (hps).

Installation guide
------------------
To install our plugin, the system administrator needs to follow the following steps:
1. Download the source code of our plugin called `job_submit_all_partitions.c` available in the project repository;
2. Copy the file in `<slurm_folder>/src/plugins/job_submit/all_partitions` of controller node(s) of Slurm;
3. Compile the source code and then copy (if it is not done automatically) the compiled version (.so extension) from `.libs` folder to the plugin directory (by default `/usr/local/lib/slurm`);
4. Set this new plugin in the Slurm configuration file (`slurm.conf`) by inserting the line `JobSubmitPlugins=job_submit/all_partitions`
5. Define the partitions as required (based on the storage needs) in the Slurm configuration file (`slurm.conf`);
6. Restart the control daemon (`slurmctld`) of Slurm.

User guide
----------
To use the plugin, when submitting a job, users need to define their own job storage requirements, as new arguments in the `sbatch` command line. These new arguments include:
- `lps-path`: the path to low performance storage,
- `hps-path`: the path to high performance storage,
- `lps-speed`: the low performance storage's speed,
- `hps-speed`: the high performance storage’s speed,
- `job-space`: the storage volume needed by the job,
- `wait-time`: waiting time for high performance storage.

If users have no know knowledge available in advance about the storage needs of the job, they can simply ignore all the parameters (arguments) and the algorithm will apply the default job submission mechanism of Slurm.

The current version of the plugin implements the scheduling mechanism published by our paper [[1]](#1). Based on the published scheduling approach, the plugin must calculate the argument `wait-time` dynamically from the system status. Because we still need time to find the way to gather this information dynamically, in the current version of the plugin, we replaced the waiting time with synthetic values, which of course needs to be modified while using in a production environment. 

We need to notice that `lps-path`, `hps-path`, `lps-speed` and `hps-speed` can be implicitly defined in the source code of the plugin. Defining them as arguments gives users the possibility of choosing only the desired storage tiers when there are more than two tiers in the cluster. 

To have a better imagination how the plugin selects storage transparently, we created a simple job script called sample_job.sh as follows:
```
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
```
This simple job writes and then deletes a 1.5 GiB test file called `testfile_<Slurm_Job_ID>.dat` to the storage tier selected by the plugin.

We can submit the job by `sbatch`, as follows (assuming the /lps and /hps for our two shared storage tiers):
```
sbatch sample_job.sh --lps-path=/lps --hps-path=/hps
```
Looking at the code of the plugin, one can detect that the function `_set_job_working_dir` will set the working directory for each job. In other words, based on the adequate storage tier selected by the plugin, this function maps the output path of each job to the hps or the lps tier.

Setup the virtual testbed
-------------------------

We have used a virtual cluster for our development and test purposes. To make this environment easily usable for everybody, we automated the setup process by Vagrant Software. To complete all requirements for producing the virtual cluster, and testing the plugin, you can follow these steps:

1. Install both **VirtualBox** and **Vagrant** software inside a Linux system.
2. Clone the repository of the project, to setup the virtual cluster.
3. Clone Slurm (https://github.com/SchedMD/slurm.git) in the same folder.
4. Run `vagrant up`.
5. Run `vagrant ssh controller`.
6. Run `sudo slurmctld -D &`.
7. Run `vagrant ssh server1` in a separate session.
8. Run `sudo slurmd -D &`.
9. Repeat steps 7 and 8 for **server2** VM.
10. Make sure that **server1** and **server2** are idle and ready to submit the jobs, by running `sinfo` on the **controller** VM.

By this setup, you will have three servers (two compute nodes and a single control daemon server), two partitions and two 5 GiB shared storage tiers (**hps** and **lps**). Now you can submit your jobs using the `sbatch` command. By passing the job storage requirements of jobs (e.g. `sbatch sample_job.sh --lps-path=/home/vagrant/lps --hps-path=/home/vagrant/hps --lps-speed=12 --hps-speed=192 --job-space=1536`), as discussed in the previous section, you will let Slurm decide which compute and data resources should be assigned to your jobs. 

## References
<a id="1">[1]</a>
Leah E. Lackner, Hamid Mohammadi Fard, Felix Wolf: Efficient Job Scheduling for Clusters with Shared Tiered Storage. In Proc. of the 19th IEEE/ACM International Symposium on Cluster, Cloud and Grid Computing (CCGrid), Larnaca, Cyprus, pages 321–330, IEEE, May 2019.
