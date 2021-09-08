// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSnapshotsFunctionLibrary.h"

#include "ApplySnapshotFilter.h"
#include "ConstantFilter.h"
#include "LevelSnapshot.h"
#include "LevelSnapshotFilters.h"
#include "LevelSnapshotsLog.h"

#include "EngineUtils.h"
#include "CustomSerialization/CustomObjectSerializationWrapper.h"

namespace
{
	struct FPropertyMapBuilder
	{
		FPropertyMapBuilder(UWorld* TargetWorld, ULevelSnapshot* Snapshot, ULevelSnapshotFilter* FilterToApply)
			:
			Snapshot(Snapshot)
		{
			Snapshot->DiffWorld(
				TargetWorld,
				ULevelSnapshot::FActorPathConsumer::CreateRaw(this, &FPropertyMapBuilder::HandleActorExistsInWorldAndSnapshot, FilterToApply),
				ULevelSnapshot::FActorPathConsumer::CreateRaw(this, &FPropertyMapBuilder::HandleActorWasRemovedFromWorld, FilterToApply),
				ULevelSnapshot::FActorConsumer::CreateRaw(this, &FPropertyMapBuilder::HandleActorWasAddedToWorld, FilterToApply)
			);
		}

		const FPropertySelectionMap& GetSelectionMap() const
		{
			return SelectionMap;
		}

	private:
		
		FPropertySelectionMap SelectionMap;
		ULevelSnapshot* Snapshot;
		
		void HandleActorExistsInWorldAndSnapshot(const FSoftObjectPath& OriginalActorPath, ULevelSnapshotFilter* FilterToApply)
		{
			UObject* ResolvedWorldActor = OriginalActorPath.ResolveObject();
            if (!ResolvedWorldActor)
            {
            	UE_LOG(LogLevelSnapshots, Warning, TEXT("Failed to resolve actor %s. Was it deleted from the world?"), *OriginalActorPath.ToString());
            	return;
            }
        
            AActor* WorldActor = Cast<AActor>(ResolvedWorldActor);
            if (ensureAlwaysMsgf(WorldActor, TEXT("A path that was previously associated with an actor no longer refers to an actor. Something is wrong.")))
            {
            	TOptional<AActor*> DeserializedSnapshotActor = Snapshot->GetDeserializedActor(OriginalActorPath);
            	if (!ensureMsgf(DeserializedSnapshotActor.Get(nullptr), TEXT("Failed to get TMap value for key %s. Is the snapshot corrupted?"), *OriginalActorPath.ToString()))
            	{
            		// Engine issue. Take snapshot. Rename actor. Update references. Value is updated correctly in TMap but look ups no longer work.
            		UE_LOG(LogLevelSnapshots, Error, TEXT("Failed to lookup actor %s OriginalActorPath. The snapshot is corrupted."));
            		return;
            	}
                    
            	if (Snapshot->HasOriginalChangedPropertiesSinceSnapshotWasTaken(*DeserializedSnapshotActor, WorldActor))
            	{
            		ULevelSnapshotsFunctionLibrary::ApplyFilterToFindSelectedProperties(Snapshot, SelectionMap, WorldActor, DeserializedSnapshotActor.GetValue(), FilterToApply);
            	}
            }
		}
		
		void HandleActorWasRemovedFromWorld(const FSoftObjectPath& OriginalActorPath, ULevelSnapshotFilter* FilterToApply)
		{
			const EFilterResult::Type FilterResult = FilterToApply->IsDeletedActorValid(
				FIsDeletedActorValidParams(
					OriginalActorPath,
					[this](const FSoftObjectPath& ObjectPath)
					{
						return Snapshot->GetDeserializedActor(ObjectPath).Get(nullptr);
					}
				)
			);
			if (EFilterResult::CanInclude(FilterResult))
			{
				SelectionMap.AddDeletedActorToRespawn(OriginalActorPath);
			}
		}
		
		void HandleActorWasAddedToWorld(AActor* WorldActor, ULevelSnapshotFilter* FilterToApply)
		{
			const EFilterResult::Type FilterResult = FilterToApply->IsAddedActorValid(FIsAddedActorValidParams(WorldActor)); 
			if (EFilterResult::CanInclude(FilterResult))
			{
				SelectionMap.AddNewActorToDespawn(WorldActor);
			}
		}
	};
}

ULevelSnapshot* ULevelSnapshotsFunctionLibrary::TakeLevelSnapshot(const UObject* WorldContextObject, const FName NewSnapshotName, const FString Description)
{
	return TakeLevelSnapshot_Internal(WorldContextObject, NewSnapshotName, nullptr, Description);
}

ULevelSnapshot* ULevelSnapshotsFunctionLibrary::TakeLevelSnapshot_Internal(const UObject* WorldContextObject, const FName NewSnapshotName, UPackage* InPackage, const FString Description)
{
	UWorld* TargetWorld = nullptr;
	if (WorldContextObject)
	{
		TargetWorld = WorldContextObject->GetWorld();
	}

	if (!ensure(TargetWorld))
	{
		return nullptr;
	}
	
	ULevelSnapshot* NewSnapshot = NewObject<ULevelSnapshot>(InPackage ? InPackage : GetTransientPackage(), NewSnapshotName, RF_NoFlags);
	NewSnapshot->SetSnapshotName(NewSnapshotName);
	NewSnapshot->SetSnapshotDescription(Description);
	NewSnapshot->SnapshotWorld(TargetWorld);
	return NewSnapshot;
}

void ULevelSnapshotsFunctionLibrary::ApplySnapshotToWorld(const UObject* WorldContextObject, ULevelSnapshot* Snapshot, ULevelSnapshotFilter* OptionalFilter)
{
	UWorld* TargetWorld = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (ensure(TargetWorld && Snapshot))
	{
		const FPropertyMapBuilder Helper(TargetWorld, Snapshot, OptionalFilter ? OptionalFilter : GetMutableDefault<UConstantFilter>());
		Snapshot->ApplySnapshotToWorld(TargetWorld, Helper.GetSelectionMap());
	}
}

void ULevelSnapshotsFunctionLibrary::ApplyFilterToFindSelectedProperties(
	ULevelSnapshot* Snapshot,
	FPropertySelectionMap& MapToAddTo, 
	AActor* WorldActor,
	AActor* DeserializedSnapshotActor,
	const ULevelSnapshotFilter* Filter,
	bool bAllowUnchangedProperties,
    bool bAllowNonEditableProperties)
{
	if (Filter == nullptr)
	{
		Filter = GetMutableDefault<UConstantFilter>();
	}
	
	FApplySnapshotFilter::Make(Snapshot, DeserializedSnapshotActor, WorldActor, Filter)
		.AllowUnchangedProperties(bAllowUnchangedProperties)
		.AllowNonEditableProperties(bAllowNonEditableProperties)
		.ApplyFilterToFindSelectedProperties(MapToAddTo);
}

void ULevelSnapshotsFunctionLibrary::ForEachMatchingCustomSubobjectPair(
	ULevelSnapshot* Snapshot,
	UObject* SnapshotRootObject,
	UObject* WorldRootObject,
	TFunction<void(UObject* SnapshotSubobject, UObject* EditorWorldSubobject)> Callback)
{
	FCustomObjectSerializationWrapper::ForEachMatchingCustomSubobjectPair(Snapshot->GetSerializedData(), SnapshotRootObject, WorldRootObject, Callback);
}
