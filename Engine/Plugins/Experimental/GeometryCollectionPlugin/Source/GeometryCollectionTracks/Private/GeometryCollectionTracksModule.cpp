// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryCollectionTracksModule.h"
#if WITH_EDITOR
#include "GeometryCollectionSequencerModule.h"
#endif // WITH_EDITOR

IMPLEMENT_MODULE(FGeometryCollectionTracksModule, GeometryCollectionTracks)

void FGeometryCollectionTracksModule::StartupModule()
{
#if WITH_EDITOR
	FGeometryCollectionSequencerModule& Module = FModuleManager::LoadModuleChecked<FGeometryCollectionSequencerModule>(TEXT("GeometryCollectionSequencer"));
#endif
}

void FGeometryCollectionTracksModule::ShutdownModule()
{
}
