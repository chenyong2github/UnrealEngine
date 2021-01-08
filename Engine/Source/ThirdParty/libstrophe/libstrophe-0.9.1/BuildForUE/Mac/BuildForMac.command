#!/bin/sh

# Copyright Epic Games, Inc. All Rights Reserved.

LIB_NAME="libstrophe"

# Drops from the location of this script to the relative root location of LIBFILES
DROP_TO_LIBROOT=../..

# Drops from the location of LIBROOT to Engine/Source/ThirdParty
DROP_TO_THIRDPARTY=../..

# Files we build, relative to LIBROOT
LIBFILES=( "Mac/Debug/libstrophe.a" "Mac/Release/libstrophe.a" )

##
## Common setup steps
##

UE_ARCHS=( "x86_64" "arm64" )

# Build script will be in <lib>/Build/Mac so get that path and drop two folders to leave us in
# the actual lib folder.
pushd . > /dev/null
SCRIPT_DIR="`dirname "${BASH_SOURCE[0]}"`"
cd ${SCRIPT_DIR}/${DROP_TO_LIBROOT}
LIB_ROOT_DIR=${PWD}
echo Changed to ${LIB_ROOT_DIR}

# We should be in ThirdParty/<libname>/<libname-version> and we want to pull in some command
# things from ThirdParty/BuildScripts/Mac/Common.
source ${DROP_TO_THIRDPARTY}/BuildScripts/Mac/Common/Common.sh

# Create a temp-dir and save it (note the TMPDIR variable is used by the functions that check
# file state).
TMPDIR="/tmp/${LIB_NAME}-$$"
mkdir -p ${TMPDIR}/${UE_ARCHS[0]} > /dev/null 2>&1
mkdir -p ${TMPDIR}/${UE_ARCHS[1]} > /dev/null 2>&1

echo Rebuilding ${LIB_NAME} using temp path ${TMPDIR}

# Checkout the library list and save their state
checkoutFiles ${LIBFILES[@]}
saveFileStates ${LIBFILES[@]}

##
## libstrophe specific steps
##

UE_SYSROOT=`xcrun --sdk macosx --show-sdk-path`
UE_SYSROOT_CFLAGS="-isysroot ${UE_SYSROOT}"

UE_OTHER_C_CXX_LD_FLAGS="-mmacosx-version-min=10.14 -gdwarf-2"

UE_SSL_INCDIR=$(cd ${DROP_TO_THIRDPARTY} && pwd)/OpenSSL/1.1.1/Include/Mac
UE_SSL_LIBDIR=$(cd ${DROP_TO_THIRDPARTY} && pwd)/OpenSSL/1.1.1/Lib/Mac
UE_SSL_BINDIR=$(cd ${DROP_TO_THIRDPARTY}/../../Binaries/ThirdParty && pwd)/OpenSSL/Mac
UE_SSL_CFLAGS="-I${UE_SSL_INCDIR}"
#UE_SSL_LIBS="-Wl,-force_load ${UE_SSL_LIBDIR}/libcrypto.a -Wl,-force_load ${UE_SSL_LIBDIR}/libssl.a"
UE_SSL_LIBS="-L${UE_SSL_BINDIR} -lcrypto -lssl"

UE_EXPAT_INCDIR=$(cd ${DROP_TO_THIRDPARTY} && pwd)/Expat/expat-2.2.0/lib
UE_EXPAT_LIBDIR=$(cd ${DROP_TO_THIRDPARTY} && pwd)/Expat/expat-2.2.0/Mac/Release
UE_EXPAT_BINDIR=$(cd ${DROP_TO_THIRDPARTY}/../../Binaries/ThirdParty && pwd)/Expat/Mac
UE_EXPAT_CFLAGS="-I${UE_EXPAT_INCDIR}"
#UE_EXPAT_LIBS="${UE_EXPAT_LIBDIR}/libexpat.a"
UE_EXPAT_LIBS="-L${UE_EXPAT_BINDIR} -lexpat"

