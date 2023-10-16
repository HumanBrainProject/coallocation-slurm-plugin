How-to-use instructions
=======================
To use our plugin in a multi-tiered storage cluster, we describe how to install and use our plugin as follows. We have considered two storage tiers, called low-performance storage (LPS) and high-performance storage (HPS).

Installation guide
------------------
Follow the steps on https://slurm.schedmd.com/add.html under _Adding a Plugin to Slurm_ to install the provided plugin (tested with Slurm version 20.02.5). The provided `Vagrantfile` automates this process for our virtual testbed.

User guide
----------
When submitting jobs with the `sbatch` command, users can use the `--bb` argument to specify job storage requirements. The plugin expects two arguments passed to the `--bb` argument in quotation marks:
- `capacity`: the high-performance storage capacity required by the job
- `io`: the intermediate data read and written to the high-performance storage during the runtime of the job

The current version of the plugin implements the scheduling mechanism published by our paper [[1]](#1).

To We provide a simple job script called sample_job.sh as follows:
```
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
```
This job creates and removes a 1.0 GiB test file called `testfile_<Slurm_Job_ID>.dat` on the storage tier selected by the plugin. Submit the job using `sbatch` as follows:
```
sbatch --bb="capacity=1024 io=8192" sample_job.sh
```

Setup the virtual testbed
-------------------------

We have used a virtual cluster for our development and test purposes. To make this environment easily usable for everybody, we automated the setup process by Vagrant Software. To complete all requirements for producing the virtual cluster and testing the plugin, the following steps are sufficient:

1. Install both **VirtualBox** and **Vagrant** software inside a Linux system.
2. Clone the repository of the project, to setup the virtual cluster.
3. Clone Slurm (https://github.com/SchedMD/slurm.git) in the same folder.
4. Checkout the Slurm version **slurm-21-08-8-2**
5. Run `vagrant up`.
6. Run `vagrant ssh controller`.
7. Run `start_slurm`.
8. Run `vagrant ssh server1` in a separate session.
9. Run `start_slurm`.
10. Repeat steps 8 and 9 for **server2** VM.
11. Make sure that **server1** and **server2** are in idle state and ready to accept jobs, by running `sinfo` on the **controller** VM.

With this setup, Vagrant creates three machines (two compute nodes and a single control daemon server) and two 5 GiB shared storage tiers (**LPS** and **HPS**). Jobs are submitted using the `sbatch` command. By passing the job storage requirements of jobs (e.g. `sbatch --bb="capacity=1024 io=8192" sample_job.sh`), as discussed in the previous section, Slurm will decide which compute and data resources are assigned to the job. 

## References
<a id="1">[1]</a>
Leah E. Lackner, Hamid Mohammadi Fard, Felix Wolf: Efficient Job Scheduling for Clusters with Shared Tiered Storage. In Proc. of the 19th IEEE/ACM International Symposium on Cluster, Cloud and Grid Computing (CCGrid), Larnaca, Cyprus, pages 321–330, IEEE, May 2019.

## Acknowledgements
This open source software was supported by the EBRAINS research infrastructure, funded from the European Union’s Horizon 2020 Framework Programme for Research and Innovation under the Specific Grant Agreement No. 785907 and No. 945539 (Human Brain Project SGA2 and SGA3).
