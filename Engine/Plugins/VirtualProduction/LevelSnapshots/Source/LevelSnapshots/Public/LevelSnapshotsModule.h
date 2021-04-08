// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "PreviewScene.h"

class LEVELSNAPSHOTS_API FLevelSnapshotsModule : public IModuleInterface
{
public:

	static FLevelSnapshotsModule& Get();
	
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	
};
