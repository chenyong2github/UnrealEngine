// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Templates/RefCounting.h"

namespace UE
{
namespace DerivedData
{

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Priority for scheduling requests. */
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

/** Interface for a request. Provides functionality common to every type of request. */
class IRequest : public IRefCountedObject
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
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** A wrapper for the request interface that manages lifetime with reference counting. */
class FRequest
{
public:
	/** Construct a null request. */
	FRequest() = default;

	/** Construct a request from a raw pointer. */
	inline explicit FRequest(IRequest* InRequest)
		: Request(InRequest)
	{
	}

	/** Reset this to null. */
	inline void Reset() { Request.SafeRelease(); }

	/** Whether the request is null. */
	inline bool IsNull() const { return !Request; }
	/** Whether the request is not null. */
	inline bool IsValid() const { return !IsNull(); }
	/** Whether the request is not null. */
	inline explicit operator bool() const { return IsValid(); }

	/**
	 * Set the priority of the request.
	 *
	 * @param Priority The new priority for the request. May be higher or lower than the original.
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

private:
	TRefCountPtr<IRequest> Request;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

} // DerivedData
} // UE

#undef UE_API
