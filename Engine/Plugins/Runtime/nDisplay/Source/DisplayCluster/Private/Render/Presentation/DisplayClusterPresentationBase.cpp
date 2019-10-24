// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Render\Presentation\DisplayClusterPresentationBase.h "
#include "Render/Synchronization/IDisplayClusterRenderSyncPolicy.h"

#include "DisplayClusterGlobals.h"
#include "DisplayClusterLog.h"


// Custom VSync interval control
static TAutoConsoleVariable<int32>  CVarVSyncInterval(
	TEXT("nDisplay.render.VSyncInterval"),
	1,
	TEXT("VSync interval"),
	ECVF_RenderThreadSafe
);


FDisplayClusterPresentationBase::FDisplayClusterPresentationBase(FViewport* const InViewport, TSharedPtr<IDisplayClusterRenderSyncPolicy>& InSyncPolicy)
	: FRHICustomPresent()
	, Viewport(InViewport)
	, SyncPolicy(InSyncPolicy)
{
}

FDisplayClusterPresentationBase::~FDisplayClusterPresentationBase()
{
}

uint32 FDisplayClusterPresentationBase::GetSwapInt() const
{
	const uint32 SyncInterval = static_cast<uint32>(CVarVSyncInterval.GetValueOnAnyThread());
	return (SyncInterval);
}

void FDisplayClusterPresentationBase::OnBackBufferResize()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
}

bool FDisplayClusterPresentationBase::Present(int32& InOutSyncInterval)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);

	// Update sync value with nDisplay value
	InOutSyncInterval = GetSwapInt();

	// Get sync policy instance
	if (SyncPolicy.IsValid())
	{
		// False results means we don't need to present current frame, the sync object already presented it
		if (!SyncPolicy->SynchronizeClusterRendering(InOutSyncInterval))
		{
			return false;
		}
	}

	return true;
}
