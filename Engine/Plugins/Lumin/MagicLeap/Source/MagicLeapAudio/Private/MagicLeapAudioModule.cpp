// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapAudioModule.h"
#include "Engine/Engine.h"

DEFINE_LOG_CATEGORY(LogMagicLeapAudio);

FMagicLeapAudioModule::FMagicLeapAudioModule()
: PrevOutputDevice(EOutputDevice::Undertermined)
{
}

FAudioDevice* FMagicLeapAudioModule::CreateAudioDevice()
{
#if PLATFORM_WINDOWS || PLATFORM_MAC
	const IMagicLeapPlugin& MLPlugin = IMagicLeapPlugin::Get();
	if (MLPlugin.IsMagicLeapHMDValid() && MLPlugin.UseMLAudioForZI())
	{
		// For an audio mixer module to be used in PIE, 
		// it needs to have been successfully created on editor launch, even though a new instance is created for PIE.
		// If MLRemote is not already running when the editor is launched, FMixerPlatformMagicLeap will cause the engine to crash.
		// Thus, create the actual ML audio mixer only if an ML HMD is connected i.e. MLRemote is running.
		// Otherwise, use a fake audio mixer so this module gets registered on editor startup, and works without crashing.
		if (UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayConnected())
		{
			return new Audio::FMixerDevice(new Audio::FMixerPlatformMagicLeap());
		}
	}

	return new Audio::FMixerDevice(new Audio::FFakeAudioMixerMagicLeap());
#elif PLATFORM_LUMIN
	return new Audio::FMixerDevice(new Audio::FMixerPlatformMagicLeap());
#else
	return nullptr;
#endif // PLATFORM_LUMIN
}

bool FMagicLeapAudioModule::SetOnAudioJackPluggedDelegate(const FMagicLeapAudioJackPluggedDelegateMulti& ResultDelegate)
{
	OnAudioJackPluggedDelegateMulti = ResultDelegate;
	return true;
}

bool FMagicLeapAudioModule::SetOnAudioJackUnpluggedDelegate(const FMagicLeapAudioJackUnpluggedDelegateMulti& ResultDelegate)
{
	OnAudioJackUnpluggedDelegateMulti = ResultDelegate;
	return true;
}

bool FMagicLeapAudioModule::CheckOutputDevice()
{
#if WITH_MLSDK
	// Output device changes are in human time, and MLAudioGetOutputDevice is an expensive call
	static constexpr double CheckOutputDeviceRate = 1.0;
	const auto CurrentTime = FPlatformTime::Seconds();
	if ((CurrentTime - LastOutputDeviceCheckedTime) >= CheckOutputDeviceRate)
	{
		LastOutputDeviceCheckedTime = CurrentTime;
		MLAudioOutputDevice CurrOutputDevice;
		MLResult Result = MLAudioGetOutputDevice(&CurrOutputDevice);
		if (Result != MLResult_Ok)
		{
			UE_LOG(LogMagicLeapAudio, Error, TEXT("MLAudioGetOutputDevice failed with error '%s'."), UTF8_TO_TCHAR(MLAudioGetResultString(Result)));
			return false;
		}

		if (CurrOutputDevice == MLAudioOutputDevice_Wearable)
		{
			if (OnAudioJackUnpluggedDelegateMulti.IsBound() && PrevOutputDevice != EOutputDevice::Wearable)
			{
				PrevOutputDevice = EOutputDevice::Wearable;
				OnAudioJackUnpluggedDelegateMulti.Broadcast();
			}
		}
		else if (OnAudioJackPluggedDelegateMulti.IsBound() && PrevOutputDevice != EOutputDevice::AudioJack)
		{
			PrevOutputDevice = EOutputDevice::AudioJack;
			OnAudioJackPluggedDelegateMulti.Broadcast();
		}
	}
	return true;
#else
	return false;
#endif // WITH_MLSDK
}

IMPLEMENT_MODULE(FMagicLeapAudioModule, MagicLeapAudio);
