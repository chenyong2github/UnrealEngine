#!/bin/bash
pushd "$( dirname "${BASH_SOURCE[0]}" )"
bash setup.sh
pushd ../..
sudo node cirrus.js
popd
popd
