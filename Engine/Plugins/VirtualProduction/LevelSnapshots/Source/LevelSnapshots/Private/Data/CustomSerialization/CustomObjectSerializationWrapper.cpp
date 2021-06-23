// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/CustomSerialization/CustomObjectSerializationWrapper.h"

#include "ApplySnapshotDataArchiveV2.h"
#include "LevelSnapshotsLog.h"
#include "LevelSnapshotsStats.h"
#include "CustomSerialization/CustomSerializationDataManager.h"
#include "LevelSnapshotsModule.h"
#include "PropertySelectionMap.h"
#include "SnapshotArchive.h"
#include "Serialization/ICustomObjectSnapshotSerializer.h"
#include "WorldSnapshotData.h"

#include "GameFramework/Actor.h"
#include "Modules/ModuleManager.h"

namespace
{
	using FSerializationDataGetter = TFunction<FCustomSerializationData*()>;
	
	FRestoreObjectScope PreObjectRestore_SnapshotWorld(
		UObject* SnapshotObject,
		FWorldSnapshotData& WorldData,
		UPackage* LocalisationSnapshotPackage,
		FSerializationDataGetter SerializationDataGetter
		)
	{
		FLevelSnapshotsModule& LevelSnapshots = FModuleManager::Get().GetModuleChecked<FLevelSnapshotsModule>("LevelSnapshots");
		TSharedPtr<ICustomObjectSnapshotSerializer> CustomSerializer = LevelSnapshots.GetCustomSerializerForClass(SnapshotObject->GetClass());
		if (!CustomSerializer.IsValid() || !ensure(SerializationDataGetter() != nullptr))
		{
			return FRestoreObjectScope(nullptr);	
		}

		FCustomSerializationDataReader SerializationDataReader = FCustomSerializationDataReader(FCustomSerializationDataGetter_ReadOnly::CreateLambda([SerializationDataGetter](){ return SerializationDataGetter();}), WorldData);
		CustomSerializer->PreApplySnapshotProperties(SnapshotObject, SerializationDataReader);
		return FRestoreObjectScope([SnapshotObject, SerializationDataGetter, &WorldData, LocalisationSnapshotPackage, SerializationDataReader, CustomSerializer]()
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
				
				FSnapshotArchive::ApplyToSnapshotWorldObject(SerializationDataGetter()->Subobjects[i], WorldData, SnapshotSubobject, LocalisationSnapshotPackage);
				{
					// Recursively check whether subobjects also have a registered ICustomObjectSnapshotSerializer
					const FRestoreObjectScope FinishRestore = PreObjectRestore_SnapshotWorld(SnapshotSubobject, WorldData, LocalisationSnapshotPackage, [&WorldData, OriginalPath = MetaData->GetOriginalPath()](){ return WorldData.GetCustomSubobjectData_ForSubobject(OriginalPath);});
				}
				
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
				UObject* EditorSubobject = CustomSerializer->FindOrRecreateSubobjectInSnapshotWorld(EditorObject, *MetaData, SerializationDataReader);
				UObject* SnapshotSubobject = CustomSerializer->FindOrRecreateSubobjectInSnapshotWorld(SnapshotObject, *MetaData, SerializationDataReader);
				if (!EditorSubobject || !SnapshotSubobject || !ensure(EditorSubobject->IsIn(EditorObject) && SnapshotSubobject->IsIn(SnapshotObject)))
				{
					UE_LOG(LogLevelSnapshots, Warning, TEXT("FindOrRecreateSubobjectInSnapshotWorld did not return any valid subobject. Skipping subobject restoration..."));
					continue;
				}

				if (const FPropertySelection* SelectedProperties = SelectionMap.GetSelectedProperties(EditorSubobject))
				{
					FCustomSerializationData* SerializationData = SerializationDataGetter();
					FApplySnapshotDataArchiveV2::ApplyToExistingEditorWorldObject(SerializationData->Subobjects[i], WorldData, EditorSubobject, SnapshotSubobject, SelectionMap, *SelectedProperties);
					CustomSerializer->OnPostSerializeEditorSubobject(EditorSubobject, *MetaData, SerializationDataReader);
					// Recursively check whether subobjects also have a registered ICustomObjectSnapshotSerializer
					const FRestoreObjectScope FinishRestore = PreObjectRestore_EditorWorld(SnapshotSubobject, EditorSubobject, WorldData, SelectionMap, LocalisationSnapshotPackage, [&WorldData, OriginalPath = MetaData->GetOriginalPath()](){ return WorldData.GetCustomSubobjectData_ForSubobject(OriginalPath);} );
				}
				else
				{
					UE_LOG(LogLevelSnapshots, Verbose, TEXT("There were no changes to the custom serialized subobject"));	
				}
				
				CustomSerializer->OnPostSerializeSnapshotSubobject(EditorSubobject, *MetaData, SerializationDataReader);
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
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("TakeSnapshotForActor"), STAT_TakeSnapshotForActor, STATGROUP_LevelSnapshots);
	
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
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("TakeSnapshotForSubobject"), STAT_TakeSnapshotForSubobject, STATGROUP_LevelSnapshots);
	
