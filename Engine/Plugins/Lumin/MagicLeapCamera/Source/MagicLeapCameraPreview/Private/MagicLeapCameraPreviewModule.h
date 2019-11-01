// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "Containers/Ticker.h"
#include "IMagicLeapCameraPreviewModule.h"

class IMediaPlayer;

DECLARE_LOG_CATEGORY_EXTERN(LogMagicLeapCameraPreview, Verbose, All);

/**
 * The public interface to this module.  In most cases, this interface is only public to sibling modules
 * within this plugin.
 */
class FMagicLeapCameraPreviewModule : public IMagicLeapCameraPreviewModule
{
public:
	TSharedPtr<IMediaPlayer, ESPMode::ThreadSafe> CreatePreviewPlayer(class IMediaEventSink& EventSink) override;
};
