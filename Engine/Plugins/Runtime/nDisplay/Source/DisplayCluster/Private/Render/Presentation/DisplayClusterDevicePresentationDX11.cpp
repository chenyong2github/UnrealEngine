// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Render/Presentation/DisplayClusterDevicePresentationDX11.h"

#include "DisplayClusterLog.h"

#include "RHI.h"
#include "RHIResources.h"

#include "Windows/AllowWindowsPlatformTypes.h"
#include "DirectX/Include/DXGI.h"
#include "Windows/HideWindowsPlatformTypes.h"


bool FDisplayClusterDevicePresentationDX11::PresentImpl(FRHIViewport* Viewport, const int32 InSyncInterval)
{
	if (!Viewport)
	{
		check(Viewport);
		UE_LOG(LogDisplayClusterRender, Warning, TEXT("Viewport is null"));
		return true;
	}

	IDXGISwapChain* const SwapChain = static_cast<IDXGISwapChain*>(Viewport->GetNativeSwapChain());
	if (!SwapChain)
	{
		check(SwapChain);
		UE_LOG(LogDisplayClusterRender, Warning, TEXT("SwapChain is null"));
		return true;
	}

	SwapChain->Present(InSyncInterval, 0);

	return false;
}
