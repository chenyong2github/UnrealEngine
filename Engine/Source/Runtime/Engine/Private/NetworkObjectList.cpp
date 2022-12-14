// Copyright Epic Games, Inc. All Rights Reserved.

#include "Engine/NetworkObjectList.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "EngineUtils.h"
#include "Serialization/Archive.h"
#include "Net/NetworkGranularMemoryLogging.h"
#include "Engine/ActorChannel.h"
#include "UObject/Package.h"

namespace UE::Net::Private
{
	bool bTrackDormantObjectsByLevel = false;
	static FAutoConsoleVariableRef CVarNetTrackDormantObjectsByLevel(
		TEXT("net.TrackDormantObjectsByLevel"),
		bTrackDormantObjectsByLevel,
		TEXT("When true, network object list will maintain a set of dormant actors per connnection per level."),
		ECVF_Default);

	static const TCHAR* LexToString(ENetSubObjectStatus SubObjectStatus)
	{
		switch (SubObjectStatus)
		{
		case ENetSubObjectStatus::Active: return TEXT("Active");
		case ENetSubObjectStatus::TearOff: return TEXT("TearOff");
		case ENetSubObjectStatus::Delete: return TEXT("Delete");
		}
		return TEXT("Missing");
	}
}

void FNetworkObjectList::AddInitialObjects(UWorld* const World, UNetDriver* NetDriver)
{
	if (World == nullptr || NetDriver == nullptr)
	{
		return;
	}

#if UE_WITH_IRIS
	if (NetDriver->GetReplicationSystem())
	{
		return;
	}
#endif // UE_WITH_IRIS


	for (FActorIterator Iter(World); Iter; ++Iter)
	{
		AActor* Actor = *Iter;
		if (IsValid(Actor) && ULevel::IsNetActor(Actor) && !UNetDriver::IsDormInitialStartupActor(Actor))
		{
			FindOrAdd(Actor, NetDriver);
		}
	}
}

TSharedPtr<FNetworkObjectInfo> FNetworkObjectList::Find(AActor* const Actor)
{
	if (Actor == nullptr)
	{
		return nullptr;
	}

	if (TSharedPtr<FNetworkObjectInfo>* InfoPtr = AllNetworkObjects.Find(Actor))
	{
		return *InfoPtr;
	}

	return TSharedPtr<FNetworkObjectInfo>();
}

TSharedPtr<FNetworkObjectInfo>* FNetworkObjectList::FindOrAdd(AActor* const Actor, UNetDriver* NetDriver, bool* OutWasAdded)
{
	if (!IsValid(Actor) ||

		// This implies the actor was added either added sometime during UWorld::DestroyActor,
		// or was potentially previously destroyed (and its Index points to a diferent, non-PendingKill object).
		!ensureAlwaysMsgf(!Actor->IsActorBeingDestroyed(), TEXT("Attempting to add an actor that's being destroyed to the NetworkObjectList Actor=%s NetDriverName=%s"), *Actor->GetPathName(), NetDriver ? *NetDriver->NetDriverName.ToString() : TEXT("None")))
	{
		return nullptr;
	}

#if UE_WITH_IRIS
	if (NetDriver && NetDriver->GetReplicationSystem())
	{
		return nullptr;
	}
#endif // UE_WITH_IRIS

	TSharedPtr<FNetworkObjectInfo>* NetworkObjectInfo = AllNetworkObjects.Find(Actor);
	
	if (NetworkObjectInfo == nullptr)
	{
		// We do a name check here so we don't add an actor to a network list that it shouldn't belong to
		if (NetDriver && NetDriver->ShouldReplicateActor(Actor))
		{
			NetworkObjectInfo = &AllNetworkObjects[AllNetworkObjects.Emplace(new FNetworkObjectInfo(Actor))];
			ActiveNetworkObjects.Add(*NetworkObjectInfo);

			UE_LOG(LogNetDormancy, VeryVerbose, TEXT("FNetworkObjectList::Add: Adding actor. Actor: %s, Total: %i, Active: %i, NetDriverName: %s"), *Actor->GetName(), AllNetworkObjects.Num(), ActiveNetworkObjects.Num(), *NetDriver->NetDriverName.ToString());

			if (OutWasAdded)
			{
				*OutWasAdded = true;
			}
		}
	}
	else
	{
		UE_LOG(LogNetDormancy, VeryVerbose, TEXT("FNetworkObjectList::Add: Already contained. Actor: %s, Total: %i, Active: %i, NetDriverName: %s"), *Actor->GetName(), AllNetworkObjects.Num(), ActiveNetworkObjects.Num(), NetDriver ? *NetDriver->NetDriverName.ToString() : TEXT("None"));
		if (OutWasAdded)
		{
			*OutWasAdded = false;
		}
	}
	
	check((ActiveNetworkObjects.Num() + ObjectsDormantOnAllConnections.Num()) == AllNetworkObjects.Num());

	return NetworkObjectInfo;
}

