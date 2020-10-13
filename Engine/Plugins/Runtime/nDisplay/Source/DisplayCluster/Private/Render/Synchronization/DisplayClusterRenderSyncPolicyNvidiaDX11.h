// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Synchronization/DisplayClusterRenderSyncPolicyNvidiaBase.h"


/**
 * DX11 NVIDIA SwapLock synchronization policy
 */
class FDisplayClusterRenderSyncPolicyNvidiaDX11
	: public FDisplayClusterRenderSyncPolicyNvidiaBase
{
public:
	FDisplayClusterRenderSyncPolicyNvidiaDX11(const TMap<FString, FString>& Parameters);
	virtual ~FDisplayClusterRenderSyncPolicyNvidiaDX11();
};
