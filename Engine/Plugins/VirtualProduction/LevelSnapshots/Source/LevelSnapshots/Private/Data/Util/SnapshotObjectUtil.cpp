// Copyright Epic Games, Inc. All Rights Reserved.

#include "Util/SnapshotObjectUtil.h"

#include "Archive/ApplySnapshotToEditorArchive.h"
#include "Archive/LoadSnapshotObjectArchive.h"
#include "Archive/TakeWorldObjectSnapshotArchive.h"
#include "LevelSnapshotsLog.h"
#include "SnapshotUtil.h"
#include "SubobjectSnapshotData.h"
#include "WorldSnapshotData.h"

#include "Components/ActorComponent.h"
#include "EngineUtils.h"
#include "ObjectTrace.h"
#include "SnapshotRestorability.h"
#include "GameFramework/Actor.h"
#include "Templates/NonNullPointer.h"

namespace
{
	UObject* ResolveExternalReference(const FSoftObjectPath& ObjectPath)
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
	
	UObject* CreateSubobjectForSnapshotWorld(FWorldSnapshotData& WorldData, int32 ObjectPathIndex, const FSoftObjectPath& SnapshotPathToSubobject, const FProcessObjectDependency& ProcessObjectDependency, const FString& LocalisationNamespace)
	{
		FSubobjectSnapshotData* SubobjectData = WorldData.Subobjects.Find(ObjectPathIndex);
		if (!SubobjectData || SubobjectData->bWasSkippedClass)
		{
			return nullptr;
		}
		if (SubobjectData->SnapshotObject.IsValid())
		{
			return SubobjectData->SnapshotObject.Get();
		}
		
	
		UClass* Class = SubobjectData->Class.TryLoadClass<UObject>();
		if (!Class)
		{
			UE_LOG(LogLevelSnapshots, Warning, TEXT("Class '%s' not found. Maybe it was removed?"), *SubobjectData->Class.ToString());
			return nullptr;
		}
		
		const int32 OuterIndex = SubobjectData->OuterIndex;
		check(OuterIndex != ObjectPathIndex);
		UObject* SubobjectOuter = SnapshotUtil::Object::ResolveObjectDependencyForSnapshotWorld(WorldData, OuterIndex, ProcessObjectDependency, LocalisationNamespace);
		if (!SubobjectOuter)
		{
			UE_LOG(LogLevelSnapshots, Warning, TEXT("Failed to create '%s' because its outer could not be created."), *SnapshotPathToSubobject.ToString());
			return nullptr;
		}
		
	
		const FName SubobjectName = *SnapshotUtil::ExtractLastSubobjectName(SnapshotPathToSubobject);
		const bool bSubobjectNameIsTaken = [SubobjectOuter, SubobjectName]()
		{
			TArray<UObject*> Subobjects;
			GetObjectsWithOuter(SubobjectOuter, Subobjects, true);
			for (UObject* Object : Subobjects)
			{
				if (Object->GetFName() == SubobjectName)
				{
					return true;
				}
			}
			return false;
		}();
		if (bSubobjectNameIsTaken)
		{
			UE_LOG(LogLevelSnapshots, Warning, TEXT("Failed to create '%s' because subobject name was already taken."), *SnapshotPathToSubobject.ToString());
			return nullptr;
		}

	
		
		UObject* Subobject = NewObject<UObject>(SubobjectOuter, Class, SubobjectName); 
		SubobjectData->SnapshotObject = Subobject;
			
		WorldData.SerializeClassDefaultsInto(Subobject);
		FLoadSnapshotObjectArchive::ApplyToSnapshotWorldObject(*SubobjectData, WorldData, Subobject, ProcessObjectDependency, LocalisationNamespace)	;

		return Subobject;
	}
	