void FNetworkObjectList::Remove(AActor* const Actor)
{
	if (Actor == nullptr)
	{
		return;
	}

	TSharedPtr<FNetworkObjectInfo>* NetworkObjectInfoPtr = AllNetworkObjects.Find(Actor);

	if (NetworkObjectInfoPtr == nullptr)
	{
		// Sanity check that we're not on the other lists either
		check(!ActiveNetworkObjects.Contains(Actor));
		check(!ObjectsDormantOnAllConnections.Contains(Actor));
		check((ActiveNetworkObjects.Num() + ObjectsDormantOnAllConnections.Num()) == AllNetworkObjects.Num());
		return;
	}

	const FName PackageName = Actor->GetLevel()->GetPackage()->GetFName();

	FNetworkObjectInfo* NetworkObjectInfo = NetworkObjectInfoPtr->Get();

	// Lower the dormant object count for each connection this object is dormant on
	for (auto ConnectionIt = NetworkObjectInfo->DormantConnections.CreateIterator(); ConnectionIt; ++ConnectionIt)
	{
		UNetConnection* Connection = (*ConnectionIt).Get();

		if (Connection == nullptr || Connection->GetConnectionState() == USOCK_Closed)
		{
			if (UE::Net::Private::bTrackDormantObjectsByLevel)
			{
				DormantObjectsPerConnection.Remove(Connection);
			}

			ConnectionIt.RemoveCurrent();
			continue;
		}

		int32& NumDormantObjectsPerConnectionRef = NumDormantObjectsPerConnection.FindOrAdd(Connection);
		check(NumDormantObjectsPerConnectionRef > 0);
		NumDormantObjectsPerConnectionRef--;

		if (UE::Net::Private::bTrackDormantObjectsByLevel)
		{
			if (TMap<FName, FNetworkObjectSet>* DormantObjectsByLevel = DormantObjectsPerConnection.Find(Connection))
			{
				if (FNetworkObjectSet* DormantObjects = DormantObjectsByLevel->Find(PackageName))
				{
					DormantObjects->Remove(Actor);
					
					if (DormantObjects->Num() == 0)
					{
						DormantObjectsByLevel->Remove(PackageName);
					}
				}
			}
		}
	}

	// Remove this object from all lists
	AllNetworkObjects.Remove(Actor);
	ActiveNetworkObjects.Remove(Actor);
	ObjectsDormantOnAllConnections.Remove(Actor);

	if (UE::Net::Private::bTrackDormantObjectsByLevel)
	{
		if (FNetworkObjectSet* FullyDormant = FullyDormantObjectsByLevel.Find(PackageName))
		{
			FullyDormant->Remove(Actor);

			if (FullyDormant->Num() == 0)
			{
				FullyDormantObjectsByLevel.Remove(PackageName);
			}
		}
	}

	check((ActiveNetworkObjects.Num() + ObjectsDormantOnAllConnections.Num()) == AllNetworkObjects.Num());
}

void FNetworkObjectList::MarkDormant(AActor* const Actor, UNetConnection* const Connection, const int32 NumConnections, UNetDriver* NetDriver)
{
	TSharedPtr<FNetworkObjectInfo>* NetworkObjectInfoPtr = FindOrAdd(Actor, NetDriver);

	if (NetworkObjectInfoPtr == nullptr)
	{
		return;		// Actor doesn't belong to this net driver name
	}

	const FName PackageName = Actor->GetLevel()->GetPackage()->GetFName();

	FNetworkObjectInfo* NetworkObjectInfo = NetworkObjectInfoPtr->Get();

	// Add the connection to the list of dormant connections (if it's not already on the list)
	if (!NetworkObjectInfo->DormantConnections.Contains(Connection))
	{
		check(ActiveNetworkObjects.Contains(Actor));

		NetworkObjectInfo->DormantConnections.Add(Connection);

		// Keep track of the number of dormant objects on each connection
		int32& NumDormantObjectsPerConnectionRef = NumDormantObjectsPerConnection.FindOrAdd(Connection);
		NumDormantObjectsPerConnectionRef++;

		if (UE::Net::Private::bTrackDormantObjectsByLevel)
		{
			// make sure the connection map exists
			TMap<FName, FNetworkObjectSet>& DormantObjectsByLevel = DormantObjectsPerConnection.FindOrAdd(Connection);

			// if not fully dormant
			if (NetworkObjectInfo->DormantConnections.Num() != NumConnections)
			{
				FNetworkObjectSet& DormantObjects = DormantObjectsByLevel.FindOrAdd(PackageName);
				DormantObjects.Add(*NetworkObjectInfoPtr);
			}
		}

		UE_LOG(LogNetDormancy, Log, TEXT("FNetworkObjectList::MarkDormant: Actor is now dormant. Actor: %s. NumDormant: %i, Connection: %s"), *Actor->GetName(), NumDormantObjectsPerConnectionRef, *Connection->GetName());
	}

	// Clean up DormantConnections list (remove possible GC'd connections)
	for (auto ConnectionIt = NetworkObjectInfo->DormantConnections.CreateIterator(); ConnectionIt; ++ConnectionIt)
	{
		if ((*ConnectionIt).Get() == nullptr || (*ConnectionIt).Get()->GetConnectionState() == USOCK_Closed)
		{
			if (UE::Net::Private::bTrackDormantObjectsByLevel)
			{
				DormantObjectsPerConnection.Remove((*ConnectionIt).Get());
			}

			ConnectionIt.RemoveCurrent();
		}
	}

	// At this point, after removing null references, we should never be over the connection count
	check(NetworkObjectInfo->DormantConnections.Num() <= NumConnections);

	// If the number of dormant connections now matches the number of actual connections, we can remove this object from the active list
	if (NetworkObjectInfo->DormantConnections.Num() == NumConnections)
	{
		ObjectsDormantOnAllConnections.Add(*NetworkObjectInfoPtr);

		if (UE::Net::Private::bTrackDormantObjectsByLevel)
		{
			FNetworkObjectSet& FullyDormant = FullyDormantObjectsByLevel.FindOrAdd(PackageName);
			FullyDormant.Add(*NetworkObjectInfoPtr);

			// Remove from connection object lists
			for (auto ConnectionIt = NetworkObjectInfo->DormantConnections.CreateIterator(); ConnectionIt; ++ConnectionIt)
			{
				if (TMap<FName, FNetworkObjectSet>* DormantObjectsByLevel = DormantObjectsPerConnection.Find((*ConnectionIt).Get()))
				{
					if (FNetworkObjectSet* DormantObjects = DormantObjectsByLevel->Find(PackageName))
					{
						const int32 NumRemoved = DormantObjects->Remove(Actor);
						checkf((NumRemoved > 0) || (Connection == (*ConnectionIt).Get()), TEXT("Actor not found in Connection->Level->Dormant map: %s"), *GetNameSafe(Actor));

						if (DormantObjects->Num() == 0)
						{
							DormantObjectsByLevel->Remove(PackageName);
						}
					}
				}
			}
		}

		ActiveNetworkObjects.Remove(Actor);

		UE_LOG(LogNetDormancy, Log, TEXT("FNetworkObjectList::MarkDormant: Actor is now dormant on all connections. Actor: %s. Total: %i, Active: %i, Connection: %s"), *Actor->GetName(), AllNetworkObjects.Num(), ActiveNetworkObjects.Num(), *Connection->GetName());
	}

	check((ActiveNetworkObjects.Num() + ObjectsDormantOnAllConnections.Num()) == AllNetworkObjects.Num());
}

