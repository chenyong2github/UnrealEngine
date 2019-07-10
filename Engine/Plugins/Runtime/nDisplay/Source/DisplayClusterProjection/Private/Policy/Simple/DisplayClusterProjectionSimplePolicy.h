// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Policy/DisplayClusterProjectionPolicyBase.h"
#include "Config/DisplayClusterConfigTypes.h"

class UDisplayClusterScreenComponent;


/**
 * Implements math behind the native (simple) quad based projections
 */
class FDisplayClusterProjectionSimplePolicy
	: public FDisplayClusterProjectionPolicyBase
{
public:
	FDisplayClusterProjectionSimplePolicy(const FString& ViewportId);
	virtual ~FDisplayClusterProjectionSimplePolicy();

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterProjectionPolicy
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void StartScene(UWorld* World) override;
	virtual void EndScene() override;
	virtual bool HandleAddViewport(const FIntPoint& ViewportSize, const uint32 ViewsAmount) override;
	virtual void HandleRemoveViewport() override;

	virtual bool CalculateView(const uint32 ViewIdx, FVector& InOutViewLocation, FRotator& InOutViewRotation, const FVector& ViewOffset, const float WorldToMeters, const float NCP, const float FCP) override;
	virtual bool GetProjectionMatrix(const uint32 ViewIdx, FMatrix& OutPrjMatrix) override;

	virtual bool IsWarpBlendSupported() override
	{ return false; }

protected:
	void InitializeMeshData();
	void ReleaseMeshData();

protected:
	// Custom projection screen geometry (hw - half-width, hh - half-height of projection screen)
	// Left bottom corner (from camera point view)
	virtual FVector GetProjectionScreenGeometryLBC(const float& hw, const float& hh) const
	{ return FVector(0.f, -hw, -hh);}
	
	// Right bottom corner (from camera point view)
	virtual FVector GetProjectionScreenGeometryRBC(const float& hw, const float& hh) const
	{ return FVector(0.f, hw, -hh);}

	// Left top corner (from camera point view)
	virtual FVector GetProjectionScreenGeometryLTC(const float& hw, const float& hh) const
	{ return FVector(0.f, -hw, hh);}

private:
	// Screen ID taken from the nDisplay config file
	FDisplayClusterConfigScreen CfgScreen;
	// Screen component
	UDisplayClusterScreenComponent* ScreenComp = nullptr;

	struct FViewData
	{
		FVector  ViewLoc;
		float    NCP;
		float    FCP;
		float    WorldToMeters;
	};

	TArray<FViewData> ViewData;
};
