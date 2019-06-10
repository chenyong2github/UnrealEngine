// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "PicpProjectionMPCDIPolicyFactory.h"
#include "PicpProjectionMPCDIPolicy.h"

#include "PicpProjectionLog.h"
#include "PicpProjectionStrings.h"


FPicpProjectionMPCDIPolicyFactory::FPicpProjectionMPCDIPolicyFactory()
{
}

FPicpProjectionMPCDIPolicyFactory::~FPicpProjectionMPCDIPolicyFactory()
{
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProjectionPolicyFactory
//////////////////////////////////////////////////////////////////////////////////////////////
TSharedPtr<IDisplayClusterProjectionPolicy> FPicpProjectionMPCDIPolicyFactory::Create(const FString& PolicyType, const FString& RHIName, const FString& ViewportId)
{
	//if (RHIName.Compare(PicpProjectionStrings::rhi::D3D11, ESearchCase::IgnoreCase) == 0)
	{
		UE_LOG(LogPicpProjectionMPCDI, Log, TEXT("Instantiating projection policy <%s>..."), *PolicyType);
		TSharedPtr<IDisplayClusterProjectionPolicy> Result = MakeShareable(new FPicpProjectionMPCDIPolicy(ViewportId));
		MPCDIPolicy.Add(Result);
		return Result;
	}

	//UE_LOG(LogPicpProjectionMPCDI, Warning, TEXT("There is no implementation of '%s' projection policy for RHI %s"), *PolicyType, *RHIName);
	
	//return nullptr;
}
