// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Presentation/DisplayClusterPresentationDX12.h"

#include "DisplayClusterLog.h"

#include "RHI.h"
#include "RHIResources.h"

#include "Windows/AllowWindowsPlatformTypes.h"
#include "DirectX/Include/DXGI.h"
#include "Windows/HideWindowsPlatformTypes.h"


FDisplayClusterPresentationDX12::FDisplayClusterPresentationDX12(FViewport* const InViewport, TSharedPtr<IDisplayClusterRenderSyncPolicy>& InSyncPolicy)
	: FDisplayClusterPresentationBase(InViewport, InSyncPolicy)
{
}

FDisplayClusterPresentationDX12::~FDisplayClusterPresentationDX12()
{
}
