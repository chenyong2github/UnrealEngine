// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Synchronization/DisplayClusterRenderSyncPolicyBase.h"

class  FRHIViewport;
struct IDXGIOutput;
struct IDXGISwapChain;


/**
 * Base network synchronization policy (soft sync)
 */
class FDisplayClusterRenderSyncPolicySoftwareBase
	: public FDisplayClusterRenderSyncPolicyBase
{
public:
	FDisplayClusterRenderSyncPolicySoftwareBase(const TMap<FString, FString>& Parameters);

	virtual ~FDisplayClusterRenderSyncPolicySoftwareBase()
	{ }

protected:
	virtual bool SynchronizeClusterRendering(int32& InOutSyncInterval) override;

protected:
	virtual void Procedure_SynchronizePresent();
	virtual void Step_InitializeFrameSynchronization();
	virtual void Step_WaitForFrameCompletion();
	virtual void Step_WaitForEthernetBarrierSignal_1();
	virtual void Step_SkipPresentationOnClosestVblank();
	virtual void Step_WaitForEthernetBarrierSignal_2();
	virtual void Step_Present();
	virtual void Step_FinalizeFrameSynchronization();

	virtual void DoSleep(float Seconds);

protected:
	double GetVBlankTimestamp() const;
	double GetRefreshPeriod() const;
	void PrintDwmStats(uint32 FrameNum);

protected:
	// Common objects
	FRHIViewport*   Viewport  = nullptr;
	IDXGISwapChain* SwapChain = nullptr;
	IDXGIOutput*    DXOutput  = nullptr;

	// Sync math
	double B1B = 0.f;  // Barrier 1 before
	double B1A = 0.f;  // Barrier 1 after
	double TToB = 0.f; // Time to VBlank
	double SB = 0.f;   // Sleep before
	double SA = 0.f;   // Sleep after
	double B2B = 0.f;  // Barrier 2 before
	double B2A = 0.f;  // Barrier 2 after
	double PB = 0.f;   // Present before
	double PA = 0.f;   // Present after

	// Sync logic (cvars)
	const bool  bSimpleSync;
	const bool  bUseCustomRefreshRate;
	float VBlankFrontEdgeThreshold = 0.f;
	float VBlankBackEdgeThreshold = 0.f;
	float VBlankThresholdSleepMultiplier = 0.f;
	const bool  VBlankBasisUpdate;
	float VBlankBasisUpdatePeriod = 0.f;

	// Sync internals
	int SyncIntervalToUse = 1;
	bool bInternalsInitialized = false;
	double VBlankBasis = 0;
	double RefreshPeriod = 0;
	uint32 FrameCounter = 0;
};
