// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/WorldSnapshotData.h"

#include "Archive/TakeClassDefaultObjectSnapshotArchive.h"
#include "Archive/TakeWorldObjectSnapshotArchive.h"
#include "LevelSnapshotsLog.h"
#include "LevelSnapshotsStats.h"
#include "PropertySelectionMap.h"
#include "Restorability/SnapshotRestorability.h"

#include "EngineUtils.h"
#if WITH_EDITOR
#include "Editor/UnrealEdEngine.h"
#include "Internationalization/Internationalization.h"
#include "Misc/ScopedSlowTask.h"
#include "UnrealEdGlobals.h"
#endif

#define LOCTEXT_NAMESPACE "LevelSnapshotsEditor"

namespace
{
	/* If Path contains a path to an actor, returns that actor.
	 * Example: /Game/MapName.MapName:PersistentLevel.StaticMeshActor_42.StaticMeshComponent returns StaticMeshActor_42's data
	 */
	TOptional<FActorSnapshotData*> FindSavedActorDataUsingObjectPath(TMap<FSoftObjectPath, FActorSnapshotData>& ActorData, const FSoftObjectPath& OriginalObjectPath, bool& bIsPathToActorSubobject)
	{
		const static FString PersistentLevelString("PersistentLevel.");
		const int32 PersistentLevelStringLength = PersistentLevelString.Len();
		const FString& SubPathString = OriginalObjectPath.GetSubPathString();
		const int32 IndexOfPersistentLevelInfo = SubPathString.Find(PersistentLevelString, ESearchCase::CaseSensitive);
		if (IndexOfPersistentLevelInfo == INDEX_NONE)
		{
			return {};
		}

		const int32 DotAfterActorNameIndex = SubPathString.Find(TEXT("."), ESearchCase::IgnoreCase, ESearchDir::FromStart, IndexOfPersistentLevelInfo + PersistentLevelStringLength);
		const bool bPathIsToActor = DotAfterActorNameIndex == INDEX_NONE;
		if (bPathIsToActor)
		{
			// Example /Game/MapName.MapName:PersistentLevel.StaticMeshActor_42
			bIsPathToActorSubobject = false;
			
			FActorSnapshotData* Result = ActorData.Find(OriginalObjectPath);
			UE_CLOG(Result == nullptr, LogLevelSnapshots, Warning, TEXT("Path %s looks like an actor path but no data was saved for it. Maybe it was a reference to an auto-generated actor, e.g. a brush or volume present in all worlds by default?"), *OriginalObjectPath.ToString());
			return Result ? Result : TOptional<FActorSnapshotData*>();
		}
		else
		{
			// Example /Game/MapName.MapName:PersistentLevel.StaticMeshActor_42.StaticMeshComponent
			bIsPathToActorSubobject = true;
			// Converts it to /Game/MapName.MapName:PersistentLevel.StaticMeshActor_42
			const FSoftObjectPath PathToActor(OriginalObjectPath.GetAssetPathName(), SubPathString.Left(DotAfterActorNameIndex));
			
			FActorSnapshotData* Result = ActorData.Find(PathToActor);
			UE_CLOG(Result == nullptr, LogLevelSnapshots, Warning, TEXT("Path %s looks like an actor path but no data was saved for it. Maybe it was a reference to an auto-generated actor, e.g. a brush or volume present in all worlds by default?"), *OriginalObjectPath.ToString());
			return Result ? Result : TOptional<FActorSnapshotData*>();  
		}
	}
}

void FWorldSnapshotData::OnCreateSnapshotWorld(UWorld* NewTempActorWorld)
{
	TempActorWorld = NewTempActorWorld;
}

void FWorldSnapshotData::OnDestroySnapshotWorld()
{
	TempActorWorld.Reset();
}

void FWorldSnapshotData::SnapshotWorld(UWorld* World)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("SnapshotWorld"), STAT_SnapshotWorld, STATGROUP_LevelSnapshots);

	ClassDefaults.Empty();
	ActorData.Empty();
	SerializedNames.Empty();
	SerializedObjectReferences.Empty();
	Subobjects.Empty();

#if WITH_EDITOR
	FScopedSlowTask TakeSnapshotTask(World->GetProgressDenominator(), LOCTEXT("TakeSnapshotKey", "Take snapshot"));
	TakeSnapshotTask.MakeDialogDelayed(1.f);
