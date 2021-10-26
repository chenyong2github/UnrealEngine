// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Helpers/ReferenceSubobjectSerializer.h"

class ILevelSnapshotsModule;

class FDisplayClusterConfigurationDataSerializer : public TReferenceSubobjectSerializer<FDisplayClusterConfigurationDataSerializer>
{
	static UClass* GetSupportedClass();
	static void MarkPropertiesAsExplicitlyUnsupported(ILevelSnapshotsModule& Module);

public:

	static void Register(ILevelSnapshotsModule& Module);
	
	//~ Begin FDisplayClusterRootActorSerializer Interface
	UObject* FindSubobject(UObject* Owner) const;
	//~ End FDisplayClusterRootActorSerializer Interface
};