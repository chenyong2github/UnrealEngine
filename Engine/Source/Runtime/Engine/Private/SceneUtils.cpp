// Copyright Epic Games, Inc. All Rights Reserved.

#include "SceneUtils.h"

bool IsMobileHDR()
{
	static auto* MobileHDRCvar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MobileHDR"));
	return MobileHDRCvar->GetValueOnAnyThread() == 1;
}

ENGINE_API EMobileHDRMode GetMobileHDRMode()
{
	EMobileHDRMode HDRMode = EMobileHDRMode::EnabledFloat16;

	if (!IsMobileHDR())
	{
		HDRMode = EMobileHDRMode::Disabled;
	}
	
	return HDRMode;
}

ENGINE_API bool IsMobileColorsRGB()
{
	static auto* MobileUseHWsRGBEncodingCVAR = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.UseHWsRGBEncoding"));
	const bool bMobileUseHWsRGBEncoding = (MobileUseHWsRGBEncodingCVAR && MobileUseHWsRGBEncodingCVAR->GetValueOnAnyThread() == 1);

	return !IsMobileHDR() && bMobileUseHWsRGBEncoding;
}
