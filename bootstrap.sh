# Preparation
apt-get update
apt-get install -y make autoconf libglib2.0-dev libgtk2.0-dev cmake git libmunge-dev libmunge2 munge ocfs2-tools gcc g++

# Network config
echo "10.0.0.10	controller" >> /etc/hosts
echo "10.0.0.11	server1" >> /etc/hosts
echo "10.0.0.12	server2" >> /etc/hosts

# Copy munge key
cp /vagrant/munge.key /etc/munge/

#Install slurm
adduser slurm --no-create-home --disabled-password --gecos ""

# Copy Slurm configuration
cp /vagrant/slurm.conf /usr/local/etc/

# Compile and install Slurm
cd /vagrant/slurm
if [ "$HOSTNAME" = "controller" ]; then
	git checkout slurm-21-08-8-2
	cp /vagrant/configure.ac /vagrant/slurm/
	cp /vagrant/Makefile.am /vagrant/slurm/src/plugins/job_submit/
	cp -r /vagrant/storage_aware /vagrant/slurm/src/plugins/job_submit/
	autoreconf
	./configure
	make --silent
fi
make --silent install

mkdir /var/log/slurm
touch /var/log/slurm/job_completions
touch /var/log/slurm/accounting
mkdir /var/spool/slurmctld
mkdir /var/spool/slurmd

chown -R slurm:root /var/log/slurm
chown -R slurm:root /var/spool/slurmctld

cp /vagrant/ocfs2_configs/cluster.conf /etc/ocfs2/cluster.conf
cp /vagrant/ocfs2_configs/o2cb /etc/default/

mkdir /home/vagrant/lps
mkdir /home/vagrant/hps
chown -R vagrant:vagrant /home/vagrant/lps
chown -R vagrant:vagrant /home/vagrant/hps
if [ "$HOSTNAME" = "controller" ]; then
  cp /vagrant/rc.local-controller /etc/rc.local
  cp /vagrant/sample_job.sh /home/vagrant/sample_job.sh
  chown vagrant:vagrant /home/vagrant/sample_job.sh
  mkfs.ocfs2 -b 4K -C 8K -L "lps" -N 8 /dev/sdb
  mkfs.ocfs2 -b 4K -C 8K -L "hps" -N 8 /dev/sdc
else
	cp /vagrant/rc.local /etc/rc.local
fi
chmod +x /etc/rc.local

if [ "$HOSTNAME" = "controller" ]; then
	echo "alias start_slurm='sudo slurmctld -D &'" > /home/vagrant/.bash_aliases
	echo "alias stop_slurm='sudo pkill slurmctld'" >> /home/vagrant/.bash_aliases
	echo "alias undrain_server1='sudo scontrol update nodename=server1 state=resume'" >> /home/vagrant/.bash_aliases
	echo "alias undrain_server2='sudo scontrol update nodename=server2 state=resume'" >> /home/vagrant/.bash_aliases
else
	echo "alias start_slurm='sudo slurmd -D &'" >> /home/vagrant/.bash_aliases
	echo "alias stop_slurm='sudo pkill slurmd'" >> /home/vagrant/.bash_aliases
fi
echo "alias restart_slurm='stop_slurm && start_slurm'" >> /home/vagrant/.bash_aliases

reboot
