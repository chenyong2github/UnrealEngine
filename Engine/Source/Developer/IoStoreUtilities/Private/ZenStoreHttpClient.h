// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IO/IoDispatcher.h"
#include "Async/Future.h"

class FCbPackage;
class FCbObject;

namespace UE {
	namespace Zen {
		struct FRequestPool;
	}

/**
 * HTTP protocol implementation of Zen Store client interface
 */
class FZenStoreHttpClient
{
public:
	FZenStoreHttpClient(const FStringView InHostName, uint16 InPort);
	~FZenStoreHttpClient();

	void Initialize(FStringView InProjectId, 
					FStringView InOplogId, 
					FStringView ServerRoot, 
					FStringView EngineRoot, 
					FStringView ProjectRoot,
					bool		IsCleanBuild);

	void InitializeReadOnly(FStringView InProjectId, FStringView InOplogId);

	void StartBuildPass();
	TIoStatusOr<uint64> EndBuildPass(FCbPackage OpEntry);

	TFuture<TIoStatusOr<uint64>> AppendOp(FCbPackage OpEntry);

	TIoStatusOr<uint64> GetChunkSize(const FIoChunkId& Id);
	TIoStatusOr<FIoBuffer> ReadChunk(const FIoChunkId& Id, uint64 Offset = 0, uint64 Size = ~0ull);

private:
	FString	HostName;
	uint16 Port;
	TUniquePtr<Zen::FRequestPool> RequestPool;
	FString OplogPath;
	FString OplogNewEntryPath;
	FString OplogPrepNewEntryPath;
	FString TempDirPath;
	uint64 StandaloneThresholdBytes = 1 * 1024 * 1024;
	bool bAllowRead = false;
	bool bAllowEdit = false;
};

}
