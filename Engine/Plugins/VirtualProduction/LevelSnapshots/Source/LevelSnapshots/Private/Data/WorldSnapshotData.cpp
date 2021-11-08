// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/WorldSnapshotData.h"

#include "Archive/ClassDefaults/ApplyClassDefaulDataArchive.h"
#include "Archive/ClassDefaults/TakeClassDefaultObjectSnapshotArchive.h"
#include "LevelSnapshotsLog.h"
#include "PropertySelectionMap.h"
#include "Restorability/SnapshotRestorability.h"
#include "Util/SortedScopedLog.h"

#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "LevelSnapshotsModule.h"
#include "SnapshotConsoleVariables.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"
#include "Serialization/BufferArchive.h"
#include "Stats/StatsMisc.h"
#include "UObject/Package.h"
#include "Util/PropertyIterator.h"
#include "Util/SnapshotUtil.h"
#if WITH_EDITOR
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Editor/UnrealEdEngine.h"
#include "Engine/Selection.h"
#include "Internationalization/Internationalization.h"
#include "Misc/ScopedSlowTask.h"
#include "ScopedTransaction.h"
#include "UnrealEdGlobals.h"
#endif

#define LOCTEXT_NAMESPACE "LevelSnapshotsEditor"

void FWorldSnapshotData::OnCreateSnapshotWorld(UWorld* NewTempActorWorld)
{
	TempActorWorld = NewTempActorWorld;
}

void FWorldSnapshotData::OnDestroySnapshotWorld()
{
	TempActorWorld.Reset();

	for (auto ClassDefaultsIt = ClassDefaults.CreateIterator(); ClassDefaultsIt; ++ClassDefaultsIt)
	{
		ClassDefaultsIt->Value.CachedLoadedClassDefault = nullptr;
	}

	for (auto SubobjectIt = Subobjects.CreateIterator(); SubobjectIt; ++SubobjectIt)
	{
		SubobjectIt->Value.SnapshotObject.Reset();
		SubobjectIt->Value.EditorObject.Reset();
	}

	for (auto ActorDataIt = ActorData.CreateIterator(); ActorDataIt; ++ ActorDataIt)
	{
		ActorDataIt->Value.ResetTransientData();
	}
}

void FWorldSnapshotData::SnapshotWorld(UWorld* World)
{
	ClassDefaults.Empty();
	ActorData.Empty();
	SerializedNames.Empty();
	SerializedObjectReferences.Empty();
	Subobjects.Empty();
	CustomSubobjectSerializationData.Empty();
	NameToIndex.Empty();
	ReferenceToIndex.Empty();
	
	SnapshotVersionInfo.Initialize();

#if WITH_EDITOR
	FScopedSlowTask TakeSnapshotTask(World->GetProgressDenominator(), LOCTEXT("TakeSnapshotKey", "Take snapshot"));
	TakeSnapshotTask.MakeDialogDelayed(1.f);
#endif

	const bool bShouldLog = SnapshotCVars::CVarLogTimeTakingSnapshots.GetValueOnAnyThread();
	FConditionalSortedScopedLog SortedLog(bShouldLog);
	for (TActorIterator<AActor> It(World, AActor::StaticClass(), EActorIteratorFlags::SkipPendingKill); It; ++It)
	{
#if WITH_EDITOR
		TakeSnapshotTask.EnterProgressFrame();
#endif
		
		AActor* Actor = *It;
		if (FSnapshotRestorability::IsActorDesirableForCapture(Actor))
		{
			FScopedLogItem LogTakeSnapshot = SortedLog.AddScopedLogItem(Actor->GetName());
			ActorData.Add(Actor, FActorSnapshotData::SnapshotActor(Actor, *this));
		}
	}
}

