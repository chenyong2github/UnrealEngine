// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"
#include "Modules/ModuleManager.h"

/** Log category for this module. */
DECLARE_LOG_CATEGORY_EXTERN(LogMediaPlate, Log, All);

class MEDIAPLATE_API FMediaPlateModule : public IModuleInterface
{
public:
	/**
	 * Call this to get the UClass for AMediaPlate.
	 */
	virtual UClass* GetAMediaPlateClass();

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
