// Copyright Epic Games, Inc. All Rights Reserved.

#include "SmartObjectSubsystem.h"
#include "SmartObjectComponent.h"
#include "SmartObjectCollection.h"
#include "GameFramework/Actor.h"
#include "Model.h"
#include "Engine/LevelBounds.h"
#include "EngineUtils.h"
#include "VisualLogger/VisualLogger.h"

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

void USmartObjectSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	FBox WorldBounds(ForceInitToZero);
	FConstLevelIterator LevelIt = World->GetLevelIterator();
	while (LevelIt)
	{
		const ALevelBounds* LevelBounds = (*LevelIt)->LevelBoundsActor.Get();
		if (LevelBounds)
		{
			WorldBounds += LevelBounds->GetComponentsBoundingBox();
		}
		++LevelIt;
	}

	const UModel* WorldBSP = World->GetModel();
	if (WorldBSP)
	{
		WorldBounds += WorldBSP->Bounds.GetBox();
	}

	if (WorldBounds.GetExtent().IsNearlyZero())
	{
		for (FActorIterator It(World); It; ++It)
		{
			WorldBounds += (*It)->GetComponentsBoundingBox();
		}
	}

	FVector Center, Extents;
	WorldBounds.GetCenterAndExtents(Center, Extents);

	new(&SmartObjectOctree) FSmartObjectOctree(Center, Extents.Size2D());

	// Register SmartObject data that we missed before the subsystem got initialized.
	RegisterCollectionInstances();

#if WITH_SMARTOBJECT_DEBUG
	check(!bInitialized);
	bInitialized = true;
#endif
}

void USmartObjectSubsystem::Deinitialize()
{
#if WITH_SMARTOBJECT_DEBUG
	check(bInitialized);
	bInitialized = false;
#endif

	Super::Deinitialize();
}

USmartObjectSubsystem* USmartObjectSubsystem::GetCurrent(const UWorld* World)
{
	return UWorld::GetSubsystem<USmartObjectSubsystem>(World);
}

void USmartObjectSubsystem::AddToCollection(USmartObjectComponent& SOComponent) const
{
	if (MainCollection == nullptr)
	{
		return;
	}

#if WITH_EDITOR
	// For "build on demand collections" we wait an explicit build request to clear and repopulate
	if (MainCollection->IsBuildOnDemand())
	{
		return;
	}
#endif // WITH_EDITOR

	MainCollection->AddSmartObject(SOComponent);
}

void USmartObjectSubsystem::RemoveFromCollection(USmartObjectComponent& SOComponent) const
{
	if (MainCollection == nullptr)
	{
		return;
	}

#if WITH_EDITOR
	// For "build on demand" collections" we wait an explicit build request to clear and repopulate
	if (MainCollection->IsBuildOnDemand())
	{
		return;
	}
#endif // WITH_EDITOR

	MainCollection->RemoveSmartObject(SOComponent);
}

void USmartObjectSubsystem::AddToSimulation(const USmartObjectComponent& Component)
{
	// @todo SO: for now we rely on the component to hold the config in memory but it will
	// switch to class sparse data once the problem with component using sparse class data is fixed
	const FSmartObjectID& ID = Component.GetRegisteredID();
	FSmartObjectRuntime& Runtime = RuntimeSmartObjects.Emplace(ID, FSmartObjectRuntime(Component.GetConfig()));
	Runtime.SetRegisteredID(ID);

	// Transfer spatial information to the runtime instance
	Runtime.SetTransform(Component.GetComponentTransform());
	Runtime.SetBounds(Component.GetSmartObjectBounds());

	// Insert instance in the octree
	const FSmartObjectOctreeIDSharedRef SharedOctreeID = MakeShareable(new FSmartObjectOctreeID());
	Runtime.SetOctreeID(SharedOctreeID);
	SmartObjectOctree.AddNode(Runtime.GetBounds(), ID, SharedOctreeID);
}

