#!/bin/bash

# Setup depot
p4 depot -t stream -i < Depot.spec

# Setup stream
p4 stream -i < Mainline.spec

# Setup root workspace for adding initial RoboMergeData
p4 -u root client -i < root_RoboMergeData_Main.spec

# Add blank branch map
p4 -u root -c root_RoboMergeData_Main add /p4testworkspaces/root_RoboMergeData_Main/email-template.html
p4 -u root -c root_RoboMergeData_Main add /p4testworkspaces/root_RoboMergeData_Main/test.branchmap.json
p4 -u root -c root_RoboMergeData_Main submit -d "Adding Blank Robomerge Branchmap file"
