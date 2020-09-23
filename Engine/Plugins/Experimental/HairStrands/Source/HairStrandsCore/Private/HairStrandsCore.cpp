// Copyright Epic Games, Inc. All Rights Reserved.

#include "HairStrandsCore.h"
#include "HairStrandsInterface.h"
#include "Interfaces/IPluginManager.h"
#include "GroomManager.h"

IMPLEMENT_MODULE(FHairStrandsCore, HairStrandsCore);

void ProcessHairStrandsBookmark(
	FRDGBuilder& GraphBuilder,
	EHairStrandsBookmark Bookmark,
	FHairStrandsBookmarkParameters& Parameters);

void ProcessHairStrandsParameters(FHairStrandsBookmarkParameters& Parameters);

void FHairStrandsCore::StartupModule()
{
	RegisterBookmarkFunction(ProcessHairStrandsBookmark, ProcessHairStrandsParameters);

	// Maps virtual shader source directory /Plugin/FX/Niagara to the plugin's actual Shaders directory.
	FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("HairStrands"))->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/Experimental/HairStrands"), PluginShaderDir);
}

void FHairStrandsCore::ShutdownModule()
{
}

