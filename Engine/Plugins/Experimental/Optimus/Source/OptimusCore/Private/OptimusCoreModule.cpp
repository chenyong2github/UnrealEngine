// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusCoreModule.h"

#include "OptimusDataTypeRegistry.h"
#include "OptimusObjectVersion.h"

#include "ComputeFramework/ComputeFramework.h"
#include "Features/IModularFeatures.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"
#include "UObject/DevObjectVersion.h"


static int32 GDeformerGraphEnable = 1;
static FAutoConsoleVariableRef CVarDeformerGraphEnable(
	TEXT("a.DeformerGraph.Enable"),
	GDeformerGraphEnable,
	TEXT("Enable the Deformer Graph.\n"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

// Unique serialization id for Optimus .
const FGuid FOptimusObjectVersion::GUID(0x93ede1aa, 0x10ca7375, 0x4df98a28, 0x49b157a0);

static FDevVersionRegistration GRegisterOptimusObjectVersion(FOptimusObjectVersion::GUID, FOptimusObjectVersion::LatestVersion, TEXT("Dev-Optimus"));


void FOptimusCoreModule::StartupModule()
{
	IModularFeatures::Get().RegisterModularFeature(IMeshDeformerProvider::ModularFeatureName, this);

	FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("Optimus"))->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/Optimus"), PluginShaderDir);

	// Make sure all our types are known at startup.
	FOptimusDataTypeRegistry::RegisterBuiltinTypes();
}

void FOptimusCoreModule::ShutdownModule()
{
	FOptimusDataTypeRegistry::UnregisterAllTypes();
	
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
