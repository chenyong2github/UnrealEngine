// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MagicLeapGraphics.h"
#include "MagicLeapCustomPresent.h"

#if PLATFORM_WINDOWS || PLATFORM_LUMIN
class FMagicLeapCustomPresentVulkan : public FMagicLeapCustomPresent
{
public:
	FMagicLeapCustomPresentVulkan(FMagicLeapHMD* plugin);

	virtual void BeginRendering() override;
	virtual void FinishRendering() override;
	virtual void UpdateViewport(const FViewport& Viewport, FRHIViewport* InViewportRHI) override;
	virtual void UpdateViewport_RenderThread() override;
	virtual void Reset() override;

protected:
	// Used as a container for the MLGraphics texture array.
	FTexture2DArrayRHIRef DestTextureRef;

	void* RenderTargetTexture;
	void* RenderTargetTextureAllocation;
	uint64 RenderTargetTextureAllocationOffset = 0;
	void* RenderTargetTextureSRGB;
	void* LastAliasedRenderTarget;
};
#endif // PLATFORM_WINDOWS || PLATFORM_LUMIN
