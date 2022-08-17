#!/bin/sh

set -e

# First we check if nodejs is installed
if ! command -v node > /dev/null ; then
  echo "ERROR: Couldn't find node.js installed..., Please install latest nodejs from https://nodejs.org/en/download/"
  exit 1
fi

# Check if npm is installed
if ! command -v npm > /dev/null ; then
  echo "ERROR: Couldn't find npm installed..., Please install npm"
  exit 1
fi

# Let's check if it is a modern nodejs
VERSION=$(node -e "console.log( process.versions.node.split('.')[0] );")
echo "Found Node.js version ${VERSION}"

if [ ${VERSION} -lt 14 ] ; then
  echo "ERROR: installed node.js version is too old (${VERSION}) :\( Please install latest nodejs from https://nodejs.org/en/download/"
  exit 1
fi

if [ ${VERSION} -ge 17 ] ; then
  #Due to changes on Node.js v17, --openssl-legacy-provider was added for handling key size on OpenSSL v3
  export NODE_OPTIONS=--openssl-legacy-provider
fi


FOLDER=$(dirname "$0")
node ${FOLDER}/Scripts/start.js "$@"

