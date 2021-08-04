// Copyright Epic Games, Inc. All Rights Reserved.

#include "FrostySensorGridShadersModule.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "ShaderCore.h"

IMPLEMENT_MODULE(IFrostySensorGridShadersModule, FrostySensorGridShaders);

void IFrostySensorGridShadersModule::StartupModule()
{
	// Maps virtual shader source directory /Plugin/FX/Niagara to the plugin's actual Shaders directory.
	FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("FrostySensorGrid"))->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/FrostySensorGrid"), PluginShaderDir);
}