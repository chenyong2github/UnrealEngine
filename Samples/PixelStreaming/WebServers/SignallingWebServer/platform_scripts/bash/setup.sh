#!/bin/bash
# Copyright Epic Games, Inc. All Rights Reserved.

# Azure specific fix to allow installing NodeJS from NodeSource
if test -f "/etc/apt/sources.list.d/azure-cli.list"; then
    sudo touch /etc/apt/sources.list.d/nodesource.list
    sudo touch /usr/share/keyrings/nodesource.gpg
    sudo chmod 644 /etc/apt/sources.list.d/nodesource.list
    sudo chmod 644 /usr/share/keyrings/nodesource.gpg
    sudo chmod 644 /etc/apt/sources.list.d/azure-cli.list
fi

declare -A Packages
num_rows=6
num_cols=8

# Versions are from current working release versions
# No version for turnserver at the moment, see below why:
#  https://github.com/coturn/coturn/issues/680
#  https://github.com/coturn/coturn/issues/843
#
# Structure for installation preparation; please note | in "how to install" -> installer will split the command
#       Need install Package name   Version   min/any      how to get version  how to install
Packages[1,1]="y"
Packages[1,2]="TURN server"
Packages[1,3]=""
Packages[1,4]="any"
Packages[1,5]="turnserver"
Packages[1,6]="sudo apt-get install -y coturn"
Packages[2,1]="y"
Packages[2,2]="node"
Packages[2,3]="v10.19.0"
Packages[2,4]="min"
Packages[2,5]="node --version"
Packages[2,6]="curl -fsSL https://deb.nodesource.com/setup_12.x | sudo -E bash - && sudo apt-get install -y nodejs"
Packages[3,1]="y"
Packages[3,2]="npm"
Packages[3,3]="8.1.2"
Packages[3,4]="min"
Packages[3,5]="npm --version"
Packages[3,6]="sudo npm install -g npm | sudo npm cache clean -f | sudo npm install -g npm@latest"
Packages[4,1]="y"
Packages[4,2]="express fw"
Packages[4,3]="4.17.1"
Packages[4,4]="min"
Packages[4,5]="sudo npm show express version"
Packages[4,6]="sudo npm install express -f"
Packages[5,1]="y"
Packages[5,2]="STUN server"
Packages[5,3]="0.0.1"
Packages[5,4]="min"
Packages[5,5]="sudo npm show cirrus-webserver version"
Packages[5,6]="sudo npm install cirrus-webserver -f"
Packages[6,1]="y"
Packages[6,2]="JSON jq"
Packages[6,3]="1.6"
Packages[6,4]="min"
Packages[6,5]="jq --version"
Packages[6,6]="sudo apt-get install -y jq"
Packages[7,1]="y"
Packages[7,2]="vulkan-utils"
Packages[7,3]="1.2.131"
Packages[7,4]="min"
Packages[7,5]="sudo apt show vulkan-utils 2>/dev/null | grep Version"
Packages[7,6]="sudo apt-get install -y vulkan-utils"
Packages[8,1]="y"
Packages[8,2]="pulseaudio"
Packages[8,3]="13.99"
Packages[8,4]="min"
Packages[8,5]="pulseaudio --version"
Packages[8,6]="sudo apt-get install -y pulseaudio"

# Install npm packages at the correct place
pushd ../.. 2&>1

# Check what to install
for ((i=1;i<=num_cols;i++)) do
 printf "Checking for %-12s ..." "${Packages[$i,2]}"
 if [ "${Packages[$i,4]}" = "any" ]; then
  printf " any version ...                "
  IsInstalled=$(command -v "${Packages[$i,5]}")
  if [ -z "$IsInstalled" ]; then
	printf " not found                           marked for installation\n"
  else
	printf " found                               no install needed        %s\n" "${IsInstalled}"
	Packages[$i,1]="n"
  fi
 elif [ "${Packages[$i,4]}" = "min" ]; then
  printf " minimum version: %-15s" "${Packages[$i,3]}"
  Wanted=$(echo "${Packages[$i,3]}" | sed -E 's/[^0-9.]//g')
  Installed=$(eval "${Packages[$i,5]}" 2>/dev/null)
  if [ -z "${Installed}" ]; then
   printf "not found an installed version\n"
  else
   printf "found version: %-20s" "${Installed}"
   Current=$(echo "${Installed}" | sed -E 's/[^0-9.]//g')
   if [ "${Current}" == "${Wanted}" ]; then
    Packages[$i,1]="n"
    printf " no install needed\n"
   else
    Newer=$(printf "%s\n%s" "${Wanted}" "${Current}" | sort -r | head -n 1)
    if [ "${Current}" != "${Newer}" ]; then
     printf " old, marked for installation\n"
    else
	 printf " new, no installation\n"
	 Packages[$i,1]="n"
	fi
   fi
  fi 
 else
  printf "Code error, please check Packages setup for %s %s\n" "${Packages[$i,2]}"
  exit
 fi
done

# Do the installation
for ((i=1;i<=num_cols;i++)) do
 if [ "${Packages[$i,1]}" != "n" ]; then
  if [[ "${Packages[$i,6]}" == :* ]]; then
   printf "Will not install %s because %s" "${Packages[$i,2]}" "${Packages[$i,6]}"
  else
   printf "Executing command: %s" "${Packages[$i,6]}"
   eval "${Packages[$i,6]}"
  fi
 fi
done

# Reverse ../.. location
popd
