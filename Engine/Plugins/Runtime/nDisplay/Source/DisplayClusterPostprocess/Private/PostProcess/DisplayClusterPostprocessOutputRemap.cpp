// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "PostProcess/DisplayClusterPostprocessOutputRemap.h"

#include "DisplayClusterPostprocessHelpers.h"
#include "DisplayClusterPostprocessLog.h"
#include "DisplayClusterPostprocessStrings.h"

#include "Engine/RendererSettings.h"
#include "Misc/Paths.h"


FDisplayClusterPostprocessOutputRemap::FDisplayClusterPostprocessOutputRemap()
	: OutputRemapAPI(IOutputRemap::Get())
	, MeshRef(-1)
{

}

FDisplayClusterPostprocessOutputRemap::~FDisplayClusterPostprocessOutputRemap()
{
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterPostProcess
//////////////////////////////////////////////////////////////////////////////////////////////
bool FDisplayClusterPostprocessOutputRemap::IsPostProcessRenderTargetAfterWarpBlendRequired()
{
	return MeshRef>=0;
}

void FDisplayClusterPostprocessOutputRemap::InitializePostProcess(const FString& CfgLine)
{
	// PFM file (optional)
	FString ExtMeshFile;
	if (DisplayClusterHelpers::str::ExtractValue(CfgLine, DisplayClusterStrings::cfg::data::postprocess::output_remap::File, ExtMeshFile))
	{
		UE_LOG(LogDisplayClusterPostprocessOutputRemap, Log, TEXT("Found Argument '%s'='%s'"), DisplayClusterStrings::cfg::data::postprocess::output_remap::File, *ExtMeshFile);
	}

	// Load ext mesh:
	uint32 OutMeshRef;
	if (!OutputRemapAPI.Load(ExtMeshFile, OutMeshRef))
	{
		UE_LOG(LogDisplayClusterPostprocessOutputRemap, Error, TEXT("Failed to load ext mesh from file '%s'"), *ExtMeshFile);
		MeshRef = -1;
	}
	else
	{
		MeshRef = (int)OutMeshRef;
	}
}

void FDisplayClusterPostprocessOutputRemap::PerformPostProcessRenderTargetAfterWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* InOutTexture) const
{
	check(MeshRef >= 0);
	FIntPoint ScreenSize = InOutTexture->GetSizeXY();

	InitializeResources_RenderThread(ScreenSize);

	if (!RTTexture || !RTTexture->IsValid())
	{
		UE_LOG(LogDisplayClusterPostprocessOutputRemap, Error, TEXT("RT not allocated"));
		return;
	}

	uint32 InMeshRef = (uint32)MeshRef;
	if (OutputRemapAPI.ApplyOutputRemap_RenderThread(RHICmdList, InOutTexture, RTTexture, InMeshRef))
	{
		//Copy remapped result back
		RHICmdList.CopyToResolveTarget(RTTexture, InOutTexture, FResolveParams());
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterPostprocessOutputRemap
//////////////////////////////////////////////////////////////////////////////////////////////

bool FDisplayClusterPostprocessOutputRemap::InitializeResources_RenderThread(const FIntPoint& ScreenSize) const
{
	check(IsInRenderingThread());

	if (!bIsRenderResourcesInitialized)
	{
		FScopeLock lock(&RenderingResourcesInitializationCS);
		if (!bIsRenderResourcesInitialized)
		{
			static const TConsoleVariableData<int32>* CVarDefaultBackBufferPixelFormat = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DefaultBackBufferPixelFormat"));
			static const EPixelFormat SceneTargetFormat = EDefaultBackBufferPixelFormat::Convert2PixelFormat(EDefaultBackBufferPixelFormat::FromInt(CVarDefaultBackBufferPixelFormat->GetValueOnRenderThread()));

			FRHIResourceCreateInfo CreateInfo;
			FTexture2DRHIRef DummyTexRef;
			RHICreateTargetableShaderResource2D(ScreenSize.X, ScreenSize.Y, SceneTargetFormat, 1, TexCreate_None, TexCreate_RenderTargetable, false, CreateInfo, RTTexture, DummyTexRef);

			bIsRenderResourcesInitialized = true;
		}
	}
	return true;
}
