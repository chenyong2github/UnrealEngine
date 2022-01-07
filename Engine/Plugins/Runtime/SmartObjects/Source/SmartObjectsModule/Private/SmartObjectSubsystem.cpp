// Copyright Epic Games, Inc. All Rights Reserved.

#include "SmartObjectSubsystem.h"
#include "SmartObjectDefinition.h"
#include "SmartObjectComponent.h"
#include "SmartObjectCollection.h"
#include "EngineUtils.h"
#include "VisualLogger/VisualLogger.h"

#if WITH_EDITOR
#include "Engine/LevelBounds.h"
#include "WorldPartition/WorldPartition.h"
#endif

namespace UE::SmartObject
{
	USmartObjectComponent* FindSmartObjectComponent(const AActor& SmartObjectActor)
	{
		return SmartObjectActor.FindComponentByClass<USmartObjectComponent>();
	}

	namespace Debug
	{
#if WITH_SMARTOBJECT_DEBUG
		static FAutoConsoleCommandWithWorld RegisterAllSmartObjectsCmd(
			TEXT("ai.debug.so.RegisterAllSmartObjects"),
			TEXT("Force register all objects registered in the subsystem to simulate & debug runtime flows (will ignore already registered components)."),
			FConsoleCommandWithWorldDelegate::CreateLambda([](const UWorld* InWorld)
			{
				if (USmartObjectSubsystem* Subsystem = USmartObjectSubsystem::GetCurrent(InWorld))
				{
					Subsystem->DebugRegisterAllSmartObjects();
				}
			})
		);

		static FAutoConsoleCommandWithWorld UnregisterAllSmartObjectsCmd(
			TEXT("ai.debug.so.UnregisterAllSmartObjects"),
			TEXT("Force unregister all objects registered in the subsystem to simulate & debug runtime flows (will ignore already unregistered components)."),
			FConsoleCommandWithWorldDelegate::CreateLambda([](const UWorld* InWorld)
			{
				if (USmartObjectSubsystem* Subsystem = USmartObjectSubsystem::GetCurrent(InWorld))
				{
					Subsystem->DebugUnregisterAllSmartObjects();
				}
			})
		);
#endif // WITH_SMARTOBJECT_DEBUG
	}
} // UE::SmartObject

//----------------------------------------------------------------------//
// USmartObjectSubsystem
//----------------------------------------------------------------------//

const FSmartObjectOctree& USmartObjectSubsystem::GetOctree() const
{
	return SmartObjectOctree;
}

USmartObjectSubsystem::USmartObjectSubsystem()
{
	using namespace UE::SmartObject;

	NextFreeUserID = InvalidID + 1;
}

void USmartObjectSubsystem::OnWorldComponentsUpdated(UWorld& World)
{
	// Register collections that were unable to register since they got loaded before the subsystem got created/initialized.
	RegisterCollectionInstances();

#if WITH_EDITOR
	SpawnMissingCollection();

	if (!World.IsGameWorld())
	{
		ComputeBounds(World, *MainCollection);
	}
#endif

	UE_CVLOG_UELOG(!IsValid(MainCollection), this, LogSmartObject, Error, TEXT("Collection is expected to be set once world components are updated."));
}

USmartObjectSubsystem* USmartObjectSubsystem::GetCurrent(const UWorld* World)
{
	return UWorld::GetSubsystem<USmartObjectSubsystem>(World);
}

void USmartObjectSubsystem::AddToSimulation(const FSmartObjectID ID, const USmartObjectDefinition& Definition, const FTransform& Transform, const FBox& Bounds)
{
	if (!ensureMsgf(ID.IsValid(), TEXT("SmartObject needs a valid ID to be added to the simulation")))
	{
		return;
	}

	if (!ensureMsgf(RuntimeSmartObjects.Find(ID) == nullptr, TEXT("ID '%s' already registered in runtime simulation"), *ID.Describe()))
	{
		return;
	}

	UE_VLOG_UELOG(this, LogSmartObject, Verbose, TEXT("Adding SmartObject '%s' to runtime simulation."), *ID.Describe());

	FSmartObjectRuntime& Runtime = RuntimeSmartObjects.Emplace(ID, FSmartObjectRuntime(Definition));
	Runtime.SetRegisteredID(ID);

	// Transfer spatial information to the runtime instance
	Runtime.SetTransform(Transform);

	// Insert instance in the octree
	const FSmartObjectOctreeIDSharedRef SharedOctreeID = MakeShareable(new FSmartObjectOctreeID());
	Runtime.SetOctreeID(SharedOctreeID);
	SmartObjectOctree.AddNode(Bounds, ID, SharedOctreeID);
}

