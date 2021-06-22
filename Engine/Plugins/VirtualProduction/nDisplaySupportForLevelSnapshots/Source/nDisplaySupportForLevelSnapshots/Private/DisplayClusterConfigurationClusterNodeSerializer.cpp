// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterConfigurationClusterNodeSerializer.h"

#include "DisplayClusterConfigurationTypes.h"
#include "ILevelSnapshotsModule.h"
#include "Serialization/ObjectSnapshotSerializationData.h"

namespace
{
	UObject* FindViewport(UObject* Owner, const ISnapshotSubobjectMetaData& ObjectData)
	{
		FString ViewportKey;
		ObjectData.ReadObjectAnnotation(FObjectAnnotator::CreateLambda([&ViewportKey](FArchive& Reader)
		{
			Reader << ViewportKey;
		}));
		
		UDisplayClusterConfigurationClusterNode* Data = Cast<UDisplayClusterConfigurationClusterNode>(Owner);
		UDisplayClusterConfigurationViewport** Result = Data->Viewports.Find(ViewportKey);
		if (ensure(Result))
		{
			return *Result;
		}
		return nullptr;
	}
}

void FDisplayClusterConfigurationClusterNodeSerializer::BlacklistCustomProperties(ILevelSnapshotsModule& Module)
{
	const FName PropertyName = GET_MEMBER_NAME_CHECKED(UDisplayClusterConfigurationClusterNode, Viewports);
	const FProperty* ViewportsProperty = UDisplayClusterConfigurationClusterNode::StaticClass()->FindPropertyByName(PropertyName);
	if (ensure(ViewportsProperty))
	{
		Module.AddBlacklistedProperties( { ViewportsProperty } );
	}
}

void FDisplayClusterConfigurationClusterNodeSerializer::UnblacklistCustomProperties(ILevelSnapshotsModule& Module)
{
	const FName PropertyName = GET_MEMBER_NAME_CHECKED(UDisplayClusterConfigurationClusterNode, Viewports);
	const FProperty* ViewportsProperty = UDisplayClusterConfigurationClusterNode::StaticClass()->FindPropertyByName(PropertyName);
	if (ViewportsProperty)
	{
		Module.RemoveBlacklistedProperties( { ViewportsProperty } );
	}
}

void FDisplayClusterConfigurationClusterNodeSerializer::OnTakeSnapshot(UObject* EditorObject, ICustomSnapshotSerializationData& DataStorage)
{
	UDisplayClusterConfigurationClusterNode* Data = Cast<UDisplayClusterConfigurationClusterNode>(EditorObject);
	for (auto ViewportIt = Data->Viewports.CreateConstIterator(); ViewportIt; ++ViewportIt)
	{
		if (ensure(ViewportIt->Value))
		{
			const int32 ViewportIndex = DataStorage.AddSubobjectSnapshot(ViewportIt->Value);
			DataStorage.GetSubobjectMetaData(ViewportIndex)
			->WriteObjectAnnotation(FObjectAnnotator::CreateLambda([&ViewportIt](FArchive& Writer)
			{
				FString ViewportKey = ViewportIt->Key;
				Writer << ViewportKey;
			}));
		}
	}
}

UObject* FDisplayClusterConfigurationClusterNodeSerializer::FindOrRecreateSubobjectInSnapshotWorld(UObject* SnapshotObject, const ISnapshotSubobjectMetaData& ObjectData, const ICustomSnapshotSerializationData& DataStorage)
{
	return FindViewport(SnapshotObject, ObjectData);
}

UObject* FDisplayClusterConfigurationClusterNodeSerializer::FindOrRecreateSubobjectInEditorWorld(UObject* EditorObject, const ISnapshotSubobjectMetaData& ObjectData, const ICustomSnapshotSerializationData& DataStorage)
{
	return FindViewport(EditorObject, ObjectData);
}

UObject* FDisplayClusterConfigurationClusterNodeSerializer::FindSubobjectInEditorWorld(UObject* EditorObject, const ISnapshotSubobjectMetaData& ObjectData, const ICustomSnapshotSerializationData& DataStorage)
{
	return FindViewport(EditorObject, ObjectData);
}
