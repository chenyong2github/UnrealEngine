// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

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

} // UE::DerivedData