#endif
	
	for (TActorIterator<AActor> It(World, AActor::StaticClass(), EActorIteratorFlags::SkipPendingKill); It; ++It)
	{
#if WITH_EDITOR
		TakeSnapshotTask.EnterProgressFrame();
#endif
		
		AActor* Actor = *It; 
		if (FSnapshotRestorability::IsActorDesirableForCapture(Actor))
		{
			ActorData.Add(Actor, FActorSnapshotData::SnapshotActor(Actor, *this));
		}
	}
}

void FWorldSnapshotData::ApplyToWorld(UWorld* WorldToApplyTo, const FPropertySelectionMap& PropertiesToSerialize)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ApplySnapshotToWorld"), STAT_ApplySnapshotToWorld, STATGROUP_LevelSnapshots);
	if (WorldToApplyTo == nullptr)
	{
		return;
	}

	const TArray<FSoftObjectPath> SelectedPaths = PropertiesToSerialize.GetKeys();
#if WITH_EDITOR
	const int32 NumActorsToRecreate = PropertiesToSerialize.GetDeletedActorsToRespawn().Num();
	const int32 NumMatchingActors = SelectedPaths.Num();
	FScopedSlowTask ApplyToWorldTask(NumActorsToRecreate + NumMatchingActors, LOCTEXT("ApplyToWorldKey", "Apply to world"));
	ApplyToWorldTask.MakeDialogDelayed(1.f, true);
#endif
	

	ApplyToWorld_HandleRemovingActors(PropertiesToSerialize);

	
	TSet<AActor*> EvaluatedActors;
#if WITH_EDITOR
	ApplyToWorldTask.EnterProgressFrame(NumActorsToRecreate);
#endif
	ApplyToWorld_HandleRecreatingActors(EvaluatedActors, PropertiesToSerialize);	


#if WITH_EDITOR
	ApplyToWorldTask.EnterProgressFrame(NumMatchingActors);
#endif
	ApplyToWorld_HandleSerializingMatchingActors(EvaluatedActors, SelectedPaths, PropertiesToSerialize);


	
	// If we're in the editor then update the gizmos locations as they can get out of sync if any of the deserialized actors were selected
#if WITH_EDITOR
	if (GUnrealEd)
	{
		GUnrealEd->UpdatePivotLocationForSelection();
	}
#endif
}

int32 FWorldSnapshotData::GetNumSavedActors() const
{
	return ActorData.Num();
}

void FWorldSnapshotData::ForEachOriginalActor(TFunction<void(const FSoftObjectPath& ActorPath)> HandleOriginalActorPath) const
{
	for (auto OriginalPathIt = ActorData.CreateConstIterator(); OriginalPathIt; ++OriginalPathIt)
	{
		HandleOriginalActorPath(OriginalPathIt->Key);
	}	
}

bool FWorldSnapshotData::HasMatchingSavedActor(const FSoftObjectPath& OriginalObjectPath) const
{
	return ActorData.Contains(OriginalObjectPath);
}

TOptional<AActor*> FWorldSnapshotData::GetDeserializedActor(const FSoftObjectPath& OriginalObjectPath)
{
	FActorSnapshotData* SerializedActor = ActorData.Find(OriginalObjectPath);
	if (SerializedActor)
	{
		return SerializedActor->GetDeserialized(TempActorWorld->GetWorld(), *this);
	}

	UE_LOG(LogLevelSnapshots, Warning, TEXT("No save data found for actor %s"), *OriginalObjectPath.ToString());
	return {};
}

bool FWorldSnapshotData::AreReferencesEquivalent(UObject* SnapshotVersion, UObject* OriginalVersion) const
{
	if (SnapshotVersion == nullptr || OriginalVersion == nullptr)
	{
		return SnapshotVersion == OriginalVersion;
	}

	if (SnapshotVersion->GetClass() != OriginalVersion->GetClass())
	{
		return false;
	}

	if (AActor* OriginalActorReference = Cast<AActor>(OriginalVersion))
	{
		const FActorSnapshotData* SavedData = ActorData.Find(OriginalActorReference);
		if (SavedData == nullptr)
		{
			return false;
		}

		// The snapshot actor was already allocated, if some other snapshot actor is referencing it
		const TOptional<AActor*> PreallocatedSnapshotVersion = SavedData->GetPreallocatedIfValidButDoNotAllocate();
		return PreallocatedSnapshotVersion.IsSet() && PreallocatedSnapshotVersion.GetValue() == SnapshotVersion;
	}

	// TODO: We can theoretically handle when OriginalVersion is a component reference
	// TODO: Handle subobjects here when subobject support is implemented
	return false;
}

