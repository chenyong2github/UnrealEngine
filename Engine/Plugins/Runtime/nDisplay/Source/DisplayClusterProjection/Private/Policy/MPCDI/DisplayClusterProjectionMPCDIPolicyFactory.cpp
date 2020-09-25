// Copyright Epic Games, Inc. All Rights Reserved.

#include "Policy/MPCDI/DisplayClusterProjectionMPCDIPolicyFactory.h"
#include "Policy/MPCDI/DisplayClusterProjectionMPCDIPolicy.h"
#include "Policy/Mesh/DisplayClusterProjectionMeshPolicy.h"

#include "DisplayClusterProjectionLog.h"
#include "DisplayClusterProjectionStrings.h"


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
TSharedPtr<IDisplayClusterProjectionPolicy> FDisplayClusterProjectionMPCDIPolicyFactory::Create(const FString& PolicyType, const FString& RHIName, const FString& ViewportId, const TMap<FString, FString>& Parameters)
{
	UE_LOG(LogDisplayClusterProjectionMPCDI, Log, TEXT("Instantiating projection policy <%s>..."), *PolicyType);

	if (PolicyType.Equals(DisplayClusterProjectionStrings::projection::MPCDI, ESearchCase::IgnoreCase))
	{
		TSharedPtr<FDisplayClusterProjectionPolicyBase> Result = MakeShared<FDisplayClusterProjectionMPCDIPolicy>(ViewportId, Parameters);
		Policy.Add(Result);
		return StaticCastSharedPtr<IDisplayClusterProjectionPolicy>(Result);
	}

	if (PolicyType.Equals(DisplayClusterProjectionStrings::projection::Mesh, ESearchCase::IgnoreCase))
	{
		TSharedPtr<FDisplayClusterProjectionPolicyBase> Result = MakeShared<FDisplayClusterProjectionMeshPolicy>(ViewportId, Parameters);
		Policy.Add(Result);
		return StaticCastSharedPtr<IDisplayClusterProjectionPolicy>(Result);
	}

	return MakeShared<FDisplayClusterProjectionMPCDIPolicy>(ViewportId, Parameters);
};
