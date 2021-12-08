// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Synchronization/DisplayClusterRenderSyncHelper.h"

#include "Misc/DisplayClusterStrings.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "DynamicRHI.h"



TUniquePtr<IDisplayClusterRenderSyncHelper> FDisplayClusterRenderSyncHelper::CreateHelper()
{
	if (GDynamicRHI)
	{
		const FString RHIName = GDynamicRHI->GetName();

		// Instantiate Vulkan helper
		if (RHIName.Equals(DisplayClusterStrings::rhi::Vulkan, ESearchCase::IgnoreCase))
		{
			return MakeUnique<FDisplayClusterRenderSyncHelper::FDCRSHelperVulkan>();
		}
	}

	// Null-helper stub as fallback
	return MakeUnique<FDisplayClusterRenderSyncHelper::FDCRSHelperNull>();
}


bool FDisplayClusterRenderSyncHelper::FDCRSHelperVulkan::IsWaitForVBlankSupported()
{
	return false;
}

bool FDisplayClusterRenderSyncHelper::FDCRSHelperVulkan::WaitForVBlank()
{
	return false;
}
