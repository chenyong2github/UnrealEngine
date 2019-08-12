// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Render/Synchronization/DisplayClusterRenderSyncPolicyNvidiaOpenGL.h"

#include "DisplayClusterLog.h"
#include "Render/Device/DisplayClusterOpenGL.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"


namespace
{
	PFNWGLSWAPINTERVALEXTPROC      DisplayCluster_wglSwapIntervalEXT_ProcAddress = nullptr;
	PFNWGLJOINSWAPGROUPNVPROC      DisplayCluster_wglJoinSwapGroupNV_ProcAddress = nullptr;
	PFNWGLBINDSWAPBARRIERNVPROC    DisplayCluster_wglBindSwapBarrierNV_ProcAddress = nullptr;
	PFNWGLQUERYSWAPGROUPNVPROC     DisplayCluster_wglQuerySwapGroupNV_ProcAddress = nullptr;
	PFNWGLQUERYMAXSWAPGROUPSNVPROC DisplayCluster_wglQueryMaxSwapGroupsNV_ProcAddress = nullptr;
	PFNWGLQUERYFRAMECOUNTNVPROC    DisplayCluster_wglQueryFrameCountNV_ProcAddress = nullptr;
	PFNWGLRESETFRAMECOUNTNVPROC    DisplayCluster_wglResetFrameCountNV_ProcAddress = nullptr;


	// Copy/pasted from OpenGLDrv.cpp
	static void DisplayClusterGetExtensionsString(FString& ExtensionsString)
	{
		GLint ExtensionCount = 0;
		ExtensionsString = TEXT("");
		if (FOpenGL::SupportsIndexedExtensions())
		{
			glGetIntegerv(GL_NUM_EXTENSIONS, &ExtensionCount);
			for (int32 ExtensionIndex = 0; ExtensionIndex < ExtensionCount; ++ExtensionIndex)
			{
				const ANSICHAR* ExtensionString = FOpenGL::GetStringIndexed(GL_EXTENSIONS, ExtensionIndex);

				ExtensionsString += TEXT(" ");
				ExtensionsString += ANSI_TO_TCHAR(ExtensionString);
			}
		}
		else
		{
			const ANSICHAR* GlGetStringOutput = (const ANSICHAR*)glGetString(GL_EXTENSIONS);
			if (GlGetStringOutput)
			{
				ExtensionsString += GlGetStringOutput;
				ExtensionsString += TEXT(" ");
			}
		}
	}
}


FDisplayClusterRenderSyncPolicyNvidiaOpenGL::FDisplayClusterRenderSyncPolicyNvidiaOpenGL()
{
	InitializeOpenGLCapabilities();
}

FDisplayClusterRenderSyncPolicyNvidiaOpenGL::~FDisplayClusterRenderSyncPolicyNvidiaOpenGL()
{
}

bool FDisplayClusterRenderSyncPolicyNvidiaOpenGL::SynchronizeClusterRendering(int32& InOutSyncInterval)
{
	check(IsInRenderingThread());

	if (bNVAPIInitialized == false)
	{
		// Use render barrier to guaranty that all nv barriers are initialized simultaneously
		SyncBarrierRenderThread();
		bNVAPIInitialized = InitializeNvidiaSwapLock();
	}

	if (!bNVAPIInitialized)
	{
		return true;
	}

	if(!(GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport))
	{
		return false;
	}

	FOpenGLViewport* pOglViewport = static_cast<FOpenGLViewport*>(GEngine->GameViewport->Viewport->GetViewportRHI().GetReference());
	check(pOglViewport);
	FPlatformOpenGLContext* const pContext = pOglViewport->GetGLContext();
	check(pContext && pContext->DeviceContext);

	if (OpenGLContext)
	{
		UpdateSwapInterval(InOutSyncInterval);
		::SwapBuffers(OpenGLContext->DeviceContext);
		return false;
	}

	return true;
}

