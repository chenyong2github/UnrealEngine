// Copyright Epic Games, Inc. All Rights Reserved.

#include "HairStrandsNiagara.h"
#include "Interfaces/IPluginManager.h"
#include "ShaderCore.h"

IMPLEMENT_MODULE(FHairStrandsNiagara, HairStrandsNiagara);

void FHairStrandsNiagara::StartupModule()
{
	static const auto UseShaderStagesCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("fx.UseShaderStages"));
	UseShaderStagesCVar->Set(1);

	// Maps virtual shader source directory /Plugin/FX/Niagara to the plugin's actual Shaders directory.
	FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("HairStrands"))->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/Experimental/HairStrands"), PluginShaderDir);
}

void FHairStrandsNiagara::ShutdownModule()
{}

