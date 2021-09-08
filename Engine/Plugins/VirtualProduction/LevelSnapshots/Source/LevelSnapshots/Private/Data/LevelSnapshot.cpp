// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/LevelSnapshot.h"

#include "LevelSnapshotsLog.h"
#include "LevelSnapshotsModule.h"
#include "PropertyInfoHelpers.h"
#include "Restorability/PropertyComparisonParams.h"

#include "Algo/Accumulate.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "SnapshotRestorability.h"
#include "CustomSerialization/CustomObjectSerializationWrapper.h"
#include "CustomSerialization/CustomSerializationDataManager.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "UObject/TextProperty.h"
#include "UObject/UnrealType.h"
#if WITH_EDITOR && !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
#include "Logging/MessageLog.h"
#endif
#if WITH_EDITOR
#include "ScopedTransaction.h"
#endif

namespace
{
	/* If this function return false, the objects are not equivalent. If true, ignore the object references. */
	bool ShouldConsiderObjectsEquivalent(const FWorldSnapshotData& SnapshotData, const FObjectPropertyBase* ObjectProperty, void* SnapshotValuePtr, void* WorldValuePtr, AActor* SnapshotActor, AActor* WorldActor)
	{
		// Native identity check handles:
			// - external references, e.g. UMaterial in content browser
			// - soft object paths: if SnapshotValuePtr is a TSoftObjectPtr<AActor> property, it points to a world actor instead of to an equivalent snapshot actor
		if (ObjectProperty->Identical(SnapshotValuePtr, WorldValuePtr, 0))
		{
			return true;
		}
		
		UObject* SnapshotObject = ObjectProperty->GetObjectPropertyValue(SnapshotValuePtr);
		UObject* WorldObject = ObjectProperty->GetObjectPropertyValue(WorldValuePtr);
		return SnapshotData.AreReferencesEquivalent(SnapshotObject, WorldObject, SnapshotActor, WorldActor);
	}

	void EnqueueMatchingCustomSubobjects(TInlineComponentArray<TPair<UObject*, UObject*>>& SnapshotOriginalPairsToProcess, const FWorldSnapshotData& WorldData, UObject* SnapshotObject, UObject* WorldObject)
	{
		FCustomObjectSerializationWrapper::ForEachMatchingCustomSubobjectPair(WorldData, SnapshotObject, WorldObject, [&SnapshotOriginalPairsToProcess, &WorldData](UObject* SnapshotSubobject, UObject* WorldSubobject)
		{
			SnapshotOriginalPairsToProcess.Add(TPair<UObject*, UObject*>(SnapshotSubobject, WorldSubobject));
			EnqueueMatchingCustomSubobjects(SnapshotOriginalPairsToProcess, WorldData, SnapshotSubobject, WorldSubobject);
		});
	}
	
	void EnqueueMatchingComponents(TInlineComponentArray<TPair<UObject*, UObject*>>& SnapshotOriginalPairsToProcess, const FWorldSnapshotData& WorldData, AActor* SnapshotActor, AActor* WorldActor)
	{
		TInlineComponentArray<UActorComponent*> SnapshotComponents;
		TInlineComponentArray<UActorComponent*> WorldComponents;
		SnapshotActor->GetComponents(SnapshotComponents);
		WorldActor->GetComponents(WorldComponents);
		for (UActorComponent* WorldComponent : WorldComponents)
		{
			const bool bIsSupported = FSnapshotRestorability::IsComponentDesirableForCapture(WorldComponent);
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

			EnqueueMatchingCustomSubobjects(SnapshotOriginalPairsToProcess, WorldData, *SnapshotComponent, WorldComponent);
			SnapshotOriginalPairsToProcess.Add(TPair<UObject*, UObject*>(*SnapshotComponent, WorldComponent));
		}
	}
}

