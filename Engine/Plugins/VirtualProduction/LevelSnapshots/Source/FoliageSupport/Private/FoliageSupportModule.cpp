// Copyright Epic Games, Inc. All Rights Reserved.

#include "FoliageSupportModule.h"

#include "FoliageSupport/FoliageSupport.h"

#define LOCTEXT_NAMESPACE "FFoliageSupportModule"

void FFoliageSupportModule::StartupModule()
{
	ILevelSnapshotsModule& Module = ILevelSnapshotsModule::Get();
	FFoliageSupport::Register(Module);
}

void FFoliageSupportModule::ShutdownModule()
{}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FFoliageSupportModule, FoliageSupport)