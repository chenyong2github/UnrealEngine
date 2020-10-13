// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Synchronization/DisplayClusterRenderSyncPolicyFactoryInternal.h"
#include "Render/Synchronization/DisplayClusterRenderSyncPolicySoftwareGeneric.h"
#include "Render/Synchronization/DisplayClusterRenderSyncPolicySoftwareDX11.h"
#include "Render/Synchronization/DisplayClusterRenderSyncPolicySoftwareDX12.h"
#include "Render/Synchronization/DisplayClusterRenderSyncPolicyNvidiaDX11.h"
#include "Render/Synchronization/DisplayClusterRenderSyncPolicyNvidiaDX12.h"
#include "Render/Synchronization/DisplayClusterRenderSyncPolicyNone.h"

#include "Misc/DisplayClusterStrings.h"
#include "DisplayClusterConfigurationStrings.h"


FDisplayClusterRenderSyncPolicyFactoryInternal::FDisplayClusterRenderSyncPolicyFactoryInternal()
{
}

FDisplayClusterRenderSyncPolicyFactoryInternal::~FDisplayClusterRenderSyncPolicyFactoryInternal()
{
}

TSharedPtr<IDisplayClusterRenderSyncPolicy> FDisplayClusterRenderSyncPolicyFactoryInternal::Create(const FString& InPolicyType, const FString& InRHIName, const TMap<FString, FString>& Parameters)
{
	if (InPolicyType.Equals(DisplayClusterConfigurationStrings::config::cluster::render_sync::None, ESearchCase::IgnoreCase))
	{
		return MakeShared<FDisplayClusterRenderSyncPolicyNone>(Parameters);
	}
	else if (InPolicyType.Equals(DisplayClusterConfigurationStrings::config::cluster::render_sync::Ethernet, ESearchCase::IgnoreCase))
	{
		if (InRHIName.Equals(DisplayClusterStrings::rhi::D3D11, ESearchCase::IgnoreCase))
		{
			return MakeShared<FDisplayClusterRenderSyncPolicySoftwareDX11>(Parameters);
		}
		else if (InRHIName.Equals(DisplayClusterStrings::rhi::D3D12, ESearchCase::IgnoreCase))
		{
			return MakeShared<FDisplayClusterRenderSyncPolicySoftwareDX12>(Parameters);
		}
	}
	else if (InPolicyType.Equals(DisplayClusterConfigurationStrings::config::cluster::render_sync::Nvidia, ESearchCase::IgnoreCase))
	{
		if (InRHIName.Equals(DisplayClusterStrings::rhi::D3D11, ESearchCase::IgnoreCase))
		{
			return MakeShared<FDisplayClusterRenderSyncPolicyNvidiaDX11>(Parameters);
		}
		else if (InRHIName.Equals(DisplayClusterStrings::rhi::D3D12, ESearchCase::IgnoreCase))
		{
			return MakeShared<FDisplayClusterRenderSyncPolicyNvidiaDX12>(Parameters);
		}
	}

	return nullptr;
}
