// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/CustomSerialization/CustomObjectSerializationWrapper.h"

#include "Archive/ApplySnapshotToEditorArchive.h"
#include "Archive/LoadSnapshotObjectArchive.h"
#include "CustomSerialization/CustomSerializationDataManager.h"
#include "Data/WorldSnapshotData.h"
#include "Interfaces/ICustomObjectSnapshotSerializer.h"
#include "LevelSnapshotsLog.h"
#include "LevelSnapshotsModule.h"
#include "PropertySelectionMap.h"

#include "GameFramework/Actor.h"
#include "Modules/ModuleManager.h"
#include "Util/SnapshotObjectUtil.h"
#include "Util/SnapshotUtil.h"

namespace
{
	using FSerializationDataGetter = TFunction<FCustomSerializationData*()>;
	
	FRestoreObjectScope PreObjectRestore_SnapshotWorld(
		UObject* SnapshotObject,
		FWorldSnapshotData& WorldData,
		const FProcessObjectDependency& ProcessObjectDependency,
		UPackage* LocalisationSnapshotPackage,
		FSerializationDataGetter SerializationDataGetter
		)
	{
		SCOPED_SNAPSHOT_CORE_TRACE(CustomObjectSerialization_PostSnapshotRestore);
		
		FLevelSnapshotsModule& LevelSnapshots = FModuleManager::Get().GetModuleChecked<FLevelSnapshotsModule>("LevelSnapshots");
		TSharedPtr<ICustomObjectSnapshotSerializer> CustomSerializer = LevelSnapshots.GetCustomSerializerForClass(SnapshotObject->GetClass());
		if (!CustomSerializer.IsValid() || !ensure(SerializationDataGetter() != nullptr))
		{
			return FRestoreObjectScope(nullptr);	
		}

		FCustomSerializationDataReader SerializationDataReader = FCustomSerializationDataReader(FCustomSerializationDataGetter_ReadOnly::CreateLambda([SerializationDataGetter](){ return SerializationDataGetter();}), WorldData);
		CustomSerializer->PreApplySnapshotProperties(SnapshotObject, SerializationDataReader);
		return FRestoreObjectScope([SnapshotObject, SerializationDataGetter, &WorldData, &ProcessObjectDependency, LocalisationSnapshotPackage, SerializationDataReader, CustomSerializer]()
		{
			for (int32 i = 0; i < SerializationDataReader.GetNumSubobjects(); ++i)
			{
				const TSharedPtr<ISnapshotSubobjectMetaData> MetaData = SerializationDataReader.GetSubobjectMetaData(i);
				UObject* SnapshotSubobject = CustomSerializer->FindOrRecreateSubobjectInSnapshotWorld(SnapshotObject, *MetaData, SerializationDataReader);
				if (!SnapshotSubobject || !ensure(SnapshotSubobject->IsIn(SnapshotObject)))
				{
					UE_LOG(LogLevelSnapshots, Warning, TEXT("FindOrRecreateSubobjectInSnapshotWorld did not return any valid subobject. Skipping subobject restoration..."));
					return;
				}

				// Recursively check whether subobjects also have a registered ICustomObjectSnapshotSerializer
				const FRestoreObjectScope FinishRestore = PreObjectRestore_SnapshotWorld(SnapshotSubobject, WorldData, ProcessObjectDependency, LocalisationSnapshotPackage,
					[&WorldData, OriginalPath = MetaData->GetOriginalPath()](){ return SnapshotUtil::Object::FindCustomSubobjectData(WorldData, OriginalPath);});
				FLoadSnapshotObjectArchive::ApplyToSnapshotWorldObject(SerializationDataGetter()->Subobjects[i], WorldData, SnapshotSubobject, ProcessObjectDependency, LocalisationSnapshotPackage);
				CustomSerializer->OnPostSerializeSnapshotSubobject(SnapshotSubobject, *MetaData, SerializationDataReader);
			}

			CustomSerializer->PostApplySnapshotProperties(SnapshotObject, SerializationDataReader);
		});	
	}

