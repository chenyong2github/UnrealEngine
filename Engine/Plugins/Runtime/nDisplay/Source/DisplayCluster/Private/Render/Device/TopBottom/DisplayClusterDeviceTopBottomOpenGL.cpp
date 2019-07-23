// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Render/Device/TopBottom/DisplayClusterDeviceTopBottomOpenGL.h"

#include "DisplayClusterLog.h"
#include "Render/Device/DisplayClusterOpenGL.h"


FDisplayClusterDeviceTopBottomOpenGL::FDisplayClusterDeviceTopBottomOpenGL()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
}

FDisplayClusterDeviceTopBottomOpenGL::~FDisplayClusterDeviceTopBottomOpenGL()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// Windows implementation
//////////////////////////////////////////////////////////////////////////////////////////////
#if PLATFORM_WINDOWS
bool FDisplayClusterDeviceTopBottomOpenGL::Present(int32& InOutSyncInterval)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);

	const int srcX1 = 0;
	const int srcY1 = 0;
	const int srcX2 = BackBuffSize.X;
	const int srcY2 = BackBuffSize.Y;

	const int dstX1 = 0;
	const int dstY1 = BackBuffSize.Y;
	const int dstX2 = BackBuffSize.X;
	const int dstY2 = 0;

	FOpenGLViewport* pOglViewport = static_cast<FOpenGLViewport*>(MainViewport->GetViewportRHI().GetReference());
	check(pOglViewport);
	FPlatformOpenGLContext* const pContext = pOglViewport->GetGLContext();
	check(pContext && pContext->DeviceContext);

	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, pContext->ViewportFramebuffer);
	glReadBuffer(GL_COLOR_ATTACHMENT0);

	UE_LOG(LogDisplayClusterRender, Verbose, TEXT("Blit framebuffer: [%d,%d - %d,%d] -> [%d,%d - %d,%d]"), srcX1, srcY1, srcX2, srcY2, dstX1, dstY1, dstX2, dstY2);
	glDrawBuffer(GL_BACK);
	glBlitFramebuffer(
		srcX1, srcY1, srcX2, srcY2,
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
#endif // PLATFORM_WINDOWS


//////////////////////////////////////////////////////////////////////////////////////////////
// IStereoRenderTargetManager
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterDeviceTopBottomOpenGL::UpdateViewport(bool bUseSeparateRenderTarget, const class FViewport& Viewport, class SViewport* ViewportWidget)
{
	FDisplayClusterDeviceBase::UpdateViewport(bUseSeparateRenderTarget, Viewport, ViewportWidget);

	// Since the GL approach performs blit in the Present method, we have to store the back buffer size for further output computations
	BackBuffSize = Viewport.GetRenderTargetTextureSizeXY();
}


//////////////////////////////////////////////////////////////////////////////////////////////
// Linux implementation
//////////////////////////////////////////////////////////////////////////////////////////////
#if PLATFORM_LINUX
bool FDisplayClusterDeviceTopBottomOpenGL::Present(int32& InOutSyncInterval)
{
	// Forward to default implementation (should be a black screen)
	return FDisplayClusterDeviceBase::Present(InOutSyncInterval);
}
#endif
