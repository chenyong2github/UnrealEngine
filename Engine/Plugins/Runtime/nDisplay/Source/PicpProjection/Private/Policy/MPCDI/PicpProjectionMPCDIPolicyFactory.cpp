// Copyright Epic Games, Inc. All Rights Reserved.

#include "PicpProjectionMPCDIPolicyFactory.h"
#include "PicpProjectionMPCDIPolicy.h"

#include "../Mesh/PicpProjectionMeshPolicy.h"

#include "PicpProjectionLog.h"
#include "PicpProjectionStrings.h"


TArray<TSharedPtr<FPicpProjectionPolicyBase>> FPicpProjectionMPCDIPolicyFactory::GetPicpPolicy()
{
	return PicpPolicy;
}

TSharedPtr<FPicpProjectionPolicyBase> FPicpProjectionMPCDIPolicyFactory::GetPicpPolicyByViewport(const FString& ViewportId)
{
	for (auto& It : PicpPolicy)
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
TSharedPtr<IDisplayClusterProjectionPolicy> FPicpProjectionMPCDIPolicyFactory::Create(const FString& PolicyType, const FString& RHIName, const FString& ViewportId, const TMap<FString, FString>& Parameters)
{
	UE_LOG(LogPicpProjectionMPCDI, Log, TEXT("Instantiating projection policy <%s>..."), *PolicyType);

	if(!PolicyType.Compare(PicpProjectionStrings::projection::PicpMPCDI,ESearchCase::IgnoreCase))
	{
		TSharedPtr<FPicpProjectionPolicyBase> Result = MakeShared<FPicpProjectionMPCDIPolicy>(ViewportId, Parameters);
		PicpPolicy.Add(Result);
		return StaticCastSharedPtr<IDisplayClusterProjectionPolicy>(Result);
	}

	if (!PolicyType.Compare(PicpProjectionStrings::projection::PicpMesh, ESearchCase::IgnoreCase))
	{
		TSharedPtr<FPicpProjectionPolicyBase> Result = MakeShared<FPicpProjectionMeshPolicy>(ViewportId, Parameters);
		PicpPolicy.Add(Result);
		return StaticCastSharedPtr<IDisplayClusterProjectionPolicy>(Result);
	}

	return nullptr;
}
