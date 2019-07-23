// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Policy/Camera/DisplayClusterProjectionCameraPolicyFactory.h"
#include "Policy/Camera/DisplayClusterProjectionCameraPolicy.h"

#include "DisplayClusterProjectionLog.h"


FDisplayClusterProjectionCameraPolicyFactory::FDisplayClusterProjectionCameraPolicyFactory()
{
}

FDisplayClusterProjectionCameraPolicyFactory ::~FDisplayClusterProjectionCameraPolicyFactory()
{
}

TSharedPtr<IDisplayClusterProjectionPolicy> FDisplayClusterProjectionCameraPolicyFactory::GetPolicyInstance(const FString& ViewportId)
{
	if (PolicyInstances.Contains(ViewportId))
	{
		return PolicyInstances[ViewportId];
	}

	return nullptr;
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProjectionPolicyFactory
//////////////////////////////////////////////////////////////////////////////////////////////
TSharedPtr<IDisplayClusterProjectionPolicy> FDisplayClusterProjectionCameraPolicyFactory::Create(const FString& PolicyType, const FString& RHIName, const FString& ViewportId)
{
	UE_LOG(LogDisplayClusterProjectionCamera, Log, TEXT("Instantiating projection policy <%s>..."), *PolicyType);
	TSharedPtr<IDisplayClusterProjectionPolicy> NewPolicy = MakeShareable(new FDisplayClusterProjectionCameraPolicy(ViewportId));
	PolicyInstances.Emplace(ViewportId, NewPolicy);
	return NewPolicy;
}
