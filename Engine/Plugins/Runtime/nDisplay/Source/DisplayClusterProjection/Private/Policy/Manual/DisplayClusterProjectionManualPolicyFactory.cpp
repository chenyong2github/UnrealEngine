// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Policy/Manual/DisplayClusterProjectionManualPolicyFactory.h"
#include "Policy/Manual/DisplayClusterProjectionManualPolicy.h"

#include "DisplayClusterProjectionLog.h"


FDisplayClusterProjectionManualPolicyFactory::FDisplayClusterProjectionManualPolicyFactory()
{
}

FDisplayClusterProjectionManualPolicyFactory ::~FDisplayClusterProjectionManualPolicyFactory()
{
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProjectionPolicyFactory
//////////////////////////////////////////////////////////////////////////////////////////////
TSharedPtr<IDisplayClusterProjectionPolicy> FDisplayClusterProjectionManualPolicyFactory::Create(const FString& PolicyType, const FString& RHIName, const FString& ViewportId)
{
	UE_LOG(LogDisplayClusterProjectionManual, Log, TEXT("Instantiating projection policy <%s>..."), *PolicyType);
	return MakeShareable(new FDisplayClusterProjectionManualPolicy(ViewportId));
}
