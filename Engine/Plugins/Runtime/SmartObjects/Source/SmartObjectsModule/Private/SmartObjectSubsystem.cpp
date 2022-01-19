// Copyright Epic Games, Inc. All Rights Reserved.

#include "SmartObjectSubsystem.h"
#include "SmartObjectDefinition.h"
#include "SmartObjectComponent.h"
#include "SmartObjectCollection.h"
#include "MassEntitySubsystem.h"
#include "EngineUtils.h"
#include "MassCommandBuffer.h"
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

void USmartObjectSubsystem::AddToSimulation(const FSmartObjectHandle ID, const USmartObjectDefinition& Definition, const FTransform& Transform, const FBox& Bounds)
{
	if (!ensureMsgf(ID.IsValid(), TEXT("SmartObject needs a valid ID to be added to the simulation")))
	{
		return;
	}

	if (!ensureMsgf(RuntimeSmartObjects.Find(ID) == nullptr, TEXT("ID '%s' already registered in runtime simulation"), *LexToString(ID)))
	{
		return;
	}

	UE_VLOG_UELOG(this, LogSmartObject, Verbose, TEXT("Adding SmartObject '%s' to runtime simulation."), *LexToString(ID));

	FSmartObjectRuntime& Runtime = RuntimeSmartObjects.Emplace(ID, FSmartObjectRuntime(Definition));
	Runtime.SetRegisteredID(ID);

	// Create runtime data and entity for each slot
	if (UMassEntitySubsystem* EntitySubsystem = GetWorldRef().GetSubsystem<UMassEntitySubsystem>())
	{
		int32 SlotIndex = 0;
		for (const FSmartObjectSlotDefinition& SlotDefinition : Definition.GetSlots())
		{
			// Build our shared fragment
			FMassArchetypeSharedFragmentValues SharedFragmentValues;
			const uint32 DefinitionHash = UE::StructUtils::GetStructCrc32(FConstStructView::Make(SlotDefinition));
			FConstSharedStruct& SharedFragment = EntitySubsystem->GetOrCreateConstSharedFragment(DefinitionHash, FSmartObjectSlotDefinitionFragment(Definition, SlotDefinition));
			SharedFragmentValues.AddConstSharedFragment(SharedFragment);
			
			FSmartObjectSlotTransform TransformFragment;
			TOptional<FTransform> OptionalTransform = Definition.GetSlotTransform(Transform, FSmartObjectSlotIndex(SlotIndex));
			TransformFragment.SetTransform(OptionalTransform.Get(Transform));

			const FMassEntityHandle EntityHandle = EntitySubsystem->ReserveEntity();
			EntitySubsystem->Defer().PushCommand(
				FBuildEntityFromFragmentInstances(EntityHandle,
					{
						FStructView::Make(TransformFragment)
					},
					SharedFragmentValues));

			FSmartObjectSlotHandle SlotHandle(EntityHandle);
			RuntimeSlotStates.Add(SlotHandle, FSmartObjectSlotClaimState());
			Runtime.SlotHandles[SlotIndex] = SlotHandle;
			SlotIndex++;
		}
	}

	// Transfer spatial information to the runtime instance
	Runtime.SetTransform(Transform);

	// Insert instance in the octree
	const FSmartObjectOctreeIDSharedRef SharedOctreeID = MakeShareable(new FSmartObjectOctreeID());
	Runtime.SetOctreeID(SharedOctreeID);
	SmartObjectOctree.AddNode(Bounds, ID, SharedOctreeID);
}

void USmartObjectSubsystem::AddToSimulation(const FSmartObjectCollectionEntry& Entry, const USmartObjectDefinition& Definition)
{
	AddToSimulation(Entry.GetHandle(), Definition, Entry.GetTransform(), Entry.GetBounds());
}

void USmartObjectSubsystem::AddToSimulation(const USmartObjectComponent& Component)
{
	if (ensureMsgf(Component.GetDefinition() != nullptr, TEXT("Component must have a valid definition asset to register to the simulation")))
	{
		AddToSimulation(Component.GetRegisteredHandle(), *Component.GetDefinition(), Component.GetComponentTransform(), Component.GetSmartObjectBounds());
	}
}