	FRestoreObjectScope PreObjectRestore_EditorWorld(
		UObject* SnapshotObject,
		UObject* EditorObject,
		FWorldSnapshotData& WorldData,
		const FPropertySelectionMap& SelectionMap,
		UPackage* LocalisationSnapshotPackage,
		FSerializationDataGetter SerializationDataGetter
		)
	{
		SCOPED_SNAPSHOT_CORE_TRACE(CustomObjectSerialization_PostEditorRestore);
		
		FLevelSnapshotsModule& LevelSnapshots = FModuleManager::Get().GetModuleChecked<FLevelSnapshotsModule>("LevelSnapshots");
		TSharedPtr<ICustomObjectSnapshotSerializer> CustomSerializer = LevelSnapshots.GetCustomSerializerForClass(EditorObject->GetClass());
		if (!CustomSerializer.IsValid())
		{
			return FRestoreObjectScope(nullptr);	
		}
		
		FCustomSerializationDataReader SerializationDataReader = FCustomSerializationDataReader(FCustomSerializationDataGetter_ReadOnly::CreateLambda([SerializationDataGetter](){ return SerializationDataGetter(); }), WorldData);
		CustomSerializer->PreApplySnapshotProperties(EditorObject, SerializationDataReader);
		return FRestoreObjectScope([SnapshotObject, EditorObject, &WorldData, &SelectionMap, LocalisationSnapshotPackage, SerializationDataGetter, SerializationDataReader, CustomSerializer]()
		{
			for (int32 i = 0; i < SerializationDataReader.GetNumSubobjects(); ++i)
			{	
				const TSharedPtr<ISnapshotSubobjectMetaData> MetaData = SerializationDataReader.GetSubobjectMetaData(i);
				UObject* EditorSubobject = CustomSerializer->FindOrRecreateSubobjectInEditorWorld(EditorObject, *MetaData, SerializationDataReader);
				UObject* SnapshotSubobject = CustomSerializer->FindOrRecreateSubobjectInSnapshotWorld(SnapshotObject, *MetaData, SerializationDataReader);
				if (!SnapshotSubobject || !ensure(SnapshotSubobject->IsIn(SnapshotObject)))
				{
					UE_LOG(LogLevelSnapshots, Warning, TEXT("FindOrRecreateSubobjectInSnapshotWorld did not return any valid snapshot subobject. Skipping this subobject for %s"), *EditorObject->GetPathName());
					continue;
				}
				if (!EditorSubobject || !ensure(EditorSubobject->IsIn(EditorObject)))
				{
					UE_LOG(LogLevelSnapshots, Warning, TEXT("FindOrRecreateSubobjectInSnapshotWorld did not return any valid editor subobject. Skipping this subobject for %s"), *EditorObject->GetPathName());
					continue;
				}

				const bool bShouldSkipProperties = SelectionMap.IsSubobjectMarkedForReferenceRestorationOnly(EditorSubobject); 
				if (bShouldSkipProperties)
				{
					continue;
				}

				if (const FPropertySelection* SelectedProperties = SelectionMap.GetObjectSelection(EditorSubobject).GetPropertySelection())
				{
					// Recursively check whether subobjects also have a registered ICustomObjectSnapshotSerializer
					const FRestoreObjectScope FinishRestore = PreObjectRestore_EditorWorld(SnapshotSubobject, EditorSubobject, WorldData, SelectionMap, LocalisationSnapshotPackage,
						[&WorldData, OriginalPath = MetaData->GetOriginalPath()](){ return SnapshotUtil::Object::FindCustomSubobjectData(WorldData, OriginalPath);} );
				
					FCustomSerializationData* SerializationData = SerializationDataGetter();
					FApplySnapshotToEditorArchive::ApplyToExistingEditorWorldObject(SerializationData->Subobjects[i], WorldData, EditorSubobject, SnapshotSubobject, SelectionMap, *SelectedProperties);
					CustomSerializer->OnPostSerializeEditorSubobject(EditorSubobject, *MetaData, SerializationDataReader);
					continue;
				}

				const FCustomSubobjectRestorationInfo* RestorationInfo = SelectionMap.GetObjectSelection(EditorObject).GetCustomSubobjectSelection();
				if (RestorationInfo && RestorationInfo->CustomSnapshotSubobjectsToRestore.Contains(SnapshotSubobject))
				{
					// Recursively check whether subobjects also have a registered ICustomObjectSnapshotSerializer
					const FRestoreObjectScope FinishRestore = PreObjectRestore_EditorWorld(SnapshotSubobject, EditorSubobject, WorldData, SelectionMap, LocalisationSnapshotPackage,
						[&WorldData, OriginalPath = MetaData->GetOriginalPath()](){ return SnapshotUtil::Object::FindCustomSubobjectData(WorldData, OriginalPath);} );

					FCustomSerializationData* SerializationData = SerializationDataGetter();
					FApplySnapshotToEditorArchive::ApplyToRecreatedEditorWorldObject(SerializationData->Subobjects[i], WorldData, EditorSubobject, SnapshotSubobject, SelectionMap);
					CustomSerializer->OnPostSerializeEditorSubobject(EditorSubobject, *MetaData, SerializationDataReader);
					continue;
				}
				
				UE_LOG(LogLevelSnapshots, Warning, TEXT("Editor subobject %s was not restored"), *EditorSubobject->GetPathName());	
			}

			CustomSerializer->PostApplySnapshotProperties(EditorObject, SerializationDataReader);
		});	
	}
}

