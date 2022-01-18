// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusCoreModule.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"

void FOptimusCoreModule::StartupModule()
{
	IModularFeatures::Get().RegisterModularFeature(IMeshDeformerProvider::ModularFeatureName, this);

	FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("Optimus"))->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/Optimus"), PluginShaderDir);
}

void FOptimusCoreModule::ShutdownModule()
{
	IModularFeatures::Get().UnregisterModularFeature(IMeshDeformerProvider::ModularFeatureName, this);
}

bool FOptimusCoreModule::IsEnabled(EShaderPlatform Platform)
{
	// todo: Per platform enable?
	return true;
}

FSoftObjectPtr FOptimusCoreModule::GetDefaultMeshDeformer()
{
	// todo: Make this a plugin setting?
	return FSoftObjectPtr();
}

IMPLEMENT_MODULE(FOptimusCoreModule, OptimusCore)

DEFINE_LOG_CATEGORY(LogOptimusCore);
