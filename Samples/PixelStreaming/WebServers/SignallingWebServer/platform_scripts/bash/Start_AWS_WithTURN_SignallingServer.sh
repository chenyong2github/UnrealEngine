#!/bin/bash
# Copyright Epic Games, Inc. All Rights Reserved.
stunserver=${2:-stun.l.google.com:19302}



pushd "$( dirname "${BASH_SOURCE[0]}" )"

bash Start_AWS_TURNServer.sh

publicip=$(curl http://169.254.169.254/latest/meta-data/public-ipv4)

if [[ -z $publicip ]]; then
    publicip="127.0.0.1"
fi

turnserver=${1:-$publicip":19303"}



echo "Public IP: $publicip"

peerconnectionoptions="{\"iceServers\":[{\"urls\":[\"stun:$stunserver\",\"turn:$turnserver\"],\"username\":\"PixelStreamingUser\",\"credential\":\"AnotherTURNintheroad\"}]}"

process="node"
arguments="cirrus --peerConnectionOptions=\"$peerconnectionoptions\" --publicIp=$publicip"
# Add arguments passed to script to arguments for executable
arguments+=" $@"

pushd ../..
echo "Running: $process $arguments"
sudo $process $arguments
popd

popd
