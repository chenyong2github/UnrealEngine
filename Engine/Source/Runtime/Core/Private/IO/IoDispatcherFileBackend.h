// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include"IO/IoDispatcherPrivate.h"
#include "IO/IoStore.h"
#include "Containers/Array.h"
#include "Containers/Map.h"

struct FFileIoStoreCacheBlockKey
{
	uint64 FileHandle;
	uint64 BlockIndex;

	friend bool operator==(const FFileIoStoreCacheBlockKey& A, const FFileIoStoreCacheBlockKey& B)
	{
		return A.BlockIndex == B.BlockIndex && A.FileHandle == B.FileHandle;
	}

	friend uint32 GetTypeHash(const FFileIoStoreCacheBlockKey& Key)
	{
		return HashCombine(GetTypeHash(Key.FileHandle), GetTypeHash(Key.BlockIndex));
	}
};

struct FFileIoStoreReadBlockScatter
{
	FIoRequestImpl* Request = nullptr;
	uint64 DstOffset;
	uint64 SrcOffset;
	uint64 Size;
};

struct FFileIoStoreReadBlock
{
	FFileIoStoreReadBlock* Next = nullptr;
	FFileIoStoreReadBlock* LruPrev = nullptr;
	FFileIoStoreReadBlock* LruNext = nullptr;
	FFileIoStoreCacheBlockKey Key;
	FIoBuffer Buffer;
	uint64 Size = 0;
	uint64 Offset = 0;
	TArray<FFileIoStoreReadBlockScatter> ScatterList;
	bool bIsReady = false;
};

struct FFileIoStoreResolvedRequest
{
	FIoRequestImpl* Request;
	uint64 ResolvedFileHandle;
	uint64 ResolvedOffset;
	uint64 ResolvedSize;
	uint64 ResolvedFileSize;
};

class FFileIoStoreReader
{
public:
	FFileIoStoreReader(FFileIoStoreImpl& InPlatformImpl);
	FIoStatus Initialize(const FIoStoreEnvironment& Environment);
	bool DoesChunkExist(const FIoChunkId& ChunkId) const;
	TIoStatusOr<uint64> GetSizeForChunk(const FIoChunkId& ChunkId) const;
	bool Resolve(FFileIoStoreResolvedRequest& ResolvedRequest);

private:
	FFileIoStoreImpl& PlatformImpl;

	TMap<FIoChunkId, FIoOffsetAndLength> Toc;
	uint64 ContainerFileHandle;
	uint64 ContainerFileSize;
};

class FFileIoStore
{
public:
	FFileIoStore(FIoDispatcherEventQueue& InEventQueue);
	FIoStatus Mount(const FIoStoreEnvironment& Environment);
	EIoStoreResolveResult Resolve(FIoRequestImpl* Request);
	bool DoesChunkExist(const FIoChunkId& ChunkId) const;
	TIoStatusOr<uint64> GetSizeForChunk(const FIoChunkId& ChunkId) const;
	bool ProcessCompletedBlock();

	static bool IsValidEnvironment(const FIoStoreEnvironment& Environment);

private:
	void InitCache();
	void ReadBlockCached(uint32 BlockIndex, const FFileIoStoreResolvedRequest& ResolvedRequest);
	void ReadBlocksUncached(uint32 BeginBlockIndex, uint32 BlockCount, FFileIoStoreResolvedRequest& ResolvedRequest);

	FFileIoStoreImpl PlatformImpl;

	mutable FRWLock IoStoreReadersLock;
	TArray<FFileIoStoreReader*> IoStoreReaders;
	TMap<FFileIoStoreCacheBlockKey, FFileIoStoreReadBlock*> CachedBlocksMap;
	FFileIoStoreReadBlock LruHead;
	FFileIoStoreReadBlock LruTail;
	const uint64 CacheBlockSize;
	uint64 CurrentCacheUsage = 0;
};
