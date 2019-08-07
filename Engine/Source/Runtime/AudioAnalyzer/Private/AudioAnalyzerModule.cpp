// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "AudioAnalyzerModule.h"

IMPLEMENT_MODULE(FAudioAnalyzerModule, AudioAnalyzer);

DEFINE_LOG_CATEGORY(LogAudioAnalyzer);

void FAudioAnalyzerModule::StartupModule()
{
	int x = 32;
}

void FAudioAnalyzerModule::ShutdownModule()
{
}
