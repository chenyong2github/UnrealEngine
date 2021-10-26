#!/bin/bash
# Copyright Epic Games, Inc. All Rights Reserved.
stunserver=${1:-stun.l.google.com:19302}

publicip=$(curl http://169.254.169.254/latest/meta-data/public-ipv4)

echo "Public IP: $publicip"

peerconnectionoptions="{\"iceServers\":[{\"urls\":[\"stun:$stunserver\"]}]}"

process="node"
arguments="cirrus --peerConnectionOptions=\"$peerconnectionoptions\" --publicIp=$publicip"
# Add arguments passed to script to arguments for executable
arguments+=" $@"

pushd ../..
echo "Running: $process $arguments"
sudo $process $arguments
popd