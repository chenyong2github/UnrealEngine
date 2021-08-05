// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Templates/Invoke.h"
#include "Templates/RefCounting.h"
#include "Templates/UniquePtr.h"
#include "Templates/UnrealTemplate.h"
#include <atomic>

#define UE_API DERIVEDDATACACHE_API

namespace UE::DerivedData::Private { class FRequestOwnerSharedBase; }

namespace UE::DerivedData
{

/** Priority for scheduling a request. */
enum class EPriority : uint8
{
	/**
	 * Lowest is the minimum priority for asynchronous requests, and primarily for requests that are
	 * speculative in nature, while minimizing impact on other requests.
	 */
	Lowest,
	/**
	 * Low is intended for requests that are below the default priority, but are used for operations
	 * that the program will execute now rather than at an unknown time in the future.
	 */
	Low,
	/**
	 * Normal is intended as the default request priority.
	 */
	Normal,
	/**
	 * High is intended for requests that are on the critical path, but are not required to maintain
	 * interactivity of the program.
	 */
	High,
	/**
	 * Highest is the maximum priority for asynchronous requests, and intended for requests that are
	 * required to maintain interactivity of the program.
	 */
	Highest,
	/**
	 * Blocking is to be used only when the thread making the request will wait on completion of the
	 * request before doing any other work. Requests at this priority level will be processed before
	 * any request at a lower priority level. This priority permits a request to be processed on the
	 * thread making the request. Waiting on a request may increase its priority to this level.
	 */
	Blocking,
};

/** Status of a request that has completed. */
enum class EStatus : uint8
{
	/** The request completed successfully. Any requested data is available. */
	Ok,
	/** The request completed unsuccessfully. Any requested data is not available. */
	Error,
	/** The request was canceled before it completed. Any requested data is not available. */
	Canceled,
};

/**
 * Interface to an asynchronous request that can be prioritized or canceled.
 *
 * Use IRequestOwner, typically FRequestOwner, to reference requests between its Begin and End.
 *
 * Requests typically invoke a callback on completion, and must not return from Cancel or Wait on
 * any thread until any associated callbacks have finished executing. This property is crucial to
 * allowing requests to be chained or nested by creating new requests from within callbacks.
 *
 * @see FRequestBase
 * @see FRequestOwner
 * @see IRequestOwner
 */
class IRequest
{
public:
	virtual ~IRequest() = default;

	/**
	 * Set the priority of the request.
	 *
	 * @param Priority   The priority, which may be higher or lower than the existing priority.
	 */
	virtual void SetPriority(EPriority Priority) = 0;

	/**
	 * Cancel the request and invoke any associated callback.
	 *
	 * Must not return until any callback for the request has finished executing, and the request
	 * has been removed from its owner by calling IRequestOwner::End.
	 */
	virtual void Cancel() = 0;

	/**
	 * Block the calling thread until the request is complete.
	 *
	 * Must not return until any callback for the request has finished executing, and the request
	 * has been removed from its owner by calling IRequestOwner::End.
	 */
	virtual void Wait() = 0;

	/** Add a reference to the request. */
	virtual void AddRef() const = 0;

	/** Release a reference. The request is deleted when the last reference is released. */
	virtual void Release() const = 0;
};

/** An asynchronous request base type that provides reference counting. */
class FRequestBase : public IRequest
{
public:
	FRequestBase() = default;
	~FRequestBase() override = default;
	FRequestBase(const FRequestBase&) = delete;
	FRequestBase& operator=(const FRequestBase&) = delete;

	inline void AddRef() const final
	{
		ReferenceCount.fetch_add(1, std::memory_order_relaxed);
	}

	inline void Release() const final
	{
		if (ReferenceCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
		{
			delete this;
		}
	}

private:
	mutable std::atomic<uint32> ReferenceCount{0};
};

/**
 * A request owner manages requests throughout their execution.
 *
 * Requests are expected to call Begin when created, and End when finished, with their completion
 * callback and arguments passed to End when possible. If a request needs to invoke more than one
 * callback, have a request barrier in scope when executing callbacks to ensure that new requests
 * are prioritized properly when the priority is being changed from another thread.
 *
 * The owner is responsible for keeping itself alive while it has active requests or barriers.
 *
 * @see FRequestBarrier
 * @see FRequestOwner
 */
class IRequestOwner
{
public:
	/**
	 * Begin tracking for the request.
	 *
	 * The owner will hold a reference to the request until End is called, and forward any cancel
	 * operation or priority change to the request. Begin must return before End is called.
	 */
	virtual void Begin(IRequest* Request) = 0;

