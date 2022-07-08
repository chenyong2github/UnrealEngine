@Rem Copyright Epic Games, Inc. All Rights Reserved.

@echo off

@Rem Set script location as working directory for commands.
pushd "%~dp0"

:arg_loop_start
SET ARG=%1
if DEFINED ARG (
    if "%ARG%"=="/h" (
        goto print_help
    )
    if "%ARG%"=="/p" (
        SET GitHubAccessToken=%2
        SHIFT
    )
    if "%ARG%"=="/b" (
        SET PSInfraTagOrBranch=%2
        SET IsTag=0
        SHIFT
    )
    if "%ARG%"=="/t" (
        SET PSInfraTagOrBranch=%2
        SET IsTag=1
        SHIFT
    )
    SHIFT
    goto arg_loop_start
)

@Rem Name and version of ps-infra that we are downloading
SET PSInfraOrg=EpicGames
SET PSInfraRepo=PixelStreamingInfrastructure

if NOT DEFINED PSInfraTagOrBranch (
    SET PSInfraTagOrBranch=v0.1.0-prerelease
    SET IsTag=1
)

if %IsTag%==1 (
  SET RefType=tags
) else (
  SET RefType=heads
)

@Rem Look for a SignallingWebServer directory next to this script
if exist SignallingWebServer\ (
  echo SignallingWebServer directory found...skipping install.
) else (
  echo SignallingWebServer directory not found...beginning ps-infra download.

  if DEFINED GitHubAccessToken (
    @Rem Download ps-infra with authentication and follow redirects.
    curl -H "Accept: application/vnd.github.v3+json" -H "Authorization: token %GitHubAccessToken%" -L https://api.github.com/repos/%PSInfraOrg%/%PSInfraRepo%/zipball/%PSInfraTagOrBranch% > ps-infra.zip
  ) else (
    @Rem Download ps-infra and follow redirects.
    curl -L https://github.com/%PSInfraOrg%/%PSInfraRepo%/archive/refs/%RefType%/%PSInfraTagOrBranch%.zip > ps-infra.zip
  )
  
  @Rem Unarchive the .zip
  tar -xmf ps-infra.zip || echo bad archive, contents: && type ps-infra.zip && exit 0

  @Rem Rename the extracted, versioned, directory
  for /d %%i in ("PixelStreamingInfrastructure-*") do (
    for /d %%j in ("%%i/*") do (
      echo "%%i\%%j"
      move "%%i\%%j" .
    )
    for %%j in ("%%i/*") do (
      echo "%%i\%%j"
      move "%%i\%%j" .
    )

    echo "%%i"
    rmdir /s /q "%%i"
  )

  @Rem Delete the downloaded zip
  del ps-infra.zip
)

exit 0

:print_help
echo "Tool for fetching PixelStreaming Infrastructure"
echo ""
echo "Usage:"
echo "  %0 [/h] [/b <branch>] [/t <tag>] [/p <personal access token>]"
echo "Where:"
echo "  /b      Specify a specific branch for the tool to download from repo (If this and /t are not set will default to recommended version)"
echo "  /t      Specify a specific tag for the tool to download from repo (If this and /b are not set will default to recommended version)"
echo "  /p      Specify a GitHub Personal Access Token to use to authorize requests (only necessary if repo is private)"
echo "  /h      Display this help message"
exit 1