bool FNetworkObjectList::MarkActiveInternal(const TSharedPtr<FNetworkObjectInfo>& ObjectInfo, UNetConnection* const Connection, UNetDriver* NetDriver)
{
	FNetworkObjectInfo* NetworkObjectInfo = ObjectInfo.Get();
	AActor* Actor = NetworkObjectInfo->Actor;

	const FName PackageName = Actor->GetLevel()->GetPackage()->GetFName();

	// Remove from the ObjectsDormantOnAllConnections if needed
	if (ObjectsDormantOnAllConnections.Remove(Actor) > 0)
	{
		// Put this object back on the active list
		ActiveNetworkObjects.Add(ObjectInfo);

		UE_LOG(LogNetDormancy, Log, TEXT("FNetworkObjectList::MarkDormant: Actor is no longer dormant on all connections. Actor: %s. Total: %i, Active: %i, Connection: %s"), *Actor->GetName(), AllNetworkObjects.Num(), ActiveNetworkObjects.Num(), *Connection->GetName());

		if (UE::Net::Private::bTrackDormantObjectsByLevel)
		{
			if (FNetworkObjectSet* FullyDormant = FullyDormantObjectsByLevel.Find(PackageName))
			{
				const int32 NumRemoved = FullyDormant->Remove(Actor);
				checkf(NumRemoved > 0, TEXT("Actor not found in full Level->Dormant map: %s"), *GetNameSafe(Actor));

				if (FullyDormant->Num() == 0)
				{
					FullyDormantObjectsByLevel.Remove(PackageName);
				}

				// add back into per connection maps
				for (auto ConnectionIt = NetworkObjectInfo->DormantConnections.CreateIterator(); ConnectionIt; ++ConnectionIt)
				{
					TMap<FName, FNetworkObjectSet>& DormantObjectsByLevel = DormantObjectsPerConnection.FindOrAdd((*ConnectionIt).Get());
					FNetworkObjectSet& DormantObjects = DormantObjectsByLevel.FindOrAdd(PackageName);
					DormantObjects.Add(ObjectInfo);
				}
			}
		}
	}

	check((ActiveNetworkObjects.Num() + ObjectsDormantOnAllConnections.Num()) == AllNetworkObjects.Num());

	// Remove connection from the dormant connection list
	if (NetworkObjectInfo->DormantConnections.Remove(Connection) > 0)
	{
		// Add the connection to the list of recently dormant connections
		NetworkObjectInfo->RecentlyDormantConnections.Add(Connection);

		int32& NumDormantObjectsPerConnectionRef = NumDormantObjectsPerConnection.FindOrAdd(Connection);
		check(NumDormantObjectsPerConnectionRef > 0);
		NumDormantObjectsPerConnectionRef--;

		if (UE::Net::Private::bTrackDormantObjectsByLevel)
		{
			if (TMap<FName, FNetworkObjectSet>* DormantObjectsByLevel = DormantObjectsPerConnection.Find(Connection))
			{
				if (FNetworkObjectSet* DormantObjects = DormantObjectsByLevel->Find(PackageName))
				{
					const int32 NumRemoved = DormantObjects->Remove(Actor);
					checkf(NumRemoved > 0, TEXT("Actor not found in Connection->Level->Dormant map: %s"), *GetNameSafe(Actor));

					if (DormantObjects->Num() == 0)
					{
						DormantObjectsByLevel->Remove(PackageName);
					}
				}
			}
		}

		UE_LOG(LogNetDormancy, Log, TEXT("FNetworkObjectList::MarkActive: Actor is no longer dormant. Actor: %s. NumDormant: %i, Connection: %s"), *Actor->GetName(), NumDormantObjectsPerConnectionRef, *Connection->GetName());
		return true;
	}

	return false;
}

