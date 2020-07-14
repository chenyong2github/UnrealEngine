// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Features/IModularFeature.h"
#include "Features/IModularFeatures.h"

class IMaxTickRateHandlerModule : public IModularFeature
{
public:
	static FName GetModularFeatureName()
	{
		static FName FeatureName = FName(TEXT("MaxTickRateHandler"));
		return FeatureName;
	}

	virtual void Initialize() = 0;

	// Return true if waiting occurred in the plugin, if false engine will use the default sleep setup
	virtual bool HandleMaxTickRate(float DesiredMaxTickRate) = 0;
};
