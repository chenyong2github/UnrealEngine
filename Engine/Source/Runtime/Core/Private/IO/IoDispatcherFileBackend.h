// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IO/IoDispatcherPrivate.h"
#include "IO/IoStore.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Stats/Stats.h"
#include "Async/TaskGraphInterfaces.h"
#include "HAL/Runnable.h"

class IMappedFileHandle;

struct FFileIoStoreContainerFile
{
	uint64 FileHandle = 0;
	uint64 FileSize = 0;
	uint64 CompressionBlockSize = 0;
	TArray<FName> CompressionMethods;
	TArray<FIoStoreCompressedBlockEntry> CompressionBlocks;
	FString FilePath;
	TUniquePtr<IMappedFileHandle> MappedFileHandle;
};

struct FFileIoStoreBuffer
{
	FFileIoStoreBuffer* Next = nullptr;
	uint8* Memory = nullptr;
};

class FFileIoStoreBufferAllocator
{
public:
	FFileIoStoreBuffer* AllocBuffer();
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
	uint64 DstOffset;
	uint64 SrcOffset;
	uint64 Size;
};

struct FFileIoStoreCompressionContext
{
	FFileIoStoreCompressionContext* Next = nullptr;
	uint64 UncompressedBufferSize = 0;
	uint8* UncompressedBuffer = nullptr;
};

struct FFileIoStoreCompressedBlock
{
	FFileIoStoreCompressedBlock* Next = nullptr;
	FFileIoStoreBlockKey Key;
	FName CompressionMethod;
	uint64 Size;
	uint64 RawOffset;
	uint64 RawSize;
	uint32 RawBlocksCount = 0;
	uint32 UnfinishedRawBlocksCount = 0;
	struct FFileIoStoreRawBlock* SingleRawBlock;
	TArray<FFileIoStoreBlockScatter, TInlineAllocator<16>> ScatterList;
	FFileIoStoreCompressionContext* CompressionContext = nullptr;
	uint8* CompressedDataBuffer = nullptr;
};

struct FFileIoStoreRawBlock
{
	FFileIoStoreRawBlock* Next = nullptr;
	FFileIoStoreBlockKey Key;
	uint64 Offset;
	uint64 Size;
	FFileIoStoreBuffer* Buffer = nullptr;
	TArray<FFileIoStoreCompressedBlock*, TInlineAllocator<4>> CompressedBlocks;
	uint32 RefCount = 0;
	FIoRequestImpl* DirectToRequest = nullptr;
	uint64 DirectToRequestOffset = 0;
};

struct FFileIoStoreResolvedRequest
{
	FIoRequestImpl* Request;
	uint64 ResolvedOffset;
	uint64 ResolvedSize;
};

class FFileIoStoreReader
{
public:
	FFileIoStoreReader(FFileIoStoreImpl& InPlatformImpl);
	FIoStatus Initialize(const FIoStoreEnvironment& Environment);
	bool DoesChunkExist(const FIoChunkId& ChunkId) const;
	TIoStatusOr<uint64> GetSizeForChunk(const FIoChunkId& ChunkId) const;
	const FIoOffsetAndLength* Resolve(const FIoChunkId& ChunkId) const;
	const FFileIoStoreContainerFile& GetContainerFile() { return ContainerFile; }
	IMappedFileHandle* GetMappedContainerFileHandle();

private:
	FFileIoStoreImpl& PlatformImpl;

	TMap<FIoChunkId, FIoOffsetAndLength> Toc;
	FFileIoStoreContainerFile ContainerFile;
};

