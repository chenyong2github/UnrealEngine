// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Render/Presentation/DisplayClusterPresentationDX11.h"

#include "DisplayClusterLog.h"

#include "RHI.h"
#include "RHIResources.h"

#include "Windows/AllowWindowsPlatformTypes.h"
#include "DirectX/Include/DXGI.h"
#include "Windows/HideWindowsPlatformTypes.h"


FDisplayClusterPresentationDX11::FDisplayClusterPresentationDX11(FViewport* const InViewport, TSharedPtr<IDisplayClusterRenderSyncPolicy>& InSyncPolicy)
	: FDisplayClusterPresentationBase(InViewport, InSyncPolicy)
{
}

FDisplayClusterPresentationDX11::~FDisplayClusterPresentationDX11()
{
}
