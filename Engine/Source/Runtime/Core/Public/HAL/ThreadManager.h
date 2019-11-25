// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/Map.h"
#include "HAL/CriticalSection.h"

/**
 * Manages runnables and runnable threads.
 */
class CORE_API FThreadManager
{
	/** Critical section for ThreadList */
	FCriticalSection ThreadsCritical;
	/** Set if thread manager is initialized. */
	static bool bIsInitialized;

	/** List of thread objects to be ticked. */
	TMap<uint32, class FRunnableThread*, TInlineSetAllocator<256>> Threads;

public:

	/**
	* Used internally to add a new thread object.
	*
	* @param Thread thread object.
	* @see RemoveThread
	*/
	void AddThread(uint32 ThreadId, class FRunnableThread* Thread);

	/**
	* Used internally to remove thread object.
	*
	* @param Thread thread object to be removed.
	* @see AddThread
	*/
	void RemoveThread(class FRunnableThread* Thread);

	/** Ticks all fake threads and their runnable objects. */
	void Tick();

	/** Returns the name of a thread given its TLS id */
	const FString& GetThreadName(uint32 ThreadId);

	/** Checks if thread manager has been initialized. Avoids creating the manager trough lazy initialization */
	FORCEINLINE static bool IsInitialized() 
	{
		return bIsInitialized;
	}

#if PLATFORM_WINDOWS || PLATFORM_MAC
	struct FThreadStackBackTrace
	{
		uint32 ThreadId;
		FString ThreadName;
		TArray<uint64, TInlineAllocator<100>> ProgramCounters;
	};

	void GetAllThreadStackBackTraces(TArray<FThreadStackBackTrace>& StackTraces);
#endif

	/**
	 * Enumerate each thread.
	 *
	 */
	void ForEachThread(TFunction<void(uint32, class FRunnableThread*)> Func);

	/**
	 * Access to the singleton object.
	 *
	 * @return Thread manager object.
	 */
	static FThreadManager& Get();
};
