#!/bin/bash -e
# Copyright Epic Games, Inc. All Rights Reserved.

ROOT_DIR=$(readlink -f "$(dirname "$BASH_SOURCE")/../..")

docker run --rm -t -v "${ROOT_DIR}":/build         centos:8 /build/BuildForUE/Linux/BuildForLinux.sh  x86_64-unknown-linux-gnu     /tmp/build
docker run --rm -t -v "${ROOT_DIR}":/build arm64v8/centos:8 /build/BuildForUE/Linux/BuildForLinux.sh aarch64-unknown-linux-gnueabi /tmp/build
