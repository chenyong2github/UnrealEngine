// Copyright Epic Games, Inc. All Rights Reserved.

#include "Policy/Domeprojection/DisplayClusterProjectionDomeprojectionPolicyFactory.h"

#include "DisplayClusterProjectionLog.h"
#include "Misc/DisplayClusterStrings.h"

#include "DisplayClusterConfigurationTypes.h"

#if PLATFORM_WINDOWS
#include "Policy/Domeprojection/Windows/DX11/DisplayClusterProjectionDomeprojectionPolicyDX11.h"
#endif


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProjectionPolicyFactory
//////////////////////////////////////////////////////////////////////////////////////////////
TSharedPtr<IDisplayClusterProjectionPolicy> FDisplayClusterProjectionDomeprojectionPolicyFactory::Create(const FString& ProjectionPolicyId, const struct FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy)
{
	FString RHIName = GDynamicRHI->GetName();

#if PLATFORM_WINDOWS
	if (RHIName.Equals(DisplayClusterStrings::rhi::D3D11, ESearchCase::IgnoreCase))
	{
		check(InConfigurationProjectionPolicy != nullptr);

		UE_LOG(LogDisplayClusterProjectionDomeprojection, Log, TEXT("Instantiating projection policy <%s> id='%s'"), *InConfigurationProjectionPolicy->Type, *ProjectionPolicyId);
		return MakeShared<FDisplayClusterProjectionDomeprojectionPolicyDX11>(ProjectionPolicyId, InConfigurationProjectionPolicy);
	}
#endif

	UE_LOG(LogDisplayClusterProjectionDomeprojection, Warning, TEXT("There is no implementation of '%s' projection policy for RHI %s"), *InConfigurationProjectionPolicy->Type, *RHIName);

	return nullptr;
}
