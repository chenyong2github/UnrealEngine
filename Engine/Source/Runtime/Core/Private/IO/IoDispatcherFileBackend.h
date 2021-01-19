// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IO/IoDispatcherFileBackendTypes.h"
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

#ifndef PLATFORM_IMPLEMENTS_IO
#define PLATFORM_IMPLEMENTS_IO 0
#endif

#if PLATFORM_IMPLEMENTS_IO
#include COMPILED_PLATFORM_HEADER(PlatformIoDispatcher.h)
#else
#include "GenericPlatform/GenericPlatformIoDispatcher.h"
typedef FGenericFileIoStoreEventQueue FFileIoStoreEventQueue;
typedef FGenericFileIoStoreImpl FFileIoStoreImpl;
#endif

class IMappedFileHandle;

struct FFileIoStoreCompressionContext
{
	FFileIoStoreCompressionContext* Next = nullptr;
	uint64 UncompressedBufferSize = 0;
	uint8* UncompressedBuffer = nullptr;
};

class FFileIoStoreReader
{
public:
	FFileIoStoreReader(FFileIoStoreImpl& InPlatformImpl);
	FIoStatus Initialize(const TCHAR* ContainerPath, int32 Order);
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
	IMappedFileHandle* GetMappedContainerFileHandle(uint64 TocOffset);
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

	static TAtomic<uint32> GlobalPartitionIndex;
};

class FFileIoStoreRequestTracker
{
public:
	FFileIoStoreRequestTracker(FFileIoStoreRequestAllocator& RequestAllocator, FFileIoStoreRequestQueue& RequestQueue);
	~FFileIoStoreRequestTracker();

	FFileIoStoreCompressedBlock* FindOrAddCompressedBlock(FFileIoStoreBlockKey Key, bool& bOutWasAdded);
	void RemoveCompressedBlock(const FFileIoStoreCompressedBlock* CompressedBlock);
	FFileIoStoreReadRequest* FindOrAddRawBlock(FFileIoStoreBlockKey Key, bool& bOutWasAdded);
	void RemoveRawBlock(const FFileIoStoreReadRequest* RawBlock);
	void AddReadRequestsToResolvedRequest(FFileIoStoreCompressedBlock* CompressedBlock, FFileIoStoreResolvedRequest& ResolvedRequest);
	void AddReadRequestsToResolvedRequest(const FFileIoStoreReadRequestList& Requests, FFileIoStoreResolvedRequest& ResolvedRequest);
	void CancelIoRequest(FFileIoStoreResolvedRequest& ResolvedRequest);
	void UpdatePriorityForIoRequest(FFileIoStoreResolvedRequest& ResolvedRequest);
	void ReleaseIoRequestReferences(FFileIoStoreResolvedRequest& ResolvedRequest);

private:
	FFileIoStoreRequestAllocator& RequestAllocator;
	FFileIoStoreRequestQueue& RequestQueue;
	TMap<FFileIoStoreBlockKey, FFileIoStoreCompressedBlock*> CompressedBlocksMap;
	TMap<FFileIoStoreBlockKey, FFileIoStoreReadRequest*> RawBlocksMap;
};

class FFileIoStore final
	: public FRunnable
	, public IIoDispatcherFileBackend
{
public:
	FFileIoStore();
	~FFileIoStore();
	void Initialize(TSharedRef<const FIoDispatcherBackendContext> Context) override;
	TIoStatusOr<FIoContainerId> Mount(const TCHAR* ContainerPath, int32 Order, const FGuid& EncryptionKeyGuid, const FAES::FAESKey& EncryptionKey);
	bool Resolve(FIoRequestImpl* Request) override;
	void CancelIoRequest(FIoRequestImpl* Request) override;
	void UpdatePriorityForIoRequest(FIoRequestImpl* Request) override;
	bool DoesChunkExist(const FIoChunkId& ChunkId) const override;
	TIoStatusOr<uint64> GetSizeForChunk(const FIoChunkId& ChunkId) const;
	FIoRequestImpl* GetCompletedRequests() override;
	TIoStatusOr<FIoMappedRegion> OpenMapped(const FIoChunkId& ChunkId, const FIoReadOptions& Options) override;
	void AppendMountedContainers(TSet<FIoContainerId>& OutContainers) override;

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

	void OnNewPendingRequestsAdded();
	void ReadBlocks(FFileIoStoreResolvedRequest& ResolvedRequest);
	void FreeBuffer(FFileIoStoreBuffer& Buffer);
	FFileIoStoreCompressionContext* AllocCompressionContext();
	void FreeCompressionContext(FFileIoStoreCompressionContext* CompressionContext);
	void ScatterBlock(FFileIoStoreCompressedBlock* CompressedBlock, bool bIsAsync);
	void CompleteDispatcherRequest(FFileIoStoreResolvedRequest* ResolvedRequest);
	void FinalizeCompressedBlock(FFileIoStoreCompressedBlock* CompressedBlock);
	void UpdateAsyncIOMinimumPriority();

	uint64 ReadBufferSize = 0;
	TSharedPtr<const FIoDispatcherBackendContext> BackendContext;
	FFileIoStoreEventQueue EventQueue;
	FFileIoStoreBlockCache BlockCache;
	FFileIoStoreBufferAllocator BufferAllocator;
	FFileIoStoreRequestAllocator RequestAllocator;
	FFileIoStoreRequestQueue RequestQueue;
	FFileIoStoreRequestTracker RequestTracker;
	FFileIoStoreImpl PlatformImpl;
	FRunnableThread* Thread = nullptr;
	bool bIsMultithreaded;
	TAtomic<bool> bStopRequested{ false };
	mutable FRWLock IoStoreReadersLock;
	TArray<FFileIoStoreReader*> UnorderedIoStoreReaders;
	TArray<FFileIoStoreReader*> OrderedIoStoreReaders;
	FFileIoStoreCompressionContext* FirstFreeCompressionContext = nullptr;
	FFileIoStoreCompressedBlock* ReadyForDecompressionHead = nullptr;
	FFileIoStoreCompressedBlock* ReadyForDecompressionTail = nullptr;
	FCriticalSection DecompressedBlocksCritical;
	FFileIoStoreCompressedBlock* FirstDecompressedBlock = nullptr;
	FIoRequestImpl* CompletedRequestsHead = nullptr;
	FIoRequestImpl* CompletedRequestsTail = nullptr;
	EAsyncIOPriorityAndFlags CurrentAsyncIOMinimumPriority = AIOP_MIN;
};
