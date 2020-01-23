// Copyright Epic Games, Inc. All Rights Reserved.

#include "IBlackmagicMediaModule.h"

#include "Blackmagic/Blackmagic.h"
#include "BlackmagicDeviceProvider.h"
#include "BlackmagicMediaPrivate.h"
#include "BlackmagicMediaPlayer.h"

#include "IMediaIOCoreModule.h"
#include "Modules/ModuleManager.h"


DEFINE_LOG_CATEGORY(LogBlackmagicMedia);

#define LOCTEXT_NAMESPACE "BlackmagicMediaModule"

/**
 * Implements the NdiMedia module.
 */
class FBlackmagicMediaModule : public IBlackmagicMediaModule
{
public:

	//~ IBlackmagicMediaModule interface
	virtual TSharedPtr<IMediaPlayer, ESPMode::ThreadSafe> CreatePlayer(IMediaEventSink& EventSink) override
	{
		if (!FBlackmagic::IsInitialized())
		{
			return nullptr;
		}

		return MakeShared<FBlackmagicMediaPlayer, ESPMode::ThreadSafe>(EventSink);
	}

	virtual bool IsInitialized() const override { return FBlackmagic::IsInitialized(); }

	virtual bool CanBeUsed() const override { return FBlackmagic::CanUseBlackmagicCard(); }

public:

	//~ IModuleInterface interface
	virtual void StartupModule() override
	{
		// initialize
		if (!FBlackmagic::Initialize())
		{
			UE_LOG(LogBlackmagicMedia, Error, TEXT("Failed to initialize Blackmagic"));
			return;
		}

		
		IMediaIOCoreModule::Get().RegisterDeviceProvider(&DeviceProvider);
	}

	virtual void ShutdownModule() override
	{
		if (IMediaIOCoreModule::IsAvailable())
		{
			IMediaIOCoreModule::Get().UnregisterDeviceProvider(&DeviceProvider);
		}

		FBlackmagic::Shutdown();
	}

private:
	FBlackmagicDeviceProvider DeviceProvider;
};

IMPLEMENT_MODULE(FBlackmagicMediaModule, BlackmagicMedia);

#undef LOCTEXT_NAMESPACE
