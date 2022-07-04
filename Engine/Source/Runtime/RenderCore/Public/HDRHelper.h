// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

RENDERCORE_API void HDRGetMetaData(EDisplayOutputFormat& OutDisplayOutputFormat, EDisplayColorGamut& OutDisplayColorGamut, bool& OutbHDRSupported,
								   const FVector2D& WindowTopLeft, const FVector2D& WindowBottomRight, void* OSWindow);

RENDERCORE_API void HDRConfigureCVars(bool bIsHDREnabled, uint32 DisplayNits, bool bFromGameSettings);
RENDERCORE_API EDisplayOutputFormat HDRGetDefaultDisplayOutputFormat();
RENDERCORE_API EDisplayColorGamut HDRGetDefaultDisplayColorGamut();
RENDERCORE_API void HDRAddCustomMetaData(void* OSWindow, EDisplayOutputFormat DisplayOutputFormat, EDisplayColorGamut DisplayColorGamut, bool bHDREnabled);
RENDERCORE_API void HDRRemoveCustomMetaData(void* OSWindow);
RENDERCORE_API FMatrix44f GamutToXYZMatrix(EDisplayColorGamut ColorGamut);
RENDERCORE_API FMatrix44f XYZToGamutMatrix(EDisplayColorGamut ColorGamut);


struct FDisplayInformation
{
	FDisplayInformation()
		: DesktopCoordinates(0, 0, 0, 0)
		, bHDRSupported(false)
	{}

	FIntRect DesktopCoordinates;
	bool   bHDRSupported;
};
