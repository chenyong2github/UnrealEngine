// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusCoreModule.h"

#include "ComputeFramework/ComputeFramework.h"
#include "Features/IModularFeatures.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"

static int32 GDeformerGraphEnable = 1;
static FAutoConsoleVariableRef CVarDeformerGraphEnable(
	TEXT("a.DeformerGraph.Enable"),
	GDeformerGraphEnable,
	TEXT("Enable the Deformer Graph.\n"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

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
	return (GDeformerGraphEnable > 0) && ComputeFramework::IsEnabled(Platform);
}

TSoftObjectPtr<UMeshDeformer> FOptimusCoreModule::GetDefaultMeshDeformer()
{
	// todo: Make this a plugin setting?
	return nullptr;
}

IMPLEMENT_MODULE(FOptimusCoreModule, OptimusCore)

DEFINE_LOG_CATEGORY(LogOptimusCore);
