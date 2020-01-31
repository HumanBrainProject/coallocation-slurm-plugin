BOX_IMAGE = "bento/ubuntu-18.04"
SERVER_COUNT = 2
BANDWITH_LIMITS = [5, 100]

$script = <<SCRIPT
apt-get update
apt-get install -y make cmake git libmunge-dev libmunge2 munge ocfs2-tools gcc g++
#Network config
echo "10.0.0.10    controller" >> /etc/hosts
echo "10.0.0.11   server1" >> /etc/hosts
echo "10.0.0.12   server2" >> /etc/hosts
#Copy munge key
cp /vagrant/munge.key /etc/munge
chown 112:112 /etc/munge/munge.key
#Install slurm
adduser slurm --no-create-home --disabled-password --gecos ""
cp /vagrant/slurm.conf /usr/local/etc/
#git clone https://github.com/SchedMD/slurm.git
cd /vagrant/slurm
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

mkdir /home/vagrant/lps
mkdir /home/vagrant/hps
SCRIPT

Vagrant.configure("2") do |config|
  config.vm.define "controller" do |subconfig|
    subconfig.vm.box = BOX_IMAGE
    subconfig.vm.hostname = "controller"
    subconfig.vm.network :private_network, ip: "10.0.0.10"
  end

  #Add Disks to the server
  config.vm.provider "virtualbox" do |vb|
    unless File.exist?("lps.vdi")
      vb.customize ['createhd', '--filename', 'lps.vdi', '--size', 5 * 1024, '--variant', 'Fixed']
      vb.customize ['modifymedium', 'disk', 'lps.vdi', '--type', 'shareable']
    end
    unless File.exist?("hps.vdi")
      vb.customize ['createhd', '--filename', 'hps.vdi', '--size', 5 * 1024, '--variant', 'Fixed']
      vb.customize ['modifymedium', 'disk', 'hps.vdi', '--type', 'shareable']
    end
  end
  #Create Server
  (1..SERVER_COUNT).each do |i|
    config.vm.define "server#{i}" do |subconfig|
      subconfig.vm.box = BOX_IMAGE
      subconfig.vm.hostname = "server#{i}"
      subconfig.vm.network :private_network, ip: "10.0.0.#{10 + i}"

      subconfig.vm.provider "virtualbox" do |vb|
        #To be comment out
        #vb.customize ['bandwidthctl', :id, 'add', 'Slow', '--type', 'disk', '--limit', '5M']
        #vb.customize ['bandwidthctl', :id, 'add', 'Fast', '--type', 'disk', '--limit', '500M']
        vb.customize ['storageattach', :id,  '--storagectl', 'SATA Controller', '--port', 1, '--device', 0, '--type', 'hdd', '--mtype', 'shareable', '--medium', "lps.vdi", '--bandwidthgroup', "Slow"]
        vb.customize ['storageattach', :id,  '--storagectl', 'SATA Controller', '--port', 2, '--device', 0, '--type', 'hdd', '--mtype', 'shareable', '--medium', "hps.vdi", '--bandwidthgroup', "Fast"]
      end
    end
  end
  config.vm.provision :shell,
    :inline => $script

end
