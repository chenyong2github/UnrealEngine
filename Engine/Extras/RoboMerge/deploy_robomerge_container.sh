#!/bin/sh

# Command-line arguments

# hub.companyname.net/companyname/robomerge-ts-service:2.0.0.trunk.4439355.46
DOCKER_IMAGE=$1
if [ "$DOCKER_IMAGE" = "" ]; then
    echo "Docker image argument is empty!"
    exit -1
fi

# robomerge-engine-ts
CONTAINER_NAME=$2
if [ "$CONTAINER_NAME" = "" ]; then
    echo "Container name argument is empty!"
    exit -1
fi

# P$ Auth Password/Secret
P4PASSWD=$3
if [ "$P4PASSWD" = "" ]; then
    echo "P4PASSWD argument is empty!"
    exit -1
fi

# engine,vp
BOTS=$4
if [ "$BOTS" = "" ]; then
    echo "Robomerge bots argument is empty!"
    exit -1
fi

# https://robomerge.companyname.net:4433
ROBO_EXTERNAL_URL=$5
if [ "$ROBO_EXTERNAL_URL" = "" ]; then
    echo "External URL argument is empty!"
    exit -1
fi

# -p4433:4433 -p8080:8080
shift 5
PORTS_ARGS=$@
if [ "$PORTS_ARGS" = "" ]; then
    echo "Robomerge ports argument is empty!"
    exit -1
fi

# Echo commands
set -x

# Docker pull latest
docker pull $DOCKER_IMAGE

# Stop any running container
docker stop $CONTAINER_NAME
docker rm $CONTAINER_NAME

# Start Robomerge service
docker run -d --name $CONTAINER_NAME --hostname $CONTAINER_NAME \
    -e "P4PASSWD=$P4PASSWD" \
    -e "BOTNAME=$BOTS" \
    -e "ROBO_EXTERNAL_URL=$ROBO_EXTERNAL_URL" \
    $PORTS_ARGS \
    -v /home/admin/robo-vault:/vault:ro -v robosettings:/root/.robomerge \
    $DOCKER_IMAGE

# Check for errors
ERR=$?
if [ "$ERR" -ne "0" ]; then
   echo Error \($ERR\) encountered deploying $CONTAINER_NAME, please check logs.
   exit 1
fi

# Remove unused Docker images (ignore errors -- this can error if two deploys happen at the same time.)
docker image prune -fa || true