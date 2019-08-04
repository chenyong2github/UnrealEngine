// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "HairStrandsNiagara.h"
#include "Interfaces/IPluginManager.h"
#include "ShaderCore.h"

void FHairStrandsNiagara::StartupModule()
{
	// Maps virtual shader source directory /Plugin/FX/Niagara to the plugin's actual Shaders directory.
	FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("HairStrands"))->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/Experimental/HairStrands"), PluginShaderDir);
}

void FHairStrandsNiagara::ShutdownModule()
{}

