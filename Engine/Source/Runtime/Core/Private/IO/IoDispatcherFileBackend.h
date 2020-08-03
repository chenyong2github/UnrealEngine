// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IO/IoDispatcher.h"
#include "IO/IoDispatcherPrivate.h"
#include "IO/IoStore.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Stats/Stats.h"
#include "Async/TaskGraphInterfaces.h"
#include "HAL/Runnable.h"
#include "Misc/AES.h"
#include "GenericPlatform/GenericPlatformFile.h"

class IMappedFileHandle;

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
	EIoDispatcherPriority Priority = IoDispatcherPriority_Count;
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
	uint64 RawOffset;
	uint32 UncompressedSize;
	uint32 CompressedSize;
	uint32 RawSize;
	uint32 RawBlocksCount = 0;
	uint32 UnfinishedRawBlocksCount = 0;
	struct FFileIoStoreReadRequest* SingleRawBlock;
	TArray<FFileIoStoreBlockScatter, TInlineAllocator<16>> ScatterList;
	FFileIoStoreCompressionContext* CompressionContext = nullptr;
	uint8* CompressedDataBuffer = nullptr;
	FAES::FAESKey EncryptionKey;
	const FSHAHash* SignatureHash = nullptr;
};

struct FFileIoStoreReadRequest
{
	FFileIoStoreReadRequest* Next = nullptr;
	uint64 FileHandle = uint64(-1);
	uint64 Offset = uint64(-1);
	uint64 Size = uint64(-1);
	FFileIoStoreBlockKey Key;
	FIoRequestImpl* DirectToRequest = nullptr;
	FFileIoStoreBuffer* Buffer = nullptr;
	TArray<FFileIoStoreCompressedBlock*, TInlineAllocator<4>> CompressedBlocks;
	uint32 RefCount = 0;
	EIoDispatcherPriority Priority = IoDispatcherPriority_Count;
	bool bIsRawBlock;
	bool bIsCacheable;
};

struct FFileIoStoreResolvedRequest
{
	FIoRequestImpl* Request;
	uint64 ResolvedOffset;
	uint64 ResolvedSize;
};

class FFileIoStoreEncryptionKeys
{
public:
	using FKeyRegisteredCallback = TFunction<void(const FGuid&, const FAES::FAESKey&)>;

	FFileIoStoreEncryptionKeys();
	~FFileIoStoreEncryptionKeys();

	bool GetEncryptionKey(const FGuid& Guid, FAES::FAESKey& OutKey) const;
	void SetKeyRegisteredCallback(FKeyRegisteredCallback&& Callback)
	{
		KeyRegisteredCallback = Callback;
	}

private:
	void RegisterEncryptionKey(const FGuid& Guid, const FAES::FAESKey& Key);

	TMap<FGuid, FAES::FAESKey> EncryptionKeysByGuid;
	mutable FCriticalSection EncryptionKeysCritical;
	FKeyRegisteredCallback KeyRegisteredCallback;
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
	void Pop(FFileIoStoreReadRequest& Request);
	void Push(FFileIoStoreReadRequest& Request);

private:
	struct FByPriority
	{
		FFileIoStoreReadRequest* Head = nullptr;
		FFileIoStoreReadRequest* Tail = nullptr;
	};

	FByPriority ByPriority[IoDispatcherPriority_Count];
};

class FFileIoStoreReader
{
public:
	FFileIoStoreReader(FFileIoStoreImpl& InPlatformImpl);
	FIoStatus Initialize(const FIoStoreEnvironment& Environment);
	void SetIndex(uint32 InIndex)
	{
		Index = InIndex;
	}
	uint32 GetIndex() const
	{
		return Index;
	}
	bool DoesChunkExist(const FIoChunkId& ChunkId) const;
	TIoStatusOr<uint64> GetSizeForChunk(const FIoChunkId& ChunkId) const;
	const FIoOffsetAndLength* Resolve(const FIoChunkId& ChunkId) const;
	const FFileIoStoreContainerFile& GetContainerFile() const { return ContainerFile; }
	IMappedFileHandle* GetMappedContainerFileHandle();
	const FIoContainerId& GetContainerId() const { return ContainerId; }
	int32 GetOrder() const { return Order; }
	bool IsEncrypted() const { return EnumHasAnyFlags(ContainerFile.ContainerFlags, EIoContainerFlags::Encrypted); }
	bool IsSigned() const { return EnumHasAnyFlags(ContainerFile.ContainerFlags, EIoContainerFlags::Signed); }
	const FGuid& GetEncryptionKeyGuid() const { return ContainerFile.EncryptionKeyGuid; }
	void SetEncryptionKey(const FAES::FAESKey& Key) { ContainerFile.EncryptionKey = Key; }
	const FAES::FAESKey& GetEncryptionKey() const { return ContainerFile.EncryptionKey; }

private:
	FFileIoStoreImpl& PlatformImpl;

