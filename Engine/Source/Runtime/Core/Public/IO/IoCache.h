// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IO/IoDispatcher.h"
#include "Memory/MemoryFwd.h"
#include "Templates/RefCounting.h"
#include <atomic>

namespace UE::IO::Private
{
	/** An abstract base class for implementing cache requests. */
	class FIoCacheRequestBase
	{
	public:
		virtual ~FIoCacheRequestBase() = default;

		virtual void Wait() = 0;
		virtual void Cancel() = 0;

		inline FIoStatus Status()
		{ 
			return FIoStatus(ErrorCode.load(std::memory_order_relaxed));
		}

		inline uint32 GetRefCount() const
		{
			return RefCount.load(std::memory_order_relaxed);
		}

		inline void AddRef()
		{
			RefCount.fetch_add(1, std::memory_order_relaxed);
		}

		inline void Release()
		{
			if (1 == RefCount.fetch_sub(1, std::memory_order_acq_rel))
			{
				delete this;
			}
		}

	protected:
		explicit FIoCacheRequestBase(FIoReadCallback&& ReadCallback);
		void CompleteRequest(FIoBuffer&& Buffer);
		void CompleteRequest(EIoErrorCode Error);

		FIoReadCallback Callback;
		std::atomic_uint32_t RefCount{0};
		std::atomic<EIoErrorCode> ErrorCode{EIoErrorCode::Unknown};
	};
}

/** Represents a pending I/O cache request. */
class CORE_API FIoCacheRequest
{
public:
	FIoCacheRequest() = default;
	FIoCacheRequest(const FIoCacheRequest&) = delete;
	FIoCacheRequest(FIoCacheRequest&& Other) = default;
	~FIoCacheRequest();
	
	FIoCacheRequest& operator=(const FIoCacheRequest&) = delete;
	FIoCacheRequest& operator=(FIoCacheRequest&& Other) = default;

	explicit FIoCacheRequest(UE::IO::Private::FIoCacheRequestBase* Base);

	bool IsValid() const
	{ 
		return Pimpl.IsValid();
	}

	FIoStatus Status()
	{
		return IsValid() ? Pimpl->Status() : FIoStatus(EIoErrorCode::InvalidCode);
	}

	void Cancel();

private:
	TRefCountPtr<UE::IO::Private::FIoCacheRequestBase> Pimpl;
};

/**
 * Interface for retrieving and storing I/O chunks identified by a 20 byte cache key.
 */
class IIoCache
{
public:
	virtual ~IIoCache() = default;
	virtual bool ContainsChunk(const FIoHash& Key) const = 0;
	virtual FIoCacheRequest GetChunk(const FIoHash& Key, const FIoReadOptions& Options, FIoReadCallback&& Callback) = 0;
	virtual FIoStatus PutChunk(const FIoHash& Key, FMemoryView Data) = 0;
};
