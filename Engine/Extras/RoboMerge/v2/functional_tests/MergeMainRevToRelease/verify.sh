#!/bin/bash
set -e

# Ensure //MergeMainRevToRelease/Release/a.txt has a second revision
# Should look like "... headRev 2" with extra whitespace after
echo Ensuring //MergeMainRevToRelease/Release/a.txt has a second revision
HEADREV=`p4 fstat -T headRev //MergeMainRevToRelease/Release/a.txt | awk '{print $3}'`
if [ "$HEADREV" != "2" ]; then
    echo Incorrect head revision of //MergeMainRevToRelease/Release/a.txt. \(Expected: 2 Actual: $HEADREV\)
    exit 1
fi
echo Success!


echo MergeMainRevToRelease testing complete. Success!