void FWorldSnapshotData::ApplyToWorld_HandleRemovingActors(const FPropertySelectionMap& PropertiesToSerialize)
{
	for (const TWeakObjectPtr<AActor>& ActorToDespawn: PropertiesToSerialize.GetNewActorsToDespawn())
	{
		if (ensureMsgf(ActorToDespawn.IsValid(), TEXT("Actor became invalid since selection set was created")))
		{
			ActorToDespawn->Destroy(true, true);
		}
	}
}

void FWorldSnapshotData::ApplyToWorld_HandleRecreatingActors(TSet<AActor*>& EvaluatedActors, const FPropertySelectionMap& PropertiesToSerialize)
{
#if WITH_EDITOR
	FScopedSlowTask RecreateActors(PropertiesToSerialize.GetDeletedActorsToRespawn().Num(), LOCTEXT("ApplyToWorld.RecreateActorsKey", "Re-creating actors"));
	RecreateActors.MakeDialogDelayed(1.f, false);
#endif
			
	TMap<FSoftObjectPath, AActor*> RecreatedActors;
	// 1st pass: allocate the actors. Serialisation is done in separate step so object references to other deleted actors resolve correctly.
	for (const FSoftObjectPath& OriginalRemovedActorPath : PropertiesToSerialize.GetDeletedActorsToRespawn())
	{
		// Not checking this will cause a fatal crash when spawning the actor since the object name is already taken
		if (ensureAlwaysMsgf(!OriginalRemovedActorPath.ResolveObject(), TEXT("Object '%s' was marked to be restored but it already exists in the world.")))
		{
			continue;
		}
		FActorSnapshotData* ActorSnapshot = ActorData.Find(OriginalRemovedActorPath);
		if (!ensure(ActorSnapshot))
		{
			continue;	
		}
		UClass* ActorClass = ActorSnapshot->GetActorClass().ResolveClass();
		if (ensure(ActorClass))
		{
			UE_LOG(LogLevelSnapshots, Warning, TEXT("Failed to resolve class '%s'. Was it removed?"), *ActorSnapshot->GetActorClass().ToString());
			continue;
		}
		
		// Example: /Game/MapName.MapName:PersistentLevel.StaticMeshActor_42.StaticMeshComponent becomes /Game/MapName.MapName
		const FSoftObjectPath PathToOwningWorldAsset = OriginalRemovedActorPath.GetAssetPathString();
		UObject* UncastWorld = PathToOwningWorldAsset.ResolveObject();
		if (!UncastWorld)
		{
			UncastWorld = PathToOwningWorldAsset.TryLoad();
		}
		if (!UncastWorld)
		{
			UE_LOG(LogLevelSnapshots, Error, TEXT("Failed to resolve world '%s'. The world should be loaded?"), *PathToOwningWorldAsset.ToString());
			continue;
		}

		// Each Level in UWorld::Levels has a corresponding UWorld associated with it in which we re-create the actor.
		if (UWorld* OwningLevelWorld = ExactCast<UWorld>(UncastWorld))
		{
			const FString& SubObjectPath = OriginalRemovedActorPath.GetSubPathString();
			const int32 LastDotIndex = SubObjectPath.Find(TEXT("."));
			// Full string: /Game/MapName.MapName:PersistentLevel.StaticMeshActor_42 SubPath: PersistentLevel.StaticMeshActor_42 
			checkf(LastDotIndex != INDEX_NONE, TEXT("There should always be at least one dot after PersistentLevel"));
			
			const int32 NameLength = SubObjectPath.Len() - LastDotIndex;
			const FString ActorName = SubObjectPath.Right(NameLength);
			
			FActorSpawnParameters SpawnParameters;
			SpawnParameters.Name = FName(*ActorName);
			SpawnParameters.bNoFail = true;
			SpawnParameters.Template = Cast<AActor>(GetClassDefault(ActorClass));
			OwningLevelWorld->SpawnActor(ActorClass, nullptr, SpawnParameters);
		}
	}
	
	// 2nd pass: serialize. 
	for (const FSoftObjectPath& OriginalRemovedActorPath : PropertiesToSerialize.GetDeletedActorsToRespawn())
	{
#if WITH_EDITOR
		RecreateActors.EnterProgressFrame();
#endif
		if (FActorSnapshotData* ActorSnapshot = ActorData.Find(OriginalRemovedActorPath))
		{
			AActor* RecreatedActor = *RecreatedActors.Find(OriginalRemovedActorPath);
			// Otherwise we'll serialize it again when we look for world actors matching the snapshot
			EvaluatedActors.Add(RecreatedActor);
			ActorSnapshot->DeserializeIntoRecreatedWorldActor(TempActorWorld.Get(), RecreatedActor, *this);
		}
	}
}

