// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Restorability/Serialization/ICustomObjectSnapshotSerializer.h"

class ILevelSnapshotsModule;

class FDisplayClusterConfigurationClusterNodeSerializer
	:
	public ICustomObjectSnapshotSerializer,
	public TSharedFromThis<FDisplayClusterConfigurationClusterNodeSerializer>
{
public:
	
	static void BlacklistCustomProperties(ILevelSnapshotsModule& Module);
	static void UnblacklistCustomProperties(ILevelSnapshotsModule& Module);
	
	//~ Begin ICustomObjectSnapshotSerializer Interface
	virtual void OnTakeSnapshot(UObject* EditorObject, ICustomSnapshotSerializationData& DataStorage) override;
	virtual UObject* FindOrRecreateSubobjectInSnapshotWorld(UObject* SnapshotObject, const ISnapshotSubobjectMetaData& ObjectData, const ICustomSnapshotSerializationData& DataStorage) override;
	virtual UObject* FindOrRecreateSubobjectInEditorWorld(UObject* EditorObject, const ISnapshotSubobjectMetaData& ObjectData, const ICustomSnapshotSerializationData& DataStorage) override;
	virtual UObject* FindSubobjectInEditorWorld(UObject* EditorObject, const ISnapshotSubobjectMetaData& ObjectData, const ICustomSnapshotSerializationData& DataStorage) override;
	//~ Begin ICustomObjectSnapshotSerializer Interface
};