void ULevelSnapshot::ApplySnapshotToWorld(UWorld* TargetWorld, const FPropertySelectionMap& SelectionSet)
{
	SCOPED_SNAPSHOT_CORE_TRACE(ApplyToWorld);
	if (TargetWorld == nullptr)
	{
		return;
	}
	
	if (MapPath != FSoftObjectPath(TargetWorld))
	{
#if WITH_EDITOR && !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		if (TargetWorld->IsPlayInEditor())
		{
			FMessageLog("PIE").Warning(
				FText::Format(NSLOCTEXT("LevelSnapshots", "IncompatibleWorlds", "This snapshot was taken in world '%s' and cannot be applied to PIE world '%s'. Snapshots can only be applied to the world they were taken in."),
				FText::FromString(MapPath.ToString()),
				FText::FromString(TargetWorld->GetPathName())
				)
			);
		}
#endif
		UE_LOG(LogLevelSnapshots, Error, TEXT("This snapshot was taken for world '%s' and cannot be applied to world '%s': snapshots currently only support applying to the world they were taken in. "), *MapPath.ToString(), *TargetWorld->GetPathName());
		return;
	}

	UE_LOG(LogLevelSnapshots, Log, TEXT("Applying snapshot %s to world %s"), *GetPathName(), *TargetWorld->GetPathName());
	ON_SCOPE_EXIT
	{
		UE_LOG(LogLevelSnapshots, Log, TEXT("Finished applying snapshot"));
	};
	
	EnsureWorldInitialised();
	
#if WITH_EDITOR
	FScopedTransaction Transaction(FText::FromString("Loading Level Snapshot."));
#endif
	SerializedData.ApplyToWorld(TargetWorld, GetPackage(), SelectionSet);
}

bool ULevelSnapshot::SnapshotWorld(UWorld* TargetWorld)
{
	SCOPED_SNAPSHOT_CORE_TRACE(SnapshotWorld);
	
	if (!ensure(TargetWorld))
	{
		UE_LOG(LogLevelSnapshots, Warning, TEXT("Unable To Snapshot World as World was invalid"));
		return false;
	}

	if (TargetWorld->WorldType != EWorldType::Editor
		&& TargetWorld->WorldType != EWorldType::EditorPreview) // To suppor tests in editor preview maps
	{
#if WITH_EDITOR && !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		if (TargetWorld->IsPlayInEditor())
		{
			FMessageLog("PIE").Warning(
			NSLOCTEXT("LevelSnapshots", "IncompatibleWorlds", "Taking snapshots in PIE is an experimental feature. The snapshot will work in the same PIE session but may no longer work when you start a new PIE session.")
			);
		}
#endif
		UE_LOG(LogLevelSnapshots, Warning, TEXT("Level snapshots currently only support editors. Snapshots taken in other world types are experimental any may not function as expected."));
	}

	FLevelSnapshotsModule& Module = FLevelSnapshotsModule::GetInternalModuleInstance();
	if (!Module.CanTakeSnapshot({ this }))
	{
		return false;
	}
	Module.OnPreTakeSnapshot().Broadcast({this});

	EnsureWorldInitialised();
	MapPath = TargetWorld;
	CaptureTime = FDateTime::UtcNow();
	SerializedData.SnapshotWorld(TargetWorld);

	Module.OnPostTakeSnapshot().Broadcast({ this });

	return true;
}

