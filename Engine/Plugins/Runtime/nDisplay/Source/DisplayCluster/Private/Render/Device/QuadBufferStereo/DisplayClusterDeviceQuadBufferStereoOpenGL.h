// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Device/QuadBufferStereo/DisplayClusterDeviceQuadBufferStereoBase.h"

class FOpenGLViewport;


/**
 * Frame sequenced active stereo
 */
class FDisplayClusterDeviceQuadBufferStereoOpenGL
	: public FDisplayClusterDeviceQuadBufferStereoBase
{
public:
	FDisplayClusterDeviceQuadBufferStereoOpenGL();
	virtual ~FDisplayClusterDeviceQuadBufferStereoOpenGL();

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// FRHICustomPresent
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool Present(int32& InOutSyncInterval) override;

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IStereoRenderTargetManager
	//////////////////////////////////////////////////////////////////////////////////////////////
	void UpdateViewport(bool bUseSeparateRenderTarget, const class FViewport& Viewport, class SViewport* ViewportWidget) override;

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// FDisplayClusterDeviceBase
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void CopyTextureToBackBuffer_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* BackBuffer, FRHITexture2D* SrcTexture, FVector2D WindowSize) const override
	{
		// Forbid the default FDisplayClusterDeviceBase::CopyTextureToBackBuffer_RenderThread implementation. The OpenGL copies data in the Present method.
	}

private:
	void SwapBuffers(FOpenGLViewport* pOglViewport, int32& InOutSyncInterval);

	// Set up swap interval for upcoming buffer swap
	void UpdateSwapInterval(int32 swapInt) const;

	// Implementation of swap policies
	void internal_SwapBuffersPolicyNone(FOpenGLViewport* pOglViewport);
	void internal_SwapBuffersPolicySoftSwapSync(FOpenGLViewport* pOglViewport);
	void internal_SwapBuffersPolicyNvSwapSync(FOpenGLViewport* pOglViewport);

private:
	FIntPoint BackBuffSize = { 0, 0 };
};