void USmartObjectSubsystem::AddToSimulation(const FSmartObjectCollectionEntry& Entry, const USmartObjectDefinition& Definition)
{
	AddToSimulation(Entry.GetID(), Definition, Entry.GetTransform(), Entry.GetBounds());
}

void USmartObjectSubsystem::AddToSimulation(const USmartObjectComponent& Component)
{
	if (ensureMsgf(Component.GetDefinition() != nullptr, TEXT("Component must have a valid definition asset to register to the simulation")))
	{
		AddToSimulation(Component.GetRegisteredID(), *Component.GetDefinition(), Component.GetComponentTransform(), Component.GetSmartObjectBounds());
	}
}

void USmartObjectSubsystem::RemoveFromSimulation(const FSmartObjectID ID)
{
	UE_VLOG_UELOG(this, LogSmartObject, Verbose, TEXT("Removing SmartObject '%s' from runtime simulation."), *ID.Describe());

	FSmartObjectRuntime SmartObjectRuntime;
	if (RuntimeSmartObjects.RemoveAndCopyValue(ID, SmartObjectRuntime))
	{
		AbortAll(SmartObjectRuntime);
	}

	FSmartObjectOctreeID& SharedOctreeID = SmartObjectRuntime.GetSharedOctreeID().Get();
	if (SharedOctreeID.ID.IsValidId())
	{
		SmartObjectOctree.RemoveNode(SharedOctreeID.ID);
		SharedOctreeID.ID = {};
	}
}

void USmartObjectSubsystem::RemoveFromSimulation(const FSmartObjectCollectionEntry& Entry)
{
	RemoveFromSimulation(Entry.GetID());
}

void USmartObjectSubsystem::RemoveFromSimulation(const USmartObjectComponent& SmartObjectComponent)
{
	RemoveFromSimulation(SmartObjectComponent.GetRegisteredID());
}

bool USmartObjectSubsystem::RegisterSmartObject(USmartObjectComponent& SmartObjectComponent)
{
	// Main collection may not assigned until world components are updated (active level set and actors registered)
	// In this case objects will be part of the loaded collection or collection will be rebuilt from registered components
	if (IsValid(MainCollection))
	{
		const UWorld& World = GetWorldRef();
		bool bAddToCollection = true;

#if WITH_EDITOR
		if (!World.IsGameWorld())
		{
			// For "build on demand collections" we wait an explicit build request to clear and repopulate
			if (MainCollection->IsBuildOnDemand())
			{
				bAddToCollection = false;
				UE_VLOG_UELOG(this, LogSmartObject, VeryVerbose, TEXT("%s not added to collection that is built on demand only."), *GetNameSafe(SmartObjectComponent.GetOwner()));
			}
			// For partition world we don't alter the collection unless we are explicitly building the collection
			else if(World.IsPartitionedWorld() && !MainCollection->IsBuildingForWorldPartition())
			{
				bAddToCollection = false;
				UE_VLOG_UELOG(this, LogSmartObject, VeryVerbose, TEXT("%s not added to collection that is owned by partitioned world."), *GetNameSafe(SmartObjectComponent.GetOwner()));
			}
		}
#endif // WITH_EDITOR

		if (bAddToCollection)
		{
			const bool bIsNewEntry = MainCollection->AddSmartObject(SmartObjectComponent);

			// At runtime we only add new collection entries to the simulation. All existing entries were added on WorldBeginPlay
			if (World.IsGameWorld() && bInitialCollectionAddedToSimulation && bIsNewEntry)
			{
				RuntimeCreatedEntries.Add(SmartObjectComponent.GetRegisteredID());
				AddToSimulation(SmartObjectComponent);
			}
		}
	}

#if WITH_EDITOR
	if (RegisteredSOComponents.Find(&SmartObjectComponent) != INDEX_NONE)
	{
		UE_VLOG_UELOG(SmartObjectComponent.GetOwner(), LogSmartObject, Error, TEXT("Trying to register SmartObject %s more than once."), *GetNameSafe(SmartObjectComponent.GetOwner()));
		return false;
	}
	RegisteredSOComponents.Add(&SmartObjectComponent);
#endif // WITH_EDITOR

#if WITH_SMARTOBJECT_DEBUG
	DebugRegisteredComponents.Add(&SmartObjectComponent);
#endif

	return true;
}

