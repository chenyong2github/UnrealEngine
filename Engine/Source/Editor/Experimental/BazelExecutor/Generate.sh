#!/bin/bash

vcpkg_root=../../../../ThirdParty/vcpkg/Linux/x64-linux
protoc=${vcpkg_root}/protobuf_x64-linux/tools/protobuf/protoc-3.13.0.0
include=${vcpkg_root}/protobuf_x64-linux/include
grpc_cpp_plugin=${vcpkg_root}/grpc_x64-linux/tools/grpc/grpc_cpp_plugin

mkdir -p ./Generated
pushd ./Schemas
for f in $(find ./ -name '*.proto'); do
	${protoc} -I=${include} --proto_path=./ --cpp_out=../Generated $f
	${protoc} -I=${include} --proto_path=./ --plugin=protoc-gen-grpc=${grpc_cpp_plugin} --grpc_out=../Generated $f
done
popd

for f in $(find ./Generated -name '*.pb.cc'); do
	cat SchemaAutogenHeader.txt >$f.new
	cat SchemaHeader.txt >>$f.new
	cat $f >>$f.new
	cat SchemaFooter.txt >>$f.new

	mv -f $f.new $f
done

for f in $(find ./Generated -name '*.pb.h'); do
	cat SchemaAutogenHeader.txt >>$f.new
	cat $f >>$f.new

	mv -f $f.new $f
done
