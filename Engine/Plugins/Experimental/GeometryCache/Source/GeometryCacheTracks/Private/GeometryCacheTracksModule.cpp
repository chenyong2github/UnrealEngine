// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "GeometryCacheTracksModule.h"
#if WITH_EDITOR
#include "GeometryCacheSequencerModule.h"
#endif // WITH_EDITOR

IMPLEMENT_MODULE(FGeometryCacheTracksModule, GeometryCache)

void FGeometryCacheTracksModule::StartupModule()
{
#if WITH_EDITOR
	FGeometryCacheSequencerModule& Module = FModuleManager::LoadModuleChecked<FGeometryCacheSequencerModule>(TEXT("GeometryCacheSequencer"));
#endif
}

void FGeometryCacheTracksModule::ShutdownModule()
{
}
