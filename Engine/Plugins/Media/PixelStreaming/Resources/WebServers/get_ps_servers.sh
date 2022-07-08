#!/bin/bash
# Copyright Epic Games, Inc. All Rights Reserved.

BASH_LOCATION=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

pushd "${BASH_LOCATION}" > /dev/null

print_help() {
 echo "
 Tool for fetching PixelStreaming Infrastructure.

 Usage:
   ${0} [-h] [-b <branch>] [-t <tag>] [-p <personal access token>]
 Where:
   -b      Specify a specific branch for the tool to download from repo (If this and -t are not set will default to recommended version)
   -t      Specify a specific tag for the tool to download from repo (If this and -b are not set will default to recommended version)
   -p      Specify a GitHub Personal Access Token to use to authorize requests (only necessary if repo is private)
   -h      Display this help message
"
 exit 1
}

while(($#)) ; do
  case "$1" in
   -h ) print_help;;
   -b ) PSInfraTagOrBranch="$2"; IsTag=0; shift 2;;
   -t ) PSInfraTagOrBranch="$2"; IsTag=1; shift 2;;
   -p ) GitHubAccessToken="$2"; shift 2;;
   * ) echo "Unknown command: $1"; shift;;
  esac
 done

# Name and version of ps-infra that we are downloading
PSInfraOrg=EpicGames
PSInfraRepo=PixelStreamingInfrastructure

if [ -z "$PSInfraTagOrBranch" ]
then
    PSInfraTagOrBranch=v0.1.0-prerelease
    IsTag=0
fi

if [ "$IsTag" -eq 1 ]
then
    RefType=tags
else
    RefType=heads
fi

# Look for a SignallingWebServer directory next to this script
if [ -d SignallingWebServer ]
then
  echo "SignallingWebServer directory found...skipping install."
else
  echo "SignallingWebServer directory not found...beginning ps-infra download."

  if [ ! -z "$GitHubAccessToken" ]
  then
    # Download ps-infra with authentication and follow redirects.
    curl -H "Accept: application/vnd.github.v3+json" -H "Authorization: token $GitHubAccessToken" -L https://api.github.com/repos/$PSInfraOrg/$PSInfraRepo/tarball/$PSInfraTagOrBranch > ps-infra.tar.gz
  else
    # Download ps-infra and follow redirects.
    curl -L https://github.com/$PSInfraOrg/$PSInfraRepo/archive/refs/$RefType/$PSInfraTagOrBranch.tar.gz > ps-infra.tar.gz
  fi
  
  # Unarchive the .tar
  tar -xmf ps-infra.tar.gz || $(echo "bad archive, contents:" && head --lines=20 ps-infra.tar.gz && exit 0)

  # Move the server folders into the current directory (WebServers) and delete the original directory
  mv EpicGames-PixelStreamingInfrastructure-*/* .
  rm -rf EpicGames-PixelStreamingInfrastructure-*

  # Delete the downloaded tar
  rm ps-infra.tar.gz
fi