bool FNetworkObjectList::MarkActive(AActor* const Actor, UNetConnection* const Connection, UNetDriver* NetDriver)
{
	TSharedPtr<FNetworkObjectInfo>* NetworkObjectInfoPtr = FindOrAdd(Actor, NetDriver);

	if (NetworkObjectInfoPtr == nullptr)
	{
		return false;		// Actor doesn't belong to this net driver name
	}

	return MarkActiveInternal(*NetworkObjectInfoPtr, Connection, NetDriver);
}

void FNetworkObjectList::MarkDirtyForReplay(AActor* const Actor)
{
	if (Actor)
	{
		if (TSharedPtr<FNetworkObjectInfo>* InfoPtr = AllNetworkObjects.Find(Actor))
		{
			if (FNetworkObjectInfo* ObjectInfo = InfoPtr->Get())
			{
				ObjectInfo->bDirtyForReplay = true;
			}
		}
	}
}

void FNetworkObjectList::ClearRecentlyDormantConnection(AActor* const Actor, UNetConnection* const Connection, UNetDriver* NetDriver)
{
	TSharedPtr<FNetworkObjectInfo>* NetworkObjectInfoPtr = FindOrAdd(Actor, NetDriver);

	if (NetworkObjectInfoPtr == nullptr)
	{
		return;		// Actor doesn't belong to this net driver name
	}

	FNetworkObjectInfo* NetworkObjectInfo = NetworkObjectInfoPtr->Get();

	NetworkObjectInfo->RecentlyDormantConnections.Remove(Connection);
}

void FNetworkObjectList::HandleConnectionAdded()
{
	// When a new connection is added, we must add all objects back to the active list so the new connection will process it
	// Once the objects is dormant on that connection, it will then be removed from the active list again
	for (auto It = ObjectsDormantOnAllConnections.CreateIterator(); It; ++It)
	{
		ActiveNetworkObjects.Add(*It);
	}

	ObjectsDormantOnAllConnections.Empty();
	FullyDormantObjectsByLevel.Empty();
}

void FNetworkObjectList::ResetDormancyState()
{
	// Reset all state related to dormancy, and move all objects back on to the active list
	ObjectsDormantOnAllConnections.Empty();
	FullyDormantObjectsByLevel.Empty();

	ActiveNetworkObjects = AllNetworkObjects;

	for (auto It = AllNetworkObjects.CreateIterator(); It; ++It)
	{
		FNetworkObjectInfo* NetworkObjectInfo = ( *It ).Get();

		NetworkObjectInfo->DormantConnections.Empty();
		NetworkObjectInfo->RecentlyDormantConnections.Empty();
	}

	NumDormantObjectsPerConnection.Empty();
	DormantObjectsPerConnection.Empty();
}

int32 FNetworkObjectList::GetNumDormantActorsForConnection(UNetConnection* const Connection) const
{
	const int32 *Count = NumDormantObjectsPerConnection.Find( Connection );

	return (Count != nullptr) ? *Count : 0;
}

void FNetworkObjectList::ForceActorRelevantNextUpdate(AActor* const Actor, UNetDriver* NetDriver)
{
	TSharedPtr<FNetworkObjectInfo>* NetworkObjectInfoPtr = FindOrAdd(Actor, NetDriver);

	if (NetworkObjectInfoPtr == nullptr)
	{
		return;		// Actor doesn't belong to this net driver name
	}

	FNetworkObjectInfo* NetworkObjectInfo = NetworkObjectInfoPtr->Get();

	NetworkObjectInfo->ForceRelevantFrame = NetDriver->ReplicationFrame + 1;
}

void FNetworkObjectList::Reset()
{
	// Reset all state
	AllNetworkObjects.Empty();
	ActiveNetworkObjects.Empty();
	ObjectsDormantOnAllConnections.Empty();
	NumDormantObjectsPerConnection.Empty();
	FullyDormantObjectsByLevel.Empty();
	DormantObjectsPerConnection.Empty();
}

void FNetworkObjectInfo::CountBytes(FArchive& Ar) const
{
	GRANULAR_NETWORK_MEMORY_TRACKING_INIT(Ar, "FNetworkObjectInfo::CountBytes");

	GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("DormantConnections", DormantConnections.CountBytes(Ar));
	GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("RecentlyDormantConnections", RecentlyDormantConnections.CountBytes(Ar));
#if UE_REPLICATED_OBJECT_REFCOUNTING
	GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("ActiveSubObjectChannelReferences", ActiveSubObjectChannelReferences.CountBytes(Ar));
	GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("InvalidSubObjectChannelReferences", InvalidSubObjectChannelReferences.CountBytes(Ar));
#endif
}

