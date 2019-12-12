// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include"IO/IoDispatcherPrivate.h"
#include "IO/IoStore.h"
#include "Containers/Array.h"
#include "Containers/Map.h"

struct FFileIoStoreCacheBlockKey
{
	uint64 FileHandle;
	uint32 BlockIndex;
	uint32 Hash;

	friend bool operator==(const FFileIoStoreCacheBlockKey& A, const FFileIoStoreCacheBlockKey& B)
	{
		return A.BlockIndex == B.BlockIndex && A.FileHandle == B.FileHandle;
	}

	friend uint32 GetTypeHash(const FFileIoStoreCacheBlockKey& Key)
	{
		return Key.Hash;
	}
};

struct FFileIoStoreReadBlockScatter
{
	FIoRequestImpl* Request = nullptr;
	uint8* Dst;
	const uint8* Src;
	uint32 Size;
};

struct FFileIoStoreReadBlock
{
	FFileIoStoreReadBlock* Next = nullptr;
	FFileIoStoreReadBlock* LruPrev = nullptr;
	FFileIoStoreReadBlock* LruNext = nullptr;
	FFileIoStoreCacheBlockKey Key;
	uint8* Buffer = nullptr;
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
	void ProcessIncomingBlocks();

	static bool IsValidEnvironment(const FIoStoreEnvironment& Environment);

private:
	void InitCache();
	void ReadBlockCached(uint32 BlockIndex, const FFileIoStoreResolvedRequest& ResolvedRequest);
	void ReadBlocksUncached(uint32 BeginBlockIndex, uint32 BlockCount, FFileIoStoreResolvedRequest& ResolvedRequest);

	static constexpr uint32 CacheMemorySize = 32 << 20;
	static constexpr uint32 CacheBlockSize = 256 << 10;
	static constexpr uint32 CacheBlockCount = CacheMemorySize / CacheBlockSize;

	FFileIoStoreImpl PlatformImpl;

	mutable FRWLock IoStoreReadersLock;
	TArray<FFileIoStoreReader*> IoStoreReaders;
	TArray<FFileIoStoreReadBlock> CacheBlocks;
	TMap<FFileIoStoreCacheBlockKey, FFileIoStoreReadBlock*> CachedBlocksMap;
	FFileIoStoreReadBlock LruHead;
	FFileIoStoreReadBlock LruTail;
};
