// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Helpers/MapSubobjectSerializer.h"

class FScriptMapHelper;
class ILevelSnapshotsModule;

class FDisplayClusterConfigurationClusterNodeSerializer : public TMapSubobjectSerializer<FDisplayClusterConfigurationClusterNodeSerializer>
{
	static UClass* GetSupportedClass();
	static void BlacklistCustomProperties(ILevelSnapshotsModule& Module);

public:
	
	static void Register(ILevelSnapshotsModule& Module);
	
	//~ Begin TMapSubobjectSerializer Interface
	static const FMapProperty* GetMapProperty();
	//~ End TMapSubobjectSerializer Interface
};
