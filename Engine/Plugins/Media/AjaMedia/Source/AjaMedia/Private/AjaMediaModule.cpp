// Copyright Epic Games, Inc. All Rights Reserved.

#include "IAjaMediaModule.h"

#include "Aja/Aja.h"
#include "AjaDeviceProvider.h"
#include "AJALib.h"
#include "Player/AjaMediaPlayer.h"

#include "CoreMinimal.h"
#include "IMediaIOCoreModule.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogAjaMedia);

#define LOCTEXT_NAMESPACE "AjaMediaModule"

/**
 * Implements the AJAMedia module.
 */
class FAjaMediaModule : public IAjaMediaModule
{
public:

	//~ IAjaMediaModule interface
	virtual TSharedPtr<IMediaPlayer, ESPMode::ThreadSafe> CreatePlayer(IMediaEventSink& EventSink) override
	{
		if (!FAja::IsInitialized())
		{
			return nullptr;
		}

		return MakeShared<FAjaMediaPlayer, ESPMode::ThreadSafe>(EventSink);
	}

	virtual bool IsInitialized() const override { return FAja::IsInitialized(); }

	virtual bool CanBeUsed() const override { return FAja::CanUseAJACard(); }

public:

	//~ IModuleInterface interface
	virtual void StartupModule() override
	{
		// initialize AJA
		if (!FAja::Initialize())
		{
			UE_LOG(LogAjaMedia, Error, TEXT("Failed to initialize AJA"));
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
		FAja::Shutdown();
	}

private:
	FAjaDeviceProvider DeviceProvider;
};

IMPLEMENT_MODULE(FAjaMediaModule, AjaMedia);

#undef LOCTEXT_NAMESPACE
