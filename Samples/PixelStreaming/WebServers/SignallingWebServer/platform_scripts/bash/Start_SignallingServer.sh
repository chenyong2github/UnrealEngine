#!/bin/bash
# Copyright Epic Games, Inc. All Rights Reserved.

source Start_Common.sh

set_start_default_values "n" "y" # Only STUN server defaults
use_args "$@"
call_setup_sh
print_parameters

peerconnectionoptions="{\"iceServers\":[{\"urls\":[\"stun:${stunserver}\"]}]}"

process="node"
arguments="cirrus --peerConnectionOptions=\"${peerconnectionoptions}\" --publicIp=${publicip}"
# Add arguments passed to script to arguments for executable
arguments+=" ${cirruscmd}"

pushd ../..
echo "Running: $process $arguments"
sudo $process $arguments
popd
