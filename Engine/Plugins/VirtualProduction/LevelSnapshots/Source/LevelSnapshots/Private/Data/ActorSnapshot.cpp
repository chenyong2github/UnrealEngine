// Copyright Epic Games, Inc. All Rights Reserved.

#include "ActorSnapshot.h"

#include "ApplySnapshotDataArchive.h"
#include "LevelSnapshotSelections.h"
#include "LevelSnapshotsLog.h"
#include "LevelSnapshotsModule.h"
#include "LevelSnapshotsStats.h"
#include "TakeSnapshotArchive.h"

#include "Algo/Compare.h"
#include "GameFramework/Actor.h"
#include "HAL/UnrealMemory.h"

namespace
{
	void NotifyPreEdit(UObject* Object, const FPropertySelection& SelectedProperties)
	{
		DECLARE_SCOPE_CYCLE_COUNTER(TEXT("PreEditChange"), STAT_PreEditChange, STATGROUP_LevelSnapshots);
		
		for (const TFieldPath<FProperty>& PropertyPath : SelectedProperties.SelectedPropertyPaths)
		{
			FProperty* SelectedProperty = PropertyPath.Get(Object->StaticClass());
			if (SelectedProperty)
			{
				Object->PreEditChange(SelectedProperty);
			}
		}
	}

	void NotifyPostEdit(UObject* Object, const FPropertySelection& SelectedProperties)
	{
		DECLARE_SCOPE_CYCLE_COUNTER(TEXT("PostEditChange"), STAT_PostEditChange, STATGROUP_LevelSnapshots);
		
		for (const TFieldPath<FProperty>& PropertyPath : SelectedProperties.SelectedPropertyPaths)
		{
			FProperty* SelectedProperty = PropertyPath.Get(Object->StaticClass());
			if (SelectedProperty)
			{
				FPropertyChangedEvent ChangeEvent(SelectedProperty);
				Object->PostEditChangeProperty(ChangeEvent);
			}
		}
	}

	struct GenerateSelectionSetFromDiffImpl
	{
		static bool AreReferencedNamesEqual(
			const FBaseObjectInfo& SavedSnapshotObjectInfo,
			const FLevelSnapshot_Property& SavedProperty,
			const FBaseObjectInfo& ComparisonObjectInfo,
			const FLevelSnapshot_Property& ComparisonProperty)
		{
			if (SavedProperty.ReferencedNamesOffsetToIndex.Num())
			{
				const bool bHaveSameNum = SavedProperty.ReferencedNamesOffsetToIndex.Num() == ComparisonProperty.ReferencedNamesOffsetToIndex.Num();
				if (bHaveSameNum)
				{
					bool bAreIdentical = true;
					auto CurrentIt = SavedProperty.ReferencedNamesOffsetToIndex.CreateConstIterator();
					auto ComparisonIt = ComparisonProperty.ReferencedNamesOffsetToIndex.CreateConstIterator();
					while (bAreIdentical && CurrentIt && ComparisonIt)
					{
						const FName& CurrentName = SavedSnapshotObjectInfo.ReferencedNames.IsValidIndex(CurrentIt->Value) ? SavedSnapshotObjectInfo.ReferencedNames[CurrentIt->Value] : FName();
						const FName& ComparisonName = ComparisonObjectInfo.ReferencedNames.IsValidIndex(ComparisonIt->Value) ? ComparisonObjectInfo.ReferencedNames[ComparisonIt->Value] : FName();
						bAreIdentical &= CurrentName == ComparisonName;
						++CurrentIt;
						++ComparisonIt;
					}
					return bAreIdentical;
				}
				return false;
			}
			return true;
		}

