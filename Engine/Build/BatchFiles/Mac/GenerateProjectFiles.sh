#!/bin/sh

echo
echo Setting up Unreal Engine 4 project files...
echo

# If ran from somewhere other then the script location we'll have the full base path
BASE_PATH="`dirname "$0"`"

# this is located inside an extra 'Mac' path unlike the Windows variant.

if [ ! -d "$BASE_PATH/../../../Binaries/DotNET" ]; then
 echo GenerateProjectFiles ERROR: It looks like you're missing some files that are required in order to generate projects.  Please check that you've downloaded and unpacked the engine source code, binaries, content and third-party dependencies before running this script.
 exit 1
fi

if [ ! -d "$BASE_PATH/../../../Source" ]; then
 echo GenerateProjectFiles ERROR: This script file does not appear to be located inside the Engine/Build/BatchFiles/Mac directory.
 exit 1
fi

. "$BASE_PATH/SetupMono.sh" "$BASE_PATH"

# make sure the UBT project has references to auto-discovered platform extension source files
"${BASE_PATH}/../FindPlatformExtensionSources.sh"

if [ -f "$BASE_PATH/../../../Source/Programs/UnrealBuildTool/UnrealBuildTool.csproj" ]; then
	xbuild "$BASE_PATH/../../../Source/Programs/UnrealBuildTool/UnrealBuildTool.csproj" /property:Configuration="Development" /verbosity:quiet /nologo /p:NoWarn=1591 |grep -i error
fi

WANT_AOT="`defaults read com.epicgames.ue4 MonoAOT`"
OPENSSL="./../../../Binaries/DotNET/IOS/openssl.exe"
if [ ! -z $WANT_AOT ]; then
	if [ $WANT_AOT == "1" ]; then
			for i in $BASE_PATH/../../../Binaries/DotNET/*.dll;
			do
				if test "$i" -nt "$i.dylib"; then
					echo Compiling $i to native...
					mono --aot $i > /dev/null 2>&1;
				fi
			done

			for i in $BASE_PATH/../../../Binaries/DotNET/*.exe;
			do
				if test "$i" -nt "$i.dylib"; then
					echo Compiling $i to native...
					mono --aot $i > /dev/null 2>&1;
				fi
			done

			for i in $BASE_PATH/../../../Binaries/DotNET/IOS/*.dll;
			do
				if test "$i" -nt "$i.dylib"; then
					echo Compiling $i to native...
					mono --aot $i > /dev/null 2>&1;
				fi
			done

			for i in $BASE_PATH/../../../Binaries/DotNET/IOS/*.exe;
			do
				if test "$i" -nt "$i.dylib"; then
					if [ $i != $OPENSSL ]; then
						echo Compiling $i to native...
						mono --aot $i > /dev/null 2>&1;
					fi
				fi
			done
	fi
fi

# pass all parameters to UBT
mono "$BASE_PATH/../../../Binaries/DotNET/UnrealBuildTool.exe" -projectfiles "$@"