	/**
	 * End tracking for the request.
	 *
	 * Requests with a single completion callback should use the callback overload of End.
	 *
	 * @return The reference to the request that was held by the owner.
	 */
	virtual TRefCountPtr<IRequest> End(IRequest* Request) = 0;

	/**
	 * End tracking of the request.
	 *
	 * Begins a barrier, ends the request, invokes the callback, then ends the barrier. Keeps its
	 * reference to the request until the callback has returned.
	 *
	 * @return The reference to the request that was held by the owner.
	 */
	template <typename CallbackType, typename... CallbackArgTypes>
	inline TRefCountPtr<IRequest> End(IRequest* Request, CallbackType&& Callback, CallbackArgTypes&&... CallbackArgs);

	/** See FRequestBarrier. */
	virtual void BeginBarrier() = 0;
	virtual void EndBarrier() = 0;

	/** Returns the priority that new requests are expected to inherit. */
	virtual EPriority GetPriority() const = 0;
	/** Returns whether the owner has been canceled, which new requests are expected to check. */
	virtual bool IsCanceled() const = 0;
};

/**
 * A request barrier is expected to be used when an owner may have new requests added to it.
 *
 * An owner may not consider its execution to be complete in the presence of a barrier, and needs
 * to take note of priority changes that occur while within a barrier, since newly-added requests
 * may have been created with the previous priority value.
 */
class FRequestBarrier
{
public:
	inline explicit FRequestBarrier(IRequestOwner& InOwner)
		: Owner(InOwner)
	{
		Owner.BeginBarrier();
	}

	inline ~FRequestBarrier()
	{
		Owner.EndBarrier();
	}

private:
	IRequestOwner& Owner;
};

/** Private base for a shared request owner, which is both an owner and a request. */
class Private::FRequestOwnerSharedBase : public IRequestOwner, public FRequestBase
{
public:
	virtual bool Poll() const = 0;
	virtual void KeepAlive() = 0;
	virtual void Destroy() = 0;
};

/**
 * A concrete request owner that also presents as a request.
 *
 * Request owners may be moved but not copied, and cancel any outstanding requests on destruction
 * unless KeepAlive has been called.
 *
 * @see IRequestOwner
 */
class FRequestOwner
{
public:
	/** Construct a request owner with the given priority. */
	UE_API explicit FRequestOwner(EPriority Priority);

	FRequestOwner(FRequestOwner&&) = default;
	FRequestOwner& operator=(FRequestOwner&&) = default;

	FRequestOwner(const FRequestOwner&) = delete;
	FRequestOwner& operator=(const FRequestOwner&) = delete;

	/** Keep requests in the owner alive until complete, even after destruction of the owner. */
	inline void KeepAlive() { Owner->KeepAlive(); }

	/** Returns the priority that new requests are expected to inherit. */
	inline EPriority GetPriority() const { return Owner->GetPriority(); }
	/** Set the priority of active and future requests in the owner. */
	inline void SetPriority(EPriority Priority) { Owner->SetPriority(Priority); }
	/** Cancel any active and future requests in the owner. */
	inline void Cancel() { Owner->Cancel(); }
	/** Wait for any active and future requests or barriers in the owner. */
	inline void Wait() { Owner->Wait(); }
	/** Poll whether the owner has any active requests or barriers. */
	inline bool Poll() const { return Owner->Poll(); }

	/** Access as a request owner. */
	inline operator IRequestOwner&() { return *Owner; }
	/** Access as a request. */
	inline operator IRequest*() { return Owner.Get(); }

private:
	struct FDeleteOwner { void operator()(Private::FRequestOwnerSharedBase* O) const { if (O) { O->Destroy(); } } };
	TUniquePtr<Private::FRequestOwnerSharedBase, FDeleteOwner> Owner;
};

template <typename CallbackType, typename... CallbackArgTypes>
TRefCountPtr<IRequest> IRequestOwner::End(IRequest* Request, CallbackType&& Callback, CallbackArgTypes&&... CallbackArgs)
{
	FRequestBarrier Barrier(*this);
	TRefCountPtr<IRequest> RequestRef = End(Request);
	Invoke(Forward<CallbackType>(Callback), Forward<CallbackArgTypes>(CallbackArgs)...);
	return RequestRef;
}

} // UE::DerivedData

#undef UE_API
