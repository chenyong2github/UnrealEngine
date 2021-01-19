// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IoDispatcher.h"

class FIoRequestImpl
{
public:
	FIoRequestImpl* NextRequest = nullptr;
	void* BackendData = nullptr;
	FIoChunkId ChunkId;
	FIoReadOptions Options;
	FIoBuffer IoBuffer;
	int32 Priority = 0;

	FIoRequestImpl(FIoDispatcherImpl& InDispatcher)
		: Dispatcher(InDispatcher)
	{

	}

	bool IsCancelled() const
	{
		return bCancelled;
	}

	void SetFailed()
	{
		bFailed = true;
	}

private:
	friend class FIoDispatcherImpl;
	friend class FIoRequest;
	friend class FIoBatch;

	void AddRef()
	{
		RefCount.IncrementExchange();
	}

	void ReleaseRef()
	{
		if (RefCount.DecrementExchange() == 1)
		{
			FreeRequest();
		}
	}

	void FreeRequest();

	FIoDispatcherImpl& Dispatcher;
	struct IIoDispatcherBackend* Backend = nullptr;
	FIoBatchImpl* Batch = nullptr;
	FIoReadCallback Callback;
	TAtomic<uint32> RefCount{ 0 };
	TAtomic<EIoErrorCode> ErrorCode{ EIoErrorCode::Unknown };
	bool bCancelled = false;
	bool bFailed = false;
};

DECLARE_DELEGATE(FWakeUpIoDispatcherThreadDelegate);

struct FIoDispatcherBackendContext
{
	FWakeUpIoDispatcherThreadDelegate WakeUpDispatcherThreadDelegate;
	FIoContainerMountedDelegate ContainerMountedDelegate;
	FIoSignatureErrorDelegate SignatureErrorDelegate;
	bool bIsMultiThreaded;
};

struct IIoDispatcherBackend
{
	virtual void Initialize(TSharedRef<const FIoDispatcherBackendContext> Context) = 0;
	virtual bool Resolve(FIoRequestImpl* Request) = 0;
	virtual void CancelIoRequest(FIoRequestImpl* Request) = 0;
	virtual void UpdatePriorityForIoRequest(FIoRequestImpl* Request) = 0;
	virtual bool DoesChunkExist(const FIoChunkId& ChunkId) const = 0;
	virtual TIoStatusOr<uint64> GetSizeForChunk(const FIoChunkId& ChunkId) const = 0;
	virtual FIoRequestImpl* GetCompletedRequests() = 0;
	virtual TIoStatusOr<FIoMappedRegion> OpenMapped(const FIoChunkId& ChunkId, const FIoReadOptions& Options) = 0;
	virtual void AppendMountedContainers(TSet<FIoContainerId>& OutContainers) = 0;
};

struct IIoDispatcherFileBackend
	: public IIoDispatcherBackend
{
	virtual TIoStatusOr<FIoContainerId> Mount(const TCHAR* ContainerPath, int32 Order, const FGuid& EncryptionKeyGuid, const FAES::FAESKey& EncryptionKey) = 0;
};

CORE_API TSharedRef<IIoDispatcherFileBackend> CreateIoDispatcherFileBackend();

