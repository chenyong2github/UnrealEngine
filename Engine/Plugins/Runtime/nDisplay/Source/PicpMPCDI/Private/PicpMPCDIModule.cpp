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
void FPicpMPCDIModule::StartupModule()
{
	//FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("nDisplay"))->GetBaseDir(), TEXT("Shaders"));
	//AddShaderSourceDirectoryMapping(TEXT("/Plugin/nDisplay"), PluginShaderDir);
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

IMPLEMENT_MODULE(FPicpMPCDIModule, PicpMPCDI);

