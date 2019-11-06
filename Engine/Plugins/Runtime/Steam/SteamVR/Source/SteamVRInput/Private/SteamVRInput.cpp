// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SteamVRInput.h"
#include "CoreMinimal.h"
#include "Runtime/Core/Public/Misc/Paths.h"
#include "openvr.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"

#define LOCTEXT_NAMESPACE "FSteamVRInputModule"

DEFINE_LOG_CATEGORY_STATIC(LogSteamInput, Log, All);

void FSteamVRInputModule::StartupModule()
{
}

void FSteamVRInputModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FSteamVRInputModule, SteamVRInput)
