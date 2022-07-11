// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioSynesthesiaModule.h"

#include "AudioAnalyzerModule.h"
#include "AudioSynesthesiaLog.h"
#include "CoreMinimal.h"
#include "Features/IModularFeatures.h"
#include "HAL/LowLevelMemTracker.h"
#include "LoudnessNRTFactory.h"
#include "Modules/ModuleInterface.h"

DEFINE_LOG_CATEGORY(LogAudioSynesthesia);

IMPLEMENT_MODULE(Audio::FAudioSynesthesiaModule, AudioSynesthesia)

namespace Audio
{
	void FAudioSynesthesiaModule::StartupModule()
	{
		LLM_SCOPE_BYTAG(AudioAnalysis);
		FModuleManager::Get().LoadModuleChecked<IModuleInterface>("AudioSynesthesiaCore");
		FModuleManager::Get().LoadModuleChecked<FAudioAnalyzerModule>("AudioAnalyzer");
	}

	void FAudioSynesthesiaModule::ShutdownModule()
	{
	}
}
