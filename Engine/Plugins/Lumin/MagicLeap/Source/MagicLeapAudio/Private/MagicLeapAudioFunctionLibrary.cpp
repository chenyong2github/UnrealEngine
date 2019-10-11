// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
