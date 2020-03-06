// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/IConsoleManager.h"
#include "RHI.h"

#if PLATFORM_ANDROID
#include "Android/AndroidPlatformMisc.h"

#if USE_ANDROID_JNI
extern bool AndroidThunkCpp_IsOculusMobileApplication();
#endif

#endif

DEFINE_LOG_CATEGORY_STATIC(LogAndroidWindowUtils, Log, All)

namespace AndroidWindowUtils
{
	static void ApplyContentScaleFactor(int32& InOutScreenWidth, int32& InOutScreenHeight)
	{
		const float AspectRatio = (float)InOutScreenWidth / (float)InOutScreenHeight;

		// CSF is a multiplier to 1280x720
		static IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MobileContentScaleFactor"));		

		float RequestedContentScaleFactor = CVar->GetFloat();

		FString CmdLineCSF;
		if (FParse::Value(FCommandLine::Get(), TEXT("mcsf="), CmdLineCSF, false))
		{
			RequestedContentScaleFactor = FCString::Atof(*CmdLineCSF);
		}

		// 0 means to use native size
		if (RequestedContentScaleFactor == 0.0f)
		{
			UE_LOG(LogAndroidWindowUtils, Log, TEXT("Setting Width=%d and Height=%d (requested scale = 0 = auto)"), InOutScreenWidth, InOutScreenHeight);
		}
		else
		{
			int32 Width = InOutScreenWidth;
			int32 Height = InOutScreenHeight;

			if (InOutScreenHeight > InOutScreenWidth)
			{
				Height = 1280 * RequestedContentScaleFactor;
			}
			else
			{
				Height = 720 * RequestedContentScaleFactor;
			}

			// apply the aspect ration to get the width
			Width = (Height * AspectRatio + 0.5f);
			// ensure Width and Height is multiple of 8
			Width = (Width / 8) * 8;
			Height = (Height / 8) * 8;

			// clamp to native resolution
			InOutScreenWidth = FPlatformMath::Min(Width, InOutScreenWidth);
			InOutScreenHeight = FPlatformMath::Min(Height, InOutScreenHeight);

			UE_LOG(LogAndroidWindowUtils, Log, TEXT("Setting Width=%d and Height=%d (requested scale = %f)"), InOutScreenWidth, InOutScreenHeight, RequestedContentScaleFactor);
		}
	}

} // end AndroidWindowUtils