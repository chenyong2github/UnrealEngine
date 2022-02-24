// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/CoreNetTypes.h"

namespace UE {
namespace Net {

/**
* Stores the SubObjects to replicate and the network condition dictating to which connection they can replicate to.
*/
struct NETCORE_API FSubObjectRegistry
{
public:

	enum class EResult : uint8
	{
		/** SubObject is new in the registry */
		NewEntry,
		/** SubObject already was in the registry */
		AlreadyRegistered,
		/** SubObject was already registered but the NetCondition passed is different from the entry */
		NetConditionConflict,
	};

	/** Adds a subobject and returns if it's a new entry or existing entry */
	FSubObjectRegistry::EResult AddSubObjectUnique(UObject* InSubObject, ELifetimeCondition InNetCondition);

	/** Remove the subobject from the replicated list. Returns true if the subobject had been registered. */
	bool RemoveSubObject(UObject* InSubObject);

	/** Find the NetCondition of a SubObject. Returns COND_MAX if not registered */
	ELifetimeCondition GetNetCondition(UObject* SubObject) const;

	struct FEntry
	{
		/** Raw pointer since users are obligated to call RemoveReplicatedSubobject before destroying the subobject otherwise it will cause a crash.  */
		UObject* SubObject = nullptr;

		/** The network condition that chooses which connection this subobject can be replicated to. Default is none which means all connections receive it. */
		ELifetimeCondition NetCondition = COND_None;

		bool operator==(const FEntry& rhs) const { return SubObject == rhs.SubObject; }
		bool operator==(UObject* rhs) const { return SubObject == rhs; }
	};

	/** Returns the list of registered subobjects */
	const TArray<FSubObjectRegistry::FEntry>& GetRegistryList() const { return Registry; }

	/** Returns true if the subobject is contained in this list */
	bool IsSubObjectInRegistry(UObject* SubObject) const;

private:

	TArray<FEntry> Registry;
};

} // namespace Net
} // namespace UE

