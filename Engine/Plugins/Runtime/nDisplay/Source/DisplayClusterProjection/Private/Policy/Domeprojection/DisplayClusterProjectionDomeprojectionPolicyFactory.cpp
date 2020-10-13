// Copyright Epic Games, Inc. All Rights Reserved.

#include "Policy/Domeprojection/DisplayClusterProjectionDomeprojectionPolicyFactory.h"
#include "Policy/Domeprojection/DX11/DisplayClusterProjectionDomeprojectionPolicyDX11.h"

#include "DisplayClusterProjectionLog.h"
#include "Misc/DisplayClusterStrings.h"


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProjectionPolicyFactory
//////////////////////////////////////////////////////////////////////////////////////////////
TSharedPtr<IDisplayClusterProjectionPolicy> FDisplayClusterProjectionDomeprojectionPolicyFactory::Create(const FString& PolicyType, const FString& RHIName, const FString& ViewportId, const TMap<FString, FString>& Parameters)
{
	if (RHIName.Equals(DisplayClusterStrings::rhi::D3D11, ESearchCase::IgnoreCase))
	{
		UE_LOG(LogDisplayClusterProjectionDomeprojection, Log, TEXT("Instantiating projection policy <%s>..."), *PolicyType);
		return MakeShared<FDisplayClusterProjectionDomeprojectionPolicyDX11>(ViewportId, Parameters);
	}

	UE_LOG(LogDisplayClusterProjectionDomeprojection, Warning, TEXT("There is no implementation of '%s' projection policy for RHI %s"), *PolicyType, *RHIName);

	return nullptr;
}
