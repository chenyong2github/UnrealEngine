// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"


#if defined(PLATFORM_WINDOWS) && PLATFORM_WINDOWS

class FExrReaderGpuModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation start */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	/** IModuleInterface implementation end */
};

#endif

