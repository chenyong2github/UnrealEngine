// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Projection/IDisplayClusterProjectionPolicyFactory.h"
#include "DisplayClusterProjectionStrings.h"

/**
 * Implements projection policy factory for the 'simple' policy
 */
class FDisplayClusterProjectionSimplePolicyFactory
	: public IDisplayClusterProjectionPolicyFactory
{
public:
	FDisplayClusterProjectionSimplePolicyFactory();
	virtual ~FDisplayClusterProjectionSimplePolicyFactory();

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterProjectionPolicyFactory
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual TSharedPtr<IDisplayClusterProjectionPolicy> Create(const FString& PolicyType, const FString& RHIName, const FString& ViewportId) override;
};
