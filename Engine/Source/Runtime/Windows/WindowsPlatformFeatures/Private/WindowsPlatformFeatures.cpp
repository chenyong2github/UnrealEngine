// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "WindowsPlatformFeatures.h"
#include "WmfPrivate.h"
#include "WindowsVideoRecordingSystem.h"
#include "Misc/CommandLine.h"

IMPLEMENT_MODULE(FWindowsPlatformFeaturesModule, WindowsPlatformFeatures);

WINDOWSPLATFORMFEATURES_START

FWindowsPlatformFeaturesModule::FWindowsPlatformFeaturesModule()
{
	// load generic modules
	StartupModules();
}

IVideoRecordingSystem* FWindowsPlatformFeaturesModule::GetVideoRecordingSystem()
{
	static FWindowsVideoRecordingSystem VideoRecordingSystem;
	return &VideoRecordingSystem;
}

bool FWindowsPlatformFeaturesModule::StartupModules()
{
	FModuleManager::Get().LoadModule(TEXT("GameplayMediaEncoder"));
	return true;
}

WINDOWSPLATFORMFEATURES_END
