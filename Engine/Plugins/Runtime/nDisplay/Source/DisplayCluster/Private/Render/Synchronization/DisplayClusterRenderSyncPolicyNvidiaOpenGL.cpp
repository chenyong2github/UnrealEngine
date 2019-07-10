// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Render/Synchronization/DisplayClusterRenderSyncPolicyNvidiaOpenGL.h"
#include "Render/Device/DisplayClusterDeviceInternals.h"

#include "DisplayClusterLog.h"

#include "Engine/GameViewportClient.h"
#include "Engine/GameEngine.h"


FDisplayClusterRenderSyncPolicyNvidiaOpenGL::FDisplayClusterRenderSyncPolicyNvidiaOpenGL()
{
}

FDisplayClusterRenderSyncPolicyNvidiaOpenGL::~FDisplayClusterRenderSyncPolicyNvidiaOpenGL()
{
}

bool FDisplayClusterRenderSyncPolicyNvidiaOpenGL::SynchronizeClusterRendering(int32& InOutSyncInterval)
{
	//@todo implement hardware based solution
	SyncBarrierRenderThread();
	// Tell a caller that he still needs to present a frame
	return true;
}

#if 0
bool FDisplayClusterRenderSyncPolicyNvidiaOpenGL::Initialize()
{
#if PLATFORM_WINDOWS
	DisplayClusterInitCapabilitiesForGL();
#endif

	return true;
}

bool FDisplayClusterRenderSyncPolicyNvidiaOpenGL::Present(int32& InOutSyncInterval)
{
	if (bNvSwapInitialized == false)
	{
		// Use render barrier to guaranty that all nv barriers are initialized simultaneously
		SyncBarrierRenderThread();
		bNvSwapInitialized = InitializeNvidiaSwapLock();
	}

	if (OpenGLContext)
	{
		UpdateSwapInterval(InOutSyncInterval);
		::SwapBuffers(OpenGLContext->DeviceContext);
		return false;
	}

	return true;
}

#if PLATFORM_WINDOWS
bool FDisplayClusterRenderSyncPolicyNvidiaOpenGL::InitializeNvidiaSwapLock()
{
	if (bNvSwapInitialized)
	{
		return true;
	}

	if (!DisplayCluster_wglJoinSwapGroupNV_ProcAddress || !DisplayCluster_wglBindSwapBarrierNV_ProcAddress)
	{
		UE_LOG(LogDisplayClusterRender, Error, TEXT("Group/Barrier functions not available"));
		return false;
	}

	if (!GEngine || !GEngine->GameViewport || 
		!GEngine->GameViewport->Viewport || 
		!GEngine->GameViewport->Viewport->GetViewportRHI().IsValid())
	{
		UE_LOG(LogDisplayClusterRender, Warning, TEXT("Game viewport hasn't been initialized yet"));
		return false;
	}
	FViewport* MainViewport = GEngine->GameViewport->Viewport;
	OpenGLViewport = static_cast<FOpenGLViewport*>(MainViewport->GetViewportRHI().GetReference());
	check(OpenGLViewport);
	OpenGLContext = OpenGLViewport->GetGLContext();
	check(OpenGLContext&& OpenGLContext->DeviceContext);

	GLuint maxGroups = 0;
	GLuint maxBarriers = 0;

	if (!DisplayCluster_wglQueryMaxSwapGroupsNV_ProcAddress(OpenGLContext->DeviceContext, &maxGroups, &maxBarriers))
	{
		UE_LOG(LogDisplayClusterRender, Error, TEXT("Couldn't query gr/br limits: %d"), glGetError());
		return false;
	}

	UE_LOG(LogDisplayClusterRender, Log, TEXT("max_groups=%d max_barriers=%d"), (int)maxGroups, (int)maxBarriers);

	if (!(maxGroups > 0 && maxBarriers > 0))
	{
		UE_LOG(LogDisplayClusterRender, Error, TEXT("There are no available groups or barriers"));
		return false;
	}

	if (!DisplayCluster_wglJoinSwapGroupNV_ProcAddress(OpenGLContext->DeviceContext, 1))
	{
		UE_LOG(LogDisplayClusterRender, Error, TEXT("Couldn't join swap group: %d"), glGetError());
		return false;
	}
	else
	{
		UE_LOG(LogDisplayClusterRender, Log, TEXT("Successfully joined the swap group: 1"));
	}

	if (!DisplayCluster_wglBindSwapBarrierNV_ProcAddress(1, 1))
	{
		UE_LOG(LogDisplayClusterRender, Error, TEXT("Couldn't bind to swap barrier: %d"), glGetError());
		return false;
	}
	else
	{
		UE_LOG(LogDisplayClusterRender, Log, TEXT("Successfully binded to the swap barrier: 1"));
	}

	return true;
}
#elif PLATFORM_LINUX
bool FDisplayClusterRenderSyncPolicyNvidiaOpenGL::InitializeNvidiaSwapLock()
{
	//@todo: Implementation for Linux
	return false;
}
#endif


void FDisplayClusterRenderSyncPolicyNvidiaOpenGL::UpdateSwapInterval(int32 InSyncInterval) const
{
#if PLATFORM_WINDOWS
	/*
	https://www.opengl.org/registry/specs/EXT/wgl_swap_control.txt
	wglSwapIntervalEXT specifies the minimum number of video frame periods
	per buffer swap for the window associated with the current context.
	The interval takes effect when SwapBuffers or wglSwapLayerBuffer
	is first called subsequent to the wglSwapIntervalEXT call.

	The parameter <interval> specifies the minimum number of video frames
	that are displayed before a buffer swap will occur.

	A video frame period is the time required by the monitor to display a
	full frame of video data.  In the case of an interlaced monitor,
	this is typically the time required to display both the even and odd
	fields of a frame of video data.  An interval set to a value of 2
	means that the color buffers will be swapped at most every other video
	frame.

	If <interval> is set to a value of 0, buffer swaps are not synchronized
	to a video frame.  The <interval> value is silently clamped to
	the maximum implementation-dependent value supported before being
	stored.

	The swap interval is not part of the render context state.  It cannot
	be pushed or popped.  The current swap interval for the window
	associated with the current context can be obtained by calling
	wglGetSwapIntervalEXT.  The default swap interval is 1.
	*/

	// Perform that each frame
	if (!DisplayCluster_wglSwapIntervalEXT_ProcAddress(InSyncInterval))
		UE_LOG(LogDisplayClusterRender, Warning, TEXT("Couldn't set swap interval: %d"), InSyncInterval);

#elif
	//@todo: Implementation for Linux
#endif
}
#endif
