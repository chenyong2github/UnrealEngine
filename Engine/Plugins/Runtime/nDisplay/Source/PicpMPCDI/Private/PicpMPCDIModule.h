// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPicpMPCDI.h"

class FPicpMPCDIData;


class FPicpMPCDIModule : public IPicpMPCDI
{
public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IModuleInterface
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IPicpMPCDI
	//////////////////////////////////////////////////////////////////////////////////////////////	
	virtual bool ApplyWarpBlend(FRHICommandListImmediate& RHICmdList, IMPCDI::FTextureWarpData& TextureWarpData, IMPCDI::FShaderInputData& ShaderInputData, FMPCDIData* MPCDIData, FPicpProjectionOverlayViewportData* ViewportOverlayData) override;
	
	virtual void ApplyBlur(UTextureRenderTarget2D* InOutRenderTarget, UTextureRenderTarget2D* TemporaryRenderTarget, int KernelRadius, float KernelScale, EPicpBlurPostProcessShaderType BlurType) override;
	virtual void ApplyCompose(UTexture* InputTexture, UTextureRenderTarget2D* OutputRenderTarget, UTextureRenderTarget2D* Result) override;
	virtual void ExecuteCompose() override;

private:
	FCriticalSection DataGuard;
};
