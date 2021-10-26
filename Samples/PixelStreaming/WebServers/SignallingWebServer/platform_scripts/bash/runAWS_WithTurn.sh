#!/bin/bash
# Copyright Epic Games, Inc. All Rights Reserved.

pushd "$( dirname "${BASH_SOURCE[0]}" )"

bash setup.sh




bash Start_AWS_WithTURN_SignallingServer.sh

popd