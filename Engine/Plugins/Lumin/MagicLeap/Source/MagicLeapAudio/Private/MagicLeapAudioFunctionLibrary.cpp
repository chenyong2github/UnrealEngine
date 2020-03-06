// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapAudioFunctionLibrary.h"
#include "MagicLeapAudioModule.h"

bool UMagicLeapAudioFunctionLibrary::SetOnAudioJackPluggedDelegate(const FMagicLeapAudioJackPluggedDelegate& InResultDelegate)
{
	FMagicLeapAudioJackPluggedDelegateMulti ResultDelegate;
	ResultDelegate.Add(InResultDelegate);
	return GetMagicLeapAudioModule().SetOnAudioJackPluggedDelegate(ResultDelegate);
}

bool UMagicLeapAudioFunctionLibrary::SetOnAudioJackUnpluggedDelegate(const FMagicLeapAudioJackUnpluggedDelegate& InResultDelegate)
{
	FMagicLeapAudioJackUnpluggedDelegateMulti ResultDelegate;
	ResultDelegate.Add(InResultDelegate);
	return GetMagicLeapAudioModule().SetOnAudioJackUnpluggedDelegate(ResultDelegate);
}

bool UMagicLeapAudioFunctionLibrary::SetMicMute(bool IsMuted)
{
#if WITH_MLSDK
	auto Result = MLAudioSetMicMute(IsMuted);
	if (Result == MLResult_Ok)
	{
		return true;
	}
#endif
	return false;
}

bool UMagicLeapAudioFunctionLibrary::IsMicMuted()
{
#if WITH_MLSDK
	auto IsMuted = false;
	auto Result = MLAudioIsMicMuted(&IsMuted);
	if (Result == MLResult_Ok)
	{
		return IsMuted;
	}
#endif
	return false;
}