class FFileIoStore
	: public FRunnable
{
public:
	FFileIoStore(FIoDispatcherEventQueue& InEventQueue, bool bInIsMultithreaded);
	~FFileIoStore();
	FIoStatus Mount(const FIoStoreEnvironment& Environment);
	EIoStoreResolveResult Resolve(FIoRequestImpl* Request);
	bool DoesChunkExist(const FIoChunkId& ChunkId) const;
	TIoStatusOr<uint64> GetSizeForChunk(const FIoChunkId& ChunkId) const;
	void ProcessCompletedBlocks();
	TIoStatusOr<FIoMappedRegion> OpenMapped(const FIoChunkId& ChunkId, const FIoReadOptions& Options);

	static bool IsValidEnvironment(const FIoStoreEnvironment& Environment);

	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;

private:
	class FDecompressAsyncTask
	{
	public:
		FDecompressAsyncTask(FFileIoStore& InOuter, FFileIoStoreCompressedBlock* InCompressedBlock)
			: Outer(InOuter)
			, CompressedBlock(InCompressedBlock)
		{

		}

		static FORCEINLINE TStatId GetStatId()
		{
			RETURN_QUICK_DECLARE_CYCLE_STAT(FIoStoreDecompressTask, STATGROUP_TaskGraphTasks);
		}

		static ENamedThreads::Type GetDesiredThread();

		FORCEINLINE static ESubsequentsMode::Type GetSubsequentsMode()
		{
			return ESubsequentsMode::FireAndForget;
		}

		void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
		{
			Outer.ScatterBlock(CompressedBlock, true);
		}

	private:
		FFileIoStore& Outer;
		FFileIoStoreCompressedBlock* CompressedBlock;
	};

	void InitCache();
	void ReadBlockAndScatter(uint32 ReaderIndex, uint32 BlockIndex, const FFileIoStoreResolvedRequest& ResolvedRequest);
	void ReadNoScatter(uint32 ReaderIndex, uint64 Offset, uint64 Size, const FFileIoStoreResolvedRequest& ResolvedRequest);
	FFileIoStoreBuffer* AllocBuffer();
	void FreeBuffer(FFileIoStoreBuffer* Buffer);
	FFileIoStoreCompressionContext* AllocCompressionContext();
	void FreeCompressionContext(FFileIoStoreCompressionContext* CompressionContext);
	void ScatterBlock(FFileIoStoreCompressedBlock* CompressedBlock, bool bIsAsync);
	void AllocMemoryForRequest(FIoRequestImpl* Request);
	void FinalizeCompressedBlock(FFileIoStoreCompressedBlock* CompressedBlock);
	void ReadPendingBlocks();

	const uint64 ReadBufferSize;
	FIoDispatcherEventQueue& EventQueue;
	bool bIsMultithreaded;
	FFileIoStoreImpl PlatformImpl;

	FRunnableThread* Thread;
	TAtomic<bool> bStopRequested{ false };
	mutable FRWLock IoStoreReadersLock;
	TArray<FFileIoStoreReader*> IoStoreReaders;
	TMap<FFileIoStoreBlockKey, FFileIoStoreCompressedBlock*> CompressedBlocksMap;
	TMap<FFileIoStoreBlockKey, FFileIoStoreRawBlock*> RawBlocksMap;
	uint8* BufferMemory = nullptr;
	FEvent* BufferAvailableEvent;
	FCriticalSection BuffersCritical;
	FFileIoStoreBuffer* FirstFreeBuffer = nullptr;
	FFileIoStoreCompressionContext* FirstFreeCompressionContext = nullptr;
	FEvent* PendingBlockEvent;
	FCriticalSection PendingBlocksCritical;
	FFileIoStoreRawBlock* PendingBlocksHead = nullptr;
	FFileIoStoreRawBlock* PendingBlocksTail = nullptr;
	FFileIoStoreCompressedBlock* ReadyForDecompressionHead = nullptr;
	FFileIoStoreCompressedBlock* ReadyForDecompressionTail = nullptr;
	FFileIoStoreRawBlock* ScheduledBlocksHead = nullptr;
	FFileIoStoreRawBlock* ScheduledBlocksTail = nullptr;
	FCriticalSection DecompressedBlocksCritical;
	FFileIoStoreCompressedBlock* FirstDecompressedBlock = nullptr;
};
