// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterRootActorSerializer.h"

#include "DisplayClusterConfigurationTypes.h"
#include "DisplayClusterRootActor.h"
#include "ILevelSnapshotsModule.h"
#include "Serialization/ObjectSnapshotSerializationData.h"

void FDisplayClusterRootActorSerializer::BlacklistCustomProperties(ILevelSnapshotsModule& Module)
{
	const FName PropertyName = ADisplayClusterRootActor::GetCurrentConfigDataMemberName();
	const FProperty* CurrentConfigDataProperty = ADisplayClusterRootActor::StaticClass()->FindPropertyByName(PropertyName);
	if (ensure(CurrentConfigDataProperty))
	{
		Module.AddBlacklistedProperties( { CurrentConfigDataProperty } );
	}
}

void FDisplayClusterRootActorSerializer::UnblacklistCustomProperties(ILevelSnapshotsModule& Module)
{
	const FName PropertyName = ADisplayClusterRootActor::GetCurrentConfigDataMemberName();
	const FProperty* CurrentConfigData = ADisplayClusterRootActor::StaticClass()->FindPropertyByName(PropertyName);
	if (CurrentConfigData)
	{
		Module.RemoveBlacklistedProperties( { CurrentConfigData } );
	}
}

void FDisplayClusterRootActorSerializer::OnTakeSnapshot(UObject* EditorObject, ICustomSnapshotSerializationData& DataStorage)
{
	ADisplayClusterRootActor* Actor = Cast<ADisplayClusterRootActor>(EditorObject);
	UDisplayClusterConfigurationData* Subobject = Actor->GetConfigData();

	if (Subobject)
	{
		DataStorage.AddSubobjectSnapshot(Subobject);
	}
}

UObject* FDisplayClusterRootActorSerializer::FindOrRecreateSubobjectInSnapshotWorld(UObject* SnapshotObject, const ISnapshotSubobjectMetaData& ObjectData, const ICustomSnapshotSerializationData& DataStorage)
{
	ADisplayClusterRootActor* Actor = Cast<ADisplayClusterRootActor>(SnapshotObject);
	UDisplayClusterConfigurationData* Subobject = Actor->GetConfigData();
	
	ensure(Subobject);
	return Subobject;
}

UObject* FDisplayClusterRootActorSerializer::FindOrRecreateSubobjectInEditorWorld(UObject* EditorObject, const ISnapshotSubobjectMetaData& ObjectData, const ICustomSnapshotSerializationData& DataStorage)
{
	ADisplayClusterRootActor* Actor = Cast<ADisplayClusterRootActor>(EditorObject);
	UDisplayClusterConfigurationData* Subobject = Actor->GetConfigData();
	
	ensure(Subobject);
	return Subobject;
}

UObject* FDisplayClusterRootActorSerializer::FindSubobjectInEditorWorld(UObject* EditorObject, const ISnapshotSubobjectMetaData& ObjectData, const ICustomSnapshotSerializationData& DataStorage)
{
	ADisplayClusterRootActor* Actor = Cast<ADisplayClusterRootActor>(EditorObject);
	UDisplayClusterConfigurationData* Subobject = Actor->GetConfigData();
	
	ensure(Subobject);
	return Subobject;
}
