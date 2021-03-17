#!/usr/bin/env bash

# Determine which release of the Unreal Engine we will be running container images for
UNREAL_ENGINE_RELEASE=master
if [[ ! -z "$1" ]]; then
	UNREAL_ENGINE_RELEASE="$1"
fi

# Build and run the Pixel Streaming example
export UNREAL_ENGINE_RELEASE
docker-compose up --build