void USmartObjectSubsystem::RemoveFromSimulation(const FSmartObjectHandle ID)
{
	UE_VLOG_UELOG(this, LogSmartObject, Verbose, TEXT("Removing SmartObject '%s' from runtime simulation."), *LexToString(ID));

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

	TArray<FMassEntityHandle> EntitiesToDestroy;
	for (const FSmartObjectSlotHandle& SlotHandle : SmartObjectRuntime.SlotHandles)
	{
		RuntimeSlotStates.Remove(SlotHandle);
		EntitiesToDestroy.Add(SlotHandle);
	}

	// Destroy entities associated to slots
	if (const UMassEntitySubsystem* EntitySubsystem = GetWorldRef().GetSubsystem<UMassEntitySubsystem>())
	{
		EntitySubsystem->Defer().BatchDestroyEntities(EntitiesToDestroy);
	}
}

void USmartObjectSubsystem::RemoveFromSimulation(const FSmartObjectCollectionEntry& Entry)
{
	RemoveFromSimulation(Entry.GetHandle());
}

void USmartObjectSubsystem::RemoveFromSimulation(const USmartObjectComponent& SmartObjectComponent)
{
	RemoveFromSimulation(SmartObjectComponent.GetRegisteredHandle());
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
				RuntimeCreatedEntries.Add(SmartObjectComponent.GetRegisteredHandle());
				AddToSimulation(SmartObjectComponent);
			}
		}
	}
	else
	{
		UE_VLOG_UELOG(this, LogSmartObject, VeryVerbose, TEXT("%s not added to collection since Main Collection is not set."), *GetNameSafe(SmartObjectComponent.GetOwner()));
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
			bRemoveFromCollection = RuntimeCreatedEntries.Find(SmartObjectComponent.GetRegisteredHandle()) != INDEX_NONE;
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

FSmartObjectClaimHandle USmartObjectSubsystem::Claim(const FSmartObjectHandle ID, const FSmartObjectRequestFilter& Filter)
{
	if (!ensureMsgf(ID.IsValid(), TEXT("SmartObject ID should be valid: %s"), *LexToString(ID)))
	{
		return FSmartObjectClaimHandle::InvalidHandle;
	}

	FSmartObjectRuntime* SORuntime = RuntimeSmartObjects.Find(ID);
	if (!ensureMsgf(SORuntime != nullptr, TEXT("A SmartObjectRuntime must be created for ID %s"), *LexToString(ID)))
	{
		return FSmartObjectClaimHandle::InvalidHandle;
	}

	const FSmartObjectSlotHandle FoundHandle = FindSlot(*SORuntime, Filter);
	if (!FoundHandle.IsValid())
	{
		return FSmartObjectClaimHandle::InvalidHandle;
	}

	return Claim(ID, FoundHandle);
}

FSmartObjectClaimHandle USmartObjectSubsystem::Claim(const FSmartObjectRequestResult& RequestResult)
{
	if (!ensureMsgf(RequestResult.IsValid(), TEXT("Must claim with a valid result: %s"), *LexToString(RequestResult)))
	{
		return FSmartObjectClaimHandle::InvalidHandle;
	}

	return Claim(RequestResult.SmartObjectHandle, RequestResult.SlotHandle);
}

FSmartObjectClaimHandle USmartObjectSubsystem::Claim(const FSmartObjectHandle ID, const FSmartObjectSlotHandle SlotHandle)
{
	// Slot might be unregistered by the time a result is used so it is possible that it can no longer be found
	if (FSmartObjectSlotClaimState* SlotState = RuntimeSlotStates.Find(SlotHandle))
	{
		const FSmartObjectUserHandle User(NextFreeUserID++);
		const bool bClaimed = SlotState->Claim(User);

		const FSmartObjectClaimHandle ClaimHandle(ID, SlotHandle, User);
		UE_VLOG_UELOG(this, LogSmartObject, Verbose, TEXT("Claim %s for handle %s"), bClaimed ? TEXT("SUCCEEDED") : TEXT("FAILED"), *LexToString(ClaimHandle));
		UE_CVLOG_LOCATION(bClaimed, this, LogSmartObject, Display, GetSlotLocation(ClaimHandle).GetValue(), 50.f, FColor::Yellow, TEXT("Claim"));

		if (bClaimed)
		{
			return ClaimHandle;
		}
	}

	return FSmartObjectClaimHandle::InvalidHandle;
}

const USmartObjectBehaviorDefinition* USmartObjectSubsystem::Use(const FSmartObjectClaimHandle& ClaimHandle, const TSubclassOf<USmartObjectBehaviorDefinition>& DefinitionClass)
{
	if (!ClaimHandle.IsValid())
	{
		UE_VLOG_UELOG(this, LogSmartObject, Error, TEXT("Must use with a valid claim handle"));
		return nullptr;
	}

	FSmartObjectRuntime* SmartObjectRuntime = RuntimeSmartObjects.Find(ClaimHandle.SmartObjectHandle);
	if (!ensureMsgf(SmartObjectRuntime != nullptr, TEXT("A SmartObjectRuntime must be created for %s"), *LexToString(ClaimHandle.SmartObjectHandle)))
	{
		return nullptr;
	}

	return Use(*SmartObjectRuntime, ClaimHandle, DefinitionClass);
}

const USmartObjectBehaviorDefinition* USmartObjectSubsystem::Use(const FSmartObjectRuntime& SmartObjectRuntime, const FSmartObjectClaimHandle& ClaimHandle, const TSubclassOf<USmartObjectBehaviorDefinition>& DefinitionClass)
{
	if (!ensureMsgf(ClaimHandle.IsValid(), TEXT("A valid claim handle is required to use a slot: %s"), *LexToString(ClaimHandle)))
	{
		return nullptr;
	}

	const USmartObjectDefinition& Definition = SmartObjectRuntime.GetDefinition();

	const FSmartObjectSlotIndex SlotIndex(SmartObjectRuntime.SlotHandles.IndexOfByKey(ClaimHandle.SlotHandle));
	const USmartObjectBehaviorDefinition* BehaviorDefinition = Definition.GetBehaviorDefinition(SlotIndex, DefinitionClass);
	if (BehaviorDefinition == nullptr)
	{
		const UClass* ClassPtr = DefinitionClass.Get();
		UE_VLOG_UELOG(this, LogSmartObject, Warning, TEXT("Unable to find a behavior definition of type %s in %s"), ClassPtr != nullptr ? *ClassPtr->GetName(): TEXT("Null"), *LexToString(Definition));
		return nullptr;
	}

	UE_VLOG_UELOG(this, LogSmartObject, Verbose, TEXT("Start using handle %s"), *LexToString(ClaimHandle));
	UE_VLOG_LOCATION(this, LogSmartObject, Display, SmartObjectRuntime.GetTransform().GetLocation(), 50.f, FColor::Green, TEXT("Use"));

	FSmartObjectSlotClaimState& SlotState = RuntimeSlotStates.FindChecked(ClaimHandle.SlotHandle);

	if (ensureMsgf(SlotState.GetState() == ESmartObjectSlotState::Claimed, TEXT("Should have been claimed first: %s"), *LexToString(ClaimHandle)) &&
		ensureMsgf(SlotState.User == ClaimHandle.UserHandle, TEXT("Attempt to use slot %s from handle %s but already assigned to %s"),
			*LexToString(SlotState), *LexToString(ClaimHandle), *LexToString(SlotState.User)))
	{
		SlotState.State = ESmartObjectSlotState::Occupied;
		return BehaviorDefinition;
	}

	return nullptr;
}

bool USmartObjectSubsystem::Release(const FSmartObjectClaimHandle& ClaimHandle)
{
	if (!ClaimHandle.IsValid())
	{
		UE_VLOG_UELOG(this, LogSmartObject, Error, TEXT("Must release slot using a valid handle"));
		return false;
	}

	FSmartObjectSlotClaimState* SlotState = RuntimeSlotStates.Find(ClaimHandle.SlotHandle);
	if (SlotState == nullptr)
	{
		return false;
	}

	const bool bSuccess = SlotState->Release(ClaimHandle, /*bAborted*/ false);
	UE_CVLOG_UELOG(bSuccess, this, LogSmartObject, Verbose, TEXT("Released using handle %s"), *LexToString(ClaimHandle));
	UE_CVLOG_LOCATION(bSuccess, this, LogSmartObject, Display, GetSlotLocation(ClaimHandle).GetValue(), 50.f, FColor::Red, TEXT("Release"));
	return bSuccess;
}

ESmartObjectSlotState USmartObjectSubsystem::GetSlotState(const FSmartObjectSlotHandle SlotHandle) const
{
	const FSmartObjectSlotClaimState* SlotState = RuntimeSlotStates.Find(SlotHandle);
	return SlotState != nullptr ? SlotState->GetState() : ESmartObjectSlotState::Invalid;
}

TOptional<FVector> USmartObjectSubsystem::GetSlotLocation(const FSmartObjectSlotHandle SlotHandle) const
{
	TOptional<FTransform> Transform = GetSlotTransform(SlotHandle);
	return (Transform.IsSet() ? Transform.GetValue().GetLocation() : TOptional<FVector>());
}

TOptional<FVector> USmartObjectSubsystem::GetSlotLocation(const FSmartObjectClaimHandle& ClaimHandle) const
{
	if (ensureMsgf(ClaimHandle.IsValid(), TEXT("Requesting slot location from an invalid claim handle.")))
	{
		return GetSlotLocation(ClaimHandle.SlotHandle);
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
		return GetSlotLocation(Result.SlotHandle);
	}
	return TOptional<FVector>();
}

TOptional<FTransform> USmartObjectSubsystem::GetSlotTransform(const FSmartObjectSlotHandle SlotHandle) const
{
	TOptional<FTransform> Transform;

	if (!ensureMsgf(SlotHandle.IsValid(), TEXT("Requesting slot transform for an invalid slot handle")))
	{
		return Transform;
	}

	if (const UMassEntitySubsystem* EntitySubsystem = GetWorldRef().GetSubsystem<UMassEntitySubsystem>())
	{
		const FSmartObjectSlotView View(*EntitySubsystem, SlotHandle);
		const FSmartObjectSlotTransform& SlotTransform = View.GetStateData<FSmartObjectSlotTransform>();
		Transform = SlotTransform.GetTransform();
	}

	return Transform;
}

TOptional<FTransform> USmartObjectSubsystem::GetSlotTransform(const FSmartObjectClaimHandle& ClaimHandle) const
{
	if (ensureMsgf(ClaimHandle.IsValid(), TEXT("Requesting slot transform from an invalid claim handle.")))
	{
		return GetSlotTransform(ClaimHandle.SlotHandle);
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
		return GetSlotTransform(Result.SlotHandle);
	}
	return TOptional<FTransform>();
}

FSmartObjectSlotClaimState* USmartObjectSubsystem::GetMutableSlotState(const FSmartObjectClaimHandle& ClaimHandle)
{
	return RuntimeSlotStates.Find(ClaimHandle.SlotHandle);
}

void USmartObjectSubsystem::RegisterSlotInvalidationCallback(const FSmartObjectClaimHandle& ClaimHandle, const FOnSlotInvalidated& Callback)
{
	FSmartObjectSlotClaimState* Slot = GetMutableSlotState(ClaimHandle);
	if (Slot != nullptr)
	{
		Slot->OnSlotInvalidatedDelegate = Callback;
	}
}

void USmartObjectSubsystem::UnregisterSlotInvalidationCallback(const FSmartObjectClaimHandle& ClaimHandle)
{
	FSmartObjectSlotClaimState* Slot = GetMutableSlotState(ClaimHandle);
	if (Slot != nullptr)
	{
		Slot->OnSlotInvalidatedDelegate.Unbind();
	}
}

void USmartObjectSubsystem::AddSlotDataDeferred(const FSmartObjectClaimHandle& ClaimHandle, const FConstStructView InData) const
{
	UMassEntitySubsystem* EntitySubsystem = GetWorldRef().GetSubsystem<UMassEntitySubsystem>();
	if (ensureMsgf(EntitySubsystem != nullptr, TEXT("Entity subsystem required to add slot data")) &&
		ensureMsgf(ClaimHandle.IsValid(), TEXT("Provided ClaimHandle is not valid. Data can't be added to slot.")) &&
		ensureMsgf(InData.GetScriptStruct()->IsChildOf(FSmartObjectSlotStateData::StaticStruct()),
			TEXT("Given struct doesn't represent a valid runtime data type. Make sure to inherit from FSmartObjectSlotState or one of its child-types.")))
	{
		EntitySubsystem->Defer().PushCommand(FCommandAddFragmentInstance(ClaimHandle.SlotHandle, InData));
	}
}

FSmartObjectSlotView USmartObjectSubsystem::GetSlotView(const FSmartObjectRequestResult& FindResult) const
{
	return (ensureMsgf(FindResult.IsValid(), TEXT("Provided RequestResult is not valid. SlotView can't be created.")))
			   ? GetSlotView(FindResult.SlotHandle)
			   : FSmartObjectSlotView();
}

FSmartObjectSlotView USmartObjectSubsystem::GetSlotView(const FSmartObjectClaimHandle& ClaimHandle) const
{
	return (ensureMsgf(ClaimHandle.IsValid(), TEXT("Provided ClaimHandle is not valid. SlotView can't be created.")))
			   ? GetSlotView(ClaimHandle.SlotHandle)
			   : FSmartObjectSlotView();
}

FSmartObjectSlotView USmartObjectSubsystem::GetSlotView(const FSmartObjectSlotHandle& SlotHandle) const
{
	if (ensureMsgf(SlotHandle.IsValid(), TEXT("Provided SlotHandle is not valid. SlotView can't be created.")))
	{
		if (const UMassEntitySubsystem* EntitySubsystem = GetWorldRef().GetSubsystem<UMassEntitySubsystem>())
		{
			return FSmartObjectSlotView(*EntitySubsystem, SlotHandle);
		}
	}

	return FSmartObjectSlotView();
}

FSmartObjectSlotHandle USmartObjectSubsystem::FindSlot(const FSmartObjectRuntime& SmartObjectRuntime, const FSmartObjectRequestFilter& Filter) const
{
	const FSmartObjectSlotHandle InvalidHandle;

	const USmartObjectDefinition& Definition = SmartObjectRuntime.GetDefinition();
	const int32 NumSlotDefinitions = Definition.GetSlots().Num();
	if (!ensureMsgf(NumSlotDefinitions > 0, TEXT("Definition should contain slot definitions at this point")))
	{
		return InvalidHandle;
	}

	const UClass* RequiredDefinitionClass = *Filter.BehaviorDefinitionClass;
	if (!ensureMsgf(RequiredDefinitionClass != nullptr, TEXT("Filter needs to provide required behavior definition type")))
	{
		return InvalidHandle;
	}

	// Validate if any available slots
	bool bAnyFreeSlot = false;

	TBitArray<> FreeSlots;
	FreeSlots.Init(false, SmartObjectRuntime.SlotHandles.Num());
	int32 SlotIndex = 0;
	for (const FSmartObjectSlotHandle& SlotHandle : SmartObjectRuntime.SlotHandles)
	{
		if (RuntimeSlotStates.FindChecked(SlotHandle).State == ESmartObjectSlotState::Free)
		{
			bAnyFreeSlot = true;
			FreeSlots[SlotIndex] = true;
		}
		SlotIndex++;
	}

	if (bAnyFreeSlot == false)
	{
		return InvalidHandle;
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
		const TConstArrayView<FSmartObjectSlotDefinition> Slots = Definition.GetSlots();
		for (int i = 0; i < Slots.Num(); ++i)
		{
			const FSmartObjectSlotDefinition& Slot = Slots[i];
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

			return SmartObjectRuntime.SlotHandles[i];
		}
	}

	return InvalidHandle;
}

void USmartObjectSubsystem::AbortAll(FSmartObjectRuntime& SmartObjectRuntime)
{
	for (const FSmartObjectSlotHandle& SlotHandle : SmartObjectRuntime.SlotHandles)
	{
		FSmartObjectSlotClaimState& SlotState = RuntimeSlotStates.FindChecked(SlotHandle);
		switch (SlotState.State)
		{
		case ESmartObjectSlotState::Claimed:
		case ESmartObjectSlotState::Occupied:
			{
				FSmartObjectClaimHandle ClaimHandle(SmartObjectRuntime.GetRegisteredID(), SlotHandle, SlotState.User);
				SlotState.Release(ClaimHandle, /* bAborted */ true);
				break;
			}
		case ESmartObjectSlotState::Free: // falling through on purpose
		default:
			UE_CVLOG_UELOG(SlotState.User.IsValid(), this, LogSmartObject, Warning,
				TEXT("Smart object %s used by %s while the slot it's assigned to is not marked Claimed nor Occupied"),
				*LexToString(SmartObjectRuntime.GetDefinition()), *LexToString(SlotState.User));
			break;
		}
	}
}

FSmartObjectRequestResult USmartObjectSubsystem::FindSmartObject(const FSmartObjectRequest& Request)
{
	// find X instances, ignore distance as long as in range, accept first available
	FSmartObjectRequestResult Result;
	const FSmartObjectRequestFilter& Filter = Request.Filter;

	SmartObjectOctree.FindFirstElementWithBoundsTest(Request.QueryBox,
		[&Result, &Filter, this](const FSmartObjectOctreeElement& Element)
		{
			Result = FindSlot(Element.SmartObjectHandle, Filter);

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
			Result = FindSlot(Element.SmartObjectHandle, Filter);
			if (Result.IsValid())
			{
				OutResults.Add(Result);
			}
			return true;
		});

	return (OutResults.Num() > 0);
}

FSmartObjectRequestResult USmartObjectSubsystem::FindSlot(const FSmartObjectHandle ID, const FSmartObjectRequestFilter& Filter) const
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

	const FSmartObjectSlotHandle FoundHandle = FindSlot(*SmartObjectRuntime, Filter);
	if (!FoundHandle.IsValid())
	{
		return InvalidResult;
	}

	return FSmartObjectRequestResult(ID, FoundHandle);
}

