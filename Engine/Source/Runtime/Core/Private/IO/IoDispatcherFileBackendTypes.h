// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IO/IoStore.h"

struct FFileIoStoreCompressionContext;

struct FFileIoStoreContainerFile
{
	uint64 FileHandle = 0;
	uint64 FileSize = 0;
	uint64 CompressionBlockSize = 0;
	TArray<FName> CompressionMethods;
	TArray<FIoStoreTocCompressedBlockEntry> CompressionBlocks;
	FString FilePath;
	TUniquePtr<IMappedFileHandle> MappedFileHandle;
	FGuid EncryptionKeyGuid;
	FAES::FAESKey EncryptionKey;
	EIoContainerFlags ContainerFlags;
	TArray<FSHAHash> BlockSignatureHashes;
};

struct FFileIoStoreBuffer
{
	FFileIoStoreBuffer* Next = nullptr;
	uint8* Memory = nullptr;
};

struct FFileIoStoreBlockKey
{
	union
	{
		struct
		{
			uint32 FileIndex;
			uint32 BlockIndex;
		};
		uint64 Hash;
	};


	friend bool operator==(const FFileIoStoreBlockKey& A, const FFileIoStoreBlockKey& B)
	{
		return A.Hash == B.Hash;
	}

	friend uint32 GetTypeHash(const FFileIoStoreBlockKey& Key)
	{
		return GetTypeHash(Key.Hash);
	}
};

struct FFileIoStoreBlockScatter
{
	FIoRequestImpl* Request = nullptr;
	uint64 DstOffset = 0;
	uint64 SrcOffset = 0;
	uint64 Size = 0;
};

struct FFileIoStoreCompressedBlock
{
	FFileIoStoreCompressedBlock* Next = nullptr;
	FFileIoStoreBlockKey Key;
	FName CompressionMethod;
	uint64 RawOffset;
	uint32 UncompressedSize;
	uint32 CompressedSize;
	uint32 RawSize;
	uint32 UnfinishedRawBlocksCount = 0;
	TArray<struct FFileIoStoreReadRequest*, TInlineAllocator<2>> RawBlocks;
	TArray<FFileIoStoreBlockScatter, TInlineAllocator<16>> ScatterList;
	FFileIoStoreCompressionContext* CompressionContext = nullptr;
	uint8* CompressedDataBuffer = nullptr;
	FAES::FAESKey EncryptionKey;
	const FSHAHash* SignatureHash = nullptr;
	bool bFailed = false;
};

struct FFileIoStoreReadRequest
{
	FFileIoStoreReadRequest()
		: Sequence(NextSequence++)
	{

	}

	FFileIoStoreReadRequest* Next = nullptr;
	uint64 FileHandle = uint64(-1);
	uint64 Offset = uint64(-1);
	uint64 Size = uint64(-1);
	FFileIoStoreBlockKey Key;
	FFileIoStoreBuffer* Buffer = nullptr;
	TArray<FFileIoStoreCompressedBlock*, TInlineAllocator<4>> CompressedBlocks;
	uint32 CompressedBlocksRefCount = 0;
	uint32 Sequence = 0;
	int32 Priority = 0;
	FFileIoStoreBlockScatter ImmediateScatter;
	bool bIsCacheable = false;
	bool bFailed = false;

	static uint32 NextSequence;
};

class FFileIoStoreReadRequestList
{
public:
	bool IsEmpty() const
	{
		return Head == nullptr;
	}

	FFileIoStoreReadRequest* GetHead() const
	{
		return Head;
	}

	FFileIoStoreReadRequest* GetTail() const
	{
		return Tail;
	}

	void Add(FFileIoStoreReadRequest* Request)
	{
		if (Tail)
		{
			Tail->Next = Request;
		}
		else
		{
			Head = Request;
		}
		Tail = Request;
		Request->Next = nullptr;
	}

	void Append(FFileIoStoreReadRequest* ListHead, FFileIoStoreReadRequest* ListTail)
	{
		check(ListHead);
		check(ListTail);
		check(!ListTail->Next);
		if (Tail)
		{
			Tail->Next = ListHead;
		}
		else
		{
			Head = ListHead;
		}
		Tail = ListTail;
	}

	void Append(FFileIoStoreReadRequestList& List)
	{
		if (List.Head)
		{
			Append(List.Head, List.Tail);
		}
	}

	void Clear()
	{
		Head = Tail = nullptr;
	}

private:
	FFileIoStoreReadRequest* Head = nullptr;
	FFileIoStoreReadRequest* Tail = nullptr;
};

struct FFileIoStoreResolvedRequest
{
	FIoRequestImpl* Request;
	uint64 ResolvedOffset;
	uint64 ResolvedSize;
};

class FFileIoStoreBufferAllocator
{
public:
	void Initialize(uint64 MemorySize, uint64 BufferSize, uint32 BufferAlignment);
	FFileIoStoreBuffer* AllocBuffer();
	void FreeBuffer(FFileIoStoreBuffer* Buffer);

private:
	uint8* BufferMemory = nullptr;
	FCriticalSection BuffersCritical;
	FFileIoStoreBuffer* FirstFreeBuffer = nullptr;
};

class FFileIoStoreBlockCache
{
public:
	FFileIoStoreBlockCache();
	~FFileIoStoreBlockCache();

	void Initialize(uint64 CacheMemorySize, uint64 ReadBufferSize);
	bool Read(FFileIoStoreReadRequest* Block);
	void Store(const FFileIoStoreReadRequest* Block);

private:
	struct FCachedBlock
	{
		FCachedBlock* LruPrev = nullptr;
		FCachedBlock* LruNext = nullptr;
		uint64 Key = 0;
		uint8* Buffer = nullptr;
		bool bLocked = false;
	};

	FCriticalSection CriticalSection;
	uint8* CacheMemory = nullptr;
	TMap<uint64, FCachedBlock*> CachedBlocks;
	FCachedBlock CacheLruHead;
	FCachedBlock CacheLruTail;
	uint64 ReadBufferSize = 0;
};

class FFileIoStoreRequestQueue
{
public:
	FFileIoStoreReadRequest* Peek();
	FFileIoStoreReadRequest* Pop();
	void Push(FFileIoStoreReadRequest& Request);
	void Push(const FFileIoStoreReadRequestList& Requests);
	void UpdateOrder();

private:
	static bool QueueSortFunc(const FFileIoStoreReadRequest& A, const FFileIoStoreReadRequest& B)
	{
		if (A.Priority == B.Priority)
		{
			return A.Sequence < B.Sequence;
		}
		return A.Priority > B.Priority;
	}
	
	TArray<FFileIoStoreReadRequest*> Heap;
	FCriticalSection CriticalSection;
};
