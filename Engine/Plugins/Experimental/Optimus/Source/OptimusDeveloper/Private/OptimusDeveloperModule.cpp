// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusDeveloperModule.h"

#include "OptimusDataTypeRegistry.h"
#include "OptimusObjectVersion.h"

#include "Modules/ModuleManager.h"
#include "UObject/DevObjectVersion.h"

// Unique serialization id for Optimus .
const FGuid FOptimusObjectVersion::GUID(0x93ede1aa, 0x10ca7375, 0x4df98a28, 0x49b157a0);

static FDevVersionRegistration GRegisterOptimusObjectVersion(FOptimusObjectVersion::GUID, FOptimusObjectVersion::LatestVersion, TEXT("Dev-Optimus"));

void FOptimusDeveloperModule::StartupModule()
{
	// Make sure all our types are known at startup.
	FOptimusDataTypeRegistry::RegisterBuiltinTypes();
}

void FOptimusDeveloperModule::ShutdownModule()
{
	FOptimusDataTypeRegistry::UnregisterAllTypes();
}

IMPLEMENT_MODULE(FOptimusDeveloperModule, OptimusDeveloper)

DEFINE_LOG_CATEGORY(LogOptimusDeveloper);
