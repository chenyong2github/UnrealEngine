// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

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

	virtual bool ToBlob(const FDirectory& InDirectory, TArray<char>& OutData, FDigest& OutDigest) = 0;
	virtual bool ToBlob(const FCommand& InCommand, TArray<char>& OutData, FDigest& OutDigest) = 0;
	virtual bool ToBlob(const FAction& InAction, TArray<char>& OutData, FDigest& OutDigest) = 0;

	virtual FStatus FindMissingBlobs(const FFindMissingBlobsRequest& Request, FFindMissingBlobsResponse& Response) = 0;
	virtual FStatus BatchUpdateBlobs(const FBatchUpdateBlobsRequest& Request, FBatchUpdateBlobsResponse& Response) = 0;
	virtual FStatus BatchReadBlobs(const FBatchReadBlobsRequest& Request, FBatchReadBlobsResponse& Response) = 0;
};
