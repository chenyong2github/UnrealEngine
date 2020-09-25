// Copyright Epic Games, Inc. All Rights Reserved.

#include "Policy/EasyBlend/DisplayClusterProjectionEasyBlendPolicyFactory.h"
#include "Policy/EasyBlend/DX11/DisplayClusterProjectionEasyBlendPolicyDX11.h"

#include "DisplayClusterProjectionLog.h"
#include "DisplayClusterProjectionStrings.h"


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProjectionPolicyFactory
//////////////////////////////////////////////////////////////////////////////////////////////
TSharedPtr<IDisplayClusterProjectionPolicy> FDisplayClusterProjectionEasyBlendPolicyFactory::Create(const FString& PolicyType, const FString& RHIName, const FString& ViewportId, const TMap<FString, FString>& Parameters)
{
	if (RHIName.Equals(DisplayClusterProjectionStrings::rhi::D3D11, ESearchCase::IgnoreCase))
	{
		UE_LOG(LogDisplayClusterProjectionEasyBlend, Log, TEXT("Instantiating projection policy <%s>..."), *PolicyType);
		return MakeShared<FDisplayClusterProjectionEasyBlendPolicyDX11>(ViewportId, Parameters);
	}

	UE_LOG(LogDisplayClusterProjectionEasyBlend, Warning, TEXT("There is no implementation of '%s' projection policy for RHI %s"), *PolicyType, *RHIName);
	
	return nullptr;
}
