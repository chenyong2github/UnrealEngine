// Copyright Epic Games, Inc. All Rights Reserved.

#include "Retargeter/IKRetargetSettings.h"
#include "Retargeter/IKRetargeter.h"

void FTargetChainSettings::CopySettingsFromAsset(const URetargetChainSettings* AssetChainSettings)
{
	*this = AssetChainSettings->Settings;
}

void FTargetRootSettings::CopySettingsFromAsset(const URetargetRootSettings* AssetRootSettings)
{
	*this = AssetRootSettings->Settings;
}