	UObject* SerializeOrCreateSubobjectForEditorWorld(FWorldSnapshotData& WorldData, FSubobjectSnapshotData& SubobjectData, UObject* ResolvedObject, const FSoftObjectPath& OriginalObjectPath, const FString& LocalisationNamespace, const FPropertySelectionMap& SelectionMap)
	{
		using namespace SnapshotUtil;
		
		if (SubobjectData.EditorObject.IsValid())
		{
			return SubobjectData.EditorObject.Get();
		}

		if (SubobjectData.bWasSkippedClass)
		{
			return !IsValid(ResolvedObject) || ResolvedObject->IsUnreachable() ? nullptr : ResolvedObject;
		}
		
		UClass* ExpectedClass = SubobjectData.Class.ResolveClass();
		if (!ExpectedClass)
		{
			UE_LOG(LogLevelSnapshots, Warning, TEXT("Class '%s' could not be resolved."), *SubobjectData.Class.ToString());
			return nullptr;
		}

		// Suppose ActorA and ActorB. ActorA is being serialized before ActorB. ActorA has a reference to a component in ActorB.
		// That component is instanced and was not recreated yet because, unluckily, we're serializing ActorA before ActorB.
		if (ExpectedClass->IsChildOf(UActorComponent::StaticClass()))
		{
			UE_LOG(LogLevelSnapshots, Error, TEXT("Object depends on a component that must be recreated and is owned by another actor. Support for this is not yet implemented."));
			return nullptr;
		}

		// Need to clean rename dead objects otherwise we'll get a name clash when instantiating new object
		if (ResolvedObject)
		{
			const bool bIsReferencingDeadObject = !IsValid(ResolvedObject) || ResolvedObject->IsUnreachable();
			const bool bIsReferencingDifferentClass = ResolvedObject && !ResolvedObject->GetClass()->IsChildOf(ExpectedClass);
			if (bIsReferencingDeadObject || bIsReferencingDifferentClass)
			{
				const FName NewName = MakeUniqueObjectName(ResolvedObject->GetOuter(), ExpectedClass, *ResolvedObject->GetName().Append(TEXT("_TRASH")));
				ResolvedObject->Rename(*NewName.ToString());
				ResolvedObject = nullptr;
			}
		}
		
		if (!ensureMsgf(SubobjectData.SnapshotObject.IsValid(), TEXT("Subobject pointer should have been set. Investigate.")))
		{
			return ResolvedObject;
		}
		
		UObject* SnapshotVersion = SubobjectData.SnapshotObject.Get();
		if (ResolvedObject)
		{
			// We can't tell whether ResolvedObject is 1: a normal referenced object or 2: a dead object that just has not been collected yet
			// If it is referenced from somewhere (case 1), we expect it to be in the selection map.
			// Otherwise we'll treat it as a dead object that we fully restore.
			if (SelectionMap.IsSubobjectMarkedForReferenceRestorationOnly(OriginalObjectPath))
			{
				// No serialization is required. Just return it.
			}
			else if (const FPropertySelection* PropertySelection = SelectionMap.GetObjectSelection(OriginalObjectPath).GetPropertySelection())
			{
				FApplySnapshotToEditorArchive::ApplyToExistingEditorWorldObject(SubobjectData, WorldData, ResolvedObject, SnapshotVersion, SelectionMap, *PropertySelection);
			}
			else
			{
				// Reuse existing instance but treat it like it was just freshly allocated
				FApplySnapshotToEditorArchive::ApplyToRecreatedEditorWorldObject(SubobjectData, WorldData, ResolvedObject, SnapshotVersion, SelectionMap);
			}
		}
		else 
		{
			UObject* Outer = Object::ResolveObjectDependencyForEditorWorld(WorldData, SubobjectData.OuterIndex, LocalisationNamespace, SelectionMap);
			if (!Outer)
			{
				UE_LOG(LogLevelSnapshots, Error, TEXT("Failed to resolve object '%s' because its outer '%s' could not be resolved"), *OriginalObjectPath.ToString(), *WorldData.SerializedObjectReferences[SubobjectData.OuterIndex].ToString());
				return nullptr;
			}

			// Must set RF_Transactional so object creation is transacted
			ResolvedObject = NewObject<UObject>(Outer, ExpectedClass, *ExtractLastSubobjectName(OriginalObjectPath), SubobjectData.GetObjectFlags() | RF_Transactional);
			if (ensureMsgf(ResolvedObject, TEXT("Failed to allocate '%s'"), *OriginalObjectPath.ToString()))
			{
				ResolvedObject->SetFlags(SubobjectData.GetObjectFlags());
				FApplySnapshotToEditorArchive::ApplyToRecreatedEditorWorldObject(SubobjectData, WorldData, ResolvedObject, SnapshotVersion, SelectionMap);
			}
		}
		
		SubobjectData.EditorObject = ResolvedObject;
		return ResolvedObject;
	}

