#!/bin/sh

if [ ! -d "$NDKROOT" ]; then
	echo Unable to locate local Android NDK location. Did you run SetupAndroid to install it?
	read -rsp $'Press any key to continue...\n' -n1 key
	exit 1
fi
echo Replacing ld.lld at $NDKROOT/toolchains/llvm/prebuilt/darwin-x86_64/bin
chmod +x ld.lld
cp ld.lld "%NDKROOT%"/toolchains/llvm/prebuilt/darwin-x86_64/bin

exit 0
