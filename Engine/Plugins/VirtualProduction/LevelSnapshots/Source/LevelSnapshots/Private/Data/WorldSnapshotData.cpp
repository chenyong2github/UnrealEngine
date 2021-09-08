// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/WorldSnapshotData.h"

#include "Archive/TakeWorldObjectSnapshotArchive.h"
#include "Archive/ClassDefaults/ApplyClassDefaulDataArchive.h"
#include "Archive/ClassDefaults/TakeClassDefaultObjectSnapshotArchive.h"
#include "LevelSnapshotsLog.h"
#include "PropertySelectionMap.h"
#include "Restorability/SnapshotRestorability.h"

#include "Components/ActorComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "UObject/Package.h"
#if WITH_EDITOR
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Editor/UnrealEdEngine.h"
#include "Engine/Selection.h"
#include "Internationalization/Internationalization.h"
#include "Misc/ScopedSlowTask.h"
#include "UnrealEdGlobals.h"
#endif

#define LOCTEXT_NAMESPACE "LevelSnapshotsEditor"

namespace
{
	/* If the path contains an actor, returns a new path to that actor. */
	TOptional<FSoftObjectPath> ExtractActorFromPath(const FSoftObjectPath& OriginalObjectPath, bool& bIsPathToActorSubobject)
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
		