	void AddSubobjectDependencyInternal(FWorldSnapshotData& WorldData, UObject* ReferenceFromOriginalObject, int32 ObjectIndex)
	{
		if (!FSnapshotRestorability::IsSubobjectDesirableForCapture(ReferenceFromOriginalObject))
		{
			WorldData.Subobjects.Add(ObjectIndex, FSubobjectSnapshotData::MakeSkippedSubobjectData());
			return;
		}
		
		const bool bNeedsToSaveSubobject = !WorldData.Subobjects.Contains(ObjectIndex);
		if (bNeedsToSaveSubobject)
		{
			// Avoid infinite recursion
			WorldData.Subobjects.Add(ObjectIndex);
			
			// Owning actor keeps reference to saved subobjects so they can be recreated faster when snapshot is loaded
			if (!ReferenceFromOriginalObject->IsA<UActorComponent>())
			{
				AActor* OwningActor = ReferenceFromOriginalObject->GetTypedOuter<AActor>();
				check(OwningActor);
				WorldData.ActorData.FindOrAdd(OwningActor).OwnedSubobjects.Add(ObjectIndex);
			}

			
			// Important: first allocate SubobjectData on stack... 
			FSubobjectSnapshotData SubobjectData; 
			UClass* Class = ReferenceFromOriginalObject->GetClass();
			SubobjectData.Class = Class;
			SubobjectData.OuterIndex = SnapshotUtil::Object::AddObjectDependency(WorldData, ReferenceFromOriginalObject->GetOuter());
			// ... because serialisation recursively calls this function. FWorldSnapshotData::Subobjects is possibly reallocated
			FTakeWorldObjectSnapshotArchive::TakeSnapshot(SubobjectData, WorldData, ReferenceFromOriginalObject);
			
			WorldData.Subobjects[ObjectIndex] = MoveTemp(SubobjectData); // MoveTemp not profiled
			WorldData.AddClassDefault(Class);
		}
	}
	
	void EnsureSubobjectIsSerialized(FWorldSnapshotData& WorldData, UObject* ExistingObject, int32 ObjectPathIndex, const FProcessObjectDependency& ProcessObjectDependency, const FString& LocalisationNamespace)
	{
		FSubobjectSnapshotData* SubobjectData = WorldData.Subobjects.Find(ObjectPathIndex);
		if (!SubobjectData || SubobjectData->bWasSkippedClass)
		{
			return;
		}

		if (SubobjectData->Class.ResolveClass() != ExistingObject->GetClass())
		{
			UE_LOG(LogLevelSnapshots, Warning, TEXT("Skipping serialisation of subobject because classes are different. Snapshot object '%s' will not contain values saved in snapshot."), *ExistingObject->GetName());
			return;
		}

		if (!SubobjectData->SnapshotObject.IsValid())
		{
			SubobjectData->SnapshotObject = ExistingObject;
			FLoadSnapshotObjectArchive::ApplyToSnapshotWorldObject(*SubobjectData, WorldData, ExistingObject, ProcessObjectDependency, LocalisationNamespace);
		}
	}
	