void FCustomObjectSerializationWrapper::TakeSnapshotForActor(
	AActor* EditorActor,
	FCustomSerializationData& ActorSerializationData,
	FWorldSnapshotData& WorldData)
{
	SCOPED_SNAPSHOT_CORE_TRACE(CustomObjectSerialization_TakeActorSnapshot);
	
	FLevelSnapshotsModule& LevelSnapshots = FModuleManager::Get().GetModuleChecked<FLevelSnapshotsModule>("LevelSnapshots");
	TSharedPtr<ICustomObjectSnapshotSerializer> CustomSerializer = LevelSnapshots.GetCustomSerializerForClass(EditorActor->GetClass());
	if (!CustomSerializer.IsValid())
	{
		return;
	}
	
	FCustomSerializationDataWriter SerializationDataWriter = FCustomSerializationDataWriter(
		FCustomSerializationDataGetter_ReadWrite::CreateLambda([&ActorSerializationData]() { return &ActorSerializationData; }),
		WorldData,
		EditorActor
		);
	CustomSerializer->OnTakeSnapshot(EditorActor, SerializationDataWriter);
}

void FCustomObjectSerializationWrapper::TakeSnapshotForSubobject(
	UObject* Subobject,
	FWorldSnapshotData& WorldData)
{
	SCOPED_SNAPSHOT_CORE_TRACE(CustomObjectSerialization_TakeSubobjectSnapshot);
	
	FLevelSnapshotsModule& LevelSnapshots = FModuleManager::Get().GetModuleChecked<FLevelSnapshotsModule>("LevelSnapshots");
	TSharedPtr<ICustomObjectSnapshotSerializer> CustomSerializer = LevelSnapshots.GetCustomSerializerForClass(Subobject->GetClass());
	if (!CustomSerializer.IsValid())
	{
		return;
	}

	const int32 SubobjectIndex = SnapshotUtil::Object::AddCustomSubobjectDependency(WorldData, Subobject); 
	FCustomSerializationDataWriter SerializationDataWriter = FCustomSerializationDataWriter(
		FCustomSerializationDataGetter_ReadWrite::CreateLambda([&WorldData, SubobjectIndex]() { return WorldData.CustomSubobjectSerializationData.Find(SubobjectIndex); }),
		WorldData,
		Subobject
		);
	CustomSerializer->OnTakeSnapshot(Subobject, SerializationDataWriter);
}

FRestoreObjectScope FCustomObjectSerializationWrapper::PreActorRestore_SnapshotWorld(
	AActor* SnapshotActor,
	FCustomSerializationData& ActorSerializationData,
	FWorldSnapshotData& WorldData,
	const FProcessObjectDependency& ProcessObjectDependency,
	UPackage* LocalisationSnapshotPackage)
{
	SCOPED_SNAPSHOT_CORE_TRACE(CustomObjectSerialization_PreSnapshotActorRestore);
	
	return PreObjectRestore_SnapshotWorld(
		SnapshotActor,
		WorldData,
		ProcessObjectDependency,
		LocalisationSnapshotPackage,
		[&ActorSerializationData](){ return &ActorSerializationData; }
		);
}

FRestoreObjectScope FCustomObjectSerializationWrapper::PreActorRestore_EditorWorld(
	AActor* EditorActor,
	FCustomSerializationData& ActorSerializationData,
	FWorldSnapshotData& WorldData,
	const FPropertySelectionMap& SelectionMap,
	UPackage* LocalisationSnapshotPackage)
{
	SCOPED_SNAPSHOT_CORE_TRACE(CustomObjectSerialization_PreEditorRestore);
	
	const TOptional<AActor*> SnapshotActor = WorldData.GetDeserializedActor(EditorActor, LocalisationSnapshotPackage);
	if (!ensure(SnapshotActor))
	{
		return FRestoreObjectScope([](){});
	}
	
	return PreObjectRestore_EditorWorld(
		*SnapshotActor,
		EditorActor,
		WorldData,
		SelectionMap,
		LocalisationSnapshotPackage,
		[&ActorSerializationData](){ return &ActorSerializationData;}
		);
}