		bIsPathToActorSubobject = !bPathIsToActor;
		return bPathIsToActor ?
			// Example /Game/MapName.MapName:PersistentLevel.StaticMeshActor_42
			OriginalObjectPath
			:
			// Converts /Game/MapName.MapName:PersistentLevel.StaticMeshActor_42.StaticMeshComponent to /Game/MapName.MapName:PersistentLevel.StaticMeshActor_42
			FSoftObjectPath(OriginalObjectPath.GetAssetPathName(), SubPathString.Left(DotAfterActorNameIndex));
	}
	
	/* If Path contains a path to an actor, returns that actor.
	 * Example: /Game/MapName.MapName:PersistentLevel.StaticMeshActor_42.StaticMeshComponent returns StaticMeshActor_42's data
	 */
	TOptional<FActorSnapshotData*> FindSavedActorDataUsingObjectPath(TMap<FSoftObjectPath, FActorSnapshotData>& ActorData, const FSoftObjectPath& OriginalObjectPath, bool& bIsPathToActorSubobject)
	{
		const TOptional<FSoftObjectPath> PathToActor = ExtractActorFromPath(OriginalObjectPath, bIsPathToActorSubobject);
		if (!PathToActor.IsSet())
		{
			return {};	
		}
		
		FActorSnapshotData* Result = ActorData.Find(*PathToActor);
		UE_CLOG(Result == nullptr, LogLevelSnapshots, Warning, TEXT("Path %s looks like an actor path but no data was saved for it. Maybe it was a reference to an auto-generated actor, e.g. a brush or volume present in all worlds by default?"), *OriginalObjectPath.ToString());
		return Result ? Result : TOptional<FActorSnapshotData*>();  
	}


	/* Takes an existing path to an actor's subobjects and replaces the actor bit with the path to another actor.
	 *
	 * E.g. /Game/MapName.MapName:PersistentLevel.StaticMeshActor_42.StaticMeshComponent could become /Game/MapName.MapName:PersistentLevel.SomeOtherActor.StaticMeshComponent
	 */
	FSoftObjectPath SetActorInPath(AActor* NewActor, const FSoftObjectPath& OriginalObjectPath)
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

		const FSoftObjectPath PathToNewActor(NewActor);
		// PersistentLevel.StaticMeshActor_42.StaticMeshComponent becomes .StaticMeshComponent
		const FString PathAfterOriginalActor = SubPathString.Right(SubPathString.Len() - DotAfterActorNameIndex); 
		return FSoftObjectPath(PathToNewActor.GetAssetPathName(), PathToNewActor.GetSubPathString() + PathAfterOriginalActor);
	}

	enum EEquivalenceResult
	{
		Equivalent,
		NotEquivalent,
		NotComparable
	};
	
	EEquivalenceResult AreActorsEquivalent(UObject* SnapshotPropertyValue, UObject* OriginalPropertyValue, const TMap<FSoftObjectPath, FActorSnapshotData>& ActorData)
	{
		// Compare actors
		if (AActor* OriginalActorReference = Cast<AActor>(OriginalPropertyValue))
		{
			const FActorSnapshotData* SavedData = ActorData.Find(OriginalActorReference);
			if (SavedData == nullptr)
			{
				return NotEquivalent;
			}

			// The snapshot actor was already allocated, if some other snapshot actor is referencing it
			const TOptional<AActor*> PreallocatedSnapshotVersion = SavedData->GetPreallocatedIfValidButDoNotAllocate();
			return PreallocatedSnapshotVersion.Get(nullptr) == SnapshotPropertyValue ? Equivalent : NotEquivalent;
		}

		return NotComparable;
	}

	EEquivalenceResult AreSubobjectsEquivalent(UObject* SnapshotPropertyValue, UObject* OriginalPropertyValue, const TMap<FSoftObjectPath, FActorSnapshotData>& ActorData)
	{
		const bool bIsWorldObject = SnapshotPropertyValue->IsInA(UWorld::StaticClass());
		if (bIsWorldObject)
		{
			AActor* SnapshotOwningActor = SnapshotPropertyValue->GetTypedOuter<AActor>();
			AActor* OriginalOwningActor = OriginalPropertyValue->GetTypedOuter<AActor>();
			if (!ensureMsgf(SnapshotOwningActor && OriginalOwningActor, TEXT("This is weird: the objects are part of a world and not actors, so they should be subobjects of actors, like components. Investigate")))
			{
				return NotEquivalent;
			}

			// Are the two subobjects owned by corresponding actors
			const FActorSnapshotData* CorrespondingSnapshotActor = ActorData.Find(OriginalOwningActor);
			const bool bAreOwnedByEquivalentActors = CorrespondingSnapshotActor == nullptr || CorrespondingSnapshotActor->GetPreallocatedIfValidButDoNotAllocate().Get(nullptr) != SnapshotOwningActor; 
			if (!bAreOwnedByEquivalentActors)
			{
				return NotEquivalent;
			}

			// TODO: Call registered callbacks from external modules to determine whether the two components match each other.
			// Needed for cases like Foliage where components are matched based on foliage type instead of name

			// Check that chain of outers correspond to each other.
			UObject* CurrentSnapshotOuter = SnapshotPropertyValue;
			UObject* CurrentOriginalOuter = OriginalPropertyValue;
			for (; CurrentSnapshotOuter != SnapshotOwningActor && CurrentOriginalOuter != OriginalOwningActor; CurrentSnapshotOuter = CurrentSnapshotOuter->GetOuter(), CurrentOriginalOuter = CurrentOriginalOuter->GetOuter())
			{
				// TODO: Call registered callbacks from external modules to determine whether the two components match each other.
				// Needed for cases like Foliage where components are matched based on foliage type instead of name

				const bool bHaveSameName = CurrentSnapshotOuter->GetFName().IsEqual(CurrentOriginalOuter->GetFName());
				// I thought of also checking whether the two outers have the same class but I see no reason to atm
				if (!bHaveSameName)
				{
					return NotEquivalent;
				}
			}

			return Equivalent;
		}
		
		return NotComparable;
	}
}

void FWorldSnapshotData::OnCreateSnapshotWorld(UWorld* NewTempActorWorld)
{
	TempActorWorld = NewTempActorWorld;
}