	int32 AddOrFindObjectReference(FWorldSnapshotData& WorldData, const FSoftObjectPath& ObjectPath)
	{
		if (const int32* ExistingIndex = WorldData.ReferenceToIndex.Find(ObjectPath))
		{
			return *ExistingIndex;
		}
		
		const int32 Index = WorldData.SerializedObjectReferences.Add(ObjectPath);
		WorldData.ReferenceToIndex.Add(ObjectPath, Index);
		return Index;
	}
}

UObject* SnapshotUtil::Object::ResolveObjectDependencyForSnapshotWorld(FWorldSnapshotData& WorldData, int32 ObjectPathIndex, const FProcessObjectDependency& ProcessObjectDependency, const FString& LocalisationNamespace)
{
	using namespace SnapshotUtil;
	
	if (!ensure(WorldData.SerializedObjectReferences.IsValidIndex(ObjectPathIndex)))
	{
		return nullptr;
	}
	
	const FSoftObjectPath& OriginalObjectPath = WorldData.SerializedObjectReferences[ObjectPathIndex];
	bool bIsPathToActorSubobject;
	const TOptional<FActorSnapshotData*> SnapshotData = FindSavedActorDataUsingObjectPath(WorldData.ActorData, OriginalObjectPath, bIsPathToActorSubobject);
	
	const bool bIsExternalAssetReference = !SnapshotData.IsSet();
	if (bIsExternalAssetReference)
	{
		return ResolveExternalReference(OriginalObjectPath);
	}

	TOptional<AActor*> Result = SnapshotData.GetValue()->GetPreallocated(WorldData.TempActorWorld->GetWorld(), WorldData);
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
	const FSoftObjectPath SnapshotPathToSubobject = SetActorInPath(SnapshotActor, OriginalObjectPath);
	if (UObject* ExistingSubobject = SnapshotPathToSubobject.ResolveObject())
	{
		EnsureSubobjectIsSerialized(WorldData, ExistingSubobject, ObjectPathIndex, ProcessObjectDependency, LocalisationNamespace);
		return ExistingSubobject;
	}

	// The subobject is instanced: recreate it recursively
	return CreateSubobjectForSnapshotWorld(WorldData, ObjectPathIndex, SnapshotPathToSubobject, ProcessObjectDependency, LocalisationNamespace);
}

UObject* SnapshotUtil::Object::ResolveObjectDependencyForEditorWorld(FWorldSnapshotData& WorldData, int32 ObjectPathIndex, const FString& LocalisationNamespace, const FPropertySelectionMap& SelectionMap)
{
	if (!ensure(WorldData.SerializedObjectReferences.IsValidIndex(ObjectPathIndex)))
	{
		return nullptr;
	}
	
	const FSoftObjectPath& OriginalObjectPath = WorldData.SerializedObjectReferences[ObjectPathIndex];
	bool bIsPathToActorSubobject;
	const TOptional<FActorSnapshotData*> SnapshotData = FindSavedActorDataUsingObjectPath(WorldData.ActorData, OriginalObjectPath, bIsPathToActorSubobject);
	
	// Dealing with an external (asset) reference, e.g. a UMaterial?
	const bool bIsExternalAssetReference = !SnapshotData.IsSet();
	if (bIsExternalAssetReference)
	{
		return ResolveExternalReference(OriginalObjectPath);
	}

	// Can return immediately for actors and components: their serialization is handled by FActorSnapshotData.
	UObject* ResolvedObject = OriginalObjectPath.ResolveObject();
	if (ResolvedObject && (ResolvedObject->IsA<AActor>() || ResolvedObject->IsA<UActorComponent>()))
	{
		// Might resolve to a dead object which still exists in memory
		return IsValid(ResolvedObject) ? ResolvedObject : nullptr;
	}
	
	if (FSubobjectSnapshotData* const SubobjectData = WorldData.Subobjects.Find(ObjectPathIndex))
	{
		return SerializeOrCreateSubobjectForEditorWorld(WorldData, *SubobjectData, ResolvedObject, OriginalObjectPath, LocalisationNamespace, SelectionMap);
	}
	
	// This should not happen after release of 4.27. This is error may happen if certain snapshots in an older data format are applied because the data wasn't yet captured.
	UE_LOG(LogLevelSnapshots, Warning, TEXT("Trying to resolve a subobject but found no associated object data."));
	return ResolvedObject;
}