void USmartObjectSubsystem::RemoveFromSimulation(const USmartObjectComponent& SOComponent)
{
	//@todo SO: this need to be handled as a runtime state instead
	const FSmartObjectID ID = SOComponent.GetRegisteredID();
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

bool USmartObjectSubsystem::RegisterSmartObject(USmartObjectComponent& SOComponent)
{
	// By default (runtime) we register only spawned components to the collection 
	// since the loaded one should have been stored in the collection
	// @todo SO: allow spawned smart object to register at runtime
	bool bShouldAddToCollection = false;

#if WITH_EDITOR
	UWorld* World = GetWorld();
	if (!ensureMsgf(World != nullptr, TEXT("Expecting a valid world")))
	{
		return false;
	}

	// In Editor we need to make a distinction between game world (PIE) and the edition world
	// since in the later case we keep track of all registrations
	if (!World->IsGameWorld())
	{
		bShouldAddToCollection = true;
	}

	// In Editor, we have various potential loading orders (Collection actor vs SmartObject actors)
	// so we make sure that we have gather the collection or spawn a missing one.
	RegisterCollectionInstances();
	SpawnMissingCollection();

	if (RegisteredSOComponents.Find(&SOComponent) != INDEX_NONE)
	{
		UE_VLOG_UELOG(SOComponent.GetOwner(), LogSmartObject, Error, TEXT("Trying to register SmartObject %s more than once."), *GetNameSafe(SOComponent.GetOwner()));
		return false;
	}
	RegisteredSOComponents.Add(&SOComponent);
#endif // WITH_EDITOR

	if (bShouldAddToCollection)
	{
		ensureMsgf(MainCollection != nullptr, TEXT("Expecting collection to be registered at this point"));
		AddToCollection(SOComponent);
	}	

#if WITH_SMARTOBJECT_DEBUG
	DebugRegisteredComponents.Add(&SOComponent);
#endif

	return true;
}

bool USmartObjectSubsystem::UnregisterSmartObject(USmartObjectComponent& SOComponent)
{
	// By default (runtime) we register only spawned components to the collection 
	// since the loaded one should have been stored in the collection so we do
	// the same for the unregister
	// @todo SO: allow spawned smart object to unregister at runtime
	bool bShouldRemoveFromCollection = false;

#if WITH_EDITOR
	UWorld* World = GetWorld();
	if (!ensureMsgf(World != nullptr, TEXT("A valid world is always expected")))
	{
		return false;
	}

	if (!World->IsGameWorld())
	{
		bShouldRemoveFromCollection = true;
	}

	if (RegisteredSOComponents.Remove(&SOComponent) == 0)
	{
		UE_VLOG_UELOG(SOComponent.GetOwner(), LogSmartObject, Error, TEXT("Trying to unregister SmartObject %s but it wasn't registered."), *GetNameSafe(SOComponent.GetOwner()));
		return false;
	}
#endif // WITH_EDITOR

	if (bShouldRemoveFromCollection)
	{
		ensureMsgf(MainCollection != nullptr, TEXT("Expecting collection to be registered at this point"));
		RemoveFromCollection(SOComponent);
	}

	if (GetWorld()->IsGameWorld())
	{
		RemoveFromSimulation(SOComponent);
	}

#if WITH_SMARTOBJECT_DEBUG
	DebugRegisteredComponents.Remove(&SOComponent);
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

FSmartObjectClaimHandle USmartObjectSubsystem::Claim(FSmartObjectID ID, const FSmartObjectRequestFilter& Filter)
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

const USmartObjectBehaviorConfigBase* USmartObjectSubsystem::Use(const FSmartObjectClaimHandle& ClaimHandle, const TSubclassOf<USmartObjectBehaviorConfigBase>& ConfigurationClass)
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

	return Use(*SmartObjectRuntime, ClaimHandle, ConfigurationClass);
}

const USmartObjectBehaviorConfigBase* USmartObjectSubsystem::Use(FSmartObjectRuntime& SmartObjectRuntime, const FSmartObjectClaimHandle& ClaimHandle, const TSubclassOf<USmartObjectBehaviorConfigBase>& ConfigurationClass)
{
	const FSmartObjectConfig& Config = SmartObjectRuntime.GetConfig();

	const USmartObjectBehaviorConfigBase* SOBehaviorConfig = Config.GetBehaviorConfig(ClaimHandle.SlotIndex, ConfigurationClass);
	if (SOBehaviorConfig == nullptr)
	{
		const UClass* ClassPtr = ConfigurationClass.Get();
		UE_VLOG_UELOG(this, LogSmartObject, Warning, TEXT("Unable to find a behavior config of type %s in %s"), ClassPtr != nullptr ? *ClassPtr->GetName(): TEXT("Null"), *Config.Describe());
		return nullptr;
	}

	UE_VLOG_UELOG(this, LogSmartObject, Verbose, TEXT("Start using handle %s"), *ClaimHandle.Describe());
	UE_VLOG_LOCATION(this, LogSmartObject, Display, SmartObjectRuntime.GetTransform().GetLocation(), 50.f, FColor::Green, TEXT("Use"));
	SmartObjectRuntime.UseSlot(ClaimHandle);
	return SOBehaviorConfig;
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

	Transform = SmartObjectRuntime->GetConfig().GetSlotTransform(SmartObjectRuntime->GetTransform(), SlotIndex);

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

	const FSmartObjectConfig& Config = SmartObjectRuntime.GetConfig();
	const int32 NumSlotDefinitions = Config.GetSlots().Num();
	if (!ensureMsgf(NumSlotDefinitions > 0, TEXT("SmartObjectConfig should contain slot definitions at this point")))
	{
		return InvalidIndex;
	}

	const UClass* RequiredConfigurationClass = *Filter.BehaviorConfigurationClass;
	if (!ensureMsgf(RequiredConfigurationClass != nullptr, TEXT("Filter needs to provide required behavior configuration type")))
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

	if (MatchesTagQueryFunc(Filter.ActivityRequirements, Config.GetActivityTags())
		&& MatchesTagQueryFunc(Config.GetObjectTagFilter(), ObjectTags)
		&& MatchesTagQueryFunc(Config.GetUserTagFilter(), Filter.UserTags))
	{
		TBitArray<> FreeSlots;
		SmartObjectRuntime.FindFreeSlots(FreeSlots);

		const TConstArrayView<FSmartObjectSlot> Slots = Config.GetSlots();
		for (int i = 0; i < Slots.Num(); ++i)
		{
			const FSmartObjectSlot& Slot = Slots[i];
			if (FreeSlots[i] == false)
			{
				continue;
			}

			if (Config.GetBehaviorConfig(FSmartObjectSlotIndex(i), Filter.BehaviorConfigurationClass) == nullptr)
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
			UE_VLOG_UELOG(this, LogSmartObject, Warning, TEXT("Smart object %s used by %s while the slot it's assigned to is not marked Claimed nor Occupied"), *SmartObjectRuntime.GetConfig().Describe(), *SlotRuntimeData.User.Describe());
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

void USmartObjectSubsystem::FindSmartObjects(const FSmartObjectRequest& Request, TArray<FSmartObjectRequestResult>& OutResults)
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
	UWorld* World = GetWorld();

	for (TActorIterator<ASmartObjectCollection> It(World); It; ++It)
	{
		ASmartObjectCollection* Collection = (*It);
		if (IsValid(Collection) && Collection->IsRegistered() == false)
		{
			UE_VLOG_UELOG(Collection, LogSmartObject, Log, TEXT("\'%s\' (0x%llx) USmartObjectSubsystem initialization - Succeeded"), *Collection->GetName(), UPTRINT(Collection));
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
		UE_VLOG_UELOG(&InCollection, LogSmartObject, Error, TEXT("Trying to register already registered SmartObjectCollection \'%s\'"), *InCollection.GetName());
		return;
	}

	if (InCollection.GetLevel()->IsPersistentLevel())
	{
		MainCollection = &InCollection;

#if WITH_EDITOR
		// For collection that are automatically updated we rebuild them on registration in the
		// Edition world.
		UWorld* World = GetWorld();
		if (World != nullptr && !World->IsGameWorld() && !MainCollection->IsBuildOnDemand())
		{
			RebuildCollection(InCollection);
		}
#endif // WITH_EDITOR
		
		InCollection.OnRegistered();
	}
	else
	{
		InCollection.MarkPendingKill();
	}
}

void USmartObjectSubsystem::UnregisterCollection(ASmartObjectCollection& InCollection)
{
	if (MainCollection != &InCollection)
	{
		UE_VLOG_UELOG(&InCollection, LogSmartObject, Verbose, TEXT("Ignoring unregistration of SmartObjectCollection \'%s\' since this is not the main collection."), *InCollection.GetName());
		return;
	}

	InCollection.OnUnregistered();
}

USmartObjectComponent* USmartObjectSubsystem::GetSmartObjectComponent(const FSmartObjectClaimHandle& ClaimHandle) const
{
	return ((MainCollection != nullptr) ? MainCollection->GetSmartObjectComponent(ClaimHandle.SmartObjectID) : nullptr);
}

void USmartObjectSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (MainCollection == nullptr)
	{
		UE_VLOG_UELOG(this, LogSmartObject, Warning, TEXT("No registered SmartObjectCollection in \'%s\'."), *GetName());
		return;
	}

	// Build all runtime from collection
	// @todo SO, we are still relying on the component for config and spatial information but this
	// should be extracted into the collection so runtime will not require components to be loaded
	for (const FSmartObjectCollectionEntry& Entry : MainCollection->GetEntries())
	{
		USmartObjectComponent* Component = Entry.GetComponent();
		if (Component == nullptr)
		{
			UE_VLOG_UELOG(MainCollection, LogSmartObject, Warning, TEXT("Unable to build runtime data from null component for \'%s\' in SmartObjectCollection \'%s\'."), *Entry.Describe(), *MainCollection->GetName());
			continue;
		}

		const FSmartObjectConfig& Config = Component->GetConfig();
		if (Config.Validate() == false)
		{
			UE_VLOG_UELOG(Component->GetOwner(), LogSmartObject, Error, TEXT("Skipped runtime data creation for SmartObject %s: Invalid configuration"), *GetNameSafe(Component->GetOwner()));
			continue;
		}

		// Create a runtime instance of that config using that ID
		Component->SetRegisteredID(Entry.GetID());
		AddToSimulation(*Component);
	}
}

#if WITH_EDITOR
void USmartObjectSubsystem::RebuildCollection(ASmartObjectCollection& InCollection)
{
	InCollection.RebuildCollection(RegisteredSOComponents);
}

void USmartObjectSubsystem::SpawnMissingCollection()
{
	if (MainCollection != nullptr)
	{
		return;
	}

	UWorld* World = GetWorld();

	FActorSpawnParameters SpawnInfo;
	SpawnInfo.OverrideLevel = World->PersistentLevel;
	SpawnInfo.bAllowDuringConstructionScript = true;
	World->SpawnActor<ASmartObjectCollection>(ASmartObjectCollection::StaticClass(), SpawnInfo);

	checkf(MainCollection != nullptr, TEXT("MainCollection must be assigned after spawning"));
	MainCollection->RebuildCollection(RegisteredSOComponents);
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
