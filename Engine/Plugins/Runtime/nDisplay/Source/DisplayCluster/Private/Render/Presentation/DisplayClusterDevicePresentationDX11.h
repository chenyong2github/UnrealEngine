// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/device/DisplayClusterDeviceInternals.h"
#include "DisplayClusterLog.h"

//#include "UnrealClient.h"
#include "D3D11Viewport.h"


/**
 * Helper class to encapsulate DX11 frame presentation
 */
class FDisplayClusterDevicePresentationDX11
{
public:
	FDisplayClusterDevicePresentationDX11() = default;
	virtual ~FDisplayClusterDevicePresentationDX11() = default;

public:
	bool PresentImpl(FRHIViewport* Viewport, const int32 InSyncInterval)
	{
		check(Viewport);
		if (Viewport == nullptr)
		{
			UE_LOG(LogDisplayClusterRender, Warning, TEXT("Viewport is nullptr. Use native present."));
			return true;
		}

		FD3D11Viewport* D3D11Viewport = static_cast<FD3D11Viewport*>(Viewport);
		check(D3D11Viewport);

		IDXGISwapChain* SwapChain = D3D11Viewport->GetSwapChain();
		check(SwapChain);

		SwapChain->Present(InSyncInterval, 0);

		return false;
	}
};