bool USmartObjectSubsystem::UnregisterSmartObject(USmartObjectComponent& SmartObjectComponent)
{
	if (IsValid(MainCollection))
	{
		const UWorld& World = GetWorldRef();
		bool bRemoveFromCollection = true;

#if WITH_EDITOR
		if (!World.IsGameWorld())
		{
			// For "build on demand collections" we wait an explicit build request to clear and repopulate
			if (MainCollection->IsBuildOnDemand())
			{
				bRemoveFromCollection = false;
				UE_VLOG_UELOG(this, LogSmartObject, VeryVerbose, TEXT("%s not removed from collection that is built on demand only."), *GetNameSafe(SmartObjectComponent.GetOwner()));
			}
			// For partition world we never remove from the collection since it is built incrementally
			else if(World.IsPartitionedWorld())
			{
				bRemoveFromCollection = false;
				UE_VLOG_UELOG(this, LogSmartObject, VeryVerbose, TEXT("%s not removed from collection that is owned by partitioned world."), *GetNameSafe(SmartObjectComponent.GetOwner()));
			}
		}
#endif // WITH_EDITOR

		// At runtime, only entries created outside the initial collection are removed from simulation and collection
		if (World.IsGameWorld() && bInitialCollectionAddedToSimulation)
		{
			bRemoveFromCollection = RuntimeCreatedEntries.Find(SmartObjectComponent.GetRegisteredID()) != INDEX_NONE;
			if (bRemoveFromCollection)
			{
				RemoveFromSimulation(SmartObjectComponent);
			}
		}

		if (bRemoveFromCollection)
		{
			MainCollection->RemoveSmartObject(SmartObjectComponent);
		}
	}

#if WITH_EDITOR
	if (RegisteredSOComponents.Remove(&SmartObjectComponent) == 0)
	{
		UE_VLOG_UELOG(SmartObjectComponent.GetOwner(), LogSmartObject, Error, TEXT("Trying to unregister SmartObject %s but it wasn't registered."), *GetNameSafe(SmartObjectComponent.GetOwner()));
		return false;
	}
#endif // WITH_EDITOR

#if WITH_SMARTOBJECT_DEBUG
	DebugRegisteredComponents.Remove(&SmartObjectComponent);
#endif // WITH_SMARTOBJECT_DEBUG

	return true;
}

bool USmartObjectSubsystem::RegisterSmartObjectActor(const AActor& SmartObjectActor)
{
	USmartObjectComponent* SOComponent = UE::SmartObject::FindSmartObjectComponent(SmartObjectActor);
	if (SOComponent == nullptr)
	{
		UE_VLOG_UELOG(&SmartObjectActor, LogSmartObject, Error, TEXT("Failed to register SmartObject for %s. USmartObjectComponent is missing."), *SmartObjectActor.GetName());
		return false;
	}

	return RegisterSmartObject(*SOComponent);
}

bool USmartObjectSubsystem::UnregisterSmartObjectActor(const AActor& SmartObjectActor)
{
	USmartObjectComponent* SOComponent = UE::SmartObject::FindSmartObjectComponent(SmartObjectActor);
	if (SOComponent == nullptr)
	{
		UE_VLOG_UELOG(&SmartObjectActor, LogSmartObject, Error, TEXT("Failed to unregister SmartObject for %s. USmartObjectComponent is missing."), *SmartObjectActor.GetName());
		return false;
	}

	return UnregisterSmartObject(*SOComponent);
}

FSmartObjectClaimHandle USmartObjectSubsystem::Claim(const FSmartObjectID ID, const FSmartObjectRequestFilter& Filter)
{
	ensureMsgf(ID.IsValid(), TEXT("SmartObject ID should be valid: %s"), *(ID.Describe()));

	FSmartObjectRuntime* SORuntime = RuntimeSmartObjects.Find(ID);
	if (!ensureMsgf(SORuntime != nullptr, TEXT("A SmartObjectRuntime must be created for ID %s"), *ID.Describe()))
	{
		return FSmartObjectClaimHandle::InvalidHandle;
	}

	const FSmartObjectSlotIndex SlotIndex = FindSlot(*SORuntime, Filter);
	if (!SlotIndex.IsValid())
	{
		return FSmartObjectClaimHandle::InvalidHandle;
	}

	const FSmartObjectUserID User(NextFreeUserID++);
	const FSmartObjectClaimHandle ClaimHandle(ID, SlotIndex, User);

	const bool bClaimed = SORuntime->ClaimSlot(ClaimHandle);
	UE_VLOG_UELOG(this, LogSmartObject, Verbose, TEXT("Claim %s for handle %s"), bClaimed ? TEXT("SUCCEEDED") : TEXT("FAILED"), *ClaimHandle.Describe());

	return (bClaimed) ? ClaimHandle : FSmartObjectClaimHandle::InvalidHandle;
}

FSmartObjectClaimHandle USmartObjectSubsystem::Claim(const FSmartObjectRequestResult& RequestResult)
{
	if (!ensureMsgf(RequestResult.IsValid(), TEXT("Must claim with a valid result: %s"), *RequestResult.Describe()))
	{
		return FSmartObjectClaimHandle::InvalidHandle;
	}

	FSmartObjectRuntime* SORuntime = RuntimeSmartObjects.Find(RequestResult.SmartObjectID);
	if (!ensureMsgf(SORuntime != nullptr, TEXT("A SmartObjectRuntime must be created for ID %s"), *RequestResult.SmartObjectID.Describe()))
	{
		return FSmartObjectClaimHandle::InvalidHandle;
	}

	const FSmartObjectUserID User(NextFreeUserID++);
	const FSmartObjectClaimHandle ClaimHandle(RequestResult.SmartObjectID, RequestResult.SlotIndex, User);
	return (SORuntime->ClaimSlot(ClaimHandle)) ? ClaimHandle : FSmartObjectClaimHandle::InvalidHandle;
}

const USmartObjectBehaviorDefinition* USmartObjectSubsystem::Use(const FSmartObjectClaimHandle& ClaimHandle, const TSubclassOf<USmartObjectBehaviorDefinition>& DefinitionClass)
{
	if (!ClaimHandle.IsValid())
	{
		UE_VLOG_UELOG(this, LogSmartObject, Error, TEXT("Must use with a valid claim handle"));
		return nullptr;
	}

	FSmartObjectRuntime* SmartObjectRuntime = RuntimeSmartObjects.Find(ClaimHandle.SmartObjectID);
	if (!ensureMsgf(SmartObjectRuntime != nullptr, TEXT("A SmartObjectRuntime must be created for ID %s"), *ClaimHandle.SmartObjectID.Describe()))
	{
		return nullptr;
	}

	return Use(*SmartObjectRuntime, ClaimHandle, DefinitionClass);
}

const USmartObjectBehaviorDefinition* USmartObjectSubsystem::Use(FSmartObjectRuntime& SmartObjectRuntime, const FSmartObjectClaimHandle& ClaimHandle, const TSubclassOf<USmartObjectBehaviorDefinition>& DefinitionClass)
{
	const USmartObjectDefinition& Definition = SmartObjectRuntime.GetDefinition();

	const USmartObjectBehaviorDefinition* BehaviorDefinition = Definition.GetBehaviorDefinition(ClaimHandle.SlotIndex, DefinitionClass);
	if (BehaviorDefinition == nullptr)
	{
		const UClass* ClassPtr = DefinitionClass.Get();
		UE_VLOG_UELOG(this, LogSmartObject, Warning, TEXT("Unable to find a behavior definition of type %s in %s"), ClassPtr != nullptr ? *ClassPtr->GetName(): TEXT("Null"), *Definition.Describe());
		return nullptr;
	}

	UE_VLOG_UELOG(this, LogSmartObject, Verbose, TEXT("Start using handle %s"), *ClaimHandle.Describe());
	UE_VLOG_LOCATION(this, LogSmartObject, Display, SmartObjectRuntime.GetTransform().GetLocation(), 50.f, FColor::Green, TEXT("Use"));
	SmartObjectRuntime.UseSlot(ClaimHandle);
	return BehaviorDefinition;
}

bool USmartObjectSubsystem::Release(const FSmartObjectClaimHandle& ClaimHandle)
{
	if (!ClaimHandle.IsValid())
	{
		UE_VLOG_UELOG(this, LogSmartObject, Error, TEXT("Must release slot using a valid handle"));
		return false;
	}

	FSmartObjectRuntime* SmartObjectRuntime = RuntimeSmartObjects.Find(ClaimHandle.SmartObjectID);
	if (SmartObjectRuntime == nullptr)
	{
		UE_VLOG_UELOG(this, LogSmartObject, Error, TEXT("Unable to find runtime data for handle: %s"), *ClaimHandle.Describe());
		return false;
	}

	const bool bSuccess = SmartObjectRuntime->ReleaseSlot(ClaimHandle, /*bAborted*/ false);
	UE_CVLOG_UELOG(bSuccess, this, LogSmartObject, Verbose, TEXT("Released using handle %s"), *ClaimHandle.Describe());
	UE_CVLOG_LOCATION(bSuccess, this, LogSmartObject, Display, SmartObjectRuntime->GetTransform().GetLocation(), 50.f, FColor::Red, TEXT("Release"));
	return bSuccess;
}

TOptional<FVector> USmartObjectSubsystem::GetSlotLocation(const FSmartObjectID SmartObjectID, const FSmartObjectSlotIndex SlotIndex) const
{
	TOptional<FTransform> Transform = GetSlotTransform(SmartObjectID, SlotIndex);
	return (Transform.IsSet() ? Transform.GetValue().GetLocation() : TOptional<FVector>());
}

TOptional<FVector> USmartObjectSubsystem::GetSlotLocation(const FSmartObjectClaimHandle& ClaimHandle) const
{
	if (ensureMsgf(ClaimHandle.IsValid(), TEXT("Requesting slot location from an invalid claim handle.")))
	{
		return GetSlotLocation(ClaimHandle.SmartObjectID, ClaimHandle.SlotIndex);
	}
	return TOptional<FVector>();
}

bool USmartObjectSubsystem::GetSlotLocation(const FSmartObjectClaimHandle& ClaimHandle, FVector& OutSlotLocation) const
{
	const TOptional<FVector> OptionalLocation = GetSlotLocation(ClaimHandle);
	OutSlotLocation = OptionalLocation.Get(FVector::ZeroVector);
	return OptionalLocation.IsSet();
}

TOptional<FVector> USmartObjectSubsystem::GetSlotLocation(const FSmartObjectRequestResult& Result) const
{
	if (ensureMsgf(Result.IsValid(), TEXT("Requesting slot location from an invalid request result.")))
	{
		return GetSlotLocation(Result.SmartObjectID, Result.SlotIndex);
	}
	return TOptional<FVector>();
}

TOptional<FTransform> USmartObjectSubsystem::GetSlotTransform(const FSmartObjectID SmartObjectID, const FSmartObjectSlotIndex SlotIndex) const
{
	TOptional<FTransform> Transform;

	if (!ensureMsgf(SmartObjectID.IsValid(), TEXT("Requesting slot transform for an invalid Id")))
	{
		return Transform;
	}

	const FSmartObjectRuntime* SmartObjectRuntime = RuntimeSmartObjects.Find(SmartObjectID);
	if (SmartObjectRuntime == nullptr)
	{
		return Transform;
	}

	Transform = SmartObjectRuntime->GetDefinition().GetSlotTransform(SmartObjectRuntime->GetTransform(), SlotIndex);

	return Transform;
}

TOptional<FTransform> USmartObjectSubsystem::GetSlotTransform(const FSmartObjectClaimHandle& ClaimHandle) const
{
	if (ensureMsgf(ClaimHandle.IsValid(), TEXT("Requesting slot transform from an invalid claim handle.")))
	{
		return GetSlotTransform(ClaimHandle.SmartObjectID, ClaimHandle.SlotIndex);
	}
	return TOptional<FTransform>();
}

bool USmartObjectSubsystem::GetSlotTransform(const FSmartObjectClaimHandle& ClaimHandle, FTransform& OutSlotTransform) const
{
	const TOptional<FTransform> OptionalLocation = GetSlotTransform(ClaimHandle);
	OutSlotTransform = OptionalLocation.Get(FTransform::Identity);
	return OptionalLocation.IsSet();
}

TOptional<FTransform> USmartObjectSubsystem::GetSlotTransform(const FSmartObjectRequestResult& Result) const
{
	if (ensureMsgf(Result.IsValid(), TEXT("Requesting slot transform from an invalid request result.")))
	{
		return GetSlotTransform(Result.SmartObjectID, Result.SlotIndex);
	}
	return TOptional<FTransform>();
}

FSmartObjectSlotRuntimeData* USmartObjectSubsystem::GetMutableRuntimeSlot(const FSmartObjectClaimHandle& ClaimHandle)
{
	FSmartObjectRuntime* SmartObjectRuntime = RuntimeSmartObjects.Find(ClaimHandle.SmartObjectID);
	if (SmartObjectRuntime == nullptr)
	{
		return nullptr;
	}

	FSmartObjectSlotRuntimeData* SlotRuntimeData = SmartObjectRuntime->SlotsRuntimeData.FindByPredicate([&ClaimHandle](const FSmartObjectSlotRuntimeData& SlotRuntimeData)
		{
			return SlotRuntimeData.SlotIndex == ClaimHandle.SlotIndex;
		});

	return SlotRuntimeData;
}

void USmartObjectSubsystem::RegisterSlotInvalidationCallback(const FSmartObjectClaimHandle& ClaimHandle, const FOnSlotInvalidated& Callback)
{
	FSmartObjectSlotRuntimeData* Slot = GetMutableRuntimeSlot(ClaimHandle);
	if (Slot != nullptr)
	{
		Slot->OnSlotInvalidatedDelegate = Callback;
	}
}

void USmartObjectSubsystem::UnregisterSlotInvalidationCallback(const FSmartObjectClaimHandle& ClaimHandle)
{
	FSmartObjectSlotRuntimeData* Slot = GetMutableRuntimeSlot(ClaimHandle);
	if (Slot != nullptr)
	{
		Slot->OnSlotInvalidatedDelegate.Unbind();
	}
}

