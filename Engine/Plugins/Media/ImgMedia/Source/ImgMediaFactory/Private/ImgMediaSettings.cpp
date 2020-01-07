// Copyright Epic Games, Inc. All Rights Reserved.

#include "ImgMediaSettings.h"


/* UImgMediaSettings structors
 *****************************************************************************/

UImgMediaSettings::UImgMediaSettings()
	: DefaultFrameRate(24, 1)
	, CacheBehindPercentage(25)
	, CacheSizeGB(1.0f)
	, CacheThreads(8)
	, CacheThreadStackSizeKB(128)
	, GlobalCacheSizeGB(1.0f)
	, UseGlobalCache(true)
	, ExrDecoderThreads(0)
	, DefaultProxy(TEXT("proxy"))
	, UseDefaultProxy(false)
{ }

#if WITH_EDITOR
void UImgMediaSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	SettingsChangedDelegate.Broadcast(this);
}

UImgMediaSettings::FOnImgMediaSettingsChanged& UImgMediaSettings::OnSettingsChanged()
{
	return SettingsChangedDelegate;
}

UImgMediaSettings::FOnImgMediaSettingsChanged UImgMediaSettings::SettingsChangedDelegate;
#endif
