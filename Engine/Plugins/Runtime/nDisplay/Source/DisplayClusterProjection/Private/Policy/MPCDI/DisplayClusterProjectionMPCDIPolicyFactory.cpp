// Copyright Epic Games, Inc. All Rights Reserved.

#include "Policy/MPCDI/DisplayClusterProjectionMPCDIPolicyFactory.h"
#include "Policy/MPCDI/DisplayClusterProjectionMPCDIPolicy.h"
#include "Policy/Mesh/DisplayClusterProjectionMeshPolicy.h"

#include "DisplayClusterProjectionLog.h"
#include "DisplayClusterProjectionStrings.h"

#include "DisplayClusterConfigurationTypes.h"

//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProjectionPolicyFactory
//////////////////////////////////////////////////////////////////////////////////////////////
TSharedPtr<IDisplayClusterProjectionPolicy> FDisplayClusterProjectionMPCDIPolicyFactory::Create(const FString& ProjectionPolicyId, const struct FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy)
{
	check(InConfigurationProjectionPolicy != nullptr);

	UE_LOG(LogDisplayClusterProjectionMPCDI, Log, TEXT("Instantiating projection policy <%s>...id='%s'"), *InConfigurationProjectionPolicy->Type, *ProjectionPolicyId);

	if (InConfigurationProjectionPolicy->Type.Equals(DisplayClusterProjectionStrings::projection::MPCDI, ESearchCase::IgnoreCase))
	{
		return MakeShared<FDisplayClusterProjectionMPCDIPolicy>(ProjectionPolicyId, InConfigurationProjectionPolicy);
	}

	if (InConfigurationProjectionPolicy->Type.Equals(DisplayClusterProjectionStrings::projection::Mesh, ESearchCase::IgnoreCase))
	{
		return MakeShared<FDisplayClusterProjectionMeshPolicy>(ProjectionPolicyId, InConfigurationProjectionPolicy);
	}

	return MakeShared<FDisplayClusterProjectionMPCDIPolicy>(ProjectionPolicyId, InConfigurationProjectionPolicy);
};
