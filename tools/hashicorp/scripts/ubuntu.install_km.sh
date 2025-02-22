#!/bin/bash -xe
#
# Copyright 2021 Kontain Inc
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
#

# assumes $storage with kkm.run and kontain.tar.gz
# installs kontain, kkm, docker, podman
# We assume this script runs as superuser.

readonly storage="/tmp"

# this seems to be needed on 'generic/ubuntu' box only
ln -sf /run/systemd/resolve/resolv.conf /etc/resolv.conf

# Install Docker
# docker repo
apt-get update -y -q
apt-get install -y -q apt-transport-https ca-certificates \
      curl gnupg-agent software-properties-common
curl -fsSL https://download.docker.com/linux/ubuntu/gpg | apt-key add - 2>&1
add-apt-repository \
   "deb [arch=amd64] https://download.docker.com/linux/ubuntu $(lsb_release -cs) stable"
# get ready for installs
apt-get update -y -q

apt-get -y -q install linux-headers-$(uname -r) docker-ce docker-ce-cli containerd.io gdb

# Install KKM... it will also validate hardware.
bash $storage/kkm.run --noprogress -- --force-install 2>&1

# Install Kontain.
mkdir -p /opt/kontain && tar -C /opt/kontain -xzf $storage/kontain.tar.gz

# Configure Docker and restart
for u in vagrant ubuntu ; do
   if getent passwd $u > /dev/null 2>&1; then
      echo "PATH=\$PATH:/opt/kontain/bin" >> $(eval echo ~$u/.bashrc)
      usermod -aG docker $u
   fi
done
/opt/kontain/bin/docker_config.sh

# Install podman and make config changes
/opt/kontain/bin/podman_config.sh
