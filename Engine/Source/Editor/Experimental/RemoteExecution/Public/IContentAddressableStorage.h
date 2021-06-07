// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"

struct FAction;
struct FBatchReadBlobsRequest;
struct FBatchReadBlobsResponse;
struct FBatchUpdateBlobsRequest;
struct FBatchUpdateBlobsResponse;
struct FCommand;
struct FDigest;
struct FDirectory;
struct FFindMissingBlobsRequest;
struct FFindMissingBlobsResponse;
struct FStatus;


class IContentAddressableStorage
{
public:
	/** Virtual destructor */
	virtual ~IContentAddressableStorage() {}

	virtual bool ToDigest(const TArray<char>& InData, FDigest& OutDigest) = 0;
	virtual bool ToBlob(const FDirectory& InDirectory, TArray<char>& OutData, FDigest& OutDigest) = 0;
	virtual bool ToBlob(const FCommand& InCommand, TArray<char>& OutData, FDigest& OutDigest) = 0;
	virtual bool ToBlob(const FAction& InAction, TArray<char>& OutData, FDigest& OutDigest) = 0;

	virtual FStatus FindMissingBlobs(const FFindMissingBlobsRequest& Request, FFindMissingBlobsResponse& Response, int64 TimeoutMs = 0) = 0;
	virtual FStatus BatchUpdateBlobs(const FBatchUpdateBlobsRequest& Request, FBatchUpdateBlobsResponse& Response, int64 TimeoutMs = 0) = 0;
	virtual FStatus BatchReadBlobs(const FBatchReadBlobsRequest& Request, FBatchReadBlobsResponse& Response, int64 TimeoutMs = 0) = 0;

	virtual TFuture<TPair<FStatus, FFindMissingBlobsResponse>> FindMissingBlobsAsync(const FFindMissingBlobsRequest& Request, TUniqueFunction<void()> CompletionCallback = nullptr, int64 TimeoutMs = 0) = 0;
	virtual TFuture<TPair<FStatus, FBatchUpdateBlobsResponse>> BatchUpdateBlobsAsync(const FBatchUpdateBlobsRequest& Request, TUniqueFunction<void()> CompletionCallback = nullptr, int64 TimeoutMs = 0) = 0;
	virtual TFuture<TPair<FStatus, FBatchReadBlobsResponse>> BatchReadBlobsAsync(const FBatchReadBlobsRequest& Request, TUniqueFunction<void()> CompletionCallback = nullptr, int64 TimeoutMs = 0) = 0;
};
