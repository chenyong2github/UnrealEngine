#!/bin/bash

cd $(dirname "$0")
source SetupDotnet.sh

LibPath="$HOME/Library/Unreal Engine/P4VUtils"

echo
echo Copying P4VUtils files to '$LibPath'...
mkdir -p "$LibPath"
cp ../../../Extras/P4VUtils/Binaries/* "$LibPath"
cp ../../../Extras/P4VUtils/P4VUtils.ini "$LibPath"
cp ../../../Restricted/NotForLicensees/* "$LibPath" 2> /dev/null

echo
echo Installing P4VUtils into p4v...
dotnet "$LibPath/P4VUtils.dll" install
