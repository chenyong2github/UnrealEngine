// Copyright Epic Games, Inc. All Rights Reserved.

#include "CineCameraRigs.h"
#include "CineSplineLog.h"
#include "ConcertSyncSettings.h"

#define LOCTEXT_NAMESPACE "FCineCameraRigsModule"

DEFINE_LOG_CATEGORY(LogCineSpline);

void FCineCameraRigsModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module

	// Update MU filter
	UConcertSyncConfig* SyncConfig = GetMutableDefault<UConcertSyncConfig>();
	if(SyncConfig)
	{ 
		FTransactionClassFilter Filter;
		Filter.ObjectOuterClass = FSoftClassPath(TEXT("/Script/Engine.World"));
		Filter.ObjectClasses.Add(FSoftClassPath(TEXT("/Script/CineCameraRigs.CineSplineMetadata")));
		SyncConfig->IncludeObjectClassFilters.Add(Filter);
	}
}

void FCineCameraRigsModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FCineCameraRigsModule, CineCameraRigs)