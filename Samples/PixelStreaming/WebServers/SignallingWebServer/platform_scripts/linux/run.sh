#!/bin/sh
sh setup.sh
pushd ../..
sudo node cirrus.js
popd