		static bool AreReferencedObjectsEqual(
            const FBaseObjectInfo& SavedSnapshotObjectInfo,
            const FLevelSnapshot_Property& SavedProperty,
            const FBaseObjectInfo& ComparisonObjectInfo,
            const FLevelSnapshot_Property& ComparisonProperty)
		{
			if (SavedProperty.ReferencedObjectOffsetToIndex.Num())
			{
				const bool bHaveSameNum = SavedProperty.ReferencedObjectOffsetToIndex.Num() == ComparisonProperty.ReferencedObjectOffsetToIndex.Num();
				if (bHaveSameNum)
				{
					bool bAreIdentical = true;
					auto CurrentIt = SavedProperty.ReferencedObjectOffsetToIndex.CreateConstIterator();
					auto ComparisonIt = ComparisonProperty.ReferencedObjectOffsetToIndex.CreateConstIterator();
					while (bAreIdentical && CurrentIt && ComparisonIt)
					{
						const FSoftObjectPath& CurrentPath = SavedSnapshotObjectInfo.ReferencedObjects.IsValidIndex(CurrentIt->Value) ? SavedSnapshotObjectInfo.ReferencedObjects[CurrentIt->Value] : FSoftObjectPath();
						const FSoftObjectPath& ComparisonPath = ComparisonObjectInfo.ReferencedObjects.IsValidIndex(ComparisonIt->Value) ? ComparisonObjectInfo.ReferencedObjects[ComparisonIt->Value] : FSoftObjectPath();
						bAreIdentical &= CurrentPath == ComparisonPath;
						++CurrentIt;
						++ComparisonIt;
					}
					return bAreIdentical;
				}
				return false;
			}
			return true;
		}
		
		static void FindPropertiesWithDifferentValues(const FBaseObjectInfo& SavedSnapshotObjectInfo, const FBaseObjectInfo& ComparisonObjectInfo, TArray<TFieldPath<FProperty>>& ModifiedProperties)
		{
			for (const TPair<FName, FLevelSnapshot_Property>& ComparisonPropertyPair : ComparisonObjectInfo.Properties)
			{
				const FLevelSnapshot_Property* CurrentProperty = SavedSnapshotObjectInfo.Properties.Find(ComparisonPropertyPair.Key);
				const bool bIsPropertyAddedAfterSnapshotWasSaved = CurrentProperty == nullptr;
				if (bIsPropertyAddedAfterSnapshotWasSaved)
				{
					// New property
					continue;
				}

				const FLevelSnapshot_Property& ComparisonProperty = ComparisonPropertyPair.Value;
				
				bool bIsIdentical = ComparisonProperty.DataSize == CurrentProperty->DataSize;
				bIsIdentical &= FMemory::Memcmp(&SavedSnapshotObjectInfo.SerializedData.Data[CurrentProperty->DataOffset], &ComparisonObjectInfo.SerializedData.Data[ComparisonPropertyPair.Value.DataOffset], ComparisonPropertyPair.Value.DataSize) == 0;

				// Referenced Names Comparison
				bIsIdentical &= AreReferencedNamesEqual(SavedSnapshotObjectInfo, *CurrentProperty, ComparisonObjectInfo, ComparisonProperty);
				// Referenced Objects Comparison
				bIsIdentical &= AreReferencedObjectsEqual(SavedSnapshotObjectInfo, *CurrentProperty, ComparisonObjectInfo, ComparisonProperty);
				
				if (!bIsIdentical)
				{
					ModifiedProperties.Add(ComparisonPropertyPair.Value.PropertyPath);
				}
			}
		}
		
