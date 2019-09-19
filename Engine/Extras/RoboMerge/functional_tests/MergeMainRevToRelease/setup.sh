#!/bin/bash
set -e

# Setup depot
p4 depot -t stream -i < data/Depot.spec

# Setup streams
p4 stream -i < data/Mainline.spec
p4 stream -i < data/Release.spec

# Setup mainline workspace 'testuser1_MergeMainRevToRelease_Main' with test data
# Create workspace
mkdir -p /p4testworkspaces/testuser1_MergeMainRevToRelease_Main
p4 -u testuser1 client -i < data/testuser1_MergeMainRevToRelease_Main.spec

# Add file 'a.txt'
echo "Simple Unit Test File" > /p4testworkspaces/testuser1_MergeMainRevToRelease_Main/a.txt
p4 -u testuser1 -c testuser1_MergeMainRevToRelease_Main add /p4testworkspaces/testuser1_MergeMainRevToRelease_Main/a.txt
p4 -u testuser1 -c testuser1_MergeMainRevToRelease_Main submit -d "Adding Initial File 'a.txt'"

# Populate Release stream
p4 -u testuser1 populate -S //MergeMainRevToRelease/Release -r -d "Initial branch of files from Main (//MergeMainRevToRelease/Main) to Release (//MergeMainRevToRelease/Release)"


