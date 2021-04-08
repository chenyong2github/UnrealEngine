// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSnapshotsModule.h"

#include "EngineUtils.h"
#include "Modules/ModuleManager.h"

FLevelSnapshotsModule& FLevelSnapshotsModule::Get()
{
	return FModuleManager::Get().GetModuleChecked<FLevelSnapshotsModule>("LevelSnapshots");
}

void FLevelSnapshotsModule::StartupModule()
{}

void FLevelSnapshotsModule::ShutdownModule()
{}
	
IMPLEMENT_MODULE(FLevelSnapshotsModule, LevelSnapshots)