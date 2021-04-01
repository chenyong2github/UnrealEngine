// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSnapshot.h"

#include "LevelSnapshotSelections.h"
#include "LevelSnapshotsLog.h"
#include "LevelSnapshotsStats.h"
#include "PropertyInfoHelpers.h"

#include "EngineUtils.h"
#include "Components/BillboardComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/DefaultPhysicsVolume.h"
#include "UObject/Package.h"

#if WITH_EDITOR
#include "ActorEditorUtils.h"
#include "Engine/LevelScriptActor.h"
#include "ScopedTransaction.h"
#endif

namespace
{
	bool DoesActorHaveSupportedClass(const AActor* Actor)
	{
		const TSet<UClass*> UnsupportedClasses = 
		{
			ALevelScriptActor::StaticClass(),		// The level blueprint. Filtered out to avoid external map errors when saving a snapshot.
			ADefaultPhysicsVolume::StaticClass()	// Does not show up in world outliner; always spawned with world.
        };

		for (UClass* Class : UnsupportedClasses)
		{
			if (Actor->IsA(Class))
			{
				return false;
			}
		}
	
		return true;
	}

	bool DoesComponentHaveSupportedClass(const UActorComponent* Component)
	{
		const TSet<UClass*> UnsupportedClasses = 
		{
			UBillboardComponent::StaticClass(),		// Attached to all editor actors > It always has a different name so we will never be able to match it.
        };

		for (UClass* Class : UnsupportedClasses)
		{
			if (Component->IsA(Class))
			{
				return false;
			}
		}
	
		return true;
	}

	/* If this function return false, the objects are not equivalent. If true, ignore the object references. */
	bool ShouldConsiderObjectsEquivalent(const FWorldSnapshotData& SnapshotData, const FObjectPropertyBase* ObjectProperty, void* SnapshotValuePtr, void* WorldValuePtr, AActor* SnapshotActor, AActor* WorldActor)
	{
		UObject* SnapshotObject = ObjectProperty->GetObjectPropertyValue(SnapshotValuePtr);
		UObject* WorldObject = ObjectProperty->GetObjectPropertyValue(WorldValuePtr);
		if (SnapshotObject == nullptr && WorldObject == nullptr)
		{
			return true;
		}

		UObject* PossiblySubobject = SnapshotObject ? SnapshotObject : WorldObject;
		AActor* PossiblyOwner = SnapshotObject ? SnapshotActor : WorldActor;
		// Handle subobjects created within actor
		const bool bIsSubobject = PossiblySubobject->IsIn(PossiblyOwner);
		if (bIsSubobject)
		{
			return true;
		}
		// Handle temporary 'subobjects'
		const bool bIsTempTransientObject = PossiblySubobject->HasAnyFlags(RF_Transient) || PossiblySubobject->GetPackage()->HasAnyFlags(RF_Transient);
		if (bIsTempTransientObject)
		{
			return true;
		}

		// Handle internal reference to other objects within the same world
		if (SnapshotData.AreReferencesEquivalent(SnapshotObject, WorldObject))
		{
			return true;
		}

		// Anything this far should be an asset reference, e.g. to a data asset or material.
		return ObjectProperty->Identical(SnapshotValuePtr, WorldValuePtr, 0);
	}
	
	void EnqueueMatchingComponents(TInlineComponentArray<TPair<UObject*, UObject*>>& SnapshotOriginalPairsToProcess, AActor* SnapshotActor, AActor* WorldActor)
	{
		TInlineComponentArray<UActorComponent*> SnapshotComponents;
		TInlineComponentArray<UActorComponent*> WorldComponents;
		SnapshotActor->GetComponents(SnapshotComponents);
		WorldActor->GetComponents(WorldComponents);
		for (UActorComponent* WorldComponent : WorldComponents)
		{
			const bool bIsSupported = ULevelSnapshot::IsComponentDesirableForCapture(WorldComponent);
			if (!bIsSupported)
			{
				continue;
			}

			UActorComponent** SnapshotComponent = SnapshotComponents.FindByPredicate([WorldComponent](UActorComponent* Other)
            {
                return Other->GetFName() == WorldComponent->GetFName();
            });
			if (SnapshotComponent == nullptr)
			{
				UE_LOG(LogLevelSnapshots, Warning, TEXT("Failed to find component named '%s' of actor '%s' on snapshot version. Has the component name changed since the snapshot was taken?"), *WorldComponent->GetName(), *WorldActor->GetName());
				continue;
			}
			UClass* SnapshotCompClass = (*SnapshotComponent)->GetClass();
			UClass* WorldCompClass = WorldComponent->GetClass();
			const bool bHaveSameClasses = SnapshotCompClass == WorldCompClass;
			if (!bHaveSameClasses)
			{
				UE_LOG(LogLevelSnapshots, Warning,
                    TEXT("World component '%s' of actor '%s' has class '%s' but snapshot version had class '%s'. Has the component changed classes since the snapshot was taken?"),
                    *WorldComponent->GetName(), *WorldActor->GetName(), *WorldCompClass->GetName(), *SnapshotCompClass->GetName());
				continue;
			}
		
			SnapshotOriginalPairsToProcess.Add(TPair<UObject*, UObject*>(*SnapshotComponent, WorldComponent));
		}
	}
}

