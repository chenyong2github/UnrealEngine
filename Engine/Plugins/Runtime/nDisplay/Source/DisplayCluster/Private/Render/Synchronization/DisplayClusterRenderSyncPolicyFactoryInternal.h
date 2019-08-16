// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Synchronization/IDisplayClusterRenderSyncPolicyFactory.h"


/**
 * Factory for internal synchronization policies
 */
class FDisplayClusterRenderSyncPolicyFactoryInternal
	: public IDisplayClusterRenderSyncPolicyFactory
{
public:
	FDisplayClusterRenderSyncPolicyFactoryInternal();
	virtual ~FDisplayClusterRenderSyncPolicyFactoryInternal();

public:
	virtual TSharedPtr<IDisplayClusterRenderSyncPolicy> Create(const FString& InPolicyType, const FString& InRHIName) override;
};
