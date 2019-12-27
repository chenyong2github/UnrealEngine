// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IMagicLeapPlugin.h"
#include "IMagicLeapAudioModule.h"
#include "MagicLeapAudioTypes.h"
#include "AudioMixer.h"
#include "AudioMixerPlatformMagicLeap.h"
#include "FakeAudioMixerMagicLeap.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Lumin/CAPIShims/LuminAPIAudio.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMagicLeapAudio, Verbose, All);

class FMagicLeapAudioModule : public IMagicLeapAudioModule
{
public:
	FMagicLeapAudioModule();
	FAudioDevice* CreateAudioDevice() override;
	bool SetOnAudioJackPluggedDelegate(const FMagicLeapAudioJackPluggedDelegateMulti& ResultDelegate);
	bool SetOnAudioJackUnpluggedDelegate(const FMagicLeapAudioJackUnpluggedDelegateMulti& ResultDelegate);
	bool CheckOutputDevice();

private:
	enum EOutputDevice
	{
		Undertermined,
		Wearable,
		AudioJack,
	};
	EOutputDevice PrevOutputDevice;
	FMagicLeapAudioJackPluggedDelegateMulti OnAudioJackPluggedDelegateMulti;
	FMagicLeapAudioJackUnpluggedDelegateMulti OnAudioJackUnpluggedDelegateMulti;
	double LastOutputDeviceCheckedTime = 0.0;
};

inline FMagicLeapAudioModule& GetMagicLeapAudioModule()
{
	return FModuleManager::Get().GetModuleChecked<FMagicLeapAudioModule>("MagicLeapAudio");
}
