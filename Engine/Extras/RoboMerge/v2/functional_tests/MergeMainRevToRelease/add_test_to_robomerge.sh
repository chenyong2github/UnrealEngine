#!/bin/bash
set -e

# Setup mainline workspace 'RoboMergeData_MergeMainRevToRelease' with test data
# Create workspace
mkdir -p /p4testworkspaces/RoboMergeData_MergeMainRevToRelease
p4 -u root client -i < data/root_RoboMergeData_MergeMainRevToRelease.spec

# Sync current test branchmap
UNIT_TEST_DIR=`pwd`
cd /p4testworkspaces/RoboMergeData_MergeMainRevToRelease
p4 -u root -c RoboMergeData_MergeMainRevToRelease sync 

# Add branches via jq
jq -s '.[0] * .[1]' test.branchmap.json $UNIT_TEST_DIR/data/branches.json > output.json

# Edit branchmap and check back in
p4 -u root -c RoboMergeData_MergeMainRevToRelease edit test.branchmap.json
mv -f output.json test.branchmap.json
p4 -u root -c RoboMergeData_MergeMainRevToRelease submit -d "Adding MergeMainRevToRelease branches to test branchmap"

# Change directory back
cd $UNIT_TEST_DIR