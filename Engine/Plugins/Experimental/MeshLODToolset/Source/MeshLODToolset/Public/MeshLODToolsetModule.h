// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Templates/SharedPointer.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMeshLODToolset, Log, All);

class IAssetTypeActions;

class FMeshLODToolsetModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;


	// registered asset actions
	TArray<TSharedRef<IAssetTypeActions>> RegisteredAssetTypeActions;
};
