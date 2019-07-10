// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/device/DisplayClusterDeviceInternals.h"
#include "DisplayClusterLog.h"

#include "D3D12Viewport.h"


/**
 * Helper class to encapsulate DX12 frame presentation
 */
class FDisplayClusterDevicePresentationDX12
{
public:
	FDisplayClusterDevicePresentationDX12() = default;
	virtual ~FDisplayClusterDevicePresentationDX12() = default;

public:
	bool PresentImpl(FRHIViewport* Viewport, const int32 InSyncInterval)
	{
		check(Viewport);
		if (Viewport == nullptr)
		{
			UE_LOG(LogDisplayClusterRender, Warning, TEXT("Viewport is nullptr. Use native present."));
			return true;
		}

		FD3D12Viewport* D3D12Viewport = static_cast<FD3D12Viewport*>(Viewport);
		check(D3D12Viewport);

		IDXGISwapChain* SwapChain = D3D12Viewport->GetSwapChain();
		check(SwapChain);

		SwapChain->Present(InSyncInterval, 0);

		return false;
	}
};
