// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Synchronization/DisplayClusterRenderSyncPolicyFactoryInternal.h"
#include "Render/Synchronization/DisplayClusterRenderSyncPolicySoftwareBase.h"
#include "Render/Synchronization/DisplayClusterRenderSyncPolicyNvidiaBase.h"
#include "Render/Synchronization/DisplayClusterRenderSyncPolicyNone.h"

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
		if (InRHIName.Equals(DisplayClusterStrings::rhi::D3D11, ESearchCase::IgnoreCase))
		{
			return MakeShared<FDisplayClusterRenderSyncPolicySoftwareBase>(Parameters);
		}
		else if (InRHIName.Equals(DisplayClusterStrings::rhi::D3D12, ESearchCase::IgnoreCase))
		{
			return MakeShared<FDisplayClusterRenderSyncPolicySoftwareBase>(Parameters);
		}
	}
	else if (InPolicyType.Equals(DisplayClusterConfigurationStrings::config::cluster::render_sync::Nvidia, ESearchCase::IgnoreCase))
	{
		if (InRHIName.Equals(DisplayClusterStrings::rhi::D3D11, ESearchCase::IgnoreCase))
		{
			return MakeShared<FDisplayClusterRenderSyncPolicyNvidiaBase>(Parameters);
		}
		else if (InRHIName.Equals(DisplayClusterStrings::rhi::D3D12, ESearchCase::IgnoreCase))
		{
			return MakeShared<FDisplayClusterRenderSyncPolicyNvidiaBase>(Parameters);
		}
	}

	return nullptr;
}
