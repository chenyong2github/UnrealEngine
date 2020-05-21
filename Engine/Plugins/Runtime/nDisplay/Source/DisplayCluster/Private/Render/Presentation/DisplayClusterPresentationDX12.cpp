// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Presentation/DisplayClusterPresentationDX12.h"

#include "Misc/DisplayClusterLog.h"

#include "RHI.h"
#include "RHIResources.h"


FDisplayClusterPresentationDX12::FDisplayClusterPresentationDX12(FViewport* const InViewport, TSharedPtr<IDisplayClusterRenderSyncPolicy>& InSyncPolicy)
	: FDisplayClusterPresentationBase(InViewport, InSyncPolicy)
{
}

FDisplayClusterPresentationDX12::~FDisplayClusterPresentationDX12()
{
}
