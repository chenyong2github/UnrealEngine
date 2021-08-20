#!/bin/bash
# Copyright Epic Games, Inc. All Rights Reserved.
set -e

required_npm_ver="\"6.14.4\""
required_node_ver="\"10.19.0\""

version_greater_equal()
{
    printf '%s\n%s\n' "$2" "$1" | sort --check=quiet --version-sort
}

pushd "$( dirname "${BASH_SOURCE[0]}" )"
pushd ../..

# Check if npm is currently installed
if ! which npm > /dev/null; then
    echo "Installing npm"
    # Install node and npm from apt
    sudo apt-get install -y nodejs-dev node-gyp libssl1.0-dev
    sudo apt-get install -y npm
fi

npm_json=$(jq -n -f <(npm version | sed 's/'\''/"/g'))
if jq -e . >/dev/null 2>&1 <<< $npm_json; then
    echo "Parsed version JSON successfully"
    node_ver=$(echo $npm_json | jq '.node')
    npm_ver=$(echo $npm_json | jq '.npm' )

    echo "Required npm version: $required_npm_ver"
    echo "Current npm version: $npm_ver"

    echo "Required node version: $required_node_ver"
    echo "Current node version: $node_ver"

    if ! version_greater_equal $node_ver $required_node_ver; then
        echo "node doesn't meet the required version... updating"
        sudo npm cache clean -f
        # Install the n package
        sudo npm install -g n
        # Use n to install latest version of node
        sudo n latest
        # Update path so bash uses the binaries of the latest versions of node
        PATH="$PATH"
        # Remove the apt version of node
        sudo apt-get remove -y nodejs
        echo "New node version: $(echo $(jq -n -f <(npm version | sed 's/'\''/"/g')) | jq '.node' )"
    fi

    if ! version_greater_equal $npm_ver $required_npm_ver; then
        echo "npm doesn't meet the required version... updating"
        sudo npm cache clean -f
        sudo npm install -g npm
        # Update path so bash uses the binaries of the latest versions of npm
        PATH="$PATH"
        # Remove the apt version of npm
        sudo apt-get remove -y npm
        echo "New npm version: $(echo $(jq -n -f <(npm version | sed 's/'\''/"/g')) | jq '.npm' )"
    fi
else
    echo "Failed to parse JSON"
fi

PATH="$PATH"
sudo npm install
popd
popd