void FNetworkObjectList::CountBytes(FArchive& Ar) const
{
	GRANULAR_NETWORK_MEMORY_TRACKING_INIT(Ar, "FNetworkObjectList::CountBytes");

	GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("ActiveNetworkObjects", ActiveNetworkObjects.CountBytes(Ar));
	GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("ObjectsDormantOnAllConnections", ObjectsDormantOnAllConnections.CountBytes(Ar));
	GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("NumDormantObjectsPerConnection", NumDormantObjectsPerConnection.CountBytes(Ar));

	GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("FullyDormantObjectsByLevel",
		FullyDormantObjectsByLevel.CountBytes(Ar);
		for (const TPair<FName, FNetworkObjectSet>& LevelPair : FullyDormantObjectsByLevel)
		{
			LevelPair.Value.CountBytes(Ar);
		}
	);
	
	GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("DormantObjectsPerConnection",
		DormantObjectsPerConnection.CountBytes(Ar);
		for (const TPair<TObjectKey<UNetConnection>, TMap<FName, FNetworkObjectSet>>& ConnectionPair : DormantObjectsPerConnection)
		{
			ConnectionPair.Value.CountBytes(Ar);

			for (const TPair<FName, FNetworkObjectSet>& LevelPair : ConnectionPair.Value)
			{
				LevelPair.Value.CountBytes(Ar);
			}
		}
	);

	// ObjectsDormantOnAllConnections and ActiveNetworkObjects are both sub sets of AllNetworkObjects
	// and only have pointers back to the data there.
	// So, to avoid double (or triple) counting, only explicit count the elements from AllNetworkObjects.
	GRANULAR_NETWORK_MEMORY_TRACKING_TRACK("AllNetworkObjects",
		AllNetworkObjects.CountBytes(Ar);
		for (const TSharedPtr<FNetworkObjectInfo>& SharedInfo : AllNetworkObjects)
		{
			if (FNetworkObjectInfo const* const Info = SharedInfo.Get())
			{
				Ar.CountBytes(sizeof(FNetworkObjectInfo), sizeof(FNetworkObjectInfo));
				Info->CountBytes(Ar);
			}
		}
	);
}

void FNetworkObjectList::FlushDormantActors(UNetConnection* const Connection, const FName& PackageName)
{
	FNetworkObjectSet DormantActors;

	// fully dormant actors
	if (const FNetworkObjectSet* FullyDormantObjects = FullyDormantObjectsByLevel.Find(PackageName))
	{
		DormantActors.Append(*FullyDormantObjects);
	}

	// connection specific dormant actors
	if (const TMap<FName, FNetworkObjectSet>* DormantObjectsByLevel = DormantObjectsPerConnection.Find(Connection))
	{
		if (const FNetworkObjectSet* DormantObjects = DormantObjectsByLevel->Find(PackageName))
		{
			DormantActors.Append(*DormantObjects);
		}
	}

	for (const TSharedPtr<FNetworkObjectInfo>& ActorInfo : DormantActors)
	{
		MarkActiveInternal(ActorInfo, Connection, Connection->Driver);
	}
}
#if UE_REPLICATED_OBJECT_REFCOUNTING
void FNetworkObjectList::SetSubObjectForDeletion(AActor* Actor, UObject* SubObject)
{
	InvalidateSubObject(Actor, SubObject, ENetSubObjectStatus::Delete);
}

void FNetworkObjectList::SetSubObjectForTearOff(AActor* Actor, UObject* SubObject)
{
	InvalidateSubObject(Actor, SubObject, ENetSubObjectStatus::TearOff);
}

void FNetworkObjectList::InvalidateSubObject(AActor* Actor, UObject* SubObject, ENetSubObjectStatus InvalidStatus)
{
	ensure(InvalidStatus != ENetSubObjectStatus::Active);

	if (TSharedPtr<FNetworkObjectInfo>* InfoPtr = AllNetworkObjects.Find(Actor))
	{
		FNetworkObjectInfo* ActorNetInfo = InfoPtr->Get();

		const TWeakObjectPtr<UObject> SubObjectPtr = SubObject;
		const FSetElementId FoundId = ActorNetInfo->ActiveSubObjectChannelReferences.FindId(SubObjectPtr);

		if (FoundId.IsValidId())
		{
			// Flag its new state
			ActorNetInfo->ActiveSubObjectChannelReferences[FoundId].Status = InvalidStatus;

			// Move the reference to the destroyed list.
			ActorNetInfo->InvalidSubObjectChannelReferences.Emplace(MoveTemp(ActorNetInfo->ActiveSubObjectChannelReferences[FoundId]));
			ActorNetInfo->ActiveSubObjectChannelReferences.Remove(FoundId);

			// Increase dirty count so channels can refresh this list
			ActorNetInfo->InvalidSubObjectDirtyCount = FMath::Max(ActorNetInfo->InvalidSubObjectDirtyCount + 1, 1); // Wrap around to 1 because 0 is the default actor channel value
		}
	}
}

