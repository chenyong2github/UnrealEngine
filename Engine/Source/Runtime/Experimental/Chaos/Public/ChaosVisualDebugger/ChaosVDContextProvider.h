// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/ContainerAllocationPolicies.h"
#include "HAL/ThreadSingleton.h"

#if WITH_CHAOS_VISUAL_DEBUGGER

/** Chaos Visual Debugger data used to context for logging or debugging purposes */
struct CHAOS_API FChaosVDContext
{
	int32 OwnerID = INDEX_NONE;
	int32 Id = INDEX_NONE;
};

/** Singleton class that manages the thread local storage used to store CVD Context data */
class CHAOS_API FChaosVDThreadContext : public TThreadSingleton<FChaosVDThreadContext>
{
public:

	/** Copies the Current CVD context data into the provided struct
	 * @return true if the copy was successful
	 */
	bool GetCurrentContext(FChaosVDContext& OutContext);
	
	/** Gets the current CVD context data -
	 * Don't use of a function that will recursively push new context data as it might invalidate the pointer
	 * @return Ptr to the Current CVD context data
	 */
	const FChaosVDContext* GetCurrentContext();
	
	/** Pushed a new CVD Context data to the local cvd context stack */
	void PushContext(const FChaosVDContext& InContext);
	
	/** Removes the CVD Context data at the top of the local cvd context stack */
	void PopContext();

protected:
	TArray<FChaosVDContext, TInlineAllocator<16>> LocalContextStack;
};

/** Utility Class that will push the provided CVD Context Data to the local thread storage
 * and remove it when it goes out of scope
 */
struct CHAOS_API FChaosVDScopeContext
{
	FChaosVDScopeContext(const FChaosVDContext& InCVDContext)
	{
		FChaosVDThreadContext::Get().PushContext(InCVDContext);
	}

	~FChaosVDScopeContext()
	{
		FChaosVDThreadContext::Get().PopContext();
	}
};

#ifndef CVD_GET_CURRENT_CONTEXT
	#define CVD_GET_CURRENT_CONTEXT(OutContext) \
		ensure(FChaosVDThreadContext::Get().GetCurrentContext(OutContext))
#endif

#ifndef CVD_SCOPE_CONTEXT
	#define CVD_SCOPE_CONTEXT(InContext) \
		FChaosVDScopeContext CVDScope(InContext);
#endif

#else // WITH_CHAOS_VISUAL_DEBUGGER

#ifndef CVD_GET_CURRENT_CONTEXT
	#define CVD_GET_CURRENT_CONTEXT(OutContext)
#endif

#ifndef CVD_SCOPE_CONTEXT
	#define CVD_SCOPE_CONTEXT(InContext)
#endif

#endif // WITH_CHAOS_VISUAL_DEBUGGER

