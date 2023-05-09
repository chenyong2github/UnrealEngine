// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "UObject/Interface.h"
#include "UObject/ObjectKey.h"
#include "UObject/ObjectMacros.h"

#include "TypedElementDataStorageCompatibilityInterface.generated.h"

class AActor;

UINTERFACE(MinimalAPI)
class UTypedElementDataStorageCompatibilityInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * Interface to provide compatibility with existing systems that don't directly
 * support the data storage.
 */
class TYPEDELEMENTFRAMEWORK_API ITypedElementDataStorageCompatibilityInterface
{
	GENERATED_BODY()

public:
	virtual void AddCompatibleObject(AActor* Actor) = 0;
	virtual void RemoveCompatibleObject(AActor* Actor) = 0;
	virtual TypedElementRowHandle FindRowWithCompatibleObject(const TObjectKey<const AActor> Actor) const = 0;
};

template<typename Subsystem>
struct TTypedElementSubsystemTraits final
{
	template<typename T, typename = void>
	struct HasRequiresGameThreadVariable 
	{ 
		static constexpr bool bAvailable = false; 
	};
	template<typename T>
	struct HasRequiresGameThreadVariable <T, decltype((void)T::bRequiresGameThread)>
	{ 
		static constexpr bool bAvailable = true; 
	};

	template<typename T, typename = void>
	struct HasIsHotReloadableVariable
	{ 
		static constexpr bool bAvailable = false;
	};
	template<typename T>
	struct HasIsHotReloadableVariable <T, decltype((void)T::bIsHotReloadable)>
	{ 
		static constexpr bool bAvailable = true;
	};

	static constexpr bool RequiresGameThread()
	{
		if constexpr (HasRequiresGameThreadVariable<Subsystem>::bAvailable)
		{
			return Subsystem::bRequiresGameThread;
		}
		else
		{
			static_assert(HasRequiresGameThreadVariable<Subsystem>::bAvailable, "Subsystem provided to the Typed Elements did not "
				"have a 'static constexpr bool bRequiresGameThread = true|false` declared or have a specialization for "
				"TTypedElementSubsystemTraits.");
			return true;
		}
	}

	static constexpr bool IsHotReloadable()
	{
		if constexpr (HasIsHotReloadableVariable<Subsystem>::bAvailable)
		{
			return Subsystem::bIsHotReloadable;
		}
		else
		{
			static_assert(HasIsHotReloadableVariable<Subsystem>::bAvailable, "Subsystem provided to the Typed Elements did not "
				"have a 'static constexpr bool bIsHotReloadable = true|false` declared or have a specialization for "
				"TTypedElementSubsystemTraits.");
			return false;
		}
	}
};