FSmartObjectSlotIndex USmartObjectSubsystem::FindSlot(const FSmartObjectRuntime& SmartObjectRuntime, const FSmartObjectRequestFilter& Filter) const
{
	const FSmartObjectSlotIndex InvalidIndex;

	const USmartObjectDefinition& Definition = SmartObjectRuntime.GetDefinition();
	const int32 NumSlotDefinitions = Definition.GetSlots().Num();
	if (!ensureMsgf(NumSlotDefinitions > 0, TEXT("Definition should contain slot definitions at this point")))
	{
		return InvalidIndex;
	}

	const UClass* RequiredDefinitionClass = *Filter.BehaviorDefinitionClass;
	if (!ensureMsgf(RequiredDefinitionClass != nullptr, TEXT("Filter needs to provide required behavior definition type")))
	{
		return InvalidIndex;
	}

	// Validate if any available slots
	if (SmartObjectRuntime.SlotsRuntimeData.Num() == NumSlotDefinitions)
	{
		bool bAnyFreeSlot = false;
		for (const FSmartObjectSlotRuntimeData& SlotRuntimeData : SmartObjectRuntime.SlotsRuntimeData)
		{
			if (SlotRuntimeData.State == ESmartObjectSlotState::Free)
			{
				bAnyFreeSlot = true;
				break;
			}
		}

		if (bAnyFreeSlot == false)
		{
			return InvalidIndex;
		}
	}

	const FGameplayTagContainer& ObjectTags = SmartObjectRuntime.GetTags();

	auto MatchesTagQueryFunc = [](const FGameplayTagQuery& Requirements, const FGameplayTagContainer& Tags)-> bool
	{
		return Requirements.IsEmpty() || Requirements.Matches(Tags);
	};

	if (MatchesTagQueryFunc(Filter.ActivityRequirements, Definition.GetActivityTags())
		&& MatchesTagQueryFunc(Definition.GetObjectTagFilter(), ObjectTags)
		&& MatchesTagQueryFunc(Definition.GetUserTagFilter(), Filter.UserTags))
	{
		TBitArray<> FreeSlots;
		SmartObjectRuntime.FindFreeSlots(FreeSlots);

		const TConstArrayView<FSmartObjectSlot> Slots = Definition.GetSlots();
		for (int i = 0; i < Slots.Num(); ++i)
		{
			const FSmartObjectSlot& Slot = Slots[i];
			if (FreeSlots[i] == false)
			{
				continue;
			}

			if (Definition.GetBehaviorDefinition(FSmartObjectSlotIndex(i), Filter.BehaviorDefinitionClass) == nullptr)
			{
				continue;
			}

			if (MatchesTagQueryFunc(Slot.UserTagFilter, Filter.UserTags) == false)
			{
				continue;
			}

			return FSmartObjectSlotIndex(i);
		}
	}

	return InvalidIndex;
}

void USmartObjectSubsystem::AbortAll(FSmartObjectRuntime& SmartObjectRuntime)
{
	for (FSmartObjectSlotRuntimeData& SlotRuntimeData : SmartObjectRuntime.SlotsRuntimeData)
	{
		switch (SlotRuntimeData.State)
		{
		case ESmartObjectSlotState::Claimed:
		case ESmartObjectSlotState::Occupied:
			{
				FSmartObjectClaimHandle ClaimHandle(SmartObjectRuntime.GetRegisteredID(), SlotRuntimeData.SlotIndex, SlotRuntimeData.User);
				const bool bFunctionWasExecuted = SlotRuntimeData.OnSlotInvalidatedDelegate.ExecuteIfBound(ClaimHandle, SlotRuntimeData.State);
				UE_VLOG_UELOG(this, LogSmartObject, Verbose, TEXT("Slot invalidated callback was%scalled for slot %s"), bFunctionWasExecuted ? TEXT(" ") : TEXT(" not "), *SlotRuntimeData.Describe());
				break;
			}
		case ESmartObjectSlotState::Free: // falling through on purpose
		default:
			UE_VLOG_UELOG(this, LogSmartObject, Warning, TEXT("Smart object %s used by %s while the slot it's assigned to is not marked Claimed nor Occupied"), *SmartObjectRuntime.GetDefinition().Describe(), *SlotRuntimeData.User.Describe());
			break;
		}
	}

	SmartObjectRuntime.SlotsRuntimeData.Reset();
}

FSmartObjectRequestResult USmartObjectSubsystem::FindSmartObject(const FSmartObjectRequest& Request)
{
	// find X instances, ignore distance as long as in range, accept first available
	FSmartObjectRequestResult Result;
	const FSmartObjectRequestFilter& Filter = Request.Filter;

	SmartObjectOctree.FindFirstElementWithBoundsTest(Request.QueryBox,
		[&Result, &Filter, this](const FSmartObjectOctreeElement& Element)
		{
			Result = FindSlot(Element.SmartObjectID, Filter);

			const bool bContinueTraversal = !Result.IsValid();
			return bContinueTraversal;
		});

	return Result;
}

