// Copyright Epic Games, Inc. All Rights Reserved.

#include "Policy/VIOSO/DisplayClusterProjectionVIOSOPolicyFactory.h"
#include "Policy/VIOSO/DisplayClusterProjectionVIOSOPolicy.h"
#include "DisplayClusterProjectionStrings.h"
#include "DisplayClusterProjectionLog.h"

FDisplayClusterProjectionVIOSOPolicyFactory::FDisplayClusterProjectionVIOSOPolicyFactory()
{
}

FDisplayClusterProjectionVIOSOPolicyFactory::~FDisplayClusterProjectionVIOSOPolicyFactory()
{
}

//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProjectionPolicyFactory
//////////////////////////////////////////////////////////////////////////////////////////////
TSharedPtr<IDisplayClusterProjectionPolicy> FDisplayClusterProjectionVIOSOPolicyFactory::Create(const FString& PolicyType, const FString& RHIName, const FString& ViewportId, const TMap<FString, FString>& Parameters)
{
	UE_LOG(LogDisplayClusterProjectionVIOSO, Log, TEXT("Instantiating projection policy <%s>..."), *PolicyType);

	return MakeShareable(new FDisplayClusterProjectionVIOSOPolicy(ViewportId, RHIName, Parameters));
};
