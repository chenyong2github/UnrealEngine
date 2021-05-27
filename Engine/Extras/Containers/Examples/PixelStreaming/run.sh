#!/usr/bin/env bash

# Determine which release of the Unreal Engine we will be running container images for
UNREAL_ENGINE_RELEASE="4.27"
if [[ ! -z "$1" ]]; then
	UNREAL_ENGINE_RELEASE="$1"
fi

# Determine whether we are instructing Docker Compose to rebuild the project container image even if it already exists
COMPOSE_FLAGS=""
if [[ "$*" == *"--rebuild"* ]]; then
	COMPOSE_FLAGS="--build"
fi

# Determine whether we are forcing the use of TURN relaying (useful for testing purposes)
EXTRA_PEERCONNECTION_OPTIONS=""
if [[ "$*" == *"--force-turn"* ]]; then
	EXTRA_PEERCONNECTION_OPTIONS=', "iceTransportPolicy": "relay"'
fi


# Verify that either curl or wget is available
if which curl 1>/dev/null; then
	HTTPS_COMMAND="curl -s"
elif which wget 1>/dev/null; then
	HTTPS_COMMAND="wget -O - -q"
else 
	echo "Please install curl or wget"
	exit 1
fi

# Retrieve the public IP address of the host system
PUBLIC_IP=$($HTTPS_COMMAND 'https://api.ipify.org')

# Run the Pixel Streaming example
export UNREAL_ENGINE_RELEASE
export EXTRA_PEERCONNECTION_OPTIONS
export PUBLIC_IP
export PWD=$(pwd)
docker-compose up --force-recreate $COMPOSE_FLAGS
