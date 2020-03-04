./configure
make --silent
make --silent install
mkdir /var/log/slurm
touch /var/log/slurm/job_completions
touch /var/log/slurm/accounting
chown -R slurm:slurm /var/log/slurm
chmod -R 777 /var/log/slurm
#chown -R slurm:slurm /var/log
chown root:slurm /var/spool
chmod g=rwx /var/spool
cp /vagrant/ocfs2_configs/cluster.conf /etc/ocfs2/cluster.conf
cp /vagrant/ocfs2_configs/o2cb  /etc/default/
cp /vagrant/rc.local /etc/rc.local

if [ "$HOSTNAME" = "controller" ]; then
  cp /vagrant/rc.local-cotroller /etc/rc.local
fi

if [ "$HOSTNAME" = "server1" ]; then
  mkfs.ocfs2 -b 4k -C 4k -L "lps" -N 8 /dev/sdb
  mkfs.ocfs2 -b 4k -C 4k -L "hps" -N 8 /dev/sdc
fi