bool USmartObjectSubsystem::FindSmartObjects(const FSmartObjectRequest& Request, TArray<FSmartObjectRequestResult>& OutResults)
{
	FSmartObjectRequestResult Result;
	const FSmartObjectRequestFilter& Filter = Request.Filter;
	SmartObjectOctree.FindFirstElementWithBoundsTest(Request.QueryBox,
		[&Result, &Filter, &OutResults, this](const FSmartObjectOctreeElement& Element)
		{
			Result = FindSlot(Element.SmartObjectID, Filter);
			if (Result.IsValid())
			{
				OutResults.Add(Result);
			}
			return true;
		});

	return (OutResults.Num() > 0);
}

FSmartObjectRequestResult USmartObjectSubsystem::FindSlot(const FSmartObjectID ID, const FSmartObjectRequestFilter& Filter) const
{
	const FSmartObjectRequestResult InvalidResult;
	if (!ID.IsValid())
	{
		UE_VLOG_UELOG(this, LogSmartObject, Error, TEXT("Requesting a valid use for an invalid smart object."));
		return InvalidResult;
	}

	if (Filter.Predicate && !Filter.Predicate(ID))
	{
		return InvalidResult;
	}

	const FSmartObjectRuntime* SmartObjectRuntime = RuntimeSmartObjects.Find(ID);
	if (!ensureMsgf(SmartObjectRuntime != nullptr, TEXT("RuntimeData should exist at this point")))
	{
		return InvalidResult;
	}

	const FSmartObjectSlotIndex FoundIndex = FindSlot(*SmartObjectRuntime, Filter);
	if (!FoundIndex.IsValid())
	{
		return InvalidResult;
	}

	return FSmartObjectRequestResult(ID, FoundIndex);
}

void USmartObjectSubsystem::RegisterCollectionInstances()
{
	for (TActorIterator<ASmartObjectCollection> It(GetWorld()); It; ++It)
	{
		ASmartObjectCollection* Collection = (*It);
		if (IsValid(Collection) && Collection->IsRegistered() == false)
		{
			UE_VLOG_UELOG(Collection, LogSmartObject, Log, TEXT("Collection '%s' registers from USmartObjectSubsystem initialization - Succeeded"), *Collection->GetName());
			RegisterCollection(*Collection);
		}
	}
}

void USmartObjectSubsystem::RegisterCollection(ASmartObjectCollection& InCollection)
{
	if (!IsValid(&InCollection))
	{
		return;
	}

	if (InCollection.IsRegistered())
	{
		UE_VLOG_UELOG(&InCollection, LogSmartObject, Error, TEXT("Trying to register collection '%s' more than once"), *InCollection.GetName());
		return;
	}

	if (InCollection.GetLevel()->IsPersistentLevel())
	{
		ensureMsgf(!IsValid(MainCollection), TEXT("Not expecting to set the main collection more than once"));
		MainCollection = &InCollection;

#if WITH_EDITOR
		OnMainCollectionChanged.Broadcast();

		// For a collection that is automatically updated, it gets rebuilt on registration in the Edition world.
		const UWorld& World = GetWorldRef();
		if (!World.IsGameWorld() &&
			!World.IsPartitionedWorld() &&
			!MainCollection->IsBuildOnDemand())
		{
			RebuildCollection(InCollection);
		}
#endif // WITH_EDITOR

		InCollection.OnRegistered();
	}
	else
	{
		InCollection.MarkAsGarbage();
	}
}

void USmartObjectSubsystem::UnregisterCollection(ASmartObjectCollection& InCollection)
{
	if (MainCollection != &InCollection)
	{
		UE_VLOG_UELOG(&InCollection, LogSmartObject, Verbose, TEXT("Ignoring unregistration of collection '%s' since this is not the main collection."), *InCollection.GetName());
		return;
	}

	InCollection.OnUnregistered();
}

USmartObjectComponent* USmartObjectSubsystem::GetSmartObjectComponent(const FSmartObjectClaimHandle& ClaimHandle) const
{
	return (IsValid(MainCollection) ? MainCollection->GetSmartObjectComponent(ClaimHandle.SmartObjectID) : nullptr);
}