void FNetworkObjectList::AddSubObjectChannelReference(AActor* OwnerActor, UObject* ReplicatedSubObject, UObject* ReferenceOwner)
{
	TSharedPtr<FNetworkObjectInfo>* InfoPtr = AllNetworkObjects.Find(OwnerActor);

	if (ensureMsgf(InfoPtr, TEXT("The owner (0x%p) %s replicated %s (0x%p) without having any network objectinfo for %s"), ReferenceOwner, *GetNameSafe(ReferenceOwner), *GetNameSafe(ReplicatedSubObject), ReplicatedSubObject, *OwnerActor->GetName()))
	{
		FNetworkObjectInfo* ActorNetInfo = InfoPtr->Get();
		const TWeakObjectPtr<UObject> SubObjectPtr = ReplicatedSubObject;

#if DO_REPLICATED_OBJECT_CHANNELREF_CHECKS
		// Make sure the object wasn't previously put in the invalid list  
		if (FNetworkObjectInfo::FSubObjectChannelReference* DestroyedObjectRef = ActorNetInfo->InvalidSubObjectChannelReferences.FindByKey(SubObjectPtr))
		{
			ensureMsgf(DestroyedObjectRef == nullptr, TEXT("SubObject %s (0x%p) owned by %s was replicated by connection (0x%p) %s after it was set to (0x%p)"), *GetNameSafe(ReplicatedSubObject), ReplicatedSubObject, *GetNameSafe(OwnerActor), ReferenceOwner, *GetNameSafe(ReferenceOwner), UE::Net::Private::LexToString(DestroyedObjectRef->Status));
		}
#endif

		if (FNetworkObjectInfo::FSubObjectChannelReference* ChannelReferences = ActorNetInfo->ActiveSubObjectChannelReferences.Find(SubObjectPtr))
		{
#if DO_REPLICATED_OBJECT_CHANNELREF_CHECKS
			ensureMsgf(!ChannelReferences->RegisteredOwners.Contains(ReferenceOwner), TEXT("SubObject %s (0x%p) owned by %s was already referenced by (0x%p) %s"), *GetNameSafe(ReplicatedSubObject), ReplicatedSubObject, *GetNameSafe(OwnerActor), ReferenceOwner, *GetNameSafe(ReferenceOwner));
			ChannelReferences->RegisteredOwners.Add(ReferenceOwner);
#endif

			ChannelReferences->ChannelRefCount++;
			check(ChannelReferences->ChannelRefCount < MAX_uint16);

			UE_LOG(LogNetSubObject, Verbose, TEXT("Adding ChannelRef (%d) to %s (0x%p) owned by %s for (0x%p) %s"), ChannelReferences->ChannelRefCount, *GetNameSafe(ReplicatedSubObject), ReplicatedSubObject,  *GetNameSafe(OwnerActor), ReferenceOwner, *GetNameSafe(ReferenceOwner));
		}
		else
		{
			bool bWasSet = false;
			FSetElementId ElementId = ActorNetInfo->ActiveSubObjectChannelReferences.Emplace(FNetworkObjectInfo::FSubObjectChannelReference(SubObjectPtr), &bWasSet);

			UE_LOG(LogNetSubObject, Verbose, TEXT("Adding ChannelRef (new) to %s (0x%p) owned by %s for (0x%p) %s"), *GetNameSafe(ReplicatedSubObject), ReplicatedSubObject, *GetNameSafe(OwnerActor), ReferenceOwner, *GetNameSafe(ReferenceOwner));

#if DO_REPLICATED_OBJECT_CHANNELREF_CHECKS
			ActorNetInfo->ActiveSubObjectChannelReferences[ElementId].RegisteredOwners.Add(ReferenceOwner);
#endif
		}
	}
}

void FNetworkObjectList::RemoveMultipleSubObjectChannelReference(AActor* OwnerActor, const TArrayView<TWeakObjectPtr<UObject>>& SubObjectsToRemove, UObject* ReferenceOwner)
{
	if (TSharedPtr<FNetworkObjectInfo>* InfoPtr = AllNetworkObjects.Find(OwnerActor))
	{
		check(InfoPtr->IsValid());

		FNetworkObjectInfo* ActorNetInfo = InfoPtr->Get();

		for (const TWeakObjectPtr<UObject>& SubObjectPtr : SubObjectsToRemove)
		{
			HandleRemoveAnySubObjectChannelRef(ActorNetInfo, SubObjectPtr, ReferenceOwner);
		}
	}
}

void FNetworkObjectList::RemoveMultipleInvalidSubObjectChannelReference(FNetworkObjectInfo* ActorNetInfo, const TArrayView<TWeakObjectPtr<UObject>>& SubObjectsToRemove, UObject* ReferenceOwner)
{
	check(ActorNetInfo);

	for (const TWeakObjectPtr<UObject>& SubObjectPtr : SubObjectsToRemove)
	{
		HandleRemoveInvalidSubObjectRef(ActorNetInfo, SubObjectPtr, ReferenceOwner);
	}
}

void FNetworkObjectList::RemoveMultipleActiveSubObjectChannelReference(FNetworkObjectInfo* ActorNetInfo, const TArrayView<TWeakObjectPtr<UObject>>& SubObjectsToRemove, UObject* ReferenceOwner)
{
	check(ActorNetInfo);

	for (const TWeakObjectPtr<UObject>& SubObjectPtr : SubObjectsToRemove)
	{
		bool bWasRemoved = HandleRemoveActiveSubObjectRef(ActorNetInfo, SubObjectPtr, ReferenceOwner);

		// If somehow the object wasn't in the active list, check if it was in the invalid list just to be sure.
		// It's possible for an object set to be deleted to have it's uobject ptr become innacessible before we replicate it's owner.
		// Calling this here would act as a safeguard to ensure its reference is removed even if the tear off/force delete command was never sent.
		if (!bWasRemoved)
		{
			HandleRemoveInvalidSubObjectRef(ActorNetInfo, SubObjectPtr, ReferenceOwner);
		}
	}
}

