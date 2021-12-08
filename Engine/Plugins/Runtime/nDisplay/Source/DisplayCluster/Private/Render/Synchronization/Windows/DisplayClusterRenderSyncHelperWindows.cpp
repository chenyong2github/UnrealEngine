// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Synchronization/DisplayClusterRenderSyncHelper.h"

#include "Misc/DisplayClusterStrings.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "DynamicRHI.h"

#pragma warning(push)
#pragma warning (disable : 4005)
#include "Windows/AllowWindowsPlatformTypes.h"
#include "dxgi.h"
#include "Windows/HideWindowsPlatformTypes.h"
#pragma warning(pop)


TUniquePtr<IDisplayClusterRenderSyncHelper> FDisplayClusterRenderSyncHelper::CreateHelper()
{
	if (GDynamicRHI)
	{
		const FString RHIName = GDynamicRHI->GetName();

		// Instantiate DX helper
		if (RHIName.Equals(DisplayClusterStrings::rhi::D3D11, ESearchCase::IgnoreCase) ||
			RHIName.Equals(DisplayClusterStrings::rhi::D3D12, ESearchCase::IgnoreCase))
		{
			return MakeUnique<FDisplayClusterRenderSyncHelper::FDCRSHelperDX>();
		}
		// Instantiate Vulkan helper
		else if (RHIName.Equals(DisplayClusterStrings::rhi::Vulkan, ESearchCase::IgnoreCase))
		{
			return MakeUnique<FDisplayClusterRenderSyncHelper::FDCRSHelperVulkan>();
		}
	}

	// Null-helper stub as fallback
	return MakeUnique<FDisplayClusterRenderSyncHelper::FDCRSHelperNull>();
}


bool FDisplayClusterRenderSyncHelper::FDCRSHelperDX::IsWaitForVBlankSupported()
{
	return true;
}

bool FDisplayClusterRenderSyncHelper::FDCRSHelperDX::WaitForVBlank()
{
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{
		if (FRHIViewport* RHIViewport = GEngine->GameViewport->Viewport->GetViewportRHI().GetReference())
		{
			if (IDXGISwapChain* DXGISwapChain = static_cast<IDXGISwapChain*>(RHIViewport->GetNativeSwapChain()))
			{
				IDXGIOutput* DXOutput = nullptr;
				DXGISwapChain->GetContainingOutput(&DXOutput);

				if (DXOutput)
				{
					DXOutput->WaitForVBlank();
					DXOutput->Release();

					// Return true to notify about successful v-blank awaiting
					return true;
				}
			}
		}
	}

	// Something went wrong. We have to let the caller know that
	// we are not at the v-blank now.
	return false;
}

bool FDisplayClusterRenderSyncHelper::FDCRSHelperVulkan::IsWaitForVBlankSupported()
{
	return false;
}

bool FDisplayClusterRenderSyncHelper::FDCRSHelperVulkan::WaitForVBlank()
{
	return false;
}
