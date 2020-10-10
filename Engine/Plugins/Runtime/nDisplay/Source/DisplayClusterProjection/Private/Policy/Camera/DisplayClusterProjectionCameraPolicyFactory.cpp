// Copyright Epic Games, Inc. All Rights Reserved.

#include "Policy/Camera/DisplayClusterProjectionCameraPolicyFactory.h"
#include "Policy/Camera/DisplayClusterProjectionCameraPolicy.h"

#include "DisplayClusterProjectionLog.h"


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
TSharedPtr<IDisplayClusterProjectionPolicy> FDisplayClusterProjectionCameraPolicyFactory::Create(const FString& PolicyType, const FString& RHIName, const FString& ViewportId, const TMap<FString, FString>& Parameters)
{
	UE_LOG(LogDisplayClusterProjectionCamera, Log, TEXT("Instantiating projection policy <%s>..."), *PolicyType);
	TSharedPtr<IDisplayClusterProjectionPolicy> NewPolicy = MakeShared<FDisplayClusterProjectionCameraPolicy>(ViewportId, Parameters);
	PolicyInstances.Emplace(ViewportId, NewPolicy);
	return NewPolicy;
}
