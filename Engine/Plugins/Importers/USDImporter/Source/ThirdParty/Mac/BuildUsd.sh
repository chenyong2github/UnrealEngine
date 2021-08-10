#!/bin/bash

export MACOSX_DEPLOYMENT_TARGET=10.14

python src/build_scripts/build_usd.py ./Build --no-tests --no-examples --no-tutorials --no-tools --no-docs --no-imaging
