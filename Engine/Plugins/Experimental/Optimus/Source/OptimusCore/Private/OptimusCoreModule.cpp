// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusCoreModule.h"

#include "OptimusObjectVersion.h"
#include "OptimusDataTypeRegistry.h"

#include "UObject/DevObjectVersion.h"
#include "Modules/ModuleManager.h"

// Unique serialization id for Optimus .
const FGuid FOptimusObjectVersion::GUID(0x93ede1aa, 0x10ca7375, 0x4df98a28, 0x49b157a0);

static FDevVersionRegistration GRegisterOptimusObjectVersion(FOptimusObjectVersion::GUID, FOptimusObjectVersion::LatestVersion, TEXT("Dev-Optimus"));


void FOptimusCoreModule::StartupModule()
{
	// Make sure all our types are known at startup.
	FOptimusDataTypeRegistry::RegisterBuiltinTypes();
}


void FOptimusCoreModule::ShutdownModule()
{
	FOptimusDataTypeRegistry::UnregisterAllTypes();
}

IMPLEMENT_MODULE(FOptimusCoreModule, OptimusCore)

DEFINE_LOG_CATEGORY(LogOptimusCore);