void FWorldSnapshotData::OnDestroySnapshotWorld()
{
	TempActorWorld.Reset();

	// Avoid object leaking
	for (auto ClassDefaultsIt = ClassDefaults.CreateIterator(); ClassDefaultsIt; ++ClassDefaultsIt)
	{
		ClassDefaultsIt->Value.CachedLoadedClassDefault = nullptr;
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
	
	SnapshotVersionInfo.Initialize();

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

void FWorldSnapshotData::ApplyToWorld(UWorld* WorldToApplyTo, UPackage* LocalisationSnapshotPackage, const FPropertySelectionMap& PropertiesToSerialize)
{
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

TOptional<AActor*> FWorldSnapshotData::GetDeserializedActor(const FSoftObjectPath& OriginalObjectPath, UPackage* LocalisationSnapshotPackage)
{
	FActorSnapshotData* SerializedActor = ActorData.Find(OriginalObjectPath);
	if (SerializedActor)
	{
		return SerializedActor->GetDeserialized(TempActorWorld->GetWorld(), *this, LocalisationSnapshotPackage);
	}

	UE_LOG(LogLevelSnapshots, Warning, TEXT("No save data found for actor %s"), *OriginalObjectPath.ToString());
	return {};
}

TOptional<FObjectSnapshotData*> FWorldSnapshotData::GetSerializedClassDefaults(UClass* Class)
{
	FObjectSnapshotData* ClassDefaultData = ClassDefaults.Find(Class);
	return ClassDefaultData ? ClassDefaultData : TOptional<FObjectSnapshotData*>();
}

bool FWorldSnapshotData::AreReferencesEquivalent(UObject* SnapshotPropertyValue, UObject* OriginalPropertyValue, AActor* SnapshotActor, AActor* OriginalActor) const
{
	const bool bOriginalIsSubobject = OriginalPropertyValue != nullptr && OriginalPropertyValue->IsIn(OriginalActor);
	const bool bNeedsUnsupportedSubobjectRestorationFeature = SnapshotPropertyValue == nullptr && bOriginalIsSubobject;
	if (bNeedsUnsupportedSubobjectRestorationFeature)
	{
		UE_LOG(LogLevelSnapshots, Verbose, TEXT("Object '%s' in of actor '%s' seems to be a subobject. Snapshots currently do not support re-creating subobjects."),
			OriginalPropertyValue ? *OriginalPropertyValue->GetName() : TEXT("null"),
			*OriginalActor->GetName()
			);
		return true;
	}
	
	if (SnapshotPropertyValue == nullptr || OriginalPropertyValue == nullptr)
	{
		return SnapshotPropertyValue == OriginalPropertyValue;
	}
	if (SnapshotPropertyValue->GetClass() != OriginalPropertyValue->GetClass())
	{
		return false;
	}

	const EEquivalenceResult ActorsEquivalent = AreActorsEquivalent(SnapshotPropertyValue, OriginalPropertyValue, ActorData);
	if (ActorsEquivalent != NotComparable)
	{
		return ActorsEquivalent == Equivalent;
	}

	const EEquivalenceResult SubobjectsEquivalent = AreSubobjectsEquivalent(SnapshotPropertyValue, OriginalPropertyValue, ActorData);
	if (SubobjectsEquivalent != NotComparable)
	{
		return SubobjectsEquivalent == Equivalent;
	}
	
	// Compare external references, like a UMaterial in the content browser.
	return SnapshotPropertyValue == OriginalPropertyValue;
}

int32 FWorldSnapshotData::AddObjectDependency(UObject* ReferenceFromOriginalObject)
{
	return SerializedObjectReferences.AddUnique(ReferenceFromOriginalObject);
}

UObject* FWorldSnapshotData::ResolveObjectDependencyForSnapshotWorld(int32 ObjectPathIndex)
{
	if (!ensure(SerializedObjectReferences.IsValidIndex(ObjectPathIndex)))
	{
		return nullptr;
	}
	
	const FSoftObjectPath& OriginalObjectPath = SerializedObjectReferences[ObjectPathIndex];
	bool bIsPathToActorSubobject;
	const TOptional<FActorSnapshotData*> SnapshotData = FindSavedActorDataUsingObjectPath(ActorData, OriginalObjectPath, bIsPathToActorSubobject);
	
	const bool bIsExternalAssetReference = !SnapshotData.IsSet();
	if (bIsExternalAssetReference)
	{
		return ResolveExternalReference(OriginalObjectPath);
	}

	TOptional<AActor*> Result = SnapshotData.GetValue()->GetPreallocated(TempActorWorld->GetWorld(), *this);
	if (!Result.IsSet())
	{
		return nullptr;
	}

	// Reference to actor
	AActor* SnapshotActor = *Result;
	const bool bIsPathToActor = !bIsPathToActorSubobject; 
	if (bIsPathToActor)
	{
		return SnapshotActor;
	}

	// Reference to default subobjects, such as components, or an object we already allocated.
	const FSoftObjectPath PathToSubobject = SetActorInPath(SnapshotActor, OriginalObjectPath);
	if (UObject* DefaultSubobject = PathToSubobject.ResolveObject())
	{
		return DefaultSubobject;
	}

	// TODO: The subobject is instanced. Recreate it here using Subobjects.Find(ObjectPathIndex) to recursively construct the subobject's outers
	return nullptr;
}

UObject* FWorldSnapshotData::ResolveObjectDependencyForEditorWorld(int32 ObjectPathIndex, const FPropertySelectionMap& SelectionMap)
{
	if (!ensure(SerializedObjectReferences.IsValidIndex(ObjectPathIndex)))
	{
		return nullptr;
	}
	
	const FSoftObjectPath& OriginalObjectPath = SerializedObjectReferences[ObjectPathIndex];

	bool bIsPathToActorSubobject;
	const TOptional<FActorSnapshotData*> SnapshotData = FindSavedActorDataUsingObjectPath(ActorData, OriginalObjectPath, bIsPathToActorSubobject);
	
	// Dealing with an external (asset) reference, e.g. a UMaterial?
	const bool bIsExternalAssetReference = !SnapshotData.IsSet();
	if (bIsExternalAssetReference)
	{
		return ResolveExternalReference(OriginalObjectPath);
	}

	UObject* ResolvedObject = OriginalObjectPath.ResolveObject();
	// Actor reference?
	const bool bIsReferenceToActor = !bIsPathToActorSubobject;
	if (bIsReferenceToActor)
	{
		return ResolvedObject;
	}


	const FSubobjectSnapshotData* SubobjectData = Subobjects.Find(ObjectPathIndex);
	if (!SubobjectData)
	{
		// This should not happen after release of 4.27. This is error may happen if certain snapshots in an older data format are applied because the data wasn't yet captured.
		UE_LOG(LogLevelSnapshots, Warning, TEXT("Trying to resolve a subobject but found no associated object data."));
		return ResolvedObject;
	}
	
	// TODO: Implement restoring subobjects, e.g. material instances, here
	UClass* ExpectedClass = SubobjectData->Class.ResolveClass();
	if (ResolvedObject == nullptr || !ensure(ExpectedClass) || ExpectedClass != ResolvedObject->GetClass())
	{
		// There was already a subobject with the same name but it is incompatible because is a different class.
		// TODO: Allocate a new instance, serialize all data into it, and purge ResolvedObject by replacing all references to it
	}
	else
	{
		// Subobject of same name already exists and is compatible
		// TODO: Check whether subobject is marked as serialized and if not serialize using SelectionMap 	
	}
	
	return ResolvedObject;
}

UObject* FWorldSnapshotData::ResolveObjectDependencyForClassDefaultObject(int32 ObjectPathIndex)
{
	if (ensure(SerializedObjectReferences.IsValidIndex(ObjectPathIndex)))
	{
		const FSoftObjectPath& OriginalObjectPath = SerializedObjectReferences[ObjectPathIndex];
		return ResolveExternalReference(OriginalObjectPath);
	}
	return nullptr;
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

int32 FWorldSnapshotData::AddCustomSubobjectDependency(UObject* ReferenceFromOriginalObject)
{
	if (!ensure(ReferenceFromOriginalObject))
	{
		return INDEX_NONE;
	}

	int32 SubobjectIndex = SerializedObjectReferences.Find(ReferenceFromOriginalObject);
	if (SubobjectIndex == INDEX_NONE)
	{
		SubobjectIndex = SerializedObjectReferences.AddUnique(ReferenceFromOriginalObject);
	}
	
	if (CustomSubobjectSerializationData.Find(SubobjectIndex) == nullptr)
	{
		CustomSubobjectSerializationData.Add(SubobjectIndex);
	}
	else
	{
		UE_LOG(LogLevelSnapshots, Warning, TEXT("Object %s was already added as dependency. Investigate"), *ReferenceFromOriginalObject->GetName());
		UE_DEBUG_BREAK();
	}
		
	return SubobjectIndex;
}

FCustomSerializationData* FWorldSnapshotData::GetCustomSubobjectData_ForSubobject(const FSoftObjectPath& ReferenceFromOriginalObject)
{
	const int32 SubobjectIndex = SerializedObjectReferences.Find(ReferenceFromOriginalObject);
	if (FCustomSerializationData* Data = CustomSubobjectSerializationData.Find(SubobjectIndex))
	{
		return Data;
	}
	return nullptr;
}

const FCustomSerializationData* FWorldSnapshotData::GetCustomSubobjectData_ForActorOrSubobject(UObject* OriginalObject) const
{
	// Is it an actor?
	if (const FActorSnapshotData* SavedActorData = ActorData.Find(OriginalObject))
	{
		return &SavedActorData->GetCustomActorSerializationData();
	}
	if (Cast<AActor>(OriginalObject))
	{
		// Return immediately to avoid searching the entire array below.
		return nullptr;
	}

	// If not an actor, it is a subobject
	const int32 ObjectReferenceIndex = SerializedObjectReferences.Find(OriginalObject);
	if (ObjectReferenceIndex != INDEX_NONE)
	{
		return CustomSubobjectSerializationData.Find(ObjectReferenceIndex);
	}

	return nullptr;
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
	FTakeClassDefaultObjectSnapshotArchive::SaveClassDefaultObject(ClassData, *this, ClassDefault);
	
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
	if (ClassDefaultData)
	{
		FApplyClassDefaulDataArchive::RestoreChangedClassDefaults(*ClassDefaultData, *this, Object);
	}
	else
	{
		UE_LOG(LogLevelSnapshots, Warning,
			TEXT("No CDO saved for class '%s'. If you changed some class default values for this class, then the affected objects will have the latest values instead of the class defaults at the time the snapshot was taken. Should be nothing major to worry about."),
			*Object->GetClass()->GetName()
			);
	}
}

const FSnapshotVersionInfo& FWorldSnapshotData::GetSnapshotVersionInfo() const
{
	return SnapshotVersionInfo;
}

void FWorldSnapshotData::PostSerialize(const FArchive& Ar)
{
	if (Ar.IsLoading() && !SnapshotVersionInfo.IsInitialized())
	{
		SnapshotVersionInfo.Initialize();
	}
}

UObject* FWorldSnapshotData::ResolveExternalReference(const FSoftObjectPath& ObjectPath)
{
	UObject* ExternalReference = ObjectPath.ResolveObject();
	const bool bNeedsToLoadFromDisk = ExternalReference == nullptr;
	if (bNeedsToLoadFromDisk)
	{
		ExternalReference = ObjectPath.TryLoad();
	}
		
	// We're supposed to be dealing with an external (asset) reference, e.g. a UMaterial.
	ensureAlwaysMsgf(ExternalReference == nullptr || (!ExternalReference->IsA<AActor>() && !ExternalReference->IsA<UActorComponent>()), TEXT("Something is wrong. We just checked that the reference is not a world object but it is an actor or component."));
	return ExternalReference;
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

	USelection* EdSelectionManager = GEditor->GetSelectedActors();
	EdSelectionManager->BeginBatchSelectOperation();
	for (const TWeakObjectPtr<AActor>& ActorToDespawn: ActorsToDespawn)
	{
		if (ensureMsgf(ActorToDespawn.IsValid(), TEXT("Actor became invalid since selection set was created")))
		{
			EdSelectionManager->Modify();
			GEditor->SelectActor(ActorToDespawn.Get(), /*bSelect =*/true, /*bNotifyForActor =*/false, /*bSelectEvenIfHidden =*/true);
		}
	}
	EdSelectionManager->EndBatchSelectOperation();

	const bool bVerifyDeletionCanHappen = true;
	const bool bWarnAboutReferences = false;
	GEditor->edactDeleteSelected(WorldToApplyTo, bVerifyDeletionCanHappen, bWarnAboutReferences, bWarnAboutReferences);
	
	// TODO: restore user selection

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

namespace
{
	void DeleteActor(AActor* ActorToDespawn)
	{
		check(ActorToDespawn);

		if (!ActorToDespawn->IsPendingKill())
		{
#if WITH_EDITOR
			GEditor->SelectActor(ActorToDespawn, /*bSelect =*/true, /*bNotifyForActor =*/false, /*bSelectEvenIfHidden =*/true);
			const bool bVerifyDeletionCanHappen = true;
			const bool bWarnAboutReferences = false;
			GEditor->edactDeleteSelected(ActorToDespawn->GetWorld(), bVerifyDeletionCanHappen, bWarnAboutReferences, bWarnAboutReferences);
#else
			ActorToDespawn->Destroy(true, true);
#endif
		}

		const FName NewName = MakeUniqueObjectName(ActorToDespawn->GetLevel(), ActorToDespawn->GetClass());
		ActorToDespawn->Rename(*NewName.ToString(), nullptr, REN_NonTransactional);
	}
}

void FWorldSnapshotData::ApplyToWorld_HandleRecreatingActors(TSet<AActor*>& EvaluatedActors, UPackage* LocalisationSnapshotPackage, const FPropertySelectionMap& PropertiesToSerialize)
{
	SCOPED_SNAPSHOT_CORE_TRACE(ApplyToWorld_RecreateActors);
	
#if WITH_EDITOR
	FScopedSlowTask RecreateActors(PropertiesToSerialize.GetDeletedActorsToRespawn().Num(), LOCTEXT("ApplyToWorld.RecreateActorsKey", "Re-creating actors"));
	RecreateActors.MakeDialogDelayed(1.f, false);
#endif
			
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
		
		if (UObject* NameClash = OriginalRemovedActorPath.ResolveObject())
		{
			AActor* Actor = Cast<AActor>(NameClash);
			DeleteActor(Actor);
		}
		
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
			
			FActorSpawnParameters SpawnParameters;
			SpawnParameters.Name = FName(*ActorName);
			SpawnParameters.bNoFail = true;
			SpawnParameters.Template = Cast<AActor>(GetClassDefault(ActorClass));
			SpawnParameters.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Required_ErrorAndReturnNull;
			if (AActor* RecreatedActor = OwningLevelWorld->SpawnActor(ActorClass, nullptr, SpawnParameters))
			{
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
			AActor** RecreatedActor = RecreatedActors.Find(OriginalRemovedActorPath);
			if (RecreatedActor)
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

			if (ensure(OriginalWorldActor))
			{
				if (!EvaluatedActors.Contains(OriginalWorldActor))
				{
					if (FActorSnapshotData* ActorSnapshot = ActorData.Find(OriginalWorldActor))
					{
						ActorSnapshot->DeserializeIntoExistingWorldActor(TempActorWorld.Get(), OriginalWorldActor, *this, LocalisationSnapshotPackage, PropertiesToSerialize);
					}
					
					EvaluatedActors.Add(OriginalWorldActor);
				}
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE