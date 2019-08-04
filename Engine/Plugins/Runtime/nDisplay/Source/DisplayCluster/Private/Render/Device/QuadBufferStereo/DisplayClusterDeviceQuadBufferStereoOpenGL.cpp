// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Render/Device/QuadBufferStereo/DisplayClusterDeviceQuadBufferStereoOpenGL.h"

#include "DisplayClusterLog.h"
#include "Render/Device/DisplayClusterOpenGL.h"


FDisplayClusterDeviceQuadBufferStereoOpenGL::FDisplayClusterDeviceQuadBufferStereoOpenGL()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
}

FDisplayClusterDeviceQuadBufferStereoOpenGL::~FDisplayClusterDeviceQuadBufferStereoOpenGL()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
}

//////////////////////////////////////////////////////////////////////////////////////////////
// Windows implementation
//////////////////////////////////////////////////////////////////////////////////////////////
#if PLATFORM_WINDOWS
bool FDisplayClusterDeviceQuadBufferStereoOpenGL::Present(int32& InOutSyncInterval)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);

	const int halfSizeX = BackBuffSize.X / 2;

	const int srcLX1 = 0;
	const int srcLY1 = 0;
	const int srcLX2 = halfSizeX;
	const int srcLY2 = BackBuffSize.Y;

	const int srcRX1 = halfSizeX;
	const int srcRY1 = 0;
	const int srcRX2 = BackBuffSize.X;
	const int srcRY2 = BackBuffSize.Y;

	const int dstX1 = 0;
	const int dstY1 = BackBuffSize.Y;
	const int dstX2 = halfSizeX;
	const int dstY2 = 0;

	FOpenGLViewport* pOglViewport = static_cast<FOpenGLViewport*>(MainViewport->GetViewportRHI().GetReference());
	check(pOglViewport);
	FPlatformOpenGLContext* const pContext = pOglViewport->GetGLContext();
	check(pContext && pContext->DeviceContext);

	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

	glBindFramebuffer(GL_READ_FRAMEBUFFER, pContext->ViewportFramebuffer);
	glReadBuffer(GL_COLOR_ATTACHMENT0);

	UE_LOG(LogDisplayClusterRender, Verbose, TEXT("Blit framebuffer [L]: [%d,%d - %d,%d] -> [%d,%d - %d,%d]"), srcLX1, srcLY1, srcLX2, srcLY2, dstX1, dstY1, dstX2, dstY2);
	glDrawBuffer(GL_BACK_LEFT);
	glBlitFramebuffer(
		srcLX1, srcLY1, srcLX2, srcLY2,
		dstX1, dstY1, dstX2, dstY2,
		GL_COLOR_BUFFER_BIT,
		GL_NEAREST);

	UE_LOG(LogDisplayClusterRender, Verbose, TEXT("Blit framebuffer [R]: [%d,%d - %d,%d] -> [%d,%d - %d,%d]"), srcRX1, srcRY1, srcRX2, srcRY2, dstX1, dstY1, dstX2, dstY2);
	glDrawBuffer(GL_BACK_RIGHT);
	glBlitFramebuffer(
		srcRX1, srcRY1, srcRX2, srcRY2,
		dstX1, dstY1, dstX2, dstY2,
		GL_COLOR_BUFFER_BIT,
		GL_NEAREST);

#if !WITH_EDITOR
	pOglViewport->IssueFrameEvent();
	pOglViewport->WaitForFrameEventCompletion();
#endif

	// Perform abstract synchronization on a higher level
	if (!FDisplayClusterDeviceBase::Present(InOutSyncInterval))
	{
		return false;
	}

	::SwapBuffers(pOglViewport->GetGLContext()->DeviceContext);
	REPORT_GL_END_BUFFER_EVENT_FOR_FRAME_DUMP();

	return false;
}
#endif


//////////////////////////////////////////////////////////////////////////////////////////////
// IStereoRenderTargetManager
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterDeviceQuadBufferStereoOpenGL::UpdateViewport(bool bUseSeparateRenderTarget, const class FViewport& Viewport, class SViewport* ViewportWidget)
{
	FDisplayClusterDeviceBase::UpdateViewport(bUseSeparateRenderTarget, Viewport, ViewportWidget);

	// Since the GL approach performs blit in the Present method, we have to store the back buffer size for further output computations
	BackBuffSize = Viewport.GetRenderTargetTextureSizeXY();
}


//////////////////////////////////////////////////////////////////////////////////////////////
// Linux implementation
//////////////////////////////////////////////////////////////////////////////////////////////
#if PLATFORM_LINUX
//@todo: Implementation for Linux
bool FDisplayClusterDeviceQuadBufferStereoOpenGL::Present(int32& InOutSyncInterval)
{
	// Forward to default implementation (should be a black screen)
	return FDisplayClusterDeviceBase::Present(InOutSyncInterval);
}
#endif
