#!/bin/bash
set -e

# Create new revision on a.txt
p4 -u testuser1 -c testuser1_MergeMainRevToRelease_Main edit /p4testworkspaces/testuser1_MergeMainRevToRelease_Main/a.txt
echo "Simple Unit Test File Rev. #2" > /p4testworkspaces/testuser1_MergeMainRevToRelease_Main/a.txt
p4 -u testuser1 -c testuser1_MergeMainRevToRelease_Main submit -d "Committing revision to 'a.txt'"