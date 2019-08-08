// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Synchronization/DisplayClusterRenderSyncPolicySoftwareBase.h"

struct IDXGIOutput;


/**
 * DX11 network synchronization policy
 */
class FDisplayClusterRenderSyncPolicySoftwareDX11
	: public FDisplayClusterRenderSyncPolicySoftwareBase
{
public:
	FDisplayClusterRenderSyncPolicySoftwareDX11();
	virtual ~FDisplayClusterRenderSyncPolicySoftwareDX11();

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterRenderSyncPolicy
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool SynchronizeClusterRendering(int32& InOutSyncInterval) override;

private:
	double GetVBlankTimestamp(IDXGIOutput* const DXOutput) const;
	double GetRefreshPeriod() const;
	void PrintDwmStats(uint32 FrameNum);

private:
	bool bTimersInitialized = false;
	bool bNvApiInitialized = false;
	bool bUseAdvancedSynchronization = false;

	double VBlankBasis = 0;
	double RefreshPeriod = 0;

	uint32 FrameCounter = 0;
};
