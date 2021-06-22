// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterConfigurationDataSerializer.h"

#include "DisplayClusterConfigurationTypes.h"
#include "ILevelSnapshotsModule.h"
#include "Serialization/ObjectSnapshotSerializationData.h"

void FDisplayClusterConfigurationDataSerializer::BlacklistCustomProperties(ILevelSnapshotsModule& Module)
{
	const FName ClusterPropertyName = GET_MEMBER_NAME_CHECKED(UDisplayClusterConfigurationData, Cluster);
	const FName ExportedObjectsPropertyName = UDisplayClusterConfigurationData_Base::GetExportedObjectsMemberName();
	
	const FProperty* ClusterProperty = UDisplayClusterConfigurationData::StaticClass()->FindPropertyByName(ClusterPropertyName);
	const FProperty* ExportedObjectsProperty = UDisplayClusterConfigurationData_Base::StaticClass()->FindPropertyByName(ExportedObjectsPropertyName);
	
	if (ensure(ClusterProperty && ExportedObjectsProperty))
	{
		Module.AddBlacklistedProperties( { ClusterProperty, ExportedObjectsProperty} );
	}
}

void FDisplayClusterConfigurationDataSerializer::UnblacklistCustomProperties(ILevelSnapshotsModule& Module)
{
	const FName ClusterPropertyName = GET_MEMBER_NAME_CHECKED(UDisplayClusterConfigurationData, Cluster);
	const FName ExportedObjectsPropertyName = UDisplayClusterConfigurationData_Base::GetExportedObjectsMemberName();
	
	const FProperty* ClusterProperty = UDisplayClusterConfigurationData::StaticClass()->FindPropertyByName(ClusterPropertyName);
	const FProperty* ExportedObjectsProperty = UDisplayClusterConfigurationData_Base::StaticClass()->FindPropertyByName(ExportedObjectsPropertyName);
	
	if (ClusterProperty && ExportedObjectsProperty)
	{
		Module.RemoveBlacklistedProperties( { ClusterProperty, ExportedObjectsProperty} );
	}
}

void FDisplayClusterConfigurationDataSerializer::OnTakeSnapshot(UObject* EditorObject, ICustomSnapshotSerializationData& DataStorage)
{
	UDisplayClusterConfigurationData* Data = Cast<UDisplayClusterConfigurationData>(EditorObject);
	UDisplayClusterConfigurationCluster* Subobject = Data->Cluster;

	if (ensure(Subobject))
	{
		DataStorage.AddSubobjectSnapshot(Subobject);
	}
}

UObject* FDisplayClusterConfigurationDataSerializer::FindOrRecreateSubobjectInSnapshotWorld(UObject* SnapshotObject, const ISnapshotSubobjectMetaData& ObjectData, const ICustomSnapshotSerializationData& DataStorage)
{
	UDisplayClusterConfigurationData* Data = Cast<UDisplayClusterConfigurationData>(SnapshotObject);
	UDisplayClusterConfigurationCluster* Subobject = Data->Cluster;

	ensure(Subobject);
	return Subobject;
}

UObject* FDisplayClusterConfigurationDataSerializer::FindOrRecreateSubobjectInEditorWorld(UObject* EditorObject, const ISnapshotSubobjectMetaData& ObjectData, const ICustomSnapshotSerializationData& DataStorage)
{
	UDisplayClusterConfigurationData* Data = Cast<UDisplayClusterConfigurationData>(EditorObject);
	UDisplayClusterConfigurationCluster* Subobject = Data->Cluster;

	ensure(Subobject);
	return Subobject;
}

UObject* FDisplayClusterConfigurationDataSerializer::FindSubobjectInEditorWorld(UObject* EditorObject, const ISnapshotSubobjectMetaData& ObjectData, const ICustomSnapshotSerializationData& DataStorage)
{
	UDisplayClusterConfigurationData* Data = Cast<UDisplayClusterConfigurationData>(EditorObject);
	UDisplayClusterConfigurationCluster* Subobject = Data->Cluster;

	ensure(Subobject);
	return Subobject;
}