	TMap<FIoChunkId, FIoOffsetAndLength> Toc;
	FFileIoStoreContainerFile ContainerFile;
	FIoContainerId ContainerId;
	uint32 Index;
	int32 Order;
};

class FFileIoStore
	: public FRunnable
{
public:
	FFileIoStore(FIoDispatcherEventQueue& InEventQueue, FIoSignatureErrorEvent& InSignatureErrorEvent, bool bInIsMultithreaded);
	~FFileIoStore();
	void Initialize();
	TIoStatusOr<FIoContainerId> Mount(const FIoStoreEnvironment& Environment);
	EIoStoreResolveResult Resolve(FIoRequestImpl* Request);
	bool DoesChunkExist(const FIoChunkId& ChunkId) const;
	TIoStatusOr<uint64> GetSizeForChunk(const FIoChunkId& ChunkId) const;
	FIoRequestImpl* GetCompletedRequests();
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

	struct FBlockMaps
	{
		TMap<FFileIoStoreBlockKey, FFileIoStoreCompressedBlock*> CompressedBlocksMap;
		TMap<FFileIoStoreBlockKey, FFileIoStoreReadRequest*> RawBlocksMap;
	};

	void InitCache();
	void OnNewPendingRequestsAdded();
	void ReadBlocks(const FFileIoStoreReader& Reader, const FFileIoStoreResolvedRequest& ResolvedRequest);
	void FreeBuffer(FFileIoStoreBuffer& Buffer);
	FFileIoStoreCompressionContext* AllocCompressionContext();
	void FreeCompressionContext(FFileIoStoreCompressionContext* CompressionContext);
	void ScatterBlock(FFileIoStoreCompressedBlock* CompressedBlock, bool bIsAsync);
	void AllocMemoryForRequest(FIoRequestImpl* Request);
	void FinalizeCompressedBlock(FFileIoStoreCompressedBlock* CompressedBlock);
	void ProcessIncomingRequests();
	void UpdateAsyncIOMinimumPriority();

	uint64 ReadBufferSize = 0;
	FIoDispatcherEventQueue& EventQueue;
	FIoSignatureErrorEvent& SignatureErrorEvent;
	FFileIoStoreBlockCache BlockCache;
	FFileIoStoreBufferAllocator BufferAllocator;
	FFileIoStoreRequestQueue RequestQueue;
	FFileIoStoreImpl PlatformImpl;
	FRunnableThread* Thread = nullptr;
	bool bIsMultithreaded;
	TAtomic<bool> bStopRequested{ false };
	mutable FRWLock IoStoreReadersLock;
	TArray<FFileIoStoreReader*> UnorderedIoStoreReaders;
	TArray<FFileIoStoreReader*> OrderedIoStoreReaders;
	FFileIoStoreCompressionContext* FirstFreeCompressionContext = nullptr;
	FCriticalSection PendingRequestsCritical;
	FFileIoStoreReadRequest* PendingRequestsHead = nullptr;
	FFileIoStoreReadRequest* PendingRequestsTail = nullptr;
	FBlockMaps BlockMapsByPrority[IoDispatcherPriority_Count];
	FFileIoStoreCompressedBlock* ReadyForDecompressionHead = nullptr;
	FFileIoStoreCompressedBlock* ReadyForDecompressionTail = nullptr;
	FCriticalSection DecompressedBlocksCritical;
	FFileIoStoreCompressedBlock* FirstDecompressedBlock = nullptr;
	FFileIoStoreEncryptionKeys EncryptionKeys;
	FIoRequestImpl* CompletedRequestsHead = nullptr;
	FIoRequestImpl* CompletedRequestsTail = nullptr;
	EAsyncIOPriorityAndFlags CurrentAsyncIOMinimumPriority = AIOP_MIN;

	uint32 SubmittedRequests = 0;
	uint32 CompletedRequests = 0;
};
