// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogAudioAnalyzer, Log, All);

class FAudioAnalyzerModule : public IModuleInterface
{
	public:
	/** IModuleInterface implementation */
	void StartupModule();
	void ShutdownModule();
};