UE_ZLIB_INCDIR=$(cd ${DROP_TO_THIRDPARTY} && pwd)/zlib/v1.2.8/include/Mac
UE_ZLIB_LIBDIR=$(cd ${DROP_TO_THIRDPARTY} && pwd)/zlib/v1.2.8/lib/Mac
UE_ZLIB_CFLAGS="-I${UE_ZLIB_INCDIR}"
#UE_ZLIB_LIBS="${UE_ZLIB_LIBDIR}/libz.a"
UE_ZLIB_LIBS="-L${UE_ZLIB_LIBDIR} -lz"

./bootstrap.sh

# x86_64
./configure --host=x86_64-apple-darwin19.6.0 \
          --prefix="${TMPDIR}/${UE_ARCHS[0]}" \
    openssl_CFLAGS="${UE_SSL_CFLAGS}" \
      openssl_LIBS="${UE_SSL_LIBS}" \
      expat_CFLAGS="${UE_EXPAT_CFLAGS}" \
        expat_LIBS="${UE_EXPAT_LIBS}" \
            CFLAGS="${UE_SYSROOT_CFLAGS} -arch ${UE_ARCHS[0]} ${UE_OTHER_C_CXX_LD_FLAGS} ${UE_ZLIB_CFLAGS}" \
          CPPFLAGS="${UE_SYSROOT_CFLAGS} -arch ${UE_ARCHS[0]} ${UE_OTHER_C_CXX_LD_FLAGS} ${UE_ZLIB_CFLAGS}" \
           LDFLAGS="${UE_SYSROOT_CFLAGS} -arch ${UE_ARCHS[0]} ${UE_OTHER_C_CXX_LD_FLAGS} ${UE_ZLIB_LIBS}" \
                CC="`xcrun -f clang`"
make -j$(get_core_count)
make install

if [ "$BUILD_UNIVERSAL" = true ] ; then
    make clean
    make distclean

    # arm64
    ./configure --host=aarch64-apple-darwin19.6.0 \
              --prefix="${TMPDIR}/${UE_ARCHS[1]}" \
        openssl_CFLAGS="${UE_SSL_CFLAGS}" \
          openssl_LIBS="${UE_SSL_LIBS}" \
          expat_CFLAGS="${UE_EXPAT_CFLAGS}" \
            expat_LIBS="${UE_EXPAT_LIBS}" \
                CFLAGS="${UE_SYSROOT_CFLAGS} -arch ${UE_ARCHS[1]} ${UE_OTHER_C_CXX_LD_FLAGS} ${UE_ZLIB_CFLAGS}" \
              CPPFLAGS="${UE_SYSROOT_CFLAGS} -arch ${UE_ARCHS[1]} ${UE_OTHER_C_CXX_LD_FLAGS} ${UE_ZLIB_CFLAGS}" \
               LDFLAGS="${UE_SYSROOT_CFLAGS} -arch ${UE_ARCHS[1]} ${UE_OTHER_C_CXX_LD_FLAGS} ${UE_ZLIB_LIBS}" \
                    CC="`xcrun -f clang`"
    make -j$(get_core_count)
    make install

    # lipo the results into universal binaries
    lipo -create ${TMPDIR}/${UE_ARCHS[0]}/lib/libstrophe.a ${TMPDIR}/${UE_ARCHS[1]}/lib/libstrophe.a -output ./Mac/Release/libstrophe.a
else
    cp -v ${TMPDIR}/${UE_ARCHS[0]}/lib/libstrophe.a ./Mac/Relesae/libstrophe.a
fi

cp -v ./Mac/Release/libstrophe.a ./Mac/Debug/libstrophe.a

make clean > /dev/null 2>&1
make distclean > /dev/null 2>&1

# check the files were all touched
checkFilesWereUpdated ${LIBFILES[@]}
checkFilesAreFatBinaries ${LIBFILES[@]}

echo The following files were rebuilt: ${LIBFILES[@]}

popd > /dev/null