void USmartObjectSubsystem::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World);

	if (!IsValid(MainCollection))
	{
		if (MainCollection != nullptr && !MainCollection->bNetLoadOnClient && World.IsNetMode(NM_Client))
		{
			UE_VLOG_UELOG(this, LogSmartObject, Verbose, TEXT("Collection not loaded on client. Initialization skipped in %s."), ANSI_TO_TCHAR(__FUNCTION__));
		}
		else
		{
			UE_VLOG_UELOG(this, LogSmartObject, Warning, TEXT("Missing collection during %s."), ANSI_TO_TCHAR(__FUNCTION__));
		}

		return;
	}

	// Initialize octree using collection bounds
	FVector Center, Extents;
	MainCollection->GetBounds().GetCenterAndExtents(Center, Extents);
	new(&SmartObjectOctree) FSmartObjectOctree(Center, Extents.Size2D());

	// Perform all validations at once since multiple entries can share the same definition
	MainCollection->ValidateDefinitions();

	// Build all runtime from collection
	for (const FSmartObjectCollectionEntry& Entry : MainCollection->GetEntries())
	{
		const USmartObjectDefinition* Definition = MainCollection->GetDefinitionForEntry(Entry);
		USmartObjectComponent* Component = Entry.GetComponent();

		if (Definition == nullptr || Definition->IsValid() == false)
		{
			UE_CVLOG_UELOG(Component != nullptr, Component->GetOwner(), LogSmartObject, Error,
				TEXT("Skipped runtime data creation for SmartObject %s: Invalid definition"), *GetNameSafe(Component->GetOwner()));
			continue;
		}

		// Create a runtime instance of that definition using that ID
		if (Component != nullptr)
		{
			Component->SetRegisteredID(Entry.GetID());
		}

		AddToSimulation(Entry, *Definition);
	}

	// Until this point all runtime entries were created from the collection, start tracking newly created
	RuntimeCreatedEntries.Reset();

	// Note that we use our own flag instead of relying on World.HasBegunPlay() since world might not be marked
	// as BegunPlay immediately after subsystem OnWorldBeingPlay gets called (e.g. waiting game mode to be ready on clients)
	bInitialCollectionAddedToSimulation = true;
}

#if WITH_EDITOR
void USmartObjectSubsystem::ComputeBounds(const UWorld& World, ASmartObjectCollection& Collection) const
{
	FBox Bounds(ForceInitToZero);

	if (const UWorldPartition* WorldPartition = World.GetWorldPartition())
	{
		Bounds = WorldPartition->GetWorldBounds();
	}
	else if (const ULevel* PersistentLevel = World.PersistentLevel.Get())
	{
		if (PersistentLevel->LevelBoundsActor.IsValid())
		{
			Bounds = PersistentLevel->LevelBoundsActor.Get()->GetComponentsBoundingBox();
		}
		else
		{
			Bounds = ALevelBounds::CalculateLevelBounds(PersistentLevel);
		}
	}
	else
	{
		UE_VLOG_UELOG(this, LogSmartObject, Error, TEXT("Unable to determine world bounds: no world partition or persistent level."));
	}

	Collection.SetBounds(Bounds);
}

void USmartObjectSubsystem::RebuildCollection(ASmartObjectCollection& InCollection)
{
	InCollection.RebuildCollection(RegisteredSOComponents);
}

void USmartObjectSubsystem::SpawnMissingCollection()
{
	if (IsValid(MainCollection))
	{
		return;
	}

	UWorld& World = GetWorldRef();

	FActorSpawnParameters SpawnInfo;
	SpawnInfo.OverrideLevel = World.PersistentLevel;
	SpawnInfo.bAllowDuringConstructionScript = true;

	UE_VLOG_UELOG(this, LogSmartObject, Log, TEXT("Spawning missing collection for world '%s'."), *World.GetName());
	World.SpawnActor<ASmartObjectCollection>(ASmartObjectCollection::StaticClass(), SpawnInfo);

	checkf(IsValid(MainCollection), TEXT("MainCollection must be assigned after spawning"));
}
#endif // WITH_EDITOR

#if WITH_SMARTOBJECT_DEBUG
void USmartObjectSubsystem::DebugUnregisterAllSmartObjects()
{
	for (TWeakObjectPtr<USmartObjectComponent>& WeakComponent : DebugRegisteredComponents)
	{
		if (const USmartObjectComponent* Cmp = WeakComponent.Get())
		{
			RemoveFromSimulation(*Cmp);
		}
	}
}

void USmartObjectSubsystem::DebugRegisterAllSmartObjects()
{
	for (TWeakObjectPtr<USmartObjectComponent>& WeakComponent : DebugRegisteredComponents)
	{
		if (const USmartObjectComponent* Cmp = WeakComponent.Get())
		{
			AddToSimulation(*Cmp);
		}
	}
}
#endif // WITH_SMARTOBJECT_DEBUG