		static bool DoComponentNamesMatchAtEndOfPaths(const FSoftObjectPath& SnapshotComponentPath, const FSoftObjectPath& TargetComponentPath)
		{
			// Example: PersistentLevel.StaticMeshActor_42.StaticMeshComponent
			const FString& SnapshotObjectSubPath = SnapshotComponentPath.GetSubPathString();
			const FString& TargetObjectSubPath = TargetComponentPath.GetSubPathString();

			FString SnapshotComponentName;
			const bool bFoundPointInSnapshot = SnapshotObjectSubPath.Split(TEXT("."), nullptr, &SnapshotComponentName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			checkf(bFoundPointInSnapshot, TEXT("Failed to find component name in component path %s"), *SnapshotObjectSubPath);

			FString TargetComponentName;
			const bool bFoundPointInTargetComponent = TargetObjectSubPath.Split(TEXT("."), nullptr, &TargetComponentName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			checkf(bFoundPointInTargetComponent, TEXT("Failed to find component name in component path %s"), *TargetObjectSubPath);

			return bFoundPointInSnapshot && bFoundPointInTargetComponent && SnapshotComponentName.Equals(TargetComponentName);
		}
	};
	
	struct DeserializeSelectPropertiesImpl
	{
		static UActorComponent* FindComponentMatchingToSavedComponentPath(AActor* InTargetActor, const FSoftObjectPath& SavedComponentPath)
		{
			// Example SavedComponentPath /Game/MapName.MapName:PersistentLevel.StaticMeshActor_42.StaticMeshComponent
			// Example for SavedActorToComponentPath PersistentLevel.StaticMeshActor_42.StaticMeshComponent
			const FString& SavedActorToComponentPath = SavedComponentPath.GetSubPathString();

			FString ComponentNameToLookFor;
			const bool bFoundPoint = SavedActorToComponentPath.Split(TEXT("."), nullptr, &ComponentNameToLookFor, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			if (!ensureAlwaysMsgf(bFoundPoint, TEXT("Object path %s is malformatted. Expected to find a path of the form /Game/MapName.MapName:PersistentLevel.StaticMeshActor_42.StaticMeshComponent"), *SavedComponentPath.ToString()))
			{
				return nullptr;
			}

			TInlineComponentArray<UActorComponent*> ActorComponents;
			InTargetActor->GetComponents(ActorComponents);
			for (UActorComponent* Component : ActorComponents)
			{
				if (Component->GetName().Equals(ComponentNameToLookFor))
				{
					return Component;
				}
			}
			
			return nullptr;
		}

		static FString GetComponentNameFromPath(const FSoftObjectPath& SavedComponentPath)
		{
			FString ComponentName;
			const bool bFoundPoint = SavedComponentPath.GetSubPathString().Split(TEXT("."), nullptr, &ComponentName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			check(bFoundPoint);
			return ComponentName;
		}

		static bool DeserializeTransientActorComponents(AActor* InTargetActor, const TArray<FLevelSnapshot_Component>& InComponentSnapshots)
		{
			DECLARE_SCOPE_CYCLE_COUNTER(TEXT("DeserializeComponents_TransientActor"), STAT_DeserializeActor_TransientActor, STATGROUP_LevelSnapshots);
			
			bool bModifiedAnyComponents = false;
			
			for (const FLevelSnapshot_Component& ComponentSnapshot : InComponentSnapshots)
			{
				if (UActorComponent* Component = DeserializeSelectPropertiesImpl::FindComponentMatchingToSavedComponentPath(InTargetActor, ComponentSnapshot.Base.SoftObjectPath))
				{

					FApplySnapshotDataArchive ComponentReader = FApplySnapshotDataArchive::MakeForDeserializingTransientObject(ComponentSnapshot.Base);
					Component->Serialize(ComponentReader);
					Component->PostLoad();

					bModifiedAnyComponents = true;
				}
				else
				{
					// TODO: Create a new component and deserialize
					UE_LOG(LogLevelSnapshots, Error,
                        TEXT("Failed to find component %s while deserializing actor from snapshot. Did you add the actor from inside the Details panel using the Add Component button? Such components are currently not supported. We only support component added by a Blueprint."),
                        *DeserializeSelectPropertiesImpl::GetComponentNameFromPath(ComponentSnapshot.Base.SoftObjectPath)
                        );
				}
			}
			
			return bModifiedAnyComponents;
		}
		
		static bool DeserializeWorldActorComponents(const bool bWasActorAlreadyModified, AActor* InTargetActor, const TArray<FLevelSnapshot_Component>& InComponentSnapshots, const ULevelSnapshotSelectionSet* InSelectedProperties)
		{
			DECLARE_SCOPE_CYCLE_COUNTER(TEXT("DeserializeComponents"), STAT_DeserializeActor, STATGROUP_LevelSnapshots);
			
			bool bModifiedAnyComponents = false;
			
			for (const FLevelSnapshot_Component& ComponentSnapshot : InComponentSnapshots)
			{
				const FPropertySelection* ComponentProperties =  InSelectedProperties->GetSelectedProperties(ComponentSnapshot.Base.SoftObjectPath);
				const bool bShouldSerializeAtLeastOneProperty = ComponentProperties != nullptr && ComponentProperties->SelectedPropertyPaths.Num() > 0;
				if (!bShouldSerializeAtLeastOneProperty)
				{
					continue;
				}
		
				if (UActorComponent* Component = DeserializeSelectPropertiesImpl::FindComponentMatchingToSavedComponentPath(InTargetActor, ComponentSnapshot.Base.SoftObjectPath))
				{
#if WITH_EDITOR
					// If we haven't modified the actor yet, Modify it here
					if (!bWasActorAlreadyModified)
					{
						InTargetActor->Modify(true);
					}
					Component->Modify(true);
#endif 

					FApplySnapshotDataArchive ComponentReader = FApplySnapshotDataArchive::MakeDeserializingIntoWorldObject(ComponentSnapshot.Base, ComponentProperties);
					NotifyPreEdit(Component, *ComponentProperties);
					Component->Serialize(ComponentReader);
					NotifyPostEdit(Component, *ComponentProperties);

					bModifiedAnyComponents = true;
				}
				else
				{
					// TODO: Create a new component and deserialize
					UE_LOG(LogLevelSnapshots, Error,
                        TEXT("Failed to find component %s while deserializing actor from snapshot. Did you add the actor from inside the Details panel using the Add Component button? Such components are currently not supported. We only support component added by a Blueprint."),
                        *DeserializeSelectPropertiesImpl::GetComponentNameFromPath(ComponentSnapshot.Base.SoftObjectPath)
                        );
				}
			}
			
			return bModifiedAnyComponents;
		}
	};	
}

bool FSerializedActorData::Serialize(FArchive& Ar) 
{
	int32 Num = Data.Num();
	Ar << Num;
	if (Ar.IsLoading())
	{
		Data.AddUninitialized(Num);
		Ar.Serialize(Data.GetData(), Num);
	}
	else if (Ar.IsSaving())
	{
		Ar.Serialize(Data.GetData(), Num);
	}
	return true;
};

FLevelSnapshot_Component::FLevelSnapshot_Component(UActorComponent* TargetComponent)
    : Base(TargetComponent)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("SnapshotComponent"), STAT_SnapshotComponent, STATGROUP_LevelSnapshots);
	
	if (USceneComponent* SceneComponent = Cast<USceneComponent>(TargetComponent))
	{
		bIsSceneComponent = true;
		if (USceneComponent* ParentComponent = SceneComponent->GetAttachParent())
		{
			ParentComponentPath = ParentComponent->GetPathName();
		}
	}

	FTakeSnapshotArchive Writer(Base);
	TargetComponent->Serialize(Writer);

	TargetComponent->MarkRenderStateDirty();
}

FLevelSnapshot_Actor::FLevelSnapshot_Actor(AActor* TargetActor)
	: Base(TargetActor)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("SnapshotActor"), STAT_SnapshotActor, STATGROUP_LevelSnapshots);
	
