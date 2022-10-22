// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sound/AudioFormatSettings.h"
#include "Sound/SoundWave.h"
#include "Algo/Transform.h"
#include "Misc/ConfigCacheIni.h"
#include "Containers/UnrealString.h"
#include "Audio.h"
#include "AudioMixerTypes.h"

namespace Audio
{
	bool ShouldAllowPlatformSpecificFormats()
	{
		static bool IsAudioLinkEnabled = []() -> bool
		{
			bool bAvailable = IModularFeatures::Get().IsModularFeatureAvailable(TEXT("AudioLink Factory"));
			UE_CLOG(bAvailable,LogAudio, Display, TEXT("AudioLink is enabled, disabling platform specific AudioFormats."));
			return bAvailable;
		}();
		return !IsAudioLinkEnabled;
	}

	FAudioFormatSettings::FAudioFormatSettings(FConfigCacheIni* InConfigSystem, const FString& InConfigFilename, const FString& InPlatformIdentifierForLogging)
	{
		ReadConfiguration(InConfigSystem, InConfigFilename, InPlatformIdentifierForLogging);
	}

	FName FAudioFormatSettings::GetWaveFormat(const USoundWave* Wave) const
	{
		FName FormatName = Audio::ToName(Wave->GetSoundAssetCompressionType());
		if (FormatName == Audio::NAME_PLATFORM_SPECIFIC)
		{
			if (ShouldAllowPlatformSpecificFormats())
			{
				if (Wave->IsStreaming())
				{
					return PlatformStreamingFormat;
				}
				else
				{
					return PlatformFormat;
				}
			}
			else
			{
				return FallbackFormat;
			}
		}
		return FormatName;
	}

	void FAudioFormatSettings::GetAllWaveFormats(TArray<FName>& OutFormats) const
	{
		OutFormats = AllWaveFormats;
	}

	void FAudioFormatSettings::GetWaveFormatModuleHints(TArray<FName>& OutHints) const
	{
		OutHints = WaveFormatModuleHints;
	}

	void FAudioFormatSettings::ReadConfiguration(FConfigCacheIni* InConfigSystem, const FString& InConfigFilename, const FString& InPlatformIdentifierForLogging)
	{
		auto ToFName = [](const FString& InName) -> FName { return { *InName }; };

		TArray<FString> FormatNames;
		if (ensure(InConfigSystem->GetArray(TEXT("Audio"), TEXT("AllWaveFormats"), FormatNames, InConfigFilename)))
		{
			Algo::Transform(FormatNames, AllWaveFormats, ToFName);
		}

		TArray<FString> FormatModuleHints;
		if (InConfigSystem->GetArray(TEXT("Audio"), TEXT("FormatModuleHints"), FormatModuleHints, InConfigFilename))
		{
			Algo::Transform(FormatNames, WaveFormatModuleHints, ToFName);
		}

		FString FallbackFormatString;
		if (ensure(InConfigSystem->GetString(TEXT("Audio"), TEXT("FallbackFormat"), FallbackFormatString, InConfigFilename)))
		{
			FallbackFormat = *FallbackFormatString;

			if (!AllWaveFormats.Contains(FallbackFormat) && AllWaveFormats.Num() > 0)
			{
				UE_LOG(LogAudio, Warning, TEXT("FallbackFormat '%s' not defined in 'AllWaveFormats'. Using first format listed '%s'"), *FallbackFormatString, *AllWaveFormats[0].ToString());
				FallbackFormat = AllWaveFormats[0];
			}
		}

		FString PlatformFormatString;
		if (ensure(InConfigSystem->GetString(TEXT("Audio"), TEXT("PlatformFormat"), PlatformFormatString, InConfigFilename)))
		{
			PlatformFormat = *PlatformFormatString;

			if (!AllWaveFormats.Contains(PlatformFormat))
			{
				UE_LOG(LogAudio, Warning, TEXT("PlatformStreamingFormat '%s' not defined in 'AllWaveFormats'. Using fallback format '%s'"), *PlatformFormatString, *FallbackFormat.ToString());
				PlatformFormat = FallbackFormat;
			}
		}

		FString PlatformStreamingFormatString;
		if (ensure(InConfigSystem->GetString(TEXT("Audio"), TEXT("PlatformStreamingFormat"), PlatformStreamingFormatString, InConfigFilename)))
		{
			PlatformStreamingFormat = *PlatformStreamingFormatString;
			if (!AllWaveFormats.Contains(PlatformStreamingFormat))
			{
				UE_LOG(LogAudio, Warning, TEXT("PlatformStreamingFormat '%s' not defined in 'AllWaveFormats'. Using fallback format '%s'"), *PlatformStreamingFormatString, *FallbackFormat.ToString());
				PlatformStreamingFormat = FallbackFormat;
			}
		}

	#if !NO_LOGGING

		// Display log for sanity
		FString AllFormatsConcat;
		for (FName i : AllWaveFormats)
		{
			AllFormatsConcat += i.ToString() + TEXT(" ");
		}

		UE_LOG(LogAudio, Verbose, TEXT("AudioFormatSettings: TargetName='%s', AllWaveFormats=( %s), PlatformFormat='%s', PlatformStreamingFormat='%s', FallbackFormat='%s'"),
			*InPlatformIdentifierForLogging, *AllFormatsConcat, *PlatformFormat.ToString(), *PlatformStreamingFormat.ToString(), *FallbackFormat.ToString());

	#endif //!NO_LOGGING
	}

}// namespace Audio
