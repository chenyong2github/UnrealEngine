// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Projection/IDisplayClusterProjectionPolicyFactory.h"
#include "Policy/DisplayClusterProjectionPolicyBase.h"


/**
 * Implements projection policy factory for the 'mpcdi' policy
 */
class FDisplayClusterProjectionMPCDIPolicyFactory
	: public IDisplayClusterProjectionPolicyFactory
{
public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterProjectionPolicyFactory
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual TSharedPtr<IDisplayClusterProjectionPolicy> Create(const FString& PolicyType, const FString& RHIName, const FString& ViewportId, const TMap<FString, FString>& Parameters) override;

	TArray<TSharedPtr<FDisplayClusterProjectionPolicyBase>> GetPolicy();
	TSharedPtr<FDisplayClusterProjectionPolicyBase>         GetPolicyByViewport(const FString& ViewportId);

private:
	TArray<TSharedPtr<FDisplayClusterProjectionPolicyBase>> Policy;
};
