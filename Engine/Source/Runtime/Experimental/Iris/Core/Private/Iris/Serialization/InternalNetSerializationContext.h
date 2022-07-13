// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Iris/ReplicationSystem/ObjectReferenceCacheFwd.h"

class UReplicationSystem;
class UObject;

namespace UE::Net::Private
{
	class FObjectReferenceCache;
}

namespace UE::Net::Private
{

class FInternalNetSerializationContext final
{
public:
	FInternalNetSerializationContext();
	FInternalNetSerializationContext(UReplicationSystem* ReplicationSystem, FNetTokenStoreState* RemoteTokenStoreState = nullptr);

	// We allow memory allocations for dynamic states
	void* Alloc(SIZE_T Size, SIZE_T Alignment);
	void Free(void* Ptr);
	void* Realloc(void* PrevAddress, SIZE_T NewSize, uint32 Alignment);

	UReplicationSystem* ReplicationSystem;
	FObjectReferenceCache* ObjectReferenceCache;
	FNetObjectResolveContext ResolveContext;

	// $IRIS TODO Roles really shouldn't be replicated as properties. In dire need of a proper authority system.
	// This is ONLY to be used by role serialization.
	uint32 bDowngradeAutonomousProxyRole : 1;

	// Allow References to be inlined in serialized state
	uint32 bInlineObjectReferenceExports : 1;
};

inline FInternalNetSerializationContext::FInternalNetSerializationContext()
: ReplicationSystem(nullptr)
, ObjectReferenceCache(nullptr)
, bDowngradeAutonomousProxyRole(0)
, bInlineObjectReferenceExports(0)
{}

}