bool ULevelSnapshot::HasOriginalChangedPropertiesSinceSnapshotWasTaken(AActor* SnapshotActor, AActor* WorldActor) const
{
	SCOPED_SNAPSHOT_CORE_TRACE(HasOriginalChangedSinceSnapshot);

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
	EnqueueMatchingCustomSubobjects(SnapshotOriginalPairsToProcess, SerializedData,SnapshotActor, WorldActor);
	EnqueueMatchingComponents(SnapshotOriginalPairsToProcess, SerializedData,SnapshotActor, WorldActor);

	FLevelSnapshotsModule& Module = FModuleManager::Get().GetModuleChecked<FLevelSnapshotsModule>("LevelSnapshots");
	for (const TPair<UObject*, UObject*>& NextPair : SnapshotOriginalPairsToProcess)
	{
		UClass* ClassToIterate = NextPair.Key->GetClass();
		UObject* const SnapshotObject = NextPair.Key;
		UObject* const WorldObject = NextPair.Value;
		
		const FPropertyComparerArray PropertyComparers = Module.GetPropertyComparerForClass(ClassToIterate);
		for (TFieldIterator<FProperty> FieldIt(ClassToIterate); FieldIt; ++FieldIt)
		{
			// Ask external modules about the property
			const FPropertyComparisonParams Params { WorldClass, *FieldIt, SnapshotObject, WorldObject, SnapshotObject, WorldObject, SnapshotActor, WorldActor} ;
			const IPropertyComparer::EPropertyComparison ComparisonResult = Module.ShouldConsiderPropertyEqual(PropertyComparers, Params);
			
			switch (ComparisonResult)
			{
			case IPropertyComparer::EPropertyComparison::TreatEqual:
				continue;
			case IPropertyComparer::EPropertyComparison::TreatUnequal:
				return true;
			default:
				if (!AreSnapshotAndOriginalPropertiesEquivalent(*FieldIt, SnapshotObject, WorldObject, SnapshotActor, WorldActor))
				{
					return true;
				}
			}
		}
	}
	return false;
}

bool ULevelSnapshot::AreSnapshotAndOriginalPropertiesEquivalent(const FProperty* LeafProperty, void* SnapshotContainer, void* WorldContainer, AActor* SnapshotActor, AActor* WorldActor) const
{
	// Ensure that property's flags are allowed. Skip check collection properties, e.g. FArrayProperty::Inner, etc.: inner properties do not have the same flags.
	const bool bIsInCollection = FPropertyInfoHelpers::IsPropertyInCollection(LeafProperty);
	if (!bIsInCollection && !FSnapshotRestorability::IsRestorableProperty(LeafProperty))
	{
		return true;
	}
	
	for (int32 i = 0; i < LeafProperty->ArrayDim; ++i)
	{
		void* SnapshotValuePtr = LeafProperty->ContainerPtrToValuePtr<void>(SnapshotContainer, i);
		void* WorldValuePtr = LeafProperty->ContainerPtrToValuePtr<void>(WorldContainer, i);

		// Check whether float is nearly equal instead of exactly equal
		if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(LeafProperty))
		{
			return FPropertyInfoHelpers::AreNumericPropertiesNearlyEqual(NumericProperty, SnapshotValuePtr, WorldValuePtr);
		}
		
		// Use our custom equality function for struct properties
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(LeafProperty))
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

		// Check whether property value points to a subobject
		if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(LeafProperty))
		{
			return ShouldConsiderObjectsEquivalent(SerializedData, ObjectProperty, SnapshotValuePtr, WorldValuePtr, SnapshotActor, WorldActor);
		}


		// Use our custom equality function for array, set, and map inner properties
		if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(LeafProperty))
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
		
		if (const FMapProperty* MapProperty = CastField<FMapProperty>(LeafProperty))
		{
			// TODO: Use custom function. Need to do something similar to UE4MapProperty_Private::IsPermutation
		}

		if (const FSetProperty* SetProperty = CastField<FSetProperty>(LeafProperty))
		{
			// TODO: Use custom function. Need to do something similar to UE4SetProperty_Private::IsPermutation
		}

		if (const FTextProperty* TextProperty = CastField<FTextProperty>(LeafProperty))
		{
			const FText& SnapshotText = TextProperty->GetPropertyValue_InContainer(SnapshotContainer);
			const FText& WorldText = TextProperty->GetPropertyValue_InContainer(WorldContainer);
			return SnapshotText.IdenticalTo(WorldText, ETextIdenticalModeFlags::None) || SnapshotText.ToString().Equals(WorldText.ToString());
		}
		
		// Use normal property comparison for all other properties
		if (!LeafProperty->Identical_InContainer(SnapshotContainer, WorldContainer, i, PPF_DeepComparison | PPF_DeepCompareDSOsOnly))
		{
			return false;
		}
	}
	return true;
}