	FTakeSnapshotArchive Writer(Base);
	TargetActor->Serialize(Writer);

	TArray<UActorComponent*> Components;
	TargetActor->GetComponents<UActorComponent>(Components);

	for (UActorComponent* Component : Components)
	{
		if (Component)
		{
			ComponentSnapshots.Add(FLevelSnapshot_Component(Component));
		}
	}
}

bool FLevelSnapshot_Actor::CorrespondsToActorInWorld(const AActor* WorldActor) const
{
	return Base.CorrespondsToObjectInWorld(WorldActor);
};

AActor* FLevelSnapshot_Actor::GetDeserializedActor(UWorld* TempWorld)
{
	if (CachedDeserialisedActor.IsValid())
	{
		return CachedDeserialisedActor.Get();
	}

	FSoftClassPath SoftClassPath(Base.ObjectClassPathName);
	UClass* TargetClass = SoftClassPath.ResolveClass();
	if (!ensureAlwaysMsgf(TargetClass, TEXT("Unknown class %s. The snapshot is mostly likely referencing a class that was deleted."), *SoftClassPath.ToString()))
	{
		return nullptr;
	}
	
	if (ensure(TempWorld))
	{
		// TODO: Strictly speaking we cannot use SpawnActor here because it calls PostActorCreated which is mutually exclusive with PostLoad, which is called in DeserializeToTransientActor
		// Problem: NewObject only creates native components. For now use SpawnActor so Blueprint components are created correctly.
		CachedDeserialisedActor = TempWorld->SpawnActor<AActor>(TargetClass);
		DeserializeTransientActorProperties(CachedDeserialisedActor.Get());
	}
	
	return CachedDeserialisedActor.Get();
}

