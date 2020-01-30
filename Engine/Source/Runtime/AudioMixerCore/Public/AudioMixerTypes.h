// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Misc/ConfigCacheIni.h"

namespace Audio {

	namespace EAudioMixerPlatformApi
	{
		enum Type
		{
			XAudio2, 	// Windows, XBoxOne
			AudioOut, 	// PS4
			CoreAudio, 	// Mac
			AudioUnit, 	// iOS
			SDL2,		// Linux
			OpenSLES, 	// Android
			Switch, 	// Switch
			Null		// Unknown/not Supported
		};
	}

	namespace EAudioMixerStreamDataFormat
	{
		enum Type
		{
			Unknown,
			Float,
			Int16,
			Unsupported
		};
	}

	/**
	 * EAudioOutputStreamState
	 * Specifies the state of the output audio stream.
	 */
	namespace EAudioOutputStreamState
	{
		enum Type
		{
			/* The audio stream is shutdown or not uninitialized. */
			Closed,
		
			/* The audio stream is open but not running. */
			Open,

			/** The audio stream is open but stopped. */
			Stopped,
		
			/** The audio output stream is stopping. */
			Stopping,

			/** The audio output stream is open and running. */
			Running,
		};
	}

}

struct AUDIOMIXERCORE_API FAudioPlatformSettings
{
	/** Sample rate to use on the platform for the mixing engine. Higher sample rates will incur more CPU cost. */
	int32 SampleRate;

	/** The amount of audio to compute each callback block. Lower values decrease latency but may increase CPU cost. */
	int32 CallbackBufferFrameSize;

	/** The number of buffers to keep enqueued. More buffers increases latency, but can compensate for variable compute availability in audio callbacks on some platforms. */
	int32 NumBuffers;

	/** The max number of channels to limit for this platform. The max channels used will be the minimum of this value and the global audio quality settings. A value of 0 will not apply a platform channel count max. */
	int32 MaxChannels;

	/** The number of workers to use to compute source audio. Will only use up to the max number of sources. Will evenly divide sources to each source worker. */
	int32 NumSourceWorkers;

	static FAudioPlatformSettings GetPlatformSettings(const TCHAR* PlatformSettingsConfigFile)
	{
		FAudioPlatformSettings Settings;

		FString TempString;

		if (GConfig->GetString(PlatformSettingsConfigFile, TEXT("AudioSampleRate"), TempString, GEngineIni))
		{
			Settings.SampleRate = FMath::Max(FCString::Atoi(*TempString), 8000);
		}

		if (GConfig->GetString(PlatformSettingsConfigFile, TEXT("AudioCallbackBufferFrameSize"), TempString, GEngineIni))
		{
			Settings.CallbackBufferFrameSize = FMath::Max(FCString::Atoi(*TempString), 256);
		}

		if (GConfig->GetString(PlatformSettingsConfigFile, TEXT("AudioNumBuffersToEnqueue"), TempString, GEngineIni))
		{
			Settings.NumBuffers = FMath::Max(FCString::Atoi(*TempString), 1);
		}

		if (GConfig->GetString(PlatformSettingsConfigFile, TEXT("AudioMaxChannels"), TempString, GEngineIni))
		{
			Settings.MaxChannels = FMath::Max(FCString::Atoi(*TempString), 0);
		}

		if (GConfig->GetString(PlatformSettingsConfigFile, TEXT("AudioNumSourceWorkers"), TempString, GEngineIni))
		{
			Settings.NumSourceWorkers = FMath::Max(FCString::Atoi(*TempString), 0);
		}

		return Settings;
	}

	FAudioPlatformSettings()
		: SampleRate(48000)
		, CallbackBufferFrameSize(1024)
		, NumBuffers(2)
		, MaxChannels(32)
		, NumSourceWorkers(0)
	{
	}
};
