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
	FDisplayClusterRenderSyncPolicyBase(const TMap<FString, FString>& InParameters)
		: Parameters(InParameters)
	{ }

	virtual ~FDisplayClusterRenderSyncPolicyBase() = default;

public:
	void SyncBarrierRenderThread();

	const TMap<FString, FString>& GetParameters() const
	{
		return Parameters;
	}

protected:
	virtual void WaitForFrameCompletion();

private:
	TMap<FString, FString> Parameters;
};