void FNetworkObjectList::RemoveSubObjectChannelReference(AActor* OwnerActor, const TWeakObjectPtr<UObject>& SubObjectPtr, UObject* ReferenceOwner)
{
	if (TSharedPtr<FNetworkObjectInfo>* InfoPtr = AllNetworkObjects.Find(OwnerActor))
	{
		check(InfoPtr->IsValid());
		HandleRemoveAnySubObjectChannelRef(InfoPtr->Get(), SubObjectPtr, ReferenceOwner);
	}
}

void FNetworkObjectList::HandleRemoveAnySubObjectChannelRef(FNetworkObjectInfo* ActorNetInfo, const TWeakObjectPtr<UObject>& SubObjectPtr, UObject* ReferenceOwner)
{
	check(ActorNetInfo);
	check(SubObjectPtr.IsExplicitlyNull() == false);

	// Look in the active set
	bool bWasRemoved = HandleRemoveActiveSubObjectRef(ActorNetInfo, SubObjectPtr, ReferenceOwner);

	// Look in the destroyed array
	if (!bWasRemoved)
	{
		bWasRemoved = HandleRemoveInvalidSubObjectRef(ActorNetInfo, SubObjectPtr, ReferenceOwner);
	}

	ensureMsgf(bWasRemoved, TEXT("HandleRemoveAnySubObjectChannelRef could not find any references for %s (0x%p) owned by %s"), *GetNameSafe(SubObjectPtr.GetEvenIfUnreachable()), SubObjectPtr.GetEvenIfUnreachable(), *GetNameSafe(ActorNetInfo->Actor));
}

bool FNetworkObjectList::HandleRemoveActiveSubObjectRef(FNetworkObjectInfo* ActorNetInfo, const TWeakObjectPtr<UObject>& SubObjectPtr, UObject* ReferenceOwner)
{
	check(ActorNetInfo);

	FSetElementId FoundId = ActorNetInfo->ActiveSubObjectChannelReferences.FindId(SubObjectPtr);
	if (FoundId.IsValidId())
	{
		FNetworkObjectInfo::FSubObjectChannelReference& ActiveReference = ActorNetInfo->ActiveSubObjectChannelReferences[FoundId];

		check(ActiveReference.SubObjectPtr.HasSameIndexAndSerialNumber(SubObjectPtr));

		ActiveReference.ChannelRefCount--;

		UE_LOG(LogNetSubObject, Verbose, TEXT("Removed ACTIVE ChannelRef (%d) for %s (0x%p) owned by %s for connection (0x%p) %s"), ActiveReference.ChannelRefCount, *GetNameSafe(SubObjectPtr.GetEvenIfUnreachable()), SubObjectPtr.GetEvenIfUnreachable(), *GetNameSafe(ActorNetInfo->Actor), ReferenceOwner, *GetNameSafe(ReferenceOwner));

#if DO_REPLICATED_OBJECT_CHANNELREF_CHECKS
		int32 Removed = ActiveReference.RegisteredOwners.RemoveSingleSwap(ReferenceOwner);
		ensureMsgf(Removed > 0, TEXT("Removed ACTIVE ref for Subobject %s (0x%p) owned by %s but it was never referenced by the connection (0x%p) %s"), *GetNameSafe(SubObjectPtr.GetEvenIfUnreachable()), SubObjectPtr.GetEvenIfUnreachable(), *GetNameSafe(ActorNetInfo->Actor), ReferenceOwner, *GetNameSafe(ReferenceOwner));
#endif

		if (ActiveReference.ChannelRefCount == 0)
		{
			ActorNetInfo->ActiveSubObjectChannelReferences.Remove(FoundId);
		}

		return true;
	}

	return false;
}

bool FNetworkObjectList::HandleRemoveInvalidSubObjectRef(FNetworkObjectInfo* ActorNetInfo, const TWeakObjectPtr<UObject>& SubObjectPtr, UObject* ReferenceOwner)
{
	check(ActorNetInfo);

	const int32 Index = ActorNetInfo->InvalidSubObjectChannelReferences.IndexOfByKey(SubObjectPtr);
	if (Index != INDEX_NONE)
	{
		FNetworkObjectInfo::FSubObjectChannelReference& InvalidReference = ActorNetInfo->InvalidSubObjectChannelReferences[Index];
		InvalidReference.ChannelRefCount--;

		UE_LOG(LogNetSubObject, Verbose, TEXT("Removed INVALID ChannelRef (%d) for %s (0x%p) owned by %s for connection (0x%p) %s"), InvalidReference.ChannelRefCount,*GetNameSafe(SubObjectPtr.GetEvenIfUnreachable()), SubObjectPtr.GetEvenIfUnreachable(), *GetNameSafe(ActorNetInfo->Actor), ReferenceOwner, *GetNameSafe(ReferenceOwner));

#if DO_REPLICATED_OBJECT_CHANNELREF_CHECKS
		int32 Removed = InvalidReference.RegisteredOwners.RemoveSingleSwap(ReferenceOwner);
		ensureMsgf(Removed > 0, TEXT("Removed INVALID ref for Subobject %s (0x%p) owned by %s but it was never referenced by the connection (0x%p) %s"), *GetNameSafe(SubObjectPtr.GetEvenIfUnreachable()), SubObjectPtr.GetEvenIfUnreachable(), *GetNameSafe(ActorNetInfo->Actor), ReferenceOwner, *GetNameSafe(ReferenceOwner));
#endif
		if (InvalidReference.ChannelRefCount == 0)
		{
			ActorNetInfo->InvalidSubObjectChannelReferences.RemoveAtSwap(Index);
		}

		return true;
	}

	return false;
}

