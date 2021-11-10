// Copyright Epic Games, Inc. All Rights Reserved.

#include "Policy/EasyBlend/DisplayClusterProjectionEasyBlendPolicyFactory.h"

#include "DisplayClusterProjectionLog.h"
#include "DisplayClusterProjectionStrings.h"

#include "DisplayClusterConfigurationTypes.h"

#if PLATFORM_WINDOWS
#include "Policy/EasyBlend/Windows/DX11/DisplayClusterProjectionEasyBlendPolicyDX11.h"
#endif

//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProjectionPolicyFactory
//////////////////////////////////////////////////////////////////////////////////////////////
TSharedPtr<IDisplayClusterProjectionPolicy, ESPMode::ThreadSafe> FDisplayClusterProjectionEasyBlendPolicyFactory::Create(const FString& ProjectionPolicyId, const struct FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy)
{
	check(InConfigurationProjectionPolicy != nullptr);

	FString RHIName = GDynamicRHI->GetName();

#if PLATFORM_WINDOWS
	if (RHIName.Equals(DisplayClusterProjectionStrings::rhi::D3D11, ESearchCase::IgnoreCase))
	{
		UE_LOG(LogDisplayClusterProjectionEasyBlend, Verbose, TEXT("Instantiating projection policy <%s> id='%s'"), *InConfigurationProjectionPolicy->Type, *ProjectionPolicyId);
		return MakeShared<FDisplayClusterProjectionEasyBlendPolicyDX11, ESPMode::ThreadSafe>(ProjectionPolicyId, InConfigurationProjectionPolicy);
	}
#endif

	UE_LOG(LogDisplayClusterProjectionEasyBlend, Warning, TEXT("There is no implementation of '%s' projection policy for RHI %s"), *InConfigurationProjectionPolicy->Type, *RHIName);
	
	return nullptr;
}
