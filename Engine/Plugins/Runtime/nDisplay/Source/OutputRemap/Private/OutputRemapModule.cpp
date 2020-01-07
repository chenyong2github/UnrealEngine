// Copyright Epic Games, Inc. All Rights Reserved.

#include "OutputRemapModule.h"
#include "OutputRemapShader.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

#include "RHICommandList.h"

#include "OutputRemapHelpers.h"

#include "OutputRemapLog.h"
#include "OutputRemapMesh.h"

#define NDISPLAY_SHADERS_MAP TEXT("/Plugin/nDisplay")


//////////////////////////////////////////////////////////////////////////////////////////////
// IModuleInterface
//////////////////////////////////////////////////////////////////////////////////////////////
void FOutputRemapModule::StartupModule()
{
	if (!AllShaderSourceDirectoryMappings().Contains(NDISPLAY_SHADERS_MAP))
	{
		FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("nDisplay"))->GetBaseDir(), TEXT("Shaders"));
		AddShaderSourceDirectoryMapping(NDISPLAY_SHADERS_MAP, PluginShaderDir);
	}
}

void FOutputRemapModule::ShutdownModule()
{
	ReleaseData();
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IOutputRemap
//////////////////////////////////////////////////////////////////////////////////////////////
void FOutputRemapModule::ReloadAll()
{
	for (auto& It : MeshAssets)
	{
		It->ReloadChangedExtFiles();
	}
}

bool FOutputRemapModule::Load(const FString& LocalRemapMeshFile, uint32& OutMeshRef)
{
	FScopeLock lock(&DataGuard);

	if (IsLoaded(LocalRemapMeshFile, OutMeshRef))
	{
		return true;
	}

	TSharedPtr<FOutputRemapMesh> MeshItem(new FOutputRemapMesh(LocalRemapMeshFile));
	if (!MeshItem->IsValid()){
		return false;
	}

	// Load and cache the file
	MeshAssets.Add(MeshItem);
	OutMeshRef = MeshAssets.Num() - 1;
	return true;
}

bool FOutputRemapModule::IsLoaded(const FString& LocalRemapMeshFile, uint32& OutMeshRef)
{
	FScopeLock lock(&DataGuard);

	for (int32 Index = 0; Index != MeshAssets.Num(); ++Index)
	{
		if (!MeshAssets[Index]->GetFileName().Compare(LocalRemapMeshFile, ESearchCase::IgnoreCase))
		{
			OutMeshRef = Index;
			return true;
		}
	}

	return false;
}

bool FOutputRemapModule::ApplyOutputRemap_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* ShaderResourceTexture, FRHITexture2D* TargetableTexture, uint32 MeshRef)
{
	FScopeLock lock(&DataGuard);

	if (MeshAssets.Num() > (int)MeshRef)
	{
		return FOutputRemapShader::ApplyOutputRemap_RenderThread(RHICmdList, ShaderResourceTexture, TargetableTexture, MeshAssets[MeshRef].Get());
	}
	else
	{
		//@todo: handle error
	}

	return false;
}

void FOutputRemapModule::ReleaseData()
{
	FScopeLock lock(&DataGuard);
	for (auto& It : MeshAssets)
	{
		It.Reset();
	}
	MeshAssets.Empty();
}

IMPLEMENT_MODULE(FOutputRemapModule, OutputRemap);