bool ULevelSnapshot::IsActorDesirableForCapture(const AActor* Actor)
{
	// TODO: Create project settings blacklist
	return DoesActorHaveSupportedClass(Actor)
			&& !Actor->IsTemplate()								// Should never happen, but we never want CDOs
			&& !Actor->HasAnyFlags(RF_Transient)				// Don't add transient actors in non-play worlds		
#if WITH_EDITOR
			&& Actor->IsEditable()
            && Actor->IsListedInSceneOutliner() 				// Only add actors that are allowed to be selected and drawn in editor
            && !FActorEditorUtils::IsABuilderBrush(Actor)		// Don't add the builder brush
#endif
        ;	
}

bool ULevelSnapshot::IsComponentDesirableForCapture(const UActorComponent* Component)
{
	// TODO: Create project settings blacklist
	// We only support native components or the ones added through the component list in Blueprints.
	return Component && DoesComponentHaveSupportedClass(Component) && (Component->CreationMethod == EComponentCreationMethod::Native || Component->CreationMethod == EComponentCreationMethod::SimpleConstructionScript);
}

void ULevelSnapshot::ApplySnapshotToWorld(UWorld* TargetWorld, ULevelSnapshotSelectionSet* SelectionSet)
{
	EnsureWorldInitialised();

#if WITH_EDITOR
	FScopedTransaction Transaction(FText::FromString("Loading Level Snapshot."));
#endif
	
	// Temporary fix until asset migration is implemented: simply use old system for old data.
	const bool bHasLegacyData = ActorSnapshots.Num() > 0;
	if (bHasLegacyData)
	{
		LegacyApplySnapshotToWorld(SelectionSet);
	}
	else
	{
		SerializedData.ApplyToWorld(TargetWorld, SelectionSet);
	}
}

void ULevelSnapshot::SnapshotWorld(UWorld* TargetWorld)
{
	if (!ensure(TargetWorld))
	{
		UE_LOG(LogLevelSnapshots, Warning, TEXT("Unable To Snapshot World as World was invalid"));
		return;
	}

	const bool bHasLegacyData = ActorSnapshots.Num() > 0;
	if (bHasLegacyData)
	{
		ActorSnapshots.Empty();
	}
	
	EnsureWorldInitialised();
	MapPath = TargetWorld;
	CaptureTime = FDateTime::UtcNow();
	SerializedData.SnapshotWorld(TargetWorld);
}

bool ULevelSnapshot::HasOriginalChangedSinceSnapshot(AActor* SnapshotActor, AActor* WorldActor) const
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("HasOriginalChangedSinceSnapshot"), STAT_AreAllSnapshotRelevantPropertiesIdentical, STATGROUP_LevelSnapshots);

	if (!IsValid(SnapshotActor) || !IsValid(WorldActor))
	{
		return SnapshotActor != WorldActor;
	}
		
	UClass* SnapshotClass = SnapshotActor->GetClass();
	UClass* WorldClass = WorldActor->GetClass();
	if (SnapshotClass != WorldClass)
	{
		return true;
	}

	TInlineComponentArray<TPair<UObject*, UObject*>> SnapshotOriginalPairsToProcess;
	SnapshotOriginalPairsToProcess.Add(TPair<UObject*, UObject*>(SnapshotActor, WorldActor));
	EnqueueMatchingComponents(SnapshotOriginalPairsToProcess, SnapshotActor, WorldActor);
	
	for (const TPair<UObject*, UObject*>& NextPair : SnapshotOriginalPairsToProcess)
	{
		UClass* ClassToIterate = NextPair.Key->GetClass();
		for (TFieldIterator<FProperty> FieldIt(ClassToIterate); FieldIt; ++FieldIt)
		{
			if (!AreSnapshotAndOriginalPropertiesEquivalent(*FieldIt, NextPair.Key, NextPair.Value, SnapshotActor, WorldActor))
			{
				return true;
			}
		}
	}
	return false;
}

bool ULevelSnapshot::AreSnapshotAndOriginalPropertiesEquivalent(const FProperty* Property, void* SnapshotContainer, void* WorldContainer, AActor* SnapshotActor, AActor* WorldActor) const
{
	// Deprecated and transient properties should not cause us to consider the property different because we do not save these properties.
	const uint64 UnsavedProperties = CPF_Deprecated | CPF_Transient;
	// We currently do not supported (instanced) subobjects
	const uint64 InstancedFlags = CPF_InstancedReference | CPF_ContainsInstancedReference | CPF_PersistentInstance;
	if (Property->HasAnyPropertyFlags(UnsavedProperties | InstancedFlags))
	{
		return true;
	}
	
	for (int32 i = 0; i < Property->ArrayDim; ++i)
	{
		void* SnapshotValuePtr = Property->ContainerPtrToValuePtr<void>(SnapshotContainer, i);
		void* WorldValuePtr = Property->ContainerPtrToValuePtr<void>(WorldContainer, i);
	
		if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			return ShouldConsiderObjectsEquivalent(SerializedData, ObjectProperty, SnapshotValuePtr, WorldValuePtr, SnapshotActor, WorldActor);
		}

		if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			FScriptArrayHelper SnapshotArray(ArrayProperty, SnapshotValuePtr);
			FScriptArrayHelper WorldArray(ArrayProperty, WorldValuePtr);
			if (SnapshotArray.Num() != WorldArray.Num())
			{
				return false;
			}

			for (int32 j = 0; j < SnapshotArray.Num(); ++j)
			{
				void* SnapshotElementValuePtr = SnapshotArray.GetRawPtr(j);
				void* WorldElementValuePtr = WorldArray.GetRawPtr(j);
			
				if (!AreSnapshotAndOriginalPropertiesEquivalent(ArrayProperty->Inner, SnapshotElementValuePtr, WorldElementValuePtr, SnapshotActor, WorldActor))
				{
					return false;
				}
			}
			return true;
		}
		
		if (const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
		{
			// TODO:
		}

		if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
		{
			// TODO:
		}
		
		if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
		{
			return FPropertyInfoHelpers::AreNumericPropertiesNearlyEqual(NumericProperty, SnapshotValuePtr, WorldValuePtr);
		}
		
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			for (TFieldIterator<FProperty> FieldIt(StructProperty->Struct); FieldIt; ++FieldIt)
			{
				if (!AreSnapshotAndOriginalPropertiesEquivalent(*FieldIt, SnapshotValuePtr, WorldValuePtr, SnapshotActor, WorldActor))
				{
					return false;
				}
			}
			return true;
		}

		// Use normal property comparison for all other properties
		if (!Property->Identical_InContainer(SnapshotContainer, WorldContainer, i, PPF_DeepComparison | PPF_DeepCompareDSOsOnly))
		{
			return false;
		}
	}
	return true;
}

TOptional<AActor*> ULevelSnapshot::GetDeserializedActor(const FSoftObjectPath& OriginalActorPath)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("GetDeserializedActor"), STAT_GetDeserializedActor, STATGROUP_LevelSnapshots);
	EnsureWorldInitialised();

	// Temporary fix until asset migration is implemented: simply use old system for old data.
	const bool bHasLegacyData = ActorSnapshots.Num() > 0;
	if (bHasLegacyData)
	{
		FLevelSnapshot_Actor* LegacySnapshotData = ActorSnapshots.Find(OriginalActorPath);
		return LegacySnapshotData ? LegacySnapshotData->GetDeserializedActor(TempActorWorld->GetWorld()) : TOptional<AActor*>();
	}
	else
	{
		return SerializedData.GetDeserializedActor(OriginalActorPath);
	}
}

void ULevelSnapshot::ForEachOriginalActor(TFunction<void(const FSoftObjectPath& ActorPath)> HandleOriginalActorPath) const
{
	const bool bHasLegacyData = ActorSnapshots.Num() > 0;
	if (bHasLegacyData)
	{
		for (auto ActorIt = ActorSnapshots.CreateConstIterator(); ActorIt; ++ActorIt)
		{
			HandleOriginalActorPath(ActorIt->Key);
		}
	}
	else
	{
		SerializedData.ForEachOriginalActor([&HandleOriginalActorPath](const FSoftObjectPath& OriginalActorPath)
        {
            HandleOriginalActorPath(OriginalActorPath);
        });
	}
}

void ULevelSnapshot::SetSnapshotName(const FName& InSnapshotName)
{
	SnapshotName = InSnapshotName;
}

void ULevelSnapshot::SetSnapshotDescription(const FString& InSnapshotDescription)
{
	SnapshotDescription = InSnapshotDescription;
}

FDateTime ULevelSnapshot::GetCaptureTime() const
{
	return CaptureTime;
}

FName ULevelSnapshot::GetSnapshotName() const
{
	return SnapshotName;
}

FString ULevelSnapshot::GetSnapshotDescription() const
{
	return SnapshotDescription;
}

void ULevelSnapshot::BeginDestroy()
{
	if (OnCleanWorldHandle.IsValid())
	{
		DestroyWorld();
	}
	
	Super::BeginDestroy();
}

void ULevelSnapshot::EnsureWorldInitialised()
{
	if (!TempActorWorld.IsValid())
	{
		FPreviewScene::ConstructionValues WorldConfig = FPreviewScene::ConstructionValues()
            .SetEditor(true)
            .AllowAudioPlayback(false)
            .SetCreatePhysicsScene(false)
            .ShouldSimulatePhysics(false)
            .SetCreateDefaultLighting(false);
		TempActorWorld = MakeShared<FPreviewScene>(WorldConfig);

		SerializedData.OnCreateSnapshotWorld(TempActorWorld->GetWorld());

		OnCleanWorldHandle = FWorldDelegates::OnWorldCleanup.AddLambda([WeakThis = TWeakObjectPtr<ULevelSnapshot>(this)](UWorld* World, bool bSessionEnded, bool bCleanResources)
		{
			// WeakThis is not valid when you fully close down the engine. BeginDestroy is called after OnWorldCleanup.
			if (WeakThis.IsValid() && bCleanResources)
			{
				WeakThis->DestroyWorld();
			}
		});
	}
}

void ULevelSnapshot::DestroyWorld()
{
	FWorldDelegates::OnWorldCleanup.Remove(OnCleanWorldHandle);
	OnCleanWorldHandle.Reset();
				
	SerializedData.OnDestroySnapshotWorld();
	TempActorWorld.Reset();
}

void ULevelSnapshot::LegacyApplySnapshotToWorld(ULevelSnapshotSelectionSet* SelectionSet)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ApplySnapshotToWorld"), STAT_ApplySnapshotToWorld, STATGROUP_LevelSnapshots);

	TSet<AActor*> EvaluatedActors;
	for (const FSoftObjectPath& Path : SelectionSet->GetSelectedWorldObjectPaths())
	{
		if (Path.IsValid())
		{
			AActor* ActorToPass = nullptr;
				
			if (UObject* SelectedObject = Path.ResolveObject())
			{
				if (AActor* AsActor = Cast<AActor>(SelectedObject))
				{
					ActorToPass = AsActor;
				}
				else if (UActorComponent* AsComponent = Cast<UActorComponent>(SelectedObject))
				{
					if (AActor* OwningActor = AsComponent->GetOwner())
					{
						ActorToPass = OwningActor;
					}
				}
			}

			if (ensure(ActorToPass))
			{
				if (!EvaluatedActors.Contains(ActorToPass))
				{
					if (const FLevelSnapshot_Actor* ActorSnapshot = ActorSnapshots.Find(ActorToPass))
					{
						ActorSnapshot->DeserializeIntoWorldActor(ActorToPass, SelectionSet);
					}
						
					EvaluatedActors.Add(ActorToPass);
				}
			}
		}
	}
}
