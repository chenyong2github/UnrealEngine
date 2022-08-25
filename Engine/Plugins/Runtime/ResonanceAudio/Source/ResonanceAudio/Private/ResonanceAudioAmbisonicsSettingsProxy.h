//
// Copyright (C) Google Inc. 2017. All rights reserved.
//

#pragma once

#include "ResonanceAudioAmbisonicsSettings.h"

#include "AudioDevice.h"
#include "HAL/LowLevelMemTracker.h"
#include "ResonanceAudioCommon.h"

class FResonanceAmbisonicsSettingsProxy : public ISoundfieldEncodingSettingsProxy
{
public:
	vraudio::RenderingMode RenderingMode;

	virtual uint32 GetUniqueId() const override
	{
		return static_cast<uint32>(RenderingMode);
	}

	virtual TUniquePtr<ISoundfieldEncodingSettingsProxy> Duplicate() const override
	{
		LLM_SCOPE_BYTAG(AudioSpatializationPlugins);
		FResonanceAmbisonicsSettingsProxy* Proxy = new FResonanceAmbisonicsSettingsProxy();
		Proxy->RenderingMode = RenderingMode;
		return TUniquePtr<ISoundfieldEncodingSettingsProxy>(Proxy);
	}
};
