#!/bin/sh

if [ ! -d "$MLSDK" ]; then
	echo Unable to locate Magic Leap SDK location.
	read -rsp $'Press any key to continue...\n' -n1 key
	exit 1
fi
echo Replacing ld.lld at $MLSDK/tools/toolchains/llvm-8/bin
chmod +x ld.lld
cp ld.lld "%MLSDK%"/tools/toolchains/llvm-8/bin

exit 0
