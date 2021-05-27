// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Synchronization/DisplayClusterRenderSyncPolicyFactoryInternal.h"

#include "Render/Synchronization/DisplayClusterRenderSyncPolicyNone.h"
#include "Render/Synchronization/DisplayClusterRenderSyncPolicyEthernet.h"
#include "Render/Synchronization/DisplayClusterRenderSyncPolicyEthernetBarrier.h"
#include "Render/Synchronization/DisplayClusterRenderSyncPolicyNvidia.h"

#include "Misc/DisplayClusterLog.h"
#include "Misc/DisplayClusterStrings.h"
#include "DisplayClusterConfigurationStrings.h"


TSharedPtr<IDisplayClusterRenderSyncPolicy> FDisplayClusterRenderSyncPolicyFactoryInternal::Create(const FString& InPolicyType, const FString& InRHIName, const TMap<FString, FString>& Parameters)
{
	if (InPolicyType.Equals(DisplayClusterConfigurationStrings::config::cluster::render_sync::None, ESearchCase::IgnoreCase))
	{
		return MakeShared<FDisplayClusterRenderSyncPolicyNone>(Parameters);
	}
	else if (InPolicyType.Equals(DisplayClusterConfigurationStrings::config::cluster::render_sync::Ethernet, ESearchCase::IgnoreCase))
	{
#if PLATFORM_WINDOWS
		if (InRHIName.Equals(DisplayClusterStrings::rhi::D3D11,  ESearchCase::IgnoreCase) ||
			InRHIName.Equals(DisplayClusterStrings::rhi::D3D12,  ESearchCase::IgnoreCase))
		{
			return MakeShared<FDisplayClusterRenderSyncPolicyEthernet>(Parameters);
		}
		else if (InRHIName.Equals(DisplayClusterStrings::rhi::Vulkan, ESearchCase::IgnoreCase))
		{
			return MakeShared<FDisplayClusterRenderSyncPolicyEthernetBarrier>(Parameters);
		}
#elif PLATFORM_LINUX
		if (InRHIName.Equals(DisplayClusterStrings::rhi::Vulkan, ESearchCase::IgnoreCase))
		{
			return MakeShared<FDisplayClusterRenderSyncPolicyEthernet>(Parameters);
		}
#endif
	}
	else if (InPolicyType.Equals(DisplayClusterConfigurationStrings::config::cluster::render_sync::Nvidia, ESearchCase::IgnoreCase))
	{
#if PLATFORM_WINDOWS
		if (InRHIName.Equals(DisplayClusterStrings::rhi::D3D11, ESearchCase::IgnoreCase) ||
			InRHIName.Equals(DisplayClusterStrings::rhi::D3D12, ESearchCase::IgnoreCase))
		{
			return MakeShared<FDisplayClusterRenderSyncPolicyNvidia>(Parameters);
		}
		else if (InRHIName.Equals(DisplayClusterStrings::rhi::Vulkan, ESearchCase::IgnoreCase))
		{
			const FString DefaultPolicy(DisplayClusterConfigurationStrings::config::cluster::render_sync::EthernetBarrier);

			UE_LOG(LogDisplayClusterRender, Warning, TEXT("Sync policy '%s' has not been implemented for '%s' RHI. Default '%s' will be used."),
				DisplayClusterConfigurationStrings::config::cluster::render_sync::Nvidia, *InRHIName, *DefaultPolicy);

			return Create(DefaultPolicy, InRHIName, Parameters);
		}
#elif PLATFORM_LINUX
		if (InRHIName.Equals(DisplayClusterStrings::rhi::Vulkan, ESearchCase::IgnoreCase))
		{
			return MakeShared<FDisplayClusterRenderSyncPolicyNvidia>(Parameters);
		}
#endif
	}

	return nullptr;
}
