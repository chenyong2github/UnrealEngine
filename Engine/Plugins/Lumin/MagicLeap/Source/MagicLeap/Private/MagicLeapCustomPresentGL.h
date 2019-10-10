// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MagicLeapCustomPresent.h"

#if PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_LUMIN
class FMagicLeapCustomPresentOpenGL : public FMagicLeapCustomPresent
{
public:
	FMagicLeapCustomPresentOpenGL(FMagicLeapHMD* plugin);

	virtual void BeginRendering() override;
	virtual void FinishRendering() override;
	virtual void UpdateViewport(const FViewport& Viewport, FRHIViewport* InViewportRHI) override;
	virtual void UpdateViewport_RenderThread() override;
	virtual void Reset() override;

protected:
	uint32_t RenderTargetTexture = 0;
	uint32_t Framebuffers[2];
	bool bFramebuffersValid;
};
#endif // PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_LUMIN
