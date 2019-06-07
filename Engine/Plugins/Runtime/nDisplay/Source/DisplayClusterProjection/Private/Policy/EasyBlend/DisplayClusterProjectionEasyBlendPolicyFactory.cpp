// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Policy/EasyBlend/DisplayClusterProjectionEasyBlendPolicyFactory.h"
#include "Policy/EasyBlend/DX11/DisplayClusterProjectionEasyBlendPolicyDX11.h"

#include "DisplayClusterProjectionLog.h"
#include "DisplayClusterProjectionStrings.h"


FDisplayClusterProjectionEasyBlendPolicyFactory::FDisplayClusterProjectionEasyBlendPolicyFactory()
{
}

FDisplayClusterProjectionEasyBlendPolicyFactory::~FDisplayClusterProjectionEasyBlendPolicyFactory()
{
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProjectionPolicyFactory
//////////////////////////////////////////////////////////////////////////////////////////////
TSharedPtr<IDisplayClusterProjectionPolicy> FDisplayClusterProjectionEasyBlendPolicyFactory::Create(const FString& PolicyType, const FString& RHIName, const FString& ViewportId)
{
	if (RHIName.Compare(DisplayClusterStrings::rhi::D3D11, ESearchCase::IgnoreCase) == 0)
	{
		UE_LOG(LogDisplayClusterProjectionEasyBlend, Log, TEXT("Instantiating projection policy <%s>..."), *PolicyType);
		return MakeShareable(new FDisplayClusterProjectionEasyBlendPolicyDX11(ViewportId));
	}

	UE_LOG(LogDisplayClusterProjectionEasyBlend, Warning, TEXT("There is no implementation of '%s' projection policy for RHI %s"), *PolicyType, *RHIName);
	
	return nullptr;
}
