// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IContentAddressableStorage.h"

#include <memory>

namespace grpc
{
	class Channel;
	class ClientContext;
}

class FBazelCompletionQueueRunnable;
class FContentAddressableStorageStub;


class FContentAddressableStorage : public IContentAddressableStorage
{
private:
	TUniquePtr<FContentAddressableStorageStub> ContentAddressableStorageStub;
	TSharedPtr<FBazelCompletionQueueRunnable> CompletionQueueRunnable;
	TMap<FString, FString> Headers;

public:
	FContentAddressableStorage(const std::shared_ptr<grpc::Channel>& Channel, TSharedPtr<FBazelCompletionQueueRunnable> CompletionQueueRunnable, const TMap<FString, FString>& Headers);
	~FContentAddressableStorage();

	bool ToDigest(const TArray<uint8>& InData, FDigest& OutDigest) override;
	bool ToBlob(const FDirectory& InDirectory, TArray<uint8>& OutData, FDigest& OutDigest) override;
	bool ToBlob(const FCommand& InCommand, TArray<uint8>& OutData, FDigest& OutDigest) override;
	bool ToBlob(const FAction& InAction, TArray<uint8>& OutData, FDigest& OutDigest) override;

	FStatus FindMissingBlobs(const FFindMissingBlobsRequest& Request, FFindMissingBlobsResponse& Response, int64 TimeoutMs = 0) override;
	FStatus BatchUpdateBlobs(const FBatchUpdateBlobsRequest& Request, FBatchUpdateBlobsResponse& Response, int64 TimeoutMs = 0) override;
	FStatus BatchReadBlobs(const FBatchReadBlobsRequest& Request, FBatchReadBlobsResponse& Response, int64 TimeoutMs = 0) override;

	TFuture<TPair<FStatus, FFindMissingBlobsResponse>> FindMissingBlobsAsync(const FFindMissingBlobsRequest& Request, TUniqueFunction<void()> CompletionCallback = nullptr, int64 TimeoutMs = 0) override;
	TFuture<TPair<FStatus, FBatchUpdateBlobsResponse>> BatchUpdateBlobsAsync(const FBatchUpdateBlobsRequest& Request, TUniqueFunction<void()> CompletionCallback = nullptr, int64 TimeoutMs = 0) override;
	TFuture<TPair<FStatus, FBatchReadBlobsResponse>> BatchReadBlobsAsync(const FBatchReadBlobsRequest& Request, TUniqueFunction<void()> CompletionCallback = nullptr, int64 TimeoutMs = 0) override;

private:
	void PrepareContext(grpc::ClientContext& ClientContext, int64 TimeoutMs = 0) const;
};
