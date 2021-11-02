#!/bin/bash
# Copyright Epic Games, Inc. All Rights Reserved.

source Start_Common.sh

set_start_default_values "y" "y" # Set both TURN and STUN server defaults
use_args "$@"
call_setup_sh
print_parameters

pushd "$( dirname "${BASH_SOURCE[0]}" )"

bash Start_TURNServer.sh --turn "${turnserver}"

peerconnectionoptions="{\"iceServers\":[{\"urls\":[\"stun:$stunserver\",\"turn:$turnserver\"],\"username\":\"PixelStreamingUser\",\"credential\":\"AnotherTURNintheroad\"}]}"

process="node"
arguments="cirrus --peerConnectionOptions=\"$peerconnectionoptions\" --publicIp=$publicip"
# Add arguments passed to script to arguments for executable
arguments+=" ${cirruscmd}"

pushd ../..
echo "Running: $process $arguments"
sudo $process $arguments
popd

popd