#if DO_REPLICATED_OBJECT_CHANNELREF_CHECKS
void FNetworkObjectList::SwapMultipleReferencesForDormancy(AActor* OwnerActor, const TArrayView<TWeakObjectPtr<UObject>>& SubObjectsToSwap, UActorChannel* PreviousChannelRefOwner, UNetConnection* NewConnectionRefOwner)
{
	if (TSharedPtr<FNetworkObjectInfo>* InfoPtr = AllNetworkObjects.Find(OwnerActor))
	{
		check(InfoPtr->IsValid());

		FNetworkObjectInfo* ActorNetInfo = InfoPtr->Get();

		for (const TWeakObjectPtr<UObject>& SubObjectPtr : SubObjectsToSwap)
		{
			HandleSwapReferenceForDormancy(ActorNetInfo, SubObjectPtr, PreviousChannelRefOwner, NewConnectionRefOwner);
		}
	}
}

void FNetworkObjectList::SwapReferenceForDormancy(AActor* OwnerActor, UObject* ReplicatedSubObject, UNetConnection* PreviousConnectionRefOwner, UActorChannel* NewChannelRefOwner)
{
	if (TSharedPtr<FNetworkObjectInfo>* InfoPtr = AllNetworkObjects.Find(OwnerActor))
	{
		check(InfoPtr->IsValid());

		const TWeakObjectPtr<UObject> SubObjectPtr = ReplicatedSubObject;
		HandleSwapReferenceForDormancy(InfoPtr->Get(), SubObjectPtr, PreviousConnectionRefOwner, NewChannelRefOwner);
	}
}

void FNetworkObjectList::HandleSwapReferenceForDormancy(FNetworkObjectInfo* ActorNetInfo, const TWeakObjectPtr<UObject>& SubObjectPtr, UObject* PreviousRefOwner, UObject* NewRefOwner)
{
	check(ActorNetInfo);

	// First check the active list
	FNetworkObjectInfo::FSubObjectChannelReference* SubObjectChannelRef = ActorNetInfo->ActiveSubObjectChannelReferences.Find(SubObjectPtr);
	
	if (!SubObjectChannelRef)
	{
		// Then check the inactive list
		SubObjectChannelRef = ActorNetInfo->InvalidSubObjectChannelReferences.FindByKey(SubObjectPtr);
	}

	if (SubObjectChannelRef)
	{
		UE_LOG(LogNetSubObject, Verbose, TEXT("Swapped ChannelRef (%d) for SubObject %s (0x%p) owned by %s. From (0x%p) %s to (0x%p) %s"), SubObjectChannelRef->ChannelRefCount, *GetNameSafe(SubObjectPtr.GetEvenIfUnreachable()), SubObjectPtr.GetEvenIfUnreachable(), *GetNameSafe(ActorNetInfo->Actor), PreviousRefOwner, *GetNameSafe(PreviousRefOwner), NewRefOwner, *GetNameSafe(NewRefOwner));

		int32 Removed = SubObjectChannelRef->RegisteredOwners.RemoveSingleSwap(PreviousRefOwner);
		ensureMsgf(Removed > 0, TEXT("SwapReferencesForDormancy could not find reference to previous reference (0x%p) %s for subobject %s (0x%p) owned by %s. Swapping to (0x%p) %s"), PreviousRefOwner, *GetNameSafe(PreviousRefOwner), *GetNameSafe(SubObjectPtr.GetEvenIfUnreachable()), SubObjectPtr.GetEvenIfUnreachable(), *GetNameSafe(ActorNetInfo->Actor), NewRefOwner, *GetNameSafe(NewRefOwner));

		ensureMsgf(!SubObjectChannelRef->RegisteredOwners.Contains(NewRefOwner), TEXT("SwapReferencesForDormancy found new reference (0x%p) %s was already registered to %s (0x%p) owned by %s"), NewRefOwner, *GetNameSafe(NewRefOwner), *GetNameSafe(SubObjectPtr.GetEvenIfUnreachable()), SubObjectPtr.GetEvenIfUnreachable(), *GetNameSafe(ActorNetInfo->Actor));
		SubObjectChannelRef->RegisteredOwners.Add(NewRefOwner);
	}
	else
	{
		ensureMsgf(false, TEXT("SwapReferencesForDormancy could not find any references to %s (0x%p) owned by %s. Swapping from (0x%p) %s to (0x%p) %s"), *GetNameSafe(SubObjectPtr.GetEvenIfUnreachable()), SubObjectPtr.GetEvenIfUnreachable(), *GetNameSafe(ActorNetInfo->Actor), PreviousRefOwner, *GetNameSafe(PreviousRefOwner), NewRefOwner, *GetNameSafe(NewRefOwner));
	}
}
#endif //#if DO_REPLICATED_OBJECT_CHANNELREF_CHECKS

#endif //#if UE_REPLICATED_OBJECT_REFCOUNTING