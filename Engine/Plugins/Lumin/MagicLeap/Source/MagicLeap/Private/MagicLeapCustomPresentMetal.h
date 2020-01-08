// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MagicLeapCustomPresent.h"

#if PLATFORM_MAC

class FMagicLeapCustomPresentMetal : public FMagicLeapCustomPresent
{
public:
	FMagicLeapCustomPresentMetal(FMagicLeapHMD* plugin);

	virtual void BeginRendering() override;
	virtual void FinishRendering() override;
	virtual void UpdateViewport(const FViewport& Viewport, FRHIViewport* InViewportRHI) override;
	virtual void UpdateViewport_RenderThread() override;
	virtual void Reset() override;

	virtual void RenderToMLSurfaces_RenderThread(class FRHICommandListImmediate& RHICmdList, class FRHITexture2D* SrcTexture) override;

protected:
	// Used as a container for the MLGraphics texture array.
	FTexture2DArrayRHIRef DestTextureRef;
	FTexture2DRHIRef SrcTextureSRGBRef;
};
#endif // PLATFORM_MAC
