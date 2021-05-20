// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IContentAddressableStorage.h"
#include "Messages.h"

#include <memory>


THIRD_PARTY_INCLUDES_START
UE_PUSH_MACRO("TEXT")
#undef TEXT
#include "build\bazel\remote\execution\v2\remote_execution.grpc.pb.h"
UE_POP_MACRO("TEXT");
THIRD_PARTY_INCLUDES_END

namespace grpc
{
	class Channel;
}


class FContentAddressableStorage : public IContentAddressableStorage
{
private:
	std::unique_ptr<build::bazel::remote::execution::v2::ContentAddressableStorage::Stub> Stub;

public:
	FContentAddressableStorage(const std::shared_ptr<grpc::Channel>& Channel);
	~FContentAddressableStorage();

	FStatus FindMissingBlobs(const FFindMissingBlobsRequest& Request, FFindMissingBlobsResponse& Response) override;
	FStatus BatchUpdateBlobs(const FBatchUpdateBlobsRequest& Request, FBatchUpdateBlobsResponse& Response) override;
	FStatus BatchReadBlobs(const FBatchReadBlobsRequest& Request, FBatchReadBlobsResponse& Response) override;
};
