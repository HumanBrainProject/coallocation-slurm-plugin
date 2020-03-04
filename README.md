How-to-use instructions
=======================
To use our plugin in a multi-tiered storage cluster, we describe how to install and use our plugin, as follows. We have considered two storage tiers, called low performance storage (lps) and high performance storage (hps).

Installation guide
------------------
To install our plugin, the system administrator needs to follow the following steps:
1. Download the source code of our plugin called `job_submit_all_partitions.c` available in the project repository;
2. Copy the file in `<slurm_folder>/src/plugin/job_submit/all_partitions` of controller node(s) of Slurm;
3. Compile the source code and then copy (if it is not done automatically) the compiled version (.so extension) from `.libs` folder to the plugin directory (by default `/usr/local/lib/slurm`);
4. Set this new plugin in the Slurm configuration file (`slurm.conf`) by inserting the line `JobSubmitPlugins=job_submit/all_partitions`
5. Define the partitions as required (based on the storage needs) in the Slurm configuration file (`slurm.conf`);
6. Restart the control daemon (`slurmctld`) of Slurm.
To ease of use, a sample Slurm configuration file defining two partitions is available in the repository of the project.

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

The current version of the plugin implements the scheduling logic published by our paper [[1]](#1). Based on the published scheduling approach, the plugin must calculate the argument `wait-time` dynamically from the system status, but still we need time to find the way to gather this information dynamically, therefore we replaced this with some random values in the current version of the plugin. 

We need to notice that `lps-path`, `hps-path`, `lps-speed` and `hps-speed` can be implicitly defined in the source code of the plugin. Defining them as arguments gives users the possibility of choosing only the desired storage tiers when there are more than two tiers in the cluster. 

To have a better imagination how the plugin selects storage transparently, we could create a simple job script called sample.sh as follows:
```
#!/bin/bash
#
#SBATCH --job-name=sample_job
#SBATCH --output=sample.out
#SBATCH --error=sample.err
#SBATCH --ntasks=1
#SBATCH --time=10:00
#SBATCH --mem-per-cpu=100
#
cp $SLURM_SUBMIT_DIR/input.txt $HPC_LOCAL/.
cd $HPC_LOCAL
mv input.txt output.txt
echo new line appended in the remote host >> output.txt
cp output.txt $SLURM_SUBMIT_DIR/.
#end of job script
```
This simple job receives a text file, called `input.txt` (located in the same directory of `sample.sh`) as input and produces an output file, called `output.txt` (located in the same directory of `sample.sh`), by appending a new line of string to the end of the input file. In this job script, the environment variable `SLURM_SUBMIT_DIR` refers the directory from which `sbatch` was invoked and `HPC_LOCAL` refers to the working directory (mapped in lps or hps tier) in the compute node running the job.

We could submit the job by `sbatch`, as follows (assuming the /lps and /hps for our two shared storage tiers):
```
sbatch sample.js --hps-path=/hps --lps-path=/lps
```
Looking at the code of the plugin, one can detect that the function `_set_job_working_dir` will set the working directory (referred by the environment variable `HPC_LOCAL`) for each job. In other words, based on the adequate storage tier selected by the plugin, this function maps the output path of each job to the hps or the lps tier.

Setup the virtual testbed
-------------------------

We have used a virtual cluster for our development and test purposes. To make this environment easily usable for everybody, we automated the setup process by Vagrant Software. To complete all requirements for producing the virtual cluster, and testing the plugin, you could follow these steps:

1. Install both **VirtualBox** and **Vagrant** software inside a Linux system.
2. Clone the repository of the project, to setup the virtual cluster.
3. Clone Slurm (https://github.com/SchedMD/slurm.git) in the same folder.
4. Run `cp job_submit_all_partitions.c slurm/src/plugin/job_submit/all_partitions/.` in the folder of the project.
5. Uncomment the lines 75-76 in the **Vagrantfile**.
6. Run `vagrant up` twice (inspite of getting any errors or no error).
7. Comment out the lines 75-76 again.
8. Run `vagrant up` again.
9. Run `vagrant ssh controller`
10. Compile the Sulrm source code in the VM and run the service (`cd /vagrant/slurm && ./configure && make & make install && sudo slurmctld -D &`).
11. Run `vagrant ssh server1`
12. Compile the Sulrm source code in the VM and run the service (`cd /vagrant/slurm && ./configure && make & make install && slurmd start`).
13. Do the same for **server2** VM.
14. Test the servers are idle and ready to submit the jobs, by running `sinfo` on the **controller** VM.

By this setup, you will have three servers, two partitions, two shared storage tiers (**hps** and **lps**) and a single control daemon server.

After this setup, the cluster is ready to use and you could submit your jobs using `sbatch` command. By passing the job storage requirements of jobs, as discussed in the previous section, you will let Slurm decide which compute and data resources should be assigned to your jobs. 

## References
<a id="1">[1]</a>
Leah E. Lackner, Hamid Mohammadi Fard, Felix Wolf: Efficient Job Scheduling for Clusters with Shared Tiered Storage. In Proc. of the 19th IEEE/ACM International Symposium on Cluster, Cloud and Grid Computing (CCGrid), Larnaca, Cyprus, pages 321–330, IEEE, May 2019.
