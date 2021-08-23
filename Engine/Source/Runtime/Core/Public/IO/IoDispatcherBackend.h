// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IoDispatcher.h"
#include "HAL/LowLevelMemTracker.h"
#include "Misc/Optional.h"

class FIoRequestImpl
{
public:
	FIoRequestImpl* NextRequest = nullptr;
	void* BackendData = nullptr;
	LLM(const UE::LLMPrivate::FTagData* InheritedLLMTag);
	FIoChunkId ChunkId;
	FIoReadOptions Options;
	int32 Priority = 0;

	FIoRequestImpl(FIoDispatcherImpl& InDispatcher)
		: Dispatcher(InDispatcher)
	{
		LLM(InheritedLLMTag = FLowLevelMemTracker::bIsDisabled ? nullptr : FLowLevelMemTracker::Get().GetActiveTagData(ELLMTracker::Default));
	}

	bool IsCancelled() const
	{
		return bCancelled;
	}

	void SetFailed()
	{
		bFailed = true;
	}

	bool HasBuffer() const
	{
		return Buffer.IsSet();
	}

	CORE_API void CreateBuffer(uint64 Size);

	FIoBuffer& GetBuffer()
	{
		return Buffer.GetValue();
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
	TOptional<FIoBuffer> Buffer;
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
	virtual TIoStatusOr<FIoContainerId> Unmount(const TCHAR* ContainerPath) = 0;
};

CORE_API TSharedRef<IIoDispatcherFileBackend> CreateIoDispatcherFileBackend();