bool FDisplayClusterRenderSyncPolicyNvidiaOpenGL::InitializeNvidiaSwapLock()
{
	if (bNVAPIInitialized)
	{
		return true;
	}

	if (!DisplayCluster_wglJoinSwapGroupNV_ProcAddress || !DisplayCluster_wglBindSwapBarrierNV_ProcAddress)
	{
		UE_LOG(LogDisplayClusterRenderSync, Error, TEXT("Group/Barrier functions not available"));
		return false;
	}

	if (!GEngine || !GEngine->GameViewport || !GEngine->GameViewport->Viewport || !GEngine->GameViewport->Viewport->GetViewportRHI().IsValid())
	{
		UE_LOG(LogDisplayClusterRenderSync, Warning, TEXT("Game viewport hasn't been initialized yet"));
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
		UE_LOG(LogDisplayClusterRenderSync, Error, TEXT("Couldn't query gr/br limits: %d"), glGetError());
		return false;
	}

	UE_LOG(LogDisplayClusterRenderSync, Log, TEXT("max_groups=%d max_barriers=%d"), (int)maxGroups, (int)maxBarriers);

	if (!(maxGroups > 0 && maxBarriers > 0))
	{
		UE_LOG(LogDisplayClusterRenderSync, Error, TEXT("There are no available groups or barriers"));
		return false;
	}

	if (!DisplayCluster_wglJoinSwapGroupNV_ProcAddress(OpenGLContext->DeviceContext, 1))
	{
		UE_LOG(LogDisplayClusterRenderSync, Error, TEXT("Couldn't join swap group: %d"), glGetError());
		return false;
	}
	else
	{
		UE_LOG(LogDisplayClusterRenderSync, Log, TEXT("Successfully joined the swap group: 1"));
	}

	if (!DisplayCluster_wglBindSwapBarrierNV_ProcAddress(1, 1))
	{
		UE_LOG(LogDisplayClusterRenderSync, Error, TEXT("Couldn't bind to swap barrier: %d"), glGetError());
		return false;
	}
	else
	{
		UE_LOG(LogDisplayClusterRenderSync, Log, TEXT("Successfully binded to the swap barrier: 1"));
	}

	return true;
}

void FDisplayClusterRenderSyncPolicyNvidiaOpenGL::InitializeOpenGLCapabilities()
{
	bool bWindowsSwapControlExtensionPresent = false;
	{
		FString ExtensionsString;
		DisplayClusterGetExtensionsString(ExtensionsString);

		if (ExtensionsString.Contains(TEXT("WGL_EXT_swap_control")))
		{
			bWindowsSwapControlExtensionPresent = true;
		}
	}

#pragma warning(push)
#pragma warning(disable:4191)
	if (bWindowsSwapControlExtensionPresent)
	{
		DisplayCluster_wglSwapIntervalEXT_ProcAddress = (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");
	}

	DisplayCluster_wglJoinSwapGroupNV_ProcAddress = (PFNWGLJOINSWAPGROUPNVPROC)wglGetProcAddress("wglJoinSwapGroupNV");
	DisplayCluster_wglBindSwapBarrierNV_ProcAddress = (PFNWGLBINDSWAPBARRIERNVPROC)wglGetProcAddress("wglBindSwapBarrierNV");
	DisplayCluster_wglQuerySwapGroupNV_ProcAddress = (PFNWGLQUERYSWAPGROUPNVPROC)wglGetProcAddress("wglQuerySwapGroupNV");
	DisplayCluster_wglQueryMaxSwapGroupsNV_ProcAddress = (PFNWGLQUERYMAXSWAPGROUPSNVPROC)wglGetProcAddress("wglQueryMaxSwapGroupsNV");
	DisplayCluster_wglQueryFrameCountNV_ProcAddress = (PFNWGLQUERYFRAMECOUNTNVPROC)wglGetProcAddress("wglQueryFrameCountNV");
	DisplayCluster_wglResetFrameCountNV_ProcAddress = (PFNWGLRESETFRAMECOUNTNVPROC)wglGetProcAddress("wglResetFrameCountNV");
#pragma warning(pop)
}

void FDisplayClusterRenderSyncPolicyNvidiaOpenGL::UpdateSwapInterval(int32 InSyncInterval) const
{
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
		UE_LOG(LogDisplayClusterRenderSync, Warning, TEXT("Couldn't set swap interval: %d"), InSyncInterval);
}
