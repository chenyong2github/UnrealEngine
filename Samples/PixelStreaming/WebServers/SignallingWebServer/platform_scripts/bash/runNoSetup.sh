#!/bin/bash
# Copyright Epic Games, Inc. All Rights Reserved.

pushd "$( dirname "${BASH_SOURCE[0]}" )"
pushd ../..
sudo node cirrus.js
popd
popd
