// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Templates/RefCounting.h"
#include "Templates/UnrealTemplate.h"

namespace UE::DerivedData
{

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Priority for scheduling a request. */
enum class EPriority : uint8
{
	/**
	 * Blocking is to be used only when the thread making the request will wait on completion of the
	 * request before doing any other work. Requests at this priority level will be processed before
	 * any request at a lower priority level. This priority permits a request to be processed on the
	 * thread making the request. Waiting on a request may increase its priority to this level.
	 */
	Blocking,
	/**
	 * Highest is the maximum priority for asynchronous requests, and intended for requests that are
	 * required to maintain interactivity of the program.
	 */
	Highest,
	/**
	 * High is intended for requests that are on the critical path, but are not required to maintain
	 * interactivity of the program.
	 */
	High,
	/**
	 * Normal is intended as the default request priority.
	 */
	Normal,
	/**
	 * Low is intended for requests that are below the default priority, but are used for operations
	 * that the program will execute now rather than at an unknown time in the future.
	 */
	Low,
	/**
	 * Lowest is the minimum priority for asynchronous requests, and primarily for requests that are
	 * speculative in nature, while minimizing impact on other requests.
	 */
	Lowest,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Interface to a request. Use via FRequest to automate reference counting. */
class IRequest
{
public:
	virtual ~IRequest() = default;

	/** Set the priority of the request. */
	virtual void SetPriority(EPriority Priority) = 0;

	/** Cancel the request and invoke its callback. */
	virtual void Cancel() = 0;

	/** Block the calling thread until the request and its callback complete. */
	virtual void Wait() = 0;

	/** Poll the request and return true if complete and false otherwise. */
	virtual bool Poll() = 0;

	/** Add a reference to the request. */
	virtual void AddRef() const = 0;

	/** Release a reference. The request is deleted when the last reference is released. */
	virtual void Release() const = 0;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Handle to a request that manages its lifetime with reference counting. */
template <typename RequestType>
class TRequest
{
public:
	/** Construct a null request. */
	TRequest() = default;

	/** Construct a request from a raw pointer. */
	inline explicit TRequest(RequestType* InRequest) : Request(InRequest) {}

	/** Construct a request from a request of a compatible type. */
	template <typename OtherRequestType, decltype(ImplicitConv<RequestType*>(DeclVal<OtherRequestType*>()))* = nullptr>
	inline TRequest(const TRequest<OtherRequestType>& InRequest) : Request(InRequest.Request) {}
	template <typename OtherRequestType, decltype(ImplicitConv<RequestType*>(DeclVal<OtherRequestType*>()))* = nullptr>
	inline TRequest(TRequest<OtherRequestType>&& InRequest) : Request(MoveTemp(InRequest.Request)) {}

	/** Assign from a request of a compatible type. */
	template <typename OtherRequestType, decltype(ImplicitConv<RequestType*>(DeclVal<OtherRequestType*>()))* = nullptr>
	inline TRequest& operator=(const TRequest<OtherRequestType>& InRequest) { Request = InRequest.Request; return *this; }
	template <typename OtherRequestType, decltype(ImplicitConv<RequestType*>(DeclVal<OtherRequestType*>()))* = nullptr>
	inline TRequest& operator=(TRequest<OtherRequestType>&& InRequest) { Request = MoveTemp(InRequest.Request); return *this; }

	/** Reset this to null. */
	inline void Reset() { Request.SafeRelease(); }

	/** Whether the request is null. */
	inline bool IsNull() const { return !Request; }
	/** Whether the request is not null. */
	inline bool IsValid() const { return !IsNull(); }
	/** Whether the request is not null. */
	inline explicit operator bool() const { return IsValid(); }

	/** Access the referenced request. */
	inline RequestType* Get() const { return Request; }
	inline RequestType* operator->() const { return Request; }
	inline RequestType& operator*() const { return *Request; }

	/**
	 * Set the priority of the request.
	 *
	 * @param Priority   The priority, which may be higher or lower than the existing priority.
	 */
	inline void SetPriority(EPriority Priority)
	{
		if (Request)
		{
			Request->SetPriority(Priority);
		}
	}

	/**
	 * Cancel the request and invoke any associated callback.
	 *
	 * Does not return until any callback for the request has finished executing.
	 */
	inline void Cancel() const
	{
		if (Request)
		{
			Request->Cancel();
		}
	}

	/**
	 * Block the calling thread until the request is complete.
	 *
	 * Avoid waiting if possible and use callbacks to manage control flow instead.
	 * Does not return until any callback for the request has finished executing.
	 */
	inline void Wait() const
	{
		if (Request)
		{
			Request->Wait();
		}
	}

	/**
	 * Poll the request and return true if complete and false otherwise.
	 *
	 * Avoid polling if possible and use callbacks to manage control flow instead.
	 * Does not return true until any callback for the request has finished executing.
	 */
	inline bool Poll() const
	{
		return !Request || Request->Poll();
	}

	template <typename OtherRequestType, decltype(ImplicitConv<IRequest*>(DeclVal<OtherRequestType*>()))* = nullptr>
	inline bool operator==(const TRequest<OtherRequestType>& Other) const { return Request == Other.Request; }
	template <typename OtherRequestType, decltype(ImplicitConv<IRequest*>(DeclVal<OtherRequestType*>()))* = nullptr>
	inline bool operator!=(const TRequest<OtherRequestType>& Other) const { return Request != Other.Request; }
	template <typename OtherRequestType, decltype(ImplicitConv<IRequest*>(DeclVal<OtherRequestType*>()))* = nullptr>
	inline bool operator<(const TRequest<OtherRequestType>& Other) const { return Request < Other.Request; }

	friend inline uint32 GetTypeHash(const TRequest& Other) { return ::GetTypeHash(Other.Request); }

	template <typename OtherRequestType>
	friend class TRequest;

private:
	TRefCountPtr<RequestType> Request;
};

/** Handle to a request that manages its lifetime with reference counting. */
using FRequest = TRequest<IRequest>;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

} // UE::DerivedData