FRestoreObjectScope FCustomObjectSerializationWrapper::PreSubobjectRestore_SnapshotWorld(
	UObject* Subobject,
	const FSoftObjectPath& OriginalSubobjectPath,
	FWorldSnapshotData& WorldData,
	const FProcessObjectDependency& ProcessObjectDependency,
	UPackage* LocalisationSnapshotPackage
	)
{
	SCOPED_SNAPSHOT_CORE_TRACE(CustomObjectSerialization_PreSnapshotRestore);
	
	return PreObjectRestore_SnapshotWorld(
		Subobject,
		WorldData,
		ProcessObjectDependency,
		LocalisationSnapshotPackage,
		[&WorldData, OriginalSubobjectPath](){ return SnapshotUtil::Object::FindCustomSubobjectData(WorldData, OriginalSubobjectPath); }
		);
}

FRestoreObjectScope FCustomObjectSerializationWrapper::PreSubobjectRestore_EditorWorld(
	UObject* SnapshotObject,
	UObject* EditorObject,
	FWorldSnapshotData& WorldData,
	const FPropertySelectionMap& SelectionMap,
	UPackage* LocalisationSnapshotPackage
	)
{
	SCOPED_SNAPSHOT_CORE_TRACE(CustomObjectSerialization_PreEditorRestore);
	
	const FSoftObjectPath SubobjectPath(EditorObject);
	if (!SnapshotUtil::Object::FindCustomSubobjectData(WorldData, SubobjectPath))
	{
		return FRestoreObjectScope(nullptr);	
	}
	
	return PreObjectRestore_EditorWorld(
		SnapshotObject,
		EditorObject,
		WorldData,
		SelectionMap,
		LocalisationSnapshotPackage,
		[&WorldData, SubobjectPath](){ return SnapshotUtil::Object::FindCustomSubobjectData(WorldData, SubobjectPath); }
		);
}

void FCustomObjectSerializationWrapper::ForEachMatchingCustomSubobjectPair(const FWorldSnapshotData& WorldData, UObject* SnapshotObject, UObject* WorldObject, FHandleCustomSubobjectPair HandleCustomSubobjectPair,  FHandleUnmatchedCustomSnapshotSubobject HandleUnmachtedCustomSnapshotSubobject)
{
	FLevelSnapshotsModule& LevelSnapshots = FModuleManager::Get().GetModuleChecked<FLevelSnapshotsModule>("LevelSnapshots");
    TSharedPtr<ICustomObjectSnapshotSerializer> CustomSerializer = LevelSnapshots.GetCustomSerializerForClass(WorldObject->GetClass());
    if (!CustomSerializer.IsValid())
    {
    	return;
    }

    const FCustomSerializationData* SubobjectData = SnapshotUtil::Object::FindCustomActorOrSubobjectData(WorldData, WorldObject);
    if (!SubobjectData)
    {
    	return;
    }

    FCustomSerializationDataReader SerializationDataReader = FCustomSerializationDataReader(FCustomSerializationDataGetter_ReadOnly::CreateLambda([SubobjectData](){ return SubobjectData;}), WorldData);
    for (int32 i = 0; i < SerializationDataReader.GetNumSubobjects(); ++i)
    {
    	const TSharedPtr<ISnapshotSubobjectMetaData> MetaData = SerializationDataReader.GetSubobjectMetaData(i);
        UObject* EditorSubobject = CustomSerializer->FindSubobjectInEditorWorld(WorldObject, *MetaData, SerializationDataReader);
        UObject* SnapshotCounterpart = CustomSerializer->FindOrRecreateSubobjectInSnapshotWorld(SnapshotObject, *MetaData, SerializationDataReader);
    	if (!SnapshotCounterpart || !ensure(SnapshotCounterpart->IsIn(SnapshotObject)))
    	{
    		UE_LOG(LogLevelSnapshots, Warning, TEXT("FindOrRecreateSubobjectInSnapshotWorld did not return any valid subobject. Skipping subobject restoration..."));
    		continue;
    	}

    	if (!EditorSubobject)
    	{
    		HandleUnmachtedCustomSnapshotSubobject(SnapshotCounterpart);
    	}
    	else if (ensure(EditorSubobject->IsIn(WorldObject)))
    	{
    		HandleCustomSubobjectPair(SnapshotCounterpart, EditorSubobject);
    	}
    }
}
