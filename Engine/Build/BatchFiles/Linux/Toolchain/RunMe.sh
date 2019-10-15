#!/bin/bash

set -eu

SCRIPT_DIR=$(cd "$(dirname "$BASH_SOURCE")" ; pwd)
SCRIPT_NAME=$(basename "$BASH_SOURCE")

# https://stackoverflow.com/questions/23513045/how-to-check-if-a-process-is-running-inside-docker-container
if ! $(grep -q "/docker/" /proc/1/cgroup); then

  ##############################################################################
  # host commands
  ##############################################################################

  ImageName=build_linux_toolchain

  echo docker run -t --name ${ImageName} -v "${SCRIPT_DIR}:/src" centos:7 /src/${SCRIPT_NAME}
  docker run -t --name ${ImageName} -v "${SCRIPT_DIR}:/src" centos:7 /src/${SCRIPT_NAME}

  echo Removing ${ImageName}...
  docker rm ${ImageName}

else

  DOCKER_BUILD_DIR=/src/build

  if [ $UID -eq 0 ]; then
    ##############################################################################
    # docker root commands
    ##############################################################################
    yum install -y epel-release centos-release-scl
    yum install -y ncurses-devel patch llvm-toolset-7 llvm-toolset-7-llvm-devel make cmake3 tree zip \
        git wget which gcc-c++ gperf bison flex texinfo bzip2 help2man file unzip autoconf libtool \
        glibc-static libstdc++-devel libstdc++-static mingw64-gcc mingw64-gcc-c++ mingw64-winpthreads-static

    # Create non-privileged user and workspace
    adduser buildmaster
    mkdir -p ${DOCKER_BUILD_DIR}
    chown buildmaster:nobody -R ${DOCKER_BUILD_DIR}

    exec su buildmaster "$0"
  fi

  ##############################################################################
  # docker user level commands
  ##############################################################################
  cd ${DOCKER_BUILD_DIR}
  /src/build_linux_toolchain.sh

fi
