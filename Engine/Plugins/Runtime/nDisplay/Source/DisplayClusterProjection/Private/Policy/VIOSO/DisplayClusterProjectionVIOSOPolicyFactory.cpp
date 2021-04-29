// Copyright Epic Games, Inc. All Rights Reserved.

#include "Policy/VIOSO/DisplayClusterProjectionVIOSOPolicyFactory.h"
#include "Policy/VIOSO/DisplayClusterProjectionVIOSOPolicy.h"
#include "DisplayClusterProjectionStrings.h"
#include "DisplayClusterProjectionLog.h"

#include "DisplayClusterConfigurationTypes.h"

FDisplayClusterProjectionVIOSOPolicyFactory::FDisplayClusterProjectionVIOSOPolicyFactory()
{
}

FDisplayClusterProjectionVIOSOPolicyFactory::~FDisplayClusterProjectionVIOSOPolicyFactory()
{
}

//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProjectionPolicyFactory
//////////////////////////////////////////////////////////////////////////////////////////////
TSharedPtr<IDisplayClusterProjectionPolicy> FDisplayClusterProjectionVIOSOPolicyFactory::Create(const FString& ProjectionPolicyId, const struct FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy)
{
	check(InConfigurationProjectionPolicy != nullptr);

	UE_LOG(LogDisplayClusterProjectionVIOSO, Log, TEXT("Instantiating projection policy <%s> id='%s'"), *InConfigurationProjectionPolicy->Type, *ProjectionPolicyId);
	return  MakeShared<FDisplayClusterProjectionVIOSOPolicy>(ProjectionPolicyId, InConfigurationProjectionPolicy);
}