	FLevelSnapshotsModule& LevelSnapshots = FModuleManager::Get().GetModuleChecked<FLevelSnapshotsModule>("LevelSnapshots");
	TSharedPtr<ICustomObjectSnapshotSerializer> CustomSerializer = LevelSnapshots.GetCustomSerializerForClass(Subobject->GetClass());
	if (!CustomSerializer.IsValid())
	{
		return;
	}

	const int32 SubobjectIndex = WorldData.AddCustomSubobjectDependency(Subobject); 
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
	UPackage* LocalisationSnapshotPackage)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("StartRestoreSnapshotForActor_SnapshotWorld"), STAT_StartRestoreSnapshotForActor_SnapshotWorld, STATGROUP_LevelSnapshots);
	return PreObjectRestore_SnapshotWorld(
		SnapshotActor,
		WorldData,
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
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("StartRestoreSnapshotForActor_EditorWorld"), STAT_StartRestoreSnapshotForActor_EditorWorld, STATGROUP_LevelSnapshots);

	const TOptional<AActor*> SnapshotActor = WorldData.GetDeserializedActor(EditorActor, LocalisationSnapshotPackage);
	check(SnapshotActor);
	
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
	UPackage* LocalisationSnapshotPackage
	)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("StartRestoreSnapshotForSubobject_SnapshotWorld"), STAT_StartRestoreSnapshotForSubobject_SnapshotWorld, STATGROUP_LevelSnapshots);
	return PreObjectRestore_SnapshotWorld(
		Subobject,
		WorldData,
		LocalisationSnapshotPackage,
		[&WorldData, OriginalSubobjectPath](){ return WorldData.GetCustomSubobjectData_ForSubobject(OriginalSubobjectPath); }
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
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("StartRestoreSnapshotForSubobject_EditorWorld"), STAT_StartRestoreSnapshotForSubobject_EditorWorld, STATGROUP_LevelSnapshots);
	
	const FSoftObjectPath SubobjectPath(EditorObject);
	if (!WorldData.GetCustomSubobjectData_ForSubobject(SubobjectPath))
	{
		return FRestoreObjectScope(nullptr);	
	}
	
	return PreObjectRestore_EditorWorld(
		SnapshotObject,
		EditorObject,
		WorldData,
		SelectionMap,
		LocalisationSnapshotPackage,
		[&WorldData, SubobjectPath](){ return WorldData.GetCustomSubobjectData_ForSubobject(SubobjectPath); }
		);
}

void FCustomObjectSerializationWrapper::ForEachMatchingCustomSubobjectPair(const FWorldSnapshotData& WorldData, UObject* SnapshotObject, UObject* WorldObject, FHandleCustomSubobjectPair Callback)
{
	FLevelSnapshotsModule& LevelSnapshots = FModuleManager::Get().GetModuleChecked<FLevelSnapshotsModule>("LevelSnapshots");
    TSharedPtr<ICustomObjectSnapshotSerializer> CustomSerializer = LevelSnapshots.GetCustomSerializerForClass(WorldObject->GetClass());
    if (!CustomSerializer.IsValid())
    {
    	return;
    }

    const FCustomSerializationData* SubobjectData = WorldData.GetCustomSubobjectData_ForActorOrSubobject(WorldObject);
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
    	if (!EditorSubobject || !SnapshotCounterpart || !ensure(EditorSubobject->IsIn(WorldObject) && SnapshotCounterpart->IsIn(SnapshotObject)))
    	{
    		UE_LOG(LogLevelSnapshots, Warning, TEXT("FindOrRecreateSubobjectInSnapshotWorld did not return any valid subobject. Skipping subobject restoration..."));
    		continue;
    	}
    	Callback(SnapshotCounterpart, EditorSubobject);
    }
}
