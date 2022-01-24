#!/bin/bash
# Copyright Epic Games, Inc. All Rights Reserved.

source turn_user_pwd.sh
source Start_Common.sh

set_start_default_values "y" "n" # TURN server defaults only
use_args "$@"
call_setup_sh
print_parameters

localip=$(hostname -I | awk '{print $1}')
echo "Private IP: $localip"

turnport="${turnserver##*:}"
if [ -z "${turnport}" ]; then
	turnport=3478
fi
echo "TURN port: ${turnport}"
echo ""

pushd "$( dirname "${BASH_SOURCE[0]}" )"

# Hmm, plain text
realm="PixelStreaming"
process="turnserver"
arguments="-p ${turnport} -r $realm -X $publicip -E $localip -L $localip --no-cli --no-tls --no-dtls --pidfile /var/run/turnserver.pid -f -a -v -n -u ${turnusername}:${turnpassword}"

# Add arguments passed to script to arguments for executable
arguments+=" ${cirruscmd}"

pushd ../..
echo "Running: $process $arguments"
# pause
sudo $process $arguments &
popd

popd