void FWorldSnapshotData::ApplyToWorld(UWorld* WorldToApplyTo, UPackage* LocalisationSnapshotPackage, const FPropertySelectionMap& PropertiesToSerialize)
{
	// Certain custom compilers, such as nDisplay, may reset the transaction context. That would cause a crash.
	PreloadClassesForRestore(PropertiesToSerialize);
#if WITH_EDITOR
	FScopedTransaction Transaction(FText::FromString("Loading Level Snapshot."));
#endif
	
	if (WorldToApplyTo == nullptr)
	{
		return;
	}

	// Clear editor world subobject cache from previous ApplyToWorld
	for (auto SubobjectIt = Subobjects.CreateIterator(); SubobjectIt; ++SubobjectIt)
	{
		SubobjectIt->Value.EditorObject.Reset();
	}
	
	const TArray<FSoftObjectPath> SelectedPaths = PropertiesToSerialize.GetKeys();
#if WITH_EDITOR
	const int32 NumActorsToRecreate = PropertiesToSerialize.GetDeletedActorsToRespawn().Num();
	const int32 NumMatchingActors = SelectedPaths.Num();
	FScopedSlowTask ApplyToWorldTask(NumActorsToRecreate + NumMatchingActors, LOCTEXT("ApplyToWorldKey", "Apply to world"));
	ApplyToWorldTask.MakeDialogDelayed(1.f, true);
#endif
	

	ApplyToWorld_HandleRemovingActors(WorldToApplyTo, PropertiesToSerialize);

	
	TSet<AActor*> EvaluatedActors;
#if WITH_EDITOR
	ApplyToWorldTask.EnterProgressFrame(NumActorsToRecreate);
#endif
	ApplyToWorld_HandleRecreatingActors(EvaluatedActors, LocalisationSnapshotPackage, PropertiesToSerialize);	


#if WITH_EDITOR
	ApplyToWorldTask.EnterProgressFrame(NumMatchingActors);
#endif
	ApplyToWorld_HandleSerializingMatchingActors(EvaluatedActors, SelectedPaths, LocalisationSnapshotPackage, PropertiesToSerialize);


	
	// If we're in the editor then update the gizmos locations as they can get out of sync if any of the serialized actors were selected
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

void FWorldSnapshotData::ForEachOriginalActor(TFunction<void(const FSoftObjectPath&, const FActorSnapshotData&)> HandleOriginalActorPath) const
{
	for (auto OriginalPathIt = ActorData.CreateConstIterator(); OriginalPathIt; ++OriginalPathIt)
	{
		HandleOriginalActorPath(OriginalPathIt->Key, OriginalPathIt->Value);
	}	
}

bool FWorldSnapshotData::HasMatchingSavedActor(const FSoftObjectPath& OriginalObjectPath) const
{
	return ActorData.Contains(OriginalObjectPath);
}

FString FWorldSnapshotData::GetActorLabel(const FSoftObjectPath& OriginalObjectPath) const
{
#if WITH_EDITORONLY_DATA
	const FActorSnapshotData* SerializedActor = ActorData.Find(OriginalObjectPath);
	if (SerializedActor && !SerializedActor->ActorLabel.IsEmpty())
	{
		return SerializedActor->ActorLabel;
	}
#endif

	return SnapshotUtil::ExtractLastSubobjectName(OriginalObjectPath); 
}

namespace WorldSnapshotData
{
	static void ConditionallyRerunConstructionScript(FWorldSnapshotData& This, const TOptional<AActor*>& RequiredActor, const TArray<int32>& OriginalObjectDependencies, UPackage* LocalisationSnapshotPackage)
	{
		bool bHadActorDependencies = false;
		for (int32 OriginalObjectIndex : OriginalObjectDependencies)
		{
			const FSoftObjectPath& ObjectPath = This.SerializedObjectReferences[OriginalObjectIndex];
			if (This.ActorData.Contains(ObjectPath))
			{
				This.GetDeserializedActor(ObjectPath, LocalisationSnapshotPackage);
				bHadActorDependencies = true;
			}
		}
		
		if (bHadActorDependencies && ensure(RequiredActor))
		{
			RequiredActor.GetValue()->RerunConstructionScripts();
		}
	}
}

TOptional<AActor*> FWorldSnapshotData::GetDeserializedActor(const FSoftObjectPath& OriginalObjectPath, UPackage* LocalisationSnapshotPackage)
{
	FActorSnapshotData* SerializedActor = ActorData.Find(OriginalObjectPath);
	if (SerializedActor)
	{
		const bool bJustReceivedSerialisation = !SerializedActor->bReceivedSerialisation;
		const TOptional<AActor*> Result = SerializedActor->GetDeserialized(TempActorWorld->GetWorld(), *this, OriginalObjectPath, LocalisationSnapshotPackage);
		if (bJustReceivedSerialisation && ensure(SerializedActor->bReceivedSerialisation))
		{
			WorldSnapshotData::ConditionallyRerunConstructionScript(*this, Result, SerializedActor->ObjectDependencies, LocalisationSnapshotPackage);
		}
		return Result;
	}

	UE_LOG(LogLevelSnapshots, Warning, TEXT("No save data found for actor %s"), *OriginalObjectPath.ToString());
	return {};
}

TOptional<FObjectSnapshotData*> FWorldSnapshotData::GetSerializedClassDefaults(UClass* Class)
{
	FObjectSnapshotData* ClassDefaultData = ClassDefaults.Find(Class);
	return ClassDefaultData ? ClassDefaultData : TOptional<FObjectSnapshotData*>();
}

void FWorldSnapshotData::AddClassDefault(UClass* Class)
{
	if (!ensure(Class) || ClassDefaults.Contains(Class))
	{
		return;
	}

	UObject* ClassDefault = Class->GetDefaultObject();
	if (!ensure(ClassDefault))
	{
		return;
	}
	
	FClassDefaultObjectSnapshotData ClassData;
	ClassData.bSerializationSkippedCDO = FLevelSnapshotsModule::GetInternalModuleInstance().ShouldSkipClassDefaultSerialization(Class); 
	if (!ClassData.bSerializationSkippedCDO)	
	{
		FTakeClassDefaultObjectSnapshotArchive::SaveClassDefaultObject(ClassData, *this, ClassDefault);
	}
	
	// Copy in case AddClassDefault was called recursively, which may reallocate ClassDefaults.
	ClassDefaults.Emplace(Class, MoveTemp(ClassData));
}

UObject* FWorldSnapshotData::GetClassDefault(UClass* Class)
{
	FClassDefaultObjectSnapshotData* ClassDefaultData = ClassDefaults.Find(Class);
	if (!ClassDefaultData)
	{
		UE_LOG(LogLevelSnapshots, Warning, TEXT("No saved CDO data available for class %s. Returning global CDO..."), *Class->GetName());
		return Class->GetDefaultObject();
	}
	
	if (ClassDefaultData->bSerializationSkippedCDO)
	{
		return Class->GetDefaultObject();
	}

	if (IsValid(ClassDefaultData->CachedLoadedClassDefault))
	{
		return ClassDefaultData->CachedLoadedClassDefault;
	}
	
	UObject* CDO = NewObject<UObject>(
		GetTransientPackage(),
		Class,
		*FString("SnapshotCDO_").Append(*MakeUniqueObjectName(GetTransientPackage(), Class).ToString())
		);
	FApplyClassDefaulDataArchive::SerializeClassDefaultObject(*ClassDefaultData, *this, CDO);

	ClassDefaultData->CachedLoadedClassDefault = CDO;
	return CDO;
}

void FWorldSnapshotData::SerializeClassDefaultsInto(UObject* Object)
{
	FClassDefaultObjectSnapshotData* ClassDefaultData = ClassDefaults.Find(Object->GetClass());
	if (ClassDefaultData && !ClassDefaultData->bSerializationSkippedCDO && !FLevelSnapshotsModule::GetInternalModuleInstance().ShouldSkipClassDefaultSerialization(Object->GetClass()))
	{
		FApplyClassDefaulDataArchive::RestoreChangedClassDefaults(*ClassDefaultData, *this, Object);
	}

	UE_CLOG(ClassDefaultData == nullptr,
			LogLevelSnapshots, Warning,
			TEXT("No CDO saved for class '%s'. If you changed some class default values for this class, then the affected objects will have the latest values instead of the class defaults at the time the snapshot was taken. Should be nothing major to worry about."),
			*Object->GetClass()->GetName()
			);
}

const FSnapshotVersionInfo& FWorldSnapshotData::GetSnapshotVersionInfo() const
{
	return SnapshotVersionInfo;
}

bool FWorldSnapshotData::Serialize(FArchive& Ar)
{
	// When this struct is saved, the save algorithm collects references. It's faster if we just give it the info directly.
	if (Ar.IsObjectReferenceCollector())
	{
		CollectReferencesAndNames(Ar);
		return true;
	}
	
	return false;
}

void FWorldSnapshotData::PostSerialize(const FArchive& Ar)
{
	if (Ar.IsLoading() && !SnapshotVersionInfo.IsInitialized())
	{
		// Assets saved before we added version tracking need to receive versioning info of 4.27.
		// Skip snapshot version info because it did not exist yet at that time (you'll get migration bugs otherwise).
		const bool bWithoutSnapshotVersion = true;
		SnapshotVersionInfo.Initialize(bWithoutSnapshotVersion);
	}
}

void FWorldSnapshotData::CollectReferencesAndNames(FArchive& Ar)
{
	// References
	Ar << SerializedObjectReferences;
	CollectActorReferences(Ar);
	CollectClassDefaultReferences(Ar);

	// Names
	Ar << SerializedNames;
	Ar << SnapshotVersionInfo.CustomVersions;

	// They're required... these names are for some reason not discovered by default...
	TArray<FName> RequiredNames = { FName("UInt16Property"), EName::DoubleProperty};
	for (FName Name : RequiredNames)
	{
		Ar << Name;
	}

	auto ProcessProperty = [&Ar](const FProperty* Property)
	{
		FName PropertyName = Property->GetFName();
		Ar << PropertyName;
	};
	auto ProcessStruct = [&Ar](UStruct* Struct)
	{
		FName StructName = Struct->GetFName();
		Ar << StructName;
	};
	
	FPropertyIterator(StaticStruct(), ProcessProperty, ProcessStruct);
	FPropertyIterator(FSnapshotVersionInfo::StaticStruct(), ProcessProperty, ProcessStruct);
	FPropertyIterator(FActorSnapshotData::StaticStruct(), ProcessProperty, ProcessStruct);
	FPropertyIterator(FSubobjectSnapshotData::StaticStruct(), ProcessProperty, ProcessStruct);
	FPropertyIterator(FCustomSerializationData::StaticStruct(), ProcessProperty, ProcessStruct);
}

void FWorldSnapshotData::CollectActorReferences(FArchive& Ar)
{
	for (auto ActorIt = ActorData.CreateConstIterator(); ActorIt; ++ActorIt)
	{
		FSoftObjectPath SavedActorPath = ActorIt->Key; 
		Ar << SavedActorPath;

		FSoftClassPath ActorClass = ActorIt->Value.ActorClass;
		Ar << ActorClass;
	}
}

void FWorldSnapshotData::CollectClassDefaultReferences(FArchive& Ar)
{
	for (auto ClassDefaultIt = ClassDefaults.CreateConstIterator(); ClassDefaultIt; ++ClassDefaultIt)
	{
		FSoftClassPath Class = ClassDefaultIt->Key;
		Ar << Class;
	}
}

void FWorldSnapshotData::PreloadClassesForRestore(const FPropertySelectionMap& SelectionMap)
{
	// Class required for respawning
	for (const FSoftObjectPath& OriginalRemovedActorPath : SelectionMap.GetDeletedActorsToRespawn())
	{
		FActorSnapshotData* ActorSnapshot = ActorData.Find(OriginalRemovedActorPath);
		if (ensure(ActorSnapshot))
		{
			UClass* ActorClass = ActorSnapshot->GetActorClass().TryLoadClass<AActor>();
			UE_CLOG(ActorClass != nullptr, LogLevelSnapshots, Warning, TEXT("Failed to resolve class '%s'. Was it removed?"), *ActorSnapshot->GetActorClass().ToString());
		}
	}

	// Technically we also have to load all component classes... we can skip it for now because the only problematic compiler right now is the nDisplay one.
}

void FWorldSnapshotData::ApplyToWorld_HandleRemovingActors(UWorld* WorldToApplyTo, const FPropertySelectionMap& PropertiesToSerialize)
{
	SCOPED_SNAPSHOT_CORE_TRACE(ApplyToWorld_RemoveActors)
	
#if WITH_EDITOR
	const TSet<TWeakObjectPtr<AActor>>& ActorsToDespawn = PropertiesToSerialize.GetNewActorsToDespawn();
	const bool bShouldDespawnActors = ActorsToDespawn.Num() > 0;
	if (!bShouldDespawnActors || !ensure(GEditor))
	{
		return;
	}
	
	// Not sure whether needed. "DELETE" command does in UUnrealEdEngine::Exec_Actor ...
	FEditorDelegates::OnDeleteActorsBegin.Broadcast();

	// Avoid accidentally deleting other user selected actors
	GEditor->SelectNone(false, false, false);
	
	FLevelSnapshotsModule& Module = FLevelSnapshotsModule::GetInternalModuleInstance();
	USelection* EdSelectionManager = GEditor->GetSelectedActors();
	EdSelectionManager->BeginBatchSelectOperation();
	for (const TWeakObjectPtr<AActor>& ActorToDespawn: ActorsToDespawn)
	{
		if (ensureMsgf(ActorToDespawn.IsValid(), TEXT("Actor became invalid since selection set was created")))
		{
			EdSelectionManager->Modify();
			Module.OnPreRemoveActor(ActorToDespawn.Get());
			GEditor->SelectActor(ActorToDespawn.Get(), /*bSelect =*/true, /*bNotifyForActor =*/false, /*bSelectEvenIfHidden =*/true);
		}
	}
	EdSelectionManager->EndBatchSelectOperation();

	const bool bVerifyDeletionCanHappen = true;
	const bool bWarnAboutReferences = false;
	GEditor->edactDeleteSelected(WorldToApplyTo, bVerifyDeletionCanHappen, bWarnAboutReferences, bWarnAboutReferences);

	// ... and call the end event like in UUnrealEdEngine
	FEditorDelegates::OnDeleteActorsEnd.Broadcast();
#else
	// In non-editor builds delete the actors like gameplay code would:
	for (const TWeakObjectPtr<AActor>& ActorToDespawn: PropertiesToSerialize.GetNewActorsToDespawn())
	{
		if (ensureMsgf(ActorToDespawn.IsValid(), TEXT("Actor became invalid since selection set was created")))
		{
			ActorToDespawn->Destroy(true, true);
		}
	}
#endif
}

namespace WorldSnapshotData
{
	static void HandleNameClash(const FSoftObjectPath& OriginalRemovedActorPath)
	{
		UObject* FoundObject = FindObject<UObject>(nullptr, *OriginalRemovedActorPath.ToString());
		if (!FoundObject)
		{
			return;
		}

		// If it's not an actor then it's possibly an UObjectRedirector
		AActor* AsActor = Cast<AActor>(FoundObject);
		if (IsValid(AsActor))
		{
#if WITH_EDITOR
			GEditor->SelectActor(AsActor, /*bSelect =*/true, /*bNotifyForActor =*/false, /*bSelectEvenIfHidden =*/true);
			const bool bVerifyDeletionCanHappen = true;
			const bool bWarnAboutReferences = false;
			GEditor->edactDeleteSelected(AsActor->GetWorld(), bVerifyDeletionCanHappen, bWarnAboutReferences, bWarnAboutReferences);
#else
			AsActor->Destroy(true, true);
#endif
		}
		
		const FName NewName = MakeUniqueObjectName(FoundObject->GetOuter(), FoundObject->GetClass());
		FoundObject->Rename(*NewName.ToString(), nullptr, REN_NonTransactional | REN_DontCreateRedirectors);
	}
}

void FWorldSnapshotData::ApplyToWorld_HandleRecreatingActors(TSet<AActor*>& EvaluatedActors, UPackage* LocalisationSnapshotPackage, const FPropertySelectionMap& PropertiesToSerialize)
{
	SCOPED_SNAPSHOT_CORE_TRACE(ApplyToWorld_RecreateActors);
	
#if WITH_EDITOR
	FScopedSlowTask RecreateActors(PropertiesToSerialize.GetDeletedActorsToRespawn().Num(), LOCTEXT("ApplyToWorld.RecreateActorsKey", "Re-creating actors"));
	RecreateActors.MakeDialogDelayed(1.f, false);
#endif
	
	FLevelSnapshotsModule& Module = FLevelSnapshotsModule::GetInternalModuleInstance();
	TMap<FSoftObjectPath, AActor*> RecreatedActors;
	// 1st pass: allocate the actors. Serialisation is done in separate step so object references to other deleted actors resolve correctly.
	for (const FSoftObjectPath& OriginalRemovedActorPath : PropertiesToSerialize.GetDeletedActorsToRespawn())
	{
		FActorSnapshotData* ActorSnapshot = ActorData.Find(OriginalRemovedActorPath);
		if (!ensure(ActorSnapshot))
		{
			continue;	
		}

		UClass* ActorClass = ActorSnapshot->GetActorClass().TryLoadClass<AActor>();
		if (!ActorClass)
		{
			UE_LOG(LogLevelSnapshots, Warning, TEXT("Failed to resolve class '%s'. Was it removed?"), *ActorSnapshot->GetActorClass().ToString());
			continue;
		}

		WorldSnapshotData::HandleNameClash(OriginalRemovedActorPath);
		
		// Example: /Game/MapName.MapName:PersistentLevel.StaticMeshActor_42.StaticMeshComponent becomes /Game/MapName.MapName
		const FSoftObjectPath PathToOwningWorldAsset = OriginalRemovedActorPath.GetAssetPathString();
		UObject* UncastWorld = PathToOwningWorldAsset.ResolveObject();
		if (!UncastWorld)
		{
			// Do not TryLoad. If the respective level is loaded, the world must already exist.
			// User has most likely removed the level from the world. We don't want to load that level and modify it by accident. 
			UE_LOG(LogLevelSnapshots, Error, TEXT("Failed to resolve world '%s'"), *PathToOwningWorldAsset.ToString());
			continue;
		}

		// Each Level in UWorld::Levels has a corresponding UWorld associated with it in which we re-create the actor.
		if (UWorld* OwningLevelWorld = ExactCast<UWorld>(UncastWorld))
		{
			const FString& SubObjectPath = OriginalRemovedActorPath.GetSubPathString();
			const int32 LastDotIndex = SubObjectPath.Find(TEXT("."));
			// Full string: /Game/MapName.MapName:PersistentLevel.StaticMeshActor_42 SubPath: PersistentLevel.StaticMeshActor_42 
			checkf(LastDotIndex != INDEX_NONE, TEXT("There should always be at least one dot after PersistentLevel"));
			
			const int32 NameLength = SubObjectPath.Len() - LastDotIndex - 1;
			const FString ActorName = SubObjectPath.Right(NameLength);

			const FName ActorFName = *ActorName;
			FActorSpawnParameters SpawnParameters;
			SpawnParameters.Name = ActorFName;
			SpawnParameters.bNoFail = true;
			SpawnParameters.Template = Cast<AActor>(GetClassDefault(ActorClass));
			SpawnParameters.ObjectFlags = ActorSnapshot->SerializedActorData.GetObjectFlags();
			Module.OnPreCreateActor(OwningLevelWorld, ActorClass, SpawnParameters);

			checkf(SpawnParameters.Name == ActorFName, TEXT("You cannot change the name of the object"));
			SpawnParameters.Name = ActorFName;
			SpawnParameters.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Required_ErrorAndReturnNull;
			if (AActor* RecreatedActor = OwningLevelWorld->SpawnActor(ActorClass, nullptr, SpawnParameters))
			{
				Module.OnPostRecreateActor(RecreatedActor);
				RecreatedActors.Add(OriginalRemovedActorPath, RecreatedActor);
			}
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
			if (AActor** RecreatedActor = RecreatedActors.Find(OriginalRemovedActorPath))
			{
				// Mark it, otherwise we'll serialize it again when we look for world actors matching the snapshot
				EvaluatedActors.Add(*RecreatedActor);
				ActorSnapshot->DeserializeIntoRecreatedEditorWorldActor(TempActorWorld.Get(), *RecreatedActor, *this, LocalisationSnapshotPackage, PropertiesToSerialize);
			}
			else
			{
				UE_LOG(LogLevelSnapshots, Error, TEXT("Failed to recreate actor %s"), *OriginalRemovedActorPath.ToString());
			}
		}
	}
}

void FWorldSnapshotData::ApplyToWorld_HandleSerializingMatchingActors(TSet<AActor*>& EvaluatedActors, const TArray<FSoftObjectPath>& SelectedPaths, UPackage* LocalisationSnapshotPackage, const FPropertySelectionMap& PropertiesToSerialize)
{
	SCOPED_SNAPSHOT_CORE_TRACE(ApplyToWorld_SerializeMatchedActors);
	
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
			else if (ensure(ResolvedObject))
			{
				OriginalWorldActor = ResolvedObject->GetTypedOuter<AActor>();
			}

			if (ensure(OriginalWorldActor)
				&& FSnapshotRestorability::IsActorRestorable(OriginalWorldActor) && !EvaluatedActors.Contains(OriginalWorldActor))
			{
				EvaluatedActors.Add(OriginalWorldActor);
				FActorSnapshotData* ActorSnapshot = ActorData.Find(OriginalWorldActor);
				if (ensure(ActorSnapshot))
				{
					ActorSnapshot->DeserializeIntoExistingWorldActor(TempActorWorld.Get(), OriginalWorldActor, *this, LocalisationSnapshotPackage, PropertiesToSerialize);
				}
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
