// Copyright Epic Games, Inc. All Rights Reserved.

#include "Policy/EasyBlend/DisplayClusterProjectionEasyBlendPolicyFactory.h"
#include "Policy/EasyBlend/DX11/DisplayClusterProjectionEasyBlendPolicyDX11.h"

#include "DisplayClusterProjectionLog.h"
#include "DisplayClusterProjectionStrings.h"

#include "DisplayClusterConfigurationTypes.h"

//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProjectionPolicyFactory
//////////////////////////////////////////////////////////////////////////////////////////////
TSharedPtr<IDisplayClusterProjectionPolicy> FDisplayClusterProjectionEasyBlendPolicyFactory::Create(const FString& ProjectionPolicyId, const struct FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy)
{
	check(InConfigurationProjectionPolicy != nullptr);

	FString RHIName = GDynamicRHI->GetName();

	if (RHIName.Equals(DisplayClusterProjectionStrings::rhi::D3D11, ESearchCase::IgnoreCase))
	{
		UE_LOG(LogDisplayClusterProjectionEasyBlend, Log, TEXT("Instantiating projection policy <%s> id='%s'"), *InConfigurationProjectionPolicy->Type, *ProjectionPolicyId);
		return  MakeShared<FDisplayClusterProjectionEasyBlendPolicyDX11>(ProjectionPolicyId, InConfigurationProjectionPolicy);
	}

	UE_LOG(LogDisplayClusterProjectionEasyBlend, Warning, TEXT("There is no implementation of '%s' projection policy for RHI %s"), *InConfigurationProjectionPolicy->Type, *RHIName);
	
	return nullptr;
}