UObject* SnapshotUtil::Object::ResolveObjectDependencyForClassDefaultObject(FWorldSnapshotData& WorldData, int32 ObjectPathIndex)
{
	if (ensure(WorldData.SerializedObjectReferences.IsValidIndex(ObjectPathIndex)))
	{
		const FSoftObjectPath& OriginalObjectPath = WorldData.SerializedObjectReferences[ObjectPathIndex];
		return ResolveExternalReference(OriginalObjectPath);
	}
	return nullptr;
}

int32 SnapshotUtil::Object::AddObjectDependency(FWorldSnapshotData& WorldData, UObject* ReferenceFromOriginalObject, bool bCheckWhetherSubobject)
{
	// Even if FSnapshotRestorability::IsSubobjectDesirableForCapture later returns false for this object, we want to track it
	const int32 Result = AddOrFindObjectReference(WorldData, ReferenceFromOriginalObject);

	if (bCheckWhetherSubobject && ReferenceFromOriginalObject && ReferenceFromOriginalObject->GetTypedOuter<AActor>())
	{
		AddSubobjectDependencyInternal(WorldData, ReferenceFromOriginalObject, Result);
	}
	
	return Result;
}

int32 SnapshotUtil::Object::AddCustomSubobjectDependency(FWorldSnapshotData& WorldData, UObject* ReferenceFromOriginalObject)
{
	if (!ensure(ReferenceFromOriginalObject))
	{
		return INDEX_NONE;
	}

	const int32 SubobjectIndex = AddOrFindObjectReference(WorldData, ReferenceFromOriginalObject);
	if (WorldData.CustomSubobjectSerializationData.Find(SubobjectIndex) == nullptr)
	{
		WorldData.CustomSubobjectSerializationData.Add(SubobjectIndex);
	}
	else
	{
		UE_LOG(LogLevelSnapshots, Warning, TEXT("Object %s was already added as dependency. Investigate"), *ReferenceFromOriginalObject->GetName());
		UE_DEBUG_BREAK();
	}
		
	return SubobjectIndex;
}

FCustomSerializationData* SnapshotUtil::Object::FindCustomSubobjectData(FWorldSnapshotData& WorldData,const FSoftObjectPath& ReferenceFromOriginalObject)
{
	const int32 SubobjectIndex = WorldData.SerializedObjectReferences.Find(ReferenceFromOriginalObject);
	return WorldData.CustomSubobjectSerializationData.Find(SubobjectIndex);
}

const FCustomSerializationData* SnapshotUtil::Object::FindCustomActorOrSubobjectData(const FWorldSnapshotData& WorldData,UObject* OriginalObject)
{
	// Is it an actor?
	if (const FActorSnapshotData* SavedActorData = WorldData.ActorData.Find(OriginalObject))
	{
		return &SavedActorData->GetCustomActorSerializationData();
	}
	if (Cast<AActor>(OriginalObject))
	{
		// Return immediately to avoid searching the entire array below.
		return nullptr;
	}

	// If not an actor, it is a subobject
	const int32 ObjectReferenceIndex = WorldData.SerializedObjectReferences.Find(OriginalObject);
	if (ObjectReferenceIndex != INDEX_NONE)
	{
		return WorldData.CustomSubobjectSerializationData.Find(ObjectReferenceIndex);
	}

	return nullptr;
}
