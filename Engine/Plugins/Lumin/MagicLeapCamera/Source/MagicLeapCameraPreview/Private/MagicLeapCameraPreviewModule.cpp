// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MagicLeapCameraPreviewModule.h"
#include "MagicLeapCameraPreviewPlayer.h"
#include "Async/Async.h"

DEFINE_LOG_CATEGORY(LogMagicLeapCameraPreview);

TSharedPtr<IMediaPlayer, ESPMode::ThreadSafe> FMagicLeapCameraPreviewModule::CreatePreviewPlayer(class IMediaEventSink& EventSink)
{
	// Disable camera preview on Vulkan until we are able to get it working.
	// Can't call FLuminPlatformMisc::ShouldUseVulkan from here ...
	bool bUseVulkan = false;
	if (GConfig != nullptr)
	{
		GConfig->GetBool(TEXT("/Script/LuminRuntimeSettings.LuminRuntimeSettings"), TEXT("bUseVulkan"), bUseVulkan, GEngineIni);
	}

	if (!bUseVulkan)
	{
		return MakeShareable(new FMagicLeapCameraPreviewPlayer(EventSink));
	}

	return nullptr;
}

IMPLEMENT_MODULE(FMagicLeapCameraPreviewModule, MagicLeapCameraPreview);
