// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Presentation/DisplayClusterPresentationDX11.h"

#include "DisplayClusterLog.h"

#include "RHI.h"
#include "RHIResources.h"


FDisplayClusterPresentationDX11::FDisplayClusterPresentationDX11(FViewport* const InViewport, TSharedPtr<IDisplayClusterRenderSyncPolicy>& InSyncPolicy)
	: FDisplayClusterPresentationBase(InViewport, InSyncPolicy)
{
}

FDisplayClusterPresentationDX11::~FDisplayClusterPresentationDX11()
{
}
