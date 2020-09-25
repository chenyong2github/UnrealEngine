// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Synchronization/IDisplayClusterRenderSyncPolicy.h"


/**
 * Base synchronization policy
 */
class FDisplayClusterRenderSyncPolicyBase
	: public IDisplayClusterRenderSyncPolicy
{
public:
	FDisplayClusterRenderSyncPolicyBase(const TMap<FString, FString>& Parameters);
	virtual ~FDisplayClusterRenderSyncPolicyBase() = 0;

public:
	void SyncBarrierRenderThread();

	const TMap<FString, FString>& GetParameters() const
	{
		return Parameters;
	}

private:
	TMap<FString, FString> Parameters;
};
