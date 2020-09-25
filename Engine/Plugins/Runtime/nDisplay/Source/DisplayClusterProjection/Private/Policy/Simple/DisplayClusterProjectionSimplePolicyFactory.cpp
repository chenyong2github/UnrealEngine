// Copyright Epic Games, Inc. All Rights Reserved.

#include "Policy/Simple/DisplayClusterProjectionSimplePolicyFactory.h"
#include "Policy/Simple/DisplayClusterProjectionSimplePolicy.h"

#include "DisplayClusterProjectionLog.h"


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProjectionPolicyFactory
//////////////////////////////////////////////////////////////////////////////////////////////
TSharedPtr<IDisplayClusterProjectionPolicy> FDisplayClusterProjectionSimplePolicyFactory::Create(const FString& PolicyType, const FString& RHIName, const FString& ViewportId, const TMap<FString, FString>& Parameters)
{
	UE_LOG(LogDisplayClusterProjectionSimple, Log, TEXT("Instantiating projection policy <%s>..."), *PolicyType);
	return MakeShared<FDisplayClusterProjectionSimplePolicy>(ViewportId, Parameters);
}
