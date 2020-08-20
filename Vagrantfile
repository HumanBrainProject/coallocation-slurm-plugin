BOX_IMAGE = "bento/ubuntu-18.04"
SERVER_COUNT = 2
LPS_PATH = "lps.vdi"
HPS_PATH = "hps.vdi"
PROVISIONED_FLAG = ".provisioned"

Vagrant.configure("2") do |config|
  
  # triggers to create and delete a file indicating that the machines have been provisioned
  config.trigger.after :up do |trigger|
    trigger.only_on = "server#{SERVER_COUNT}"
    trigger.ruby do |env,machine|
      unless File.exist?(PROVISIONED_FLAG)
        File.open(PROVISIONED_FLAG, "w") {}
      end
    end
  end

  config.trigger.after :destroy do |trigger|
    trigger.only_on = "controller"
    trigger.ruby do |env,machine|
      if File.exist?(PROVISIONED_FLAG)
        File.delete(PROVISIONED_FLAG)
      end
    end
  end
  
  # define the controller
  config.vm.define "controller" do |subconfig|
    subconfig.vm.box = BOX_IMAGE
    subconfig.vm.hostname = "controller"
    subconfig.vm.network :private_network, ip: "10.0.0.10"
    subconfig.vm.provider "virtualbox" do |vb|
      vb.cpus = 1
      vb.memory = 1024
      unless File.exist?(PROVISIONED_FLAG)
        vb.customize ['createhd', '--filename', LPS_PATH, '--size', 5 * 1024, '--variant', 'Fixed']
        vb.customize ['modifymedium', 'disk', LPS_PATH, '--type', 'shareable']
        vb.customize ['createhd', '--filename', HPS_PATH, '--size', 5 * 1024, '--variant', 'Fixed']
        vb.customize ['modifymedium', 'disk', HPS_PATH, '--type', 'shareable']
        vb.customize ['bandwidthctl', :id, 'add', 'Slow', '--type', 'disk', '--limit', '12M']
        vb.customize ['bandwidthctl', :id, 'add', 'Fast', '--type', 'disk', '--limit', '192M']
      end
      vb.customize ['storageattach', :id, '--storagectl', 'SATA Controller', '--port', 2, '--device', 0, '--type', 'hdd', '--mtype', 'shareable', '--medium', LPS_PATH, '--bandwidthgroup', "Slow"]
      vb.customize ['storageattach', :id, '--storagectl', 'SATA Controller', '--port', 3, '--device', 0, '--type', 'hdd', '--mtype', 'shareable', '--medium', HPS_PATH, '--bandwidthgroup', "Fast"]
    end
  end

  # define the servers
  (1..SERVER_COUNT).each do |i|
    config.vm.define "server#{i}" do |subconfig|
      subconfig.vm.box = BOX_IMAGE
      subconfig.vm.hostname = "server#{i}"
      subconfig.vm.network :private_network, ip: "10.0.0.#{10 + i}"
      subconfig.vm.provider "virtualbox" do |vb|
        vb.cpus = 1
        vb.memory = 1024
        unless File.exist?(PROVISIONED_FLAG)
          vb.customize ['bandwidthctl', :id, 'add', 'Slow', '--type', 'disk', '--limit', '12M']
          vb.customize ['bandwidthctl', :id, 'add', 'Fast', '--type', 'disk', '--limit', '192M']
        end
        vb.customize ['storageattach', :id, '--storagectl', 'SATA Controller', '--port', 2, '--device', 0, '--type', 'hdd', '--mtype', 'shareable', '--medium', LPS_PATH, '--bandwidthgroup', "Slow"]
        vb.customize ['storageattach', :id, '--storagectl', 'SATA Controller', '--port', 3, '--device', 0, '--type', 'hdd', '--mtype', 'shareable', '--medium', HPS_PATH, '--bandwidthgroup', "Fast"]
      end
    end
  end
  config.vm.provision "shell", path: "bootstrap.sh"

end
