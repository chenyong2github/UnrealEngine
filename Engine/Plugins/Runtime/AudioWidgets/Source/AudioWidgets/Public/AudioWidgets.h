// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AudioSliderStyle.h"
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FAudioWidgetsModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	TSharedPtr<FAudioSliderStyle> AudioSliderStyleSet;
};
