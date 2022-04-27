// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/World.h"
#include "Subsystems/WorldSubsystem.h"

/**
 * Traits describing how a given piece of code can be used by Mass. By default everything is treated as
 * needing to be accessed only on game thread (implying single-thread access). To change the behavior for a given
 * class just specialize this template, like so:
 *
 * template<>
 * struct TMassExternalSubsystemTraits<UMyCustomManager>
 * {
 *		enum { GameThreadOnly = false; }
 * }
 *
 * this will let Mass know it can access UMyCustomManager on any thread.
 *
 * This information is being used to calculate processor and query dependencies as well as appropriate distribution of
 * calculations across threads.
 */
template <typename T>
struct TMassExternalSubsystemTraits final
{
	enum
	{
		GameThreadOnly = true,
		ThreadSafeRead = false,
		ThreadSafeWrite = false,
	};
};

namespace FMassExternalSubsystemTraits
{
	/** 
	Every TMassExternalSubsystemTraits specialization needs to implement the following. Not supplying default implementations
	to be able to catch missing implementations and header inclusion at compilation time.
	
	This is a getter function that given a UWorld* fetches an instance.
	*/
	template<typename T, typename = typename TEnableIf<TIsDerivedFrom<T, UWorldSubsystem>::IsDerived>::Type>
	static T* GetInstance(const UWorld* World)
	{ 
		// note that the default implementation works only for UWorldSubsystems
		return UWorld::GetSubsystem<T>(World);
	}
}
