// Copyright Epic Games, Inc. All Rights Reserved.

#include "Policy/MPCDI/DisplayClusterProjectionMPCDIPolicyFactory.h"
#include "Policy/MPCDI/DisplayClusterProjectionMPCDIPolicy.h"

#include "DisplayClusterProjectionLog.h"
#include "DisplayClusterProjectionStrings.h"


FDisplayClusterProjectionMPCDIPolicyFactory::FDisplayClusterProjectionMPCDIPolicyFactory()
{
}

FDisplayClusterProjectionMPCDIPolicyFactory::~FDisplayClusterProjectionMPCDIPolicyFactory()
{
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProjectionPolicyFactory
//////////////////////////////////////////////////////////////////////////////////////////////
TSharedPtr<IDisplayClusterProjectionPolicy> FDisplayClusterProjectionMPCDIPolicyFactory::Create(const FString& PolicyType, const FString& RHIName, const FString& ViewportId)
{
	UE_LOG(LogDisplayClusterProjectionMPCDI, Log, TEXT("Instantiating projection policy <%s>..."), *PolicyType);
	return MakeShareable(new FDisplayClusterProjectionMPCDIPolicy(ViewportId));
}
