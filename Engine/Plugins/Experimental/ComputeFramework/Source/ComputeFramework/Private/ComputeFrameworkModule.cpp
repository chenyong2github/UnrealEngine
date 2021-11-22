// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComputeFramework/ComputeFrameworkModule.h"

#include "ComputeFramework/ComputeSystem.h"
#include "ComputeSystemInterface.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"

FComputeFrameworkSystem* FComputeFrameworkModule::ComputeSystem = nullptr;

void FComputeFrameworkModule::StartupModule()
{
	FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("ComputeFramework"))->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/ComputeFramework"), PluginShaderDir);

	ensure(ComputeSystem == nullptr);
	ComputeSystem = new class FComputeFrameworkSystem;
	ComputeSystemInterface::RegisterSystem(ComputeSystem);
}

void FComputeFrameworkModule::ShutdownModule()
{
	ensure(ComputeSystem != nullptr);
	ComputeSystemInterface::UnregisterSystem(ComputeSystem);
	delete ComputeSystem;
	ComputeSystem = nullptr;
}

IMPLEMENT_MODULE(FComputeFrameworkModule, ComputeFramework)
