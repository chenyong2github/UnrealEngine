// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Policy/DisplayClusterProjectionPolicyBase.h"
#include "Policy/EasyBlend/DisplayClusterProjectionEasyBlendViewAdapterBase.h"


/**
 * EasyBlend projection policy
 */
class FDisplayClusterProjectionEasyBlendPolicyBase
	: public FDisplayClusterProjectionPolicyBase
{
public:
	FDisplayClusterProjectionEasyBlendPolicyBase(const FString& ProjectionPolicyId, const struct FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy);

	virtual const FString GetTypeId() const
	{ return DisplayClusterProjectionStrings::projection::EasyBlend; }

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterProjectionPolicy
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool HandleStartScene(class IDisplayClusterViewport* InViewport) override;
	virtual void HandleEndScene(class IDisplayClusterViewport* InViewport) override;

	virtual bool CalculateView(class IDisplayClusterViewport* InViewport, const uint32 InContextNum, FVector& InOutViewLocation, FRotator& InOutViewRotation, const FVector& ViewOffset, const float WorldToMeters, const float NCP, const float FCP) override;
	virtual bool GetProjectionMatrix(class IDisplayClusterViewport* InViewport, const uint32 InContextNum, FMatrix& OutPrjMatrix) override;

	virtual bool IsWarpBlendSupported() override
	{ return true; }
	virtual void ApplyWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, const class IDisplayClusterViewportProxy* InViewportProxy) override;

	// Request additional targetable resources for easyblend external warpblend
	virtual bool ShouldUseAdditionalTargetableResource() const override
	{ return true; }

	virtual bool IsEasyBlendRenderingEnabled() = 0;

protected:
	// Delegate view adapter instantiation to the RHI specific children
	virtual TUniquePtr<FDisplayClusterProjectionEasyBlendViewAdapterBase> CreateViewAdapter(const FDisplayClusterProjectionEasyBlendViewAdapterBase::FInitParams& InitParams) = 0;

private:
	// Parse EasyBlend related data from the nDisplay config file
	bool ReadConfigData(const FString& InViewportId, FString& OutFile, FString& OutOrigin, float& OutGeometryScale);

private:
	FString OriginCompId;
	float EasyBlendScale = 1.f;
	bool bInitializeOnce = false;

	// RHI depended view adapter (different RHI require different DLL/API etc.)
	TUniquePtr<FDisplayClusterProjectionEasyBlendViewAdapterBase> ViewAdapter;
};
