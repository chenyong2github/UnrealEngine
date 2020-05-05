#!/bin/sh

UE_THIRD_PARTY_DIR=`cd "../../../.."; pwd`
TEMP_DIR_RELEASE="/tmp/local-icu-release-$$"
TEMP_DIR_DEBUG="/tmp/local-icu-debug-$$"
BASE_DIR=`cd "../../BuildForUE"; pwd`

OSX_VERSION="10.9"

mkdir $TEMP_DIR_RELEASE
mkdir $TEMP_DIR_DEBUG

cd $TEMP_DIR_RELEASE
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_OSX_DEPLOYMENT_TARGET="$OSX_VERSION" "$BASE_DIR"
make -j4
cp -v ../libicu.a "$BASE_DIR/../lib/Mac/libicu.a"

cd $TEMP_DIR_DEBUG
cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_OSX_DEPLOYMENT_TARGET="$OSX_VERSION" "$BASE_DIR"
make -j4
cp -v ../libicu.a "$BASE_DIR/../lib/Mac/libicud.a"
