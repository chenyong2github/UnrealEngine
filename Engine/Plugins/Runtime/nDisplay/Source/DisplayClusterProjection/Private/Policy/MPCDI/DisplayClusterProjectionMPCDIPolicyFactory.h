// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Projection/IDisplayClusterProjectionPolicyFactory.h"


/**
 * Implements projection policy factory for the 'mpcdi' policy
 */
class FDisplayClusterProjectionMPCDIPolicyFactory
	: public IDisplayClusterProjectionPolicyFactory
{
public:
	FDisplayClusterProjectionMPCDIPolicyFactory();
	virtual ~FDisplayClusterProjectionMPCDIPolicyFactory();

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterProjectionPolicyFactory
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual TSharedPtr<IDisplayClusterProjectionPolicy> Create(const FString& PolicyType, const FString& RHIName, const FString& ViewportId) override;
};