void FLevelSnapshot_Actor::DeserializeIntoWorldActor(AActor* InTargetActor, TOptional<const ULevelSnapshotSelectionSet*> InPropertiesToDeserializeInto) const
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("DeserializeIntoWorldActor"), STAT_DeserializeIntoWorldActor, STATGROUP_LevelSnapshots);
	
	if (!ensureAlways(InTargetActor && CorrespondsToActorInWorld(InTargetActor)))
	{
		return;
	}

	const ULevelSnapshotSelectionSet* PropertiesToDeserializeInto = InPropertiesToDeserializeInto.Get(nullptr);
	if (PropertiesToDeserializeInto == nullptr)
	{
		ULevelSnapshotSelectionSet* SavedPropertiesDifferentFromClassDefaults = NewObject<ULevelSnapshotSelectionSet>(GetTransientPackage(), ULevelSnapshotSelectionSet::StaticClass(), NAME_None, EObjectFlags::RF_Transient);
		GenerateSelectionSetFromDiff(InTargetActor, SavedPropertiesDifferentFromClassDefaults, WorldActor);
		PropertiesToDeserializeInto = SavedPropertiesDifferentFromClassDefaults;
	}

	DeserializeWorldActorProperties(InTargetActor, PropertiesToDeserializeInto);
}

void FLevelSnapshot_Actor::DeserializeTransientActorProperties(AActor* InTargetActor) const
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("DeserializeActorProperties (transient actor)"), STAT_DeserializeActorProperties_TransientActor, STATGROUP_LevelSnapshots);
	auto DeserializeActor = [this, InTargetActor]()
	{
		DECLARE_SCOPE_CYCLE_COUNTER(TEXT("DeserializeActor (transient actor)"), STAT_DeserializeActor_TransientActor, STATGROUP_LevelSnapshots);
		FApplySnapshotDataArchive Reader = FApplySnapshotDataArchive::MakeForDeserializingTransientObject(Base);
		InTargetActor->Serialize(Reader);
	};
	auto PostDeserialisationWork = [InTargetActor]()
	{
		DECLARE_SCOPE_CYCLE_COUNTER(TEXT("PostDeserializeWork (transient actor)"), STAT_PostDeserializeWork_TransientActor, STATGROUP_LevelSnapshots);
		InTargetActor->PostLoad();
		InTargetActor->UpdateComponentTransforms();
	};
	
	DeserializeActor();	
	DeserializeSelectPropertiesImpl::DeserializeTransientActorComponents(InTargetActor, ComponentSnapshots);
	PostDeserialisationWork();
}

void FLevelSnapshot_Actor::DeserializeWorldActorProperties(AActor* InTargetActor, const ULevelSnapshotSelectionSet* InSelectedProperties) const
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("DeserializeActorProperties (world actor)"), STAT_DeserializeWorldActorProperties_WorldActor, STATGROUP_LevelSnapshots);
	auto DeserializeActor = [this, InTargetActor, InSelectedProperties]()
	{
		DECLARE_SCOPE_CYCLE_COUNTER(TEXT("DeserializeActor (world actor)"), STAT_DeserializeActor_WorldActor, STATGROUP_LevelSnapshots);
			
		const FPropertySelection* SelectedProperties = InSelectedProperties->GetSelectedProperties(FSoftObjectPath(InTargetActor));
		const bool bNeedToDeserializeActor = SelectedProperties && SelectedProperties->SelectedPropertyPaths.Num() > 0;
		if (bNeedToDeserializeActor)
		{
#if WITH_EDITOR
			InTargetActor->Modify(true);
#endif

			FApplySnapshotDataArchive Reader = FApplySnapshotDataArchive::MakeDeserializingIntoWorldObject(Base, SelectedProperties);

			NotifyPreEdit(InTargetActor, *SelectedProperties);
			InTargetActor->Serialize(Reader);
			NotifyPostEdit(InTargetActor, *SelectedProperties);
		}

		return bNeedToDeserializeActor;
	};

	const bool bModifiedActor = DeserializeActor();
	const bool bModifiedAnyComponents = DeserializeSelectPropertiesImpl::DeserializeWorldActorComponents(bModifiedActor, InTargetActor, ComponentSnapshots, InSelectedProperties);

	const bool bChangedAnyProperties = bModifiedActor || bModifiedAnyComponents;
	if (bChangedAnyProperties)
	{
		DECLARE_SCOPE_CYCLE_COUNTER(TEXT("PostDeserializeWork (world actor)"), STAT_PostDeserializeWork_WorldActor, STATGROUP_LevelSnapshots);
		InTargetActor->UpdateComponentTransforms();
	}
}

