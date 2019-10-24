// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Projection/IDisplayClusterProjectionPolicyFactory.h"


/**
 * Implements projection policy factory for the 'camera' policy
 */
class FDisplayClusterProjectionCameraPolicyFactory
	: public IDisplayClusterProjectionPolicyFactory
{
public:
	FDisplayClusterProjectionCameraPolicyFactory();
	virtual ~FDisplayClusterProjectionCameraPolicyFactory();

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterProjectionPolicyFactory
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual TSharedPtr<IDisplayClusterProjectionPolicy> Create(const FString& PolicyType, const FString& RHIName, const FString& ViewportId) override;

public:
	TSharedPtr<IDisplayClusterProjectionPolicy> GetPolicyInstance(const FString& ViewportId);

private:
	TMap<FString, TSharedPtr<IDisplayClusterProjectionPolicy>> PolicyInstances;
};
