// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AudioSynesthesiaModule.h"
#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Features/IModularFeatures.h"
#include "LoudnessNRTFactory.h"

DEFINE_LOG_CATEGORY(LogAudioSynesthesia);

IMPLEMENT_MODULE(Audio::FAudioSynesthesiaModule, AudioSynesthesia)

namespace Audio
{
	void FAudioSynesthesiaModule::StartupModule()
	{
		// Register factories on startup
		IModularFeatures::Get().RegisterModularFeature(FLoudnessNRTFactory::GetModularFeatureName(), &LoudnessFactory);
	}

	void FAudioSynesthesiaModule::ShutdownModule()
	{
		// Unregister factories on shutdown
		IModularFeatures::Get().UnregisterModularFeature(FLoudnessNRTFactory::GetModularFeatureName(), &LoudnessFactory);
	}
}
