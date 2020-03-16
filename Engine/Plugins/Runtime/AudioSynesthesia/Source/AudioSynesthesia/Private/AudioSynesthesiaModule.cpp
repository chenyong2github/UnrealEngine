// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioSynesthesiaModule.h"
#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Features/IModularFeatures.h"
#include "LoudnessNRTFactory.h"
#include "AudioSynesthesiaLog.h"

DEFINE_LOG_CATEGORY(LogAudioSynesthesia);

IMPLEMENT_MODULE(Audio::FAudioSynesthesiaModule, AudioSynesthesia)

namespace Audio
{
	void FAudioSynesthesiaModule::StartupModule()
	{
		FModuleManager::Get().LoadModuleChecked<IModuleInterface>("AudioSynesthesiaCore");
	}

	void FAudioSynesthesiaModule::ShutdownModule()
	{
	}
}
