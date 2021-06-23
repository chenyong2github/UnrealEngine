// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterConfigurationClusterSerializer.h"

#include "DisplayClusterConfigurationTypes.h"
#include "ILevelSnapshotsModule.h"
#include "Serialization/ObjectSnapshotSerializationData.h"

namespace
{
	UObject* FindDisplayCluster(UObject* Owner, const ISnapshotSubobjectMetaData& ObjectData)
	{
		FString ClusterNodeKey;
		ObjectData.ReadObjectAnnotation(FObjectAnnotator::CreateLambda([&ClusterNodeKey](FArchive& Reader)
		{
			Reader << ClusterNodeKey;
		}));
		
		UDisplayClusterConfigurationCluster* Data = Cast<UDisplayClusterConfigurationCluster>(Owner);
		UDisplayClusterConfigurationClusterNode** Result = Data->Nodes.Find(ClusterNodeKey);
		if (ensure(Result))
		{
			return *Result;
		}
		return nullptr;
	}
}

void FDisplayClusterConfigurationClusterSerializer::BlacklistCustomProperties(ILevelSnapshotsModule& Module)
{
	const FName PropertyName = GET_MEMBER_NAME_CHECKED(UDisplayClusterConfigurationCluster, Nodes);
	const FProperty* NodesProperty = UDisplayClusterConfigurationCluster::StaticClass()->FindPropertyByName(PropertyName);
	if (ensure(NodesProperty))
	{
		Module.AddBlacklistedProperties( { NodesProperty } );
	}
}

void FDisplayClusterConfigurationClusterSerializer::UnblacklistCustomProperties(ILevelSnapshotsModule& Module)
{
	const FName PropertyName = GET_MEMBER_NAME_CHECKED(UDisplayClusterConfigurationCluster, Nodes);
	const FProperty* NodesProperty = UDisplayClusterConfigurationCluster::StaticClass()->FindPropertyByName(PropertyName);
	if (NodesProperty)
	{
		Module.RemoveBlacklistedProperties( { NodesProperty } );
	}
}

void FDisplayClusterConfigurationClusterSerializer::OnTakeSnapshot(UObject* EditorObject, ICustomSnapshotSerializationData& DataStorage)
{
	UDisplayClusterConfigurationCluster* Data = Cast<UDisplayClusterConfigurationCluster>(EditorObject);
	for (auto ClusterNodeIt = Data->Nodes.CreateConstIterator(); ClusterNodeIt; ++ClusterNodeIt)
	{
		if (ensure(ClusterNodeIt->Value))
		{
			const int32 ClusterNodeIndex = DataStorage.AddSubobjectSnapshot(ClusterNodeIt->Value);
			DataStorage.GetSubobjectMetaData(ClusterNodeIndex)
				->WriteObjectAnnotation(FObjectAnnotator::CreateLambda([&ClusterNodeIt](FArchive& Writer)
				{
					FString ClusterKey = ClusterNodeIt->Key;
					Writer << ClusterKey;
				}));
		}
	}
}

UObject* FDisplayClusterConfigurationClusterSerializer::FindOrRecreateSubobjectInSnapshotWorld(UObject* SnapshotObject, const ISnapshotSubobjectMetaData& ObjectData, const ICustomSnapshotSerializationData& DataStorage)
{
	return FindDisplayCluster(SnapshotObject, ObjectData);
}

UObject* FDisplayClusterConfigurationClusterSerializer::FindOrRecreateSubobjectInEditorWorld(UObject* EditorObject, const ISnapshotSubobjectMetaData& ObjectData, const ICustomSnapshotSerializationData& DataStorage)
{
	return FindDisplayCluster(EditorObject, ObjectData);
}

UObject* FDisplayClusterConfigurationClusterSerializer::FindSubobjectInEditorWorld(UObject* EditorObject, const ISnapshotSubobjectMetaData& ObjectData, const ICustomSnapshotSerializationData& DataStorage)
{
	return FindDisplayCluster(EditorObject, ObjectData);
}
