#Preparation
apt-get update
apt-get install -y make cmake git libmunge-dev libmunge2 munge ocfs2-tools gcc g++

#Network config
echo "10.0.0.10	controller" >> /etc/hosts
echo "10.0.0.11	server1" >> /etc/hosts
echo "10.0.0.12	server2" >> /etc/hosts

#Copy munge key
cp /vagrant/munge.key /etc/munge/

#Install slurm
adduser slurm --no-create-home --disabled-password --gecos ""

cp /vagrant/slurm.conf /usr/local/etc/
cd /vagrant/slurm
git checkout slurm-20-02-4-1
cp /vagrant/job_submit_all_partitions.c /vagrant/slurm/src/plugins/job_submit/all_partitions/

./configure
make --silent
make --silent install

mkdir /var/log/slurm
touch /var/log/slurm/job_completions
touch /var/log/slurm/accounting
mkdir /var/spool/slurm
mkdir /var/spool/slurmd

chown -R slurm:root /var/log/slurm
chown -R slurm:root /var/spool/slurm

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

reboot
