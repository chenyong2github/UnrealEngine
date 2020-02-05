// Copyright Epic Games, Inc. All Rights Reserved.

#include "Policy/MPCDI/DisplayClusterProjectionMPCDIPolicyFactory.h"
#include "Policy/MPCDI/DisplayClusterProjectionMPCDIPolicy.h"
#include "Policy/Mesh/DisplayClusterProjectionMeshPolicy.h"

#include "DisplayClusterProjectionLog.h"
#include "DisplayClusterProjectionStrings.h"


FDisplayClusterProjectionMPCDIPolicyFactory::FDisplayClusterProjectionMPCDIPolicyFactory()
{
}

FDisplayClusterProjectionMPCDIPolicyFactory::~FDisplayClusterProjectionMPCDIPolicyFactory()
{
}

TArray<TSharedPtr<FDisplayClusterProjectionPolicyBase>> FDisplayClusterProjectionMPCDIPolicyFactory::GetPolicy()
{
	return Policy;
}

TSharedPtr<FDisplayClusterProjectionPolicyBase> FDisplayClusterProjectionMPCDIPolicyFactory::GetPolicyByViewport(const FString& ViewportId)
{
	for (auto& It : Policy)
	{
		if (!ViewportId.Compare(It->GetViewportId(), ESearchCase::IgnoreCase))
		{
			return It;
		}
	}
	return nullptr;
}

//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProjectionPolicyFactory
//////////////////////////////////////////////////////////////////////////////////////////////
TSharedPtr<IDisplayClusterProjectionPolicy> FDisplayClusterProjectionMPCDIPolicyFactory::Create(const FString& PolicyType, const FString& RHIName, const FString& ViewportId)
{
	UE_LOG(LogDisplayClusterProjectionMPCDI, Log, TEXT("Instantiating projection policy <%s>..."), *PolicyType);

	if (!PolicyType.Compare(DisplayClusterStrings::projection::MPCDI, ESearchCase::IgnoreCase))
	{
		TSharedPtr<FDisplayClusterProjectionPolicyBase> Result = MakeShareable(new FDisplayClusterProjectionMPCDIPolicy(ViewportId));
		Policy.Add(Result);
		return StaticCastSharedPtr<IDisplayClusterProjectionPolicy>(Result);
	}

	if (!PolicyType.Compare(DisplayClusterStrings::projection::Mesh, ESearchCase::IgnoreCase))
	{
		TSharedPtr<FDisplayClusterProjectionPolicyBase> Result = MakeShareable(new FDisplayClusterProjectionMeshPolicy(ViewportId));
		Policy.Add(Result);
		return StaticCastSharedPtr<IDisplayClusterProjectionPolicy>(Result);
	}

	return MakeShareable(new FDisplayClusterProjectionMPCDIPolicy(ViewportId));
};
