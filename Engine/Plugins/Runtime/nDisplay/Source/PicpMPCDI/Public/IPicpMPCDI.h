// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

#include "CoreMinimal.h"

#include "IMPCDI.h"

#include "TextureResource.h"
#include "PicpPostProcessing.h"

#include "Overlay/PicpProjectionOverlayRender.h"

class FRHICommandListImmediate;

namespace MPCDI
{	
	struct FMPCDIRegion;
};

class IPicpMPCDI : public IModuleInterface
{
public:
	static constexpr auto ModuleName = TEXT("PicpMPCDI");

public:
	/**
	 * Singleton-like access to this module's interface.  This is just for convenience!
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	static inline IPicpMPCDI& Get()
	{
		return FModuleManager::GetModuleChecked<IPicpMPCDI>(IPicpMPCDI::ModuleName);
	}

	/**
	 * Checks to see if this module is loaded and ready.  It is only valid to call Get() if IsAvailable() returns true.
	 *
	 * @return True if the module is loaded and ready to use
	 */
	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded(IPicpMPCDI::ModuleName);
	}

public:
	virtual bool ApplyWarpBlend(FRHICommandListImmediate& RHICmdList, IMPCDI::FTextureWarpData& TextureWarpData, IMPCDI::FShaderInputData& ShaderInputData, FMPCDIData* MPCDIData, FPicpProjectionOverlayViewportData* ViewportOverlayData) = 0;

	virtual void ApplyBlur(UTextureRenderTarget2D* InOutRenderTarget, UTextureRenderTarget2D* TemporaryRenderTarget, int KernelRadius, float KernelScale, EPicpBlurPostProcessShaderType BlurType) = 0;
	virtual void ApplyCompose(UTexture* InputTexture, UTextureRenderTarget2D* OutputRenderTarget, UTextureRenderTarget2D* Result) = 0;
	virtual void ExecuteCompose() = 0;
};