void USmartObjectSubsystem::RegisterCollectionInstances()
{
	for (TActorIterator<ASmartObjectCollection> It(GetWorld()); It; ++It)
	{
		ASmartObjectCollection* Collection = (*It);
		if (IsValid(Collection) && Collection->IsRegistered() == false)
		{
			const ESmartObjectCollectionRegistrationResult Result = RegisterCollection(*Collection);
			UE_VLOG_UELOG(Collection, LogSmartObject, Log, TEXT("Collection '%s' registration from USmartObjectSubsystem initialization - %s"), *Collection->GetName(), *UEnum::GetValueAsString(Result));
		}
	}
}

ESmartObjectCollectionRegistrationResult USmartObjectSubsystem::RegisterCollection(ASmartObjectCollection& InCollection)
{
	if (!IsValid(&InCollection))
	{
		return ESmartObjectCollectionRegistrationResult::Failed_InvalidCollection;
	}

	if (InCollection.IsRegistered())
	{
		UE_VLOG_UELOG(&InCollection, LogSmartObject, Error, TEXT("Trying to register collection '%s' more than once"), *InCollection.GetName());
		return ESmartObjectCollectionRegistrationResult::Failed_AlreadyRegistered;
	}

	ESmartObjectCollectionRegistrationResult Result = ESmartObjectCollectionRegistrationResult::Succeeded;
	if (InCollection.GetLevel()->IsPersistentLevel())
	{
		ensureMsgf(!IsValid(MainCollection), TEXT("Not expecting to set the main collection more than once"));
		UE_VLOG_UELOG(&InCollection, LogSmartObject, Log, TEXT("Main collection '%s' registered with %d entries"), *InCollection.GetName(), InCollection.GetEntries().Num());
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
		Result = ESmartObjectCollectionRegistrationResult::Succeeded;
	}
	else
	{
		InCollection.MarkAsGarbage();
		Result = ESmartObjectCollectionRegistrationResult::Failed_NotFromPersistentLevel;
	}

	return Result;
}

void USmartObjectSubsystem::UnregisterCollection(ASmartObjectCollection& InCollection)
{
	if (MainCollection != &InCollection)
	{
		UE_VLOG_UELOG(&InCollection, LogSmartObject, Verbose, TEXT("Ignoring unregistration of collection '%s' since this is not the main collection."), *InCollection.GetName());
		return;
	}

	MainCollection = nullptr;
	InCollection.OnUnregistered();
}

USmartObjectComponent* USmartObjectSubsystem::GetSmartObjectComponent(const FSmartObjectClaimHandle& ClaimHandle) const
{
	return (IsValid(MainCollection) ? MainCollection->GetSmartObjectComponent(ClaimHandle.SmartObjectHandle) : nullptr);
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
			Component->SetRegisteredHandle(Entry.GetHandle());
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