TOptional<AActor*> ULevelSnapshot::GetDeserializedActor(const FSoftObjectPath& OriginalActorPath)
{
	SCOPED_SNAPSHOT_CORE_TRACE(GetDeserializedActor);
	
	EnsureWorldInitialised();
	return SerializedData.GetDeserializedActor(OriginalActorPath, GetPackage());
}

int32 ULevelSnapshot::GetNumSavedActors() const
{
	return SerializedData.GetNumSavedActors();
}

namespace
{
	FSoftObjectPath ExtractPathWithoutSubobjects(UObject* Object)
	{
		int32 ColonIndex;
		const FString Path = Object->GetPathName();
		Path.FindChar(':', ColonIndex);
		return Path.Left(ColonIndex);
	}
}

void ULevelSnapshot::DiffWorld(UWorld* World, FActorPathConsumer HandleMatchedActor, FActorPathConsumer HandleRemovedActor, FActorConsumer HandleAddedActor) const
{
	SCOPED_SNAPSHOT_CORE_TRACE(DiffWorld);
	
	if (!ensure(World && HandleMatchedActor.IsBound() && HandleRemovedActor.IsBound() && HandleAddedActor.IsBound()))
	{
		return;
	}
	UE_LOG(LogLevelSnapshots, Log, TEXT("Diffing snapshot %s in world %s"), *GetPathName(), *World->GetPathName());
	ON_SCOPE_EXIT
	{
		UE_LOG(LogLevelSnapshots, Log, TEXT("Finished diffing snapshot"));
	};


	// Find actors that are not present in the snapshot
	TSet<AActor*> AllActors;
	TSet<FSoftObjectPath> LoadedLevels;
	{
		SCOPED_SNAPSHOT_CORE_TRACE(DiffWorld_FindAllActors);

		const int32 NumActorsInWorld = Algo::Accumulate(World->GetLevels(), 0, [](int64 Size, const ULevel* Level){ return Size + Level->Actors.Num(); });
		AllActors.Reserve(NumActorsInWorld);
		for (ULevel* Level : World->GetLevels())
		{
			LoadedLevels.Add(ExtractPathWithoutSubobjects(Level));
			
			for (AActor* ActorInLevel : Level->Actors)
			{
				AllActors.Add(ActorInLevel);
			
				// Warning: ActorInLevel can be null, e.g. when an actor was just removed from the world (and still in undo buffer)
				if (IsValid(ActorInLevel) && !SerializedData.HasMatchingSavedActor(ActorInLevel) && FSnapshotRestorability::ShouldConsiderNewActorForRemoval(ActorInLevel))
				{
					HandleAddedActor.Execute(ActorInLevel);
				}
			}
		}
	}
	


	// Try to find world actors and call appropriate callback
	{
		SCOPED_SNAPSHOT_CORE_TRACE(DiffWorld_IteratorAllActors);
		
		SerializedData.ForEachOriginalActor([&HandleMatchedActor, &HandleRemovedActor, &HandleAddedActor, &AllActors, &LoadedLevels](const FSoftObjectPath& OriginalActorPath, const FActorSnapshotData& SavedData)
		{
			// TODO: we need to check whether the actor's class was blacklisted in the project settings
			const FSoftObjectPath LevelPath = OriginalActorPath.GetAssetPathString();
			if (!LoadedLevels.Contains(LevelPath))
			{
				UE_LOG(LogLevelSnapshots, Warning, TEXT("Skipping actor %s because level %s is not loaded or does not exist (see Levels window)."), *OriginalActorPath.ToString(), *LevelPath.ToString());
				return;
			}
			
			UObject* ResolvedActor = OriginalActorPath.ResolveObject();
			// OriginalActorPath may still resolve to a live actor if it was just removed. We need to check the ULevel::Actors to see whether it was removed.
			const bool bWasRemovedFromWorld = ResolvedActor == nullptr || !AllActors.Contains(Cast<AActor>(ResolvedActor));

			// We do not need to call IsActorDesirableForCapture: it was already called when we took this snapshot
			if (bWasRemovedFromWorld)
			{
				HandleRemovedActor.Execute(OriginalActorPath);
				return;
			}

			UClass* ActorClass = SavedData.GetActorClass().TryLoadClass<AActor>();
			if (!ActorClass)
			{
				UE_LOG(LogLevel, Warning, TEXT("Cannot find class %s. Saved actor %s will not be restored."), *SavedData.GetActorClass().ToString(), *OriginalActorPath.ToString());
				return;
			}

			// Possible scenario: Right-click actor > Replace Selected Actors with; deletes the original and replaces it with new actor.
			if (ResolvedActor->GetClass() != ActorClass)
			{
				HandleRemovedActor.Execute(OriginalActorPath);
				HandleAddedActor.Execute(Cast<AActor>(ResolvedActor));
			}
			else
			{
				HandleMatchedActor.Execute(OriginalActorPath);
			}
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

void ULevelSnapshot::BeginDestroy()
{
	if (SnapshotContainerWorld)
	{
		DestroyWorld();
	}
	
	Super::BeginDestroy();
}

void ULevelSnapshot::EnsureWorldInitialised()
{
	if (SnapshotContainerWorld == nullptr)
	{
		SnapshotContainerWorld = NewObject<UWorld>(GetTransientPackage(), NAME_None);
		SnapshotContainerWorld->WorldType = EWorldType::EditorPreview;

		// Note: Do NOT create a FWorldContext for this world.
		// If you do, the render thread will send render commands every tick (and crash cuz we do not init the scene below).
		SnapshotContainerWorld->InitializeNewWorld(UWorld::InitializationValues()
			.InitializeScenes(false)		// This is memory only world: no rendering
            .AllowAudioPlayback(false)
            .RequiresHitProxies(false)		
            .CreatePhysicsScene(false)
            .CreateNavigation(false)
            .CreateAISystem(false)
            .ShouldSimulatePhysics(false)
			.EnableTraceCollision(false)
            .SetTransactional(false)
            .CreateFXSystem(false)
            );

		// Destroy our temporary world when the editor (or game) world is destroyed. Reasons:
		// 1. After unloading a map checks for world GC leaks; it would fatally crash if we did not clear here.
		// 2. Our temp map stores a "copy" of actors from the original world: the original world is no longer relevant, so neither is our temp world.
		if (ensure(GEngine))
		{
			OnWorldDestroyed = GEngine->OnWorldDestroyed().AddLambda([WeakThis = TWeakObjectPtr<ULevelSnapshot>(this)](UWorld* WorldBeingDestroyed)
	        {
				const bool bIsEditorOrGameMap = WorldBeingDestroyed->WorldType == EWorldType::Editor || WorldBeingDestroyed->WorldType == EWorldType::Game;
	            if (ensureAlways(WeakThis.IsValid()) && bIsEditorOrGameMap)
	            {
	                WeakThis->DestroyWorld();
	            }
	        });
		}
		
		SerializedData.OnCreateSnapshotWorld(SnapshotContainerWorld);
	}
}

void ULevelSnapshot::DestroyWorld()
{
	if (ensureAlwaysMsgf(SnapshotContainerWorld, TEXT("World was already destroyed.")))
	{
		if (ensure(GEngine))
		{
			GEngine->OnWorldDestroyed().Remove(OnWorldDestroyed);
			OnWorldDestroyed.Reset();
		}
				
		SerializedData.OnDestroySnapshotWorld();
	
		SnapshotContainerWorld->CleanupWorld();
		SnapshotContainerWorld = nullptr;
	}
}
