#!/bin/bash
# Copyright Epic Games, Inc. All Rights Reserved.

pushd "$( dirname "${BASH_SOURCE[0]}" )"
cd ../..

publicip=$(curl -s https://api.ipify.org)
if [[ -z $publicip ]]; then
    publicip="127.0.0.1"
fi

arguments="--PublicIP=${publicip}"
echo $arguments

npm start -- $arguments

popd
