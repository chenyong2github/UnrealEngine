#!/bin/bash
# Copyright Epic Games, Inc. All Rights Reserved.

pushd "$( dirname "${BASH_SOURCE[0]}" )"

bash setup.sh



# Run node server
# If running with matchmaker web server and accessing outside of localhost pass in --publicIp=<ip_of_machine>
bash Start_AWS_SignallingServer.sh "$@"

popd