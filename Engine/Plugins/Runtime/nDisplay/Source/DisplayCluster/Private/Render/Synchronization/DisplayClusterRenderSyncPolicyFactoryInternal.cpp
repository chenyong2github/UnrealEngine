// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Synchronization/DisplayClusterRenderSyncPolicyFactoryInternal.h"
#include "Render/Synchronization/DisplayClusterRenderSyncPolicySoftwareGeneric.h"
#include "Render/Synchronization/DisplayClusterRenderSyncPolicySoftwareDX11.h"
#include "Render/Synchronization/DisplayClusterRenderSyncPolicySoftwareDX12.h"
#include "Render/Synchronization/DisplayClusterRenderSyncPolicyNvidiaDX11.h"
#include "Render/Synchronization/DisplayClusterRenderSyncPolicyNvidiaDX12.h"
#include "Render/Synchronization/DisplayClusterRenderSyncPolicyNone.h"

#include "DisplayClusterStrings.h"


FDisplayClusterRenderSyncPolicyFactoryInternal::FDisplayClusterRenderSyncPolicyFactoryInternal()
{
}

FDisplayClusterRenderSyncPolicyFactoryInternal::~FDisplayClusterRenderSyncPolicyFactoryInternal()
{
}

TSharedPtr<IDisplayClusterRenderSyncPolicy> FDisplayClusterRenderSyncPolicyFactoryInternal::Create(const FString& InPolicyType, const FString& InRHIName)
{
	if (InPolicyType.Compare(TEXT("0"), ESearchCase::IgnoreCase) == 0)
	{
		return MakeShareable(new FDisplayClusterRenderSyncPolicyNone);
	}
	else if (InPolicyType.Compare(TEXT("1"), ESearchCase::IgnoreCase) == 0)
	{
		if (InRHIName.Compare(DisplayClusterStrings::rhi::D3D11, ESearchCase::IgnoreCase) == 0)
		{
			return MakeShareable(new FDisplayClusterRenderSyncPolicySoftwareDX11);
		}
		else if (InRHIName.Compare(DisplayClusterStrings::rhi::D3D12, ESearchCase::IgnoreCase) == 0)
		{
			return MakeShareable(new FDisplayClusterRenderSyncPolicySoftwareDX12);
		}
	}
	else if (InPolicyType.Compare(TEXT("2"), ESearchCase::IgnoreCase) == 0)
	{
		if (InRHIName.Compare(DisplayClusterStrings::rhi::D3D11, ESearchCase::IgnoreCase) == 0)
		{
			return MakeShareable(new FDisplayClusterRenderSyncPolicyNvidiaDX11);
		}
		else if (InRHIName.Compare(DisplayClusterStrings::rhi::D3D12, ESearchCase::IgnoreCase) == 0)
		{
			return MakeShareable(new FDisplayClusterRenderSyncPolicyNvidiaDX12);
		}
	}

	return nullptr;
}
