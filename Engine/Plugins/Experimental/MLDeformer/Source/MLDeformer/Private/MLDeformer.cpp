// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLDeformer.h"

#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"


MLDEFORMER_API DEFINE_LOG_CATEGORY(LogMLDeformer)

#define LOCTEXT_NAMESPACE "MLDeformerModule"

IMPLEMENT_MODULE(FMLDeformerModule, MLDeformer)

void FMLDeformerModule::StartupModule()
{
	const FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("MLDeformer"))->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/MLDeformer"), PluginShaderDir);
}

void FMLDeformerModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE
