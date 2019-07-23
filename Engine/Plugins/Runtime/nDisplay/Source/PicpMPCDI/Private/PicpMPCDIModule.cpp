// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "PicpMPCDIModule.h"

#include "PicpMPCDIShader.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"
#include "PicpBlurPostProcess.h"

//////////////////////////////////////////////////////////////////////////////////////////////
// IModuleInterface
//////////////////////////////////////////////////////////////////////////////////////////////

#define NDISPLAY_SHADERS_MAP TEXT("/Plugin/nDisplay")

void FPicpMPCDIModule::StartupModule()
{
	if (!AllShaderSourceDirectoryMappings().Contains(NDISPLAY_SHADERS_MAP))
	{
		FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("nDisplay"))->GetBaseDir(), TEXT("Shaders"));
		AddShaderSourceDirectoryMapping(NDISPLAY_SHADERS_MAP, PluginShaderDir);
	}
}

void FPicpMPCDIModule::ShutdownModule()
{
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IPicpMPCDI
//////////////////////////////////////////////////////////////////////////////////////////////
bool FPicpMPCDIModule::ApplyWarpBlend(FRHICommandListImmediate& RHICmdList, IMPCDI::FTextureWarpData& TextureWarpData, IMPCDI::FShaderInputData& ShaderInputData, FMPCDIData* MPCDIData, FPicpProjectionOverlayViewportData* ViewportOverlayData)
{
	FScopeLock lock(&DataGuard);
	return FPicpMPCDIShader::ApplyWarpBlend(RHICmdList, TextureWarpData, ShaderInputData, MPCDIData, ViewportOverlayData);
}


void FPicpMPCDIModule::ApplyBlur(UTextureRenderTarget2D* InOutRenderTarget, UTextureRenderTarget2D* TemporaryRenderTarget, int KernelRadius, float KernelScale, EPicpBlurPostProcessShaderType BlurType)
{
	FPicpBlurPostProcess::ApplyBlur(InOutRenderTarget, TemporaryRenderTarget, KernelRadius, KernelScale, BlurType);
}

void FPicpMPCDIModule::ApplyCompose(UTexture* InputTexture, UTextureRenderTarget2D* OutputRenderTarget, UTextureRenderTarget2D* Result)
{
	FPicpBlurPostProcess::ApplyCompose(InputTexture, OutputRenderTarget, Result);
}

void FPicpMPCDIModule::ExecuteCompose()
{
	FPicpBlurPostProcess::ExecuteCompose();
}

IMPLEMENT_MODULE(FPicpMPCDIModule, PicpMPCDI);

