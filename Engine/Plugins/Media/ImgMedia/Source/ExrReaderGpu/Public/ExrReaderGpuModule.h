// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

#if defined(PLATFORM_WINDOWS) && PLATFORM_WINDOWS

DECLARE_LOG_CATEGORY_EXTERN(LogExrReaderGpu, Log, All);

class FExrReaderGpuModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation start */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	/** IModuleInterface implementation end */
};

#endif

