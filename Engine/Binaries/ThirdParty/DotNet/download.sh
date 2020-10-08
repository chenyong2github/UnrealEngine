#!/bin/bash
# Copyright Epic Games, Inc. All Rights Reserved.

destFolder=./Unknown
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
  destFolder=./Linux
elif [[ "$OSTYPE" == "darwin"* ]]; then
  destFolder=./Mac
else
  echo "Unknown OS!"
  exit 1
fi

source <(curl -s https://dotnet.microsoft.com/download/dotnet-core/scripts/v1/dotnet-install.sh) --no-path --channel 3.1 --install-dir "$destFolder"