void FLevelSnapshot_Actor::GenerateSelectionSetFromDiff(AActor* TargetActor, ULevelSnapshotSelectionSet* SelectionSet, EActorType TargetActorType) const
{
	struct Local
	{
		static bool DoesSnapshotComponentMatchComparisonComponent(const FLevelSnapshot_Component& ComponentFromSnapshot, const FLevelSnapshot_Component& ComparisonComponent, EActorType TargetActorType)
		{	
			switch (TargetActorType)
			{
			case TransientActor:
				// Example path for ComponentFromSnapshot actor /Game/MapName.MapName:PersistentLevel.StaticMeshActor_42.StaticMeshComponent
				// Example path for ComparisonComponent actor /Engine/Transient.LevelSnapshotsDeserialisedActorWorld:PersistentLevel.StaticMeshActor_C_21:StaticMeshComponent
				// Pay special attention to the ":" above; it separates AssetPathName and SubPathString in FSoftObjectPath.

				// Comparing with FBaseObjectInfo::ObjectName works in most cases but not always: soft path is updated by Unreal when user changes a component's name while ObjectName is not.
				return GenerateSelectionSetFromDiffImpl::DoComponentNamesMatchAtEndOfPaths(ComponentFromSnapshot.Base.SoftObjectPath, ComparisonComponent.Base.SoftObjectPath);

			case WorldActor:
				// Example path /Game/MapName.MapName:PersistentLevel.StaticMeshActorName.StaticMeshComponent
				return ComponentFromSnapshot.Base.SoftObjectPath == ComparisonComponent.Base.SoftObjectPath;
	
			default:
				checkNoEntry();
			}

			return false;
		}
	};
	
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("GenerateSelectionSetFromDiff"), STAT_GenerateSelectionSetFromDiff, STATGROUP_LevelSnapshots);
	
	if (!ensure(SelectionSet))
	{
		return;
	}
	const FLevelSnapshot_Actor ComparisonSnapshot(TargetActor);

	TArray<TFieldPath<FProperty>> ModifiedProperties;
	const int32 MaxNumPossibilyModifiedProperties = Base.Properties.Num();
	ModifiedProperties.Reserve(MaxNumPossibilyModifiedProperties);
	
	GenerateSelectionSetFromDiffImpl::FindPropertiesWithDifferentValues(Base, ComparisonSnapshot.Base, ModifiedProperties);
	SelectionSet->AddObjectProperties(TargetActor, ModifiedProperties);

	for (const FLevelSnapshot_Component& ComparisonComponentSnapshot : ComparisonSnapshot.ComponentSnapshots)
	{
		const FLevelSnapshot_Component* CurrentComponentSnapshot = ComponentSnapshots.FindByPredicate([TargetActorType, &ComparisonComponentSnapshot](const FLevelSnapshot_Component& Snapshot)
			{
				return Local::DoesSnapshotComponentMatchComparisonComponent(Snapshot, ComparisonComponentSnapshot, TargetActorType);
			});

		const bool bComponentWasAddedOrRenamedAfterSnapshotWasSaved = CurrentComponentSnapshot == nullptr;
		if (bComponentWasAddedOrRenamedAfterSnapshotWasSaved)
		{
			continue;
		}
		
		ModifiedProperties.Empty(false);
		GenerateSelectionSetFromDiffImpl::FindPropertiesWithDifferentValues(CurrentComponentSnapshot->Base, ComparisonComponentSnapshot.Base, ModifiedProperties);
		SelectionSet->AddObjectProperties(CurrentComponentSnapshot->Base.SoftObjectPath, ModifiedProperties);
	}
}
