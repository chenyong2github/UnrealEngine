// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContentAddressableStorage.h"
#include "HordeExecutorModule.h"
#include "ProtoConverter.h"

THIRD_PARTY_INCLUDES_START
UE_PUSH_MACRO("TEXT")
#undef TEXT
#include <grpcpp/grpcpp.h>
UE_POP_MACRO("TEXT");
THIRD_PARTY_INCLUDES_END


FContentAddressableStorage::FContentAddressableStorage(const std::shared_ptr<grpc::Channel>& Channel) :
	Stub(build::bazel::remote::execution::v2::ContentAddressableStorage::NewStub(Channel))
{
}

FContentAddressableStorage::~FContentAddressableStorage()
{
}

FStatus FContentAddressableStorage::FindMissingBlobs(const FFindMissingBlobsRequest& Request, FFindMissingBlobsResponse& Response)
{
	grpc::ClientContext Context;

	build::bazel::remote::execution::v2::FindMissingBlobsRequest ProtoRequest;
	ProtoConverter::ToProto(Request, ProtoRequest);

	build::bazel::remote::execution::v2::FindMissingBlobsResponse ProtoResponse;
	grpc::Status ProtoStatus = Stub->FindMissingBlobs(&Context, ProtoRequest, &ProtoResponse);
	FStatus Status;
	ProtoConverter::FromProto(ProtoStatus, Status);
	ProtoConverter::FromProto(ProtoResponse, Response);

	return MoveTemp(Status);
}

FStatus FContentAddressableStorage::BatchUpdateBlobs(const FBatchUpdateBlobsRequest& Request, FBatchUpdateBlobsResponse& Response)
{
	grpc::ClientContext Context;

	build::bazel::remote::execution::v2::BatchUpdateBlobsRequest ProtoRequest;
	ProtoConverter::ToProto(Request, ProtoRequest);

	build::bazel::remote::execution::v2::BatchUpdateBlobsResponse ProtoResponse;
	grpc::Status ProtoStatus = Stub->BatchUpdateBlobs(&Context, ProtoRequest, &ProtoResponse);
	FStatus Status;
	ProtoConverter::FromProto(ProtoStatus, Status);
	ProtoConverter::FromProto(ProtoResponse, Response);

	return MoveTemp(Status);
}

FStatus FContentAddressableStorage::BatchReadBlobs(const FBatchReadBlobsRequest& Request, FBatchReadBlobsResponse& Response)
{
	grpc::ClientContext Context;

	build::bazel::remote::execution::v2::BatchReadBlobsRequest ProtoRequest;
	ProtoConverter::ToProto(Request, ProtoRequest);

	build::bazel::remote::execution::v2::BatchReadBlobsResponse ProtoResponse;
	grpc::Status ProtoStatus = Stub->BatchReadBlobs(&Context, ProtoRequest, &ProtoResponse);
	FStatus Status;
	ProtoConverter::FromProto(ProtoStatus, Status);
	ProtoConverter::FromProto(ProtoResponse, Response);

	return MoveTemp(Status);
}
