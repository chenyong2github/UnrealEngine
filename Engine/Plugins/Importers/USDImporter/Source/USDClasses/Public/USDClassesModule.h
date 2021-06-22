// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class IUsdClassesModule : public IModuleInterface
{
public:
	/** Updates all plugInfo.json to point their LibraryPaths to TargetDllFolder */
	USDCLASSES_API static void UpdatePlugInfoFiles( const FString& PluginDirectory, const FString& TargetDllFolder );
};