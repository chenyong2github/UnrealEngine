// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Projection/IDisplayClusterProjectionPolicyFactory.h"


/**
 * Implements projection policy factory for the 'manual' policy
 */
class FDisplayClusterProjectionManualPolicyFactory
	: public IDisplayClusterProjectionPolicyFactory
{
public:
	FDisplayClusterProjectionManualPolicyFactory();
	virtual ~FDisplayClusterProjectionManualPolicyFactory();

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterProjectionPolicyFactory
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual TSharedPtr<IDisplayClusterProjectionPolicy> Create(const FString& PolicyType, const FString& RHIName, const FString& ViewportId) override;
};
