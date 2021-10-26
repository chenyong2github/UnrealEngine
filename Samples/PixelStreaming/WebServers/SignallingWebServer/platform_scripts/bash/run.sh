#!/bin/bash
# Copyright Epic Games, Inc. All Rights Reserved.

pushd "$( dirname "${BASH_SOURCE[0]}" )"
bash setup.sh
pushd ../..
sudo node cirrus.js
popd
popd
