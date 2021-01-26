// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTypes.h"

#include "Modules/ModuleManager.h"
#include "Misc/ConfigCacheIni.h"

#include "PlayerRuntimeGlobal.h"
#include "Core/MediaInterlocked.h"
#include "Renderer/RendererVideo.h"

#include "Renderer/RendererAudio.h"
#include "Decoder/VideoDecoderH264.h"
#include "Decoder/AudioDecoderAAC.h"

#include "ElectraPlayerPrivate.h"
#include "IElectraPlayerRuntimeModule.h"
#include "ElectraPlayer.h"
#include "ElectraPlayerPlatform.h"

DEFINE_LOG_CATEGORY(LogElectraPlayer);
DEFINE_LOG_CATEGORY(LogElectraPlayerStats);

#define LOCTEXT_NAMESPACE "ElectraPlayerRuntimeModule"

// -----------------------------------------------------------------------------------------------------------------------------------

// Implements the ElectraPlayer Runtime Module module.
class FElectraPlayerRuntimeModule : public IElectraPlayerRuntimeModule
{
public:
	// IElectraPlayerRuntimeModule interface

	virtual bool IsInitialized() const override
	{
		return bInitialized;
	}

public:
	// IModuleInterface interface

	void StartupModule() override
	{
		check(!bInitialized);

		if (!Electra::PlatformEarlyStartup())
		{

			return;
		}

		// Hook-up platform memory system (if any)
		Electra::PlatformMemorySetup();

		// Core
		Electra::Configuration ElectraConfig;
		FString AnalyticsEventsFromIni;
	
		// Read Analytics Events from ini configuring the events sent during playback
		if (GConfig->GetString(TEXT("ElectraPlayer"), TEXT("AnalyticsEvents"), AnalyticsEventsFromIni, GEngineIni))
		{
			// Parse comma delimited strings into arrays
			TArray<FString> EnabledAnalyticsEvents;
			AnalyticsEventsFromIni.ParseIntoArray(EnabledAnalyticsEvents, TEXT(","), /*bCullEmpty=*/true);
			for (auto EnabledEvent : EnabledAnalyticsEvents)
			{
				EnabledEvent.TrimStartAndEndInline();
				ElectraConfig.EnabledAnalyticsEvents.Add(EnabledEvent, true);
			}
		}
		Electra::Startup(ElectraConfig);

		// Video renderer
		FElectraRendererVideo::SystemConfiguration SysCfgVideo;
		FElectraRendererVideo::Startup(SysCfgVideo);
		// Audio renderer
		FElectraRendererAudio::SystemConfiguration SysCfgAudio;
		FElectraRendererAudio::Startup(SysCfgAudio);

		// H.264 decoder
		IVideoDecoderH264::FSystemConfiguration SysCfgH264;
		IVideoDecoderH264::Startup(SysCfgH264);

		// AAC decoder
		IAudioDecoderAAC::FSystemConfiguration SysCfgAAC;
		IAudioDecoderAAC::Startup(SysCfgAAC);

		bInitialized = true;
	}

	void ShutdownModule() override
	{
		if (bInitialized)
		{
			Electra::WaitForAllPlayersToHaveTerminated();
			IAudioDecoderAAC::Shutdown();
			IVideoDecoderH264::Shutdown();
			FElectraRendererAudio::Shutdown();
			FElectraRendererVideo::Shutdown();
			Electra::Shutdown();
			Electra::PlatformShutdown();
			bInitialized = false;
		}
	}

private:
	bool bInitialized = false;
};

IMPLEMENT_MODULE(FElectraPlayerRuntimeModule, ElectraPlayerRuntime);

#undef LOCTEXT_NAMESPACE

