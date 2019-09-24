// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AudioSynesthesiaModule.h"
#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Features/IModularFeatures.h"

DEFINE_LOG_CATEGORY(LogAudioSynesthesia);

IMPLEMENT_MODULE(Audio::FAudioSynesthesiaModule, AudioSynesthesia)

namespace Audio
{
	void FAudioSynesthesiaModule::StartupModule()
	{
	}

	void FAudioSynesthesiaModule::ShutdownModule()
	{
	}
}
