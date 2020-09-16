// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class FAssetPlacementEdMode : public IModuleInterface
{
public:
	virtual void StartupModule();
	virtual void ShutdownModule();
};
