// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusCoreModule.h"

#include "OptimusDataTypeRegistry.h"
#include "OptimusObjectVersion.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"
#include "UObject/DevObjectVersion.h"

// Unique serialization id for Optimus .
const FGuid FOptimusObjectVersion::GUID(0x93ede1aa, 0x10ca7375, 0x4df98a28, 0x49b157a0);

static FDevVersionRegistration GRegisterOptimusObjectVersion(FOptimusObjectVersion::GUID, FOptimusObjectVersion::LatestVersion, TEXT("Dev-Optimus"));


void FOptimusCoreModule::StartupModule()
{
	// Make sure all our types are known at startup.
	FOptimusDataTypeRegistry::RegisterBuiltinTypes();

	FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("Optimus"))->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/Optimus"), PluginShaderDir);
}


void FOptimusCoreModule::ShutdownModule()
{
	FOptimusDataTypeRegistry::UnregisterAllTypes();
}

IMPLEMENT_MODULE(FOptimusCoreModule, OptimusCore)

DEFINE_LOG_CATEGORY(LogOptimusCore);
