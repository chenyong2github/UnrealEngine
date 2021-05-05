// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class IWorldPartitionHLODUtilities;

class IWorldPartitionHLODUtilitiesModule : public IModuleInterface
{
public:
	virtual IWorldPartitionHLODUtilities* GetUtilities() = 0;
};

/**
* IWorldPartitionHLODUtilities module interface
*/
class WORLDPARTITIONHLODUTILITIES_API FWorldPartitionHLODUtilitiesModule : public IWorldPartitionHLODUtilitiesModule
{
public:
	virtual void ShutdownModule() override;
	virtual void StartupModule() override;

	virtual IWorldPartitionHLODUtilities* GetUtilities() override;

private:
	IWorldPartitionHLODUtilities* Utilities;
};