void FWorldSnapshotData::ApplyToWorld_HandleSerializingMatchingActors(TSet<AActor*>& EvaluatedActors, const TArray<FSoftObjectPath>& SelectedPaths, const FPropertySelectionMap& PropertiesToSerialize)
{
#if WITH_EDITOR
	FScopedSlowTask ExitingActorTask(SelectedPaths.Num(), LOCTEXT("ApplyToWorld.MatchingPropertiesKey", "Writing existing actors"));
	ExitingActorTask.MakeDialogDelayed(1.f, true);
#endif
	for (const FSoftObjectPath& SelectedObject : SelectedPaths)
	{
#if WITH_EDITOR
		ExitingActorTask.EnterProgressFrame();
		if (ExitingActorTask.ShouldCancel())
		{
			return;
		}
#endif
		
		if (SelectedObject.IsValid())
		{
			AActor* OriginalWorldActor = nullptr;
			UObject* ResolvedObject = SelectedObject.ResolveObject();
			if (AActor* AsActor = Cast<AActor>(ResolvedObject))
			{
				OriginalWorldActor = AsActor;
			}
			else if (UActorComponent* AsComponent = Cast<UActorComponent>(ResolvedObject))
			{
				if (AActor* OwningActor = AsComponent->GetOwner())
				{
					OriginalWorldActor = OwningActor;
				}
			}

			if (ensure(OriginalWorldActor))
			{
				if (!EvaluatedActors.Contains(OriginalWorldActor))
				{
					if (FActorSnapshotData* ActorSnapshot = ActorData.Find(OriginalWorldActor))
					{
						ActorSnapshot->DeserializeIntoExistingWorldActor(TempActorWorld.Get(), OriginalWorldActor, *this, PropertiesToSerialize);
					}
					
					EvaluatedActors.Add(OriginalWorldActor);
				}
			}
		}
	}
}

int32 FWorldSnapshotData::AddObjectDependency(UObject* ReferenceFromOriginalObject)
{
	return SerializedObjectReferences.AddUnique(ReferenceFromOriginalObject);
}

TOptional<UObject*> FWorldSnapshotData::ResolveObjectDependency(int32 ObjectPathIndex, EResolveType ResolveType)
{
	if (!ensure(SerializedObjectReferences.IsValidIndex(ObjectPathIndex)))
	{
		return {};
	}
	
	const FSoftObjectPath& OriginalObjectPath = SerializedObjectReferences[ObjectPathIndex];
	if (ResolveType == EResolveType::ResolveForUseInOriginalWorld)
	{
		UObject* Result = OriginalObjectPath.ResolveObject();
		return Result ? Result : TOptional<UObject*>();
	}
	checkf(ResolveType == EResolveType::ResolveForUseInTempWorld, TEXT("Did you add a new entry to EResolveType?"));

	bool bIsPathToActorSubobject;
	const TOptional<FActorSnapshotData*> SnapshotData = FindSavedActorDataUsingObjectPath(ActorData, OriginalObjectPath, bIsPathToActorSubobject); 
	const bool bIsExternalAssetReference = !SnapshotData.IsSet();
	if (bIsExternalAssetReference)
	{
		// We're dealing with an external (asset) reference, e.g. a UMaterial.
		UObject* Result = OriginalObjectPath.ResolveObject();
		return Result ? Result : TOptional<UObject*>();
	}

	if (bIsPathToActorSubobject)
	{
		UE_LOG(LogLevelSnapshots, Warning, TEXT("Skipping subobject reference '%s'. The snapshot saved the subobject for future compatibility but we currently do not support restoring it. The reference will be set to null."), *OriginalObjectPath.ToString());
		return {};
	}
	else
	{
		TOptional<AActor*> Result = SnapshotData.GetValue()->GetPreallocated(TempActorWorld->GetWorld(), *this);
		return Result ? TOptional<UObject*>(*Result) : TOptional<UObject*>();
	}
}

