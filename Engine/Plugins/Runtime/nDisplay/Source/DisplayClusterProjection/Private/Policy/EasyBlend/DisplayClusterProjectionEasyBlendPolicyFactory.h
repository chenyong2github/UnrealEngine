// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Projection/IDisplayClusterProjectionPolicyFactory.h"


/**
 * Implements projection policy factory for the 'EasyBlend' policy
 */
class FDisplayClusterProjectionEasyBlendPolicyFactory
	: public IDisplayClusterProjectionPolicyFactory
{
public:
	FDisplayClusterProjectionEasyBlendPolicyFactory();
	virtual ~FDisplayClusterProjectionEasyBlendPolicyFactory();

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterProjectionPolicyFactory
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual TSharedPtr<IDisplayClusterProjectionPolicy> Create(const FString& PolicyType, const FString& RHIName, const FString& ViewportId) override;
};