int32 FWorldSnapshotData::AddSubobjectDependency(UObject* ReferenceFromOriginalObject)
{
	const int32 Index = AddObjectDependency(ReferenceFromOriginalObject);
	const bool bSubobjectAlreadyRegistered = Subobjects.Contains(Index);
	if (!bSubobjectAlreadyRegistered)
	{
		FSubobjectSnapshotData& SubobjectData = Subobjects.Add(Index);
		UClass* Class = ReferenceFromOriginalObject->GetClass();

		SubobjectData.Class = Class;
		SubobjectData.OuterIndex = AddObjectDependency(ReferenceFromOriginalObject->GetOuter());

		FTakeWorldObjectSnapshotArchive Serializer = FTakeWorldObjectSnapshotArchive::MakeArchiveForSavingWorldObject(SubobjectData, *this, ReferenceFromOriginalObject);
		ReferenceFromOriginalObject->Serialize(Serializer);

		AddClassDefault(Class);
	}
	
	return Index;
}

void FWorldSnapshotData::AddClassDefault(UClass* Class)
{
	if (!ClassDefaults.Contains(Class))
	{
		FObjectSnapshotData& ClassData = ClassDefaults.Add(Class);
		FTakeClassDefaultObjectSnapshotArchive SaveClass = FTakeClassDefaultObjectSnapshotArchive::MakeArchiveForSavingClassDefaultObject(ClassData, *this);
		Class->GetDefaultObject()->Serialize(SaveClass);
	}
}

UObject* FWorldSnapshotData::GetClassDefault(UClass* Class)
{
	FClassDefaultObjectSnapshotData* ClassDefaultData = ClassDefaults.Find(Class);
	if (!ClassDefaultData)
	{
		UE_LOG(LogLevelSnapshots, Warning, TEXT("No saved CDO data available for class %s. Returning global CDO..."), *Class->GetName());
		return Class->GetDefaultObject();
	}

	if (IsValid(ClassDefaultData->CachedLoadedClassDefault))
	{
		return ClassDefaultData->CachedLoadedClassDefault;
	}
	
	UObject* Default = NewObject<UObject>(GetTransientPackage(), Class);
	FTakeClassDefaultObjectSnapshotArchive RestoreArchive = FTakeClassDefaultObjectSnapshotArchive::MakeArchiveForRestoringClassDefaultObject(*ClassDefaultData, *this);
	Default->SetFlags(RF_ArchetypeObject); // No direct reason for this though it acts as as archetype so we should probably mark it as such
	Default->Serialize(RestoreArchive);

	ClassDefaultData->CachedLoadedClassDefault = Default;
	return Default;
	
}

void FWorldSnapshotData::SerializeClassDefaultsInto(UObject* Object)
{
	FClassDefaultObjectSnapshotData* ClassDefaultData = ClassDefaults.Find(Object->GetClass());
	if (ClassDefaultData)
	{
		FTakeClassDefaultObjectSnapshotArchive RestoreArchive = FTakeClassDefaultObjectSnapshotArchive::MakeArchiveForRestoringClassDefaultObject(*ClassDefaultData, *this);
		Object->Serialize(RestoreArchive);
	}
	else
	{
		UE_LOG(LogLevelSnapshots, Warning,
			TEXT("No CDO saved for class '%s'. If you changed some class default values for this class, then the affected objects will have the latest values instead of the class defaults at the time the snapshot was taken. Should be nothing major to worry about."),
			*Object->GetClass()->GetName()
			);
	}
}

#undef LOCTEXT_NAMESPACE