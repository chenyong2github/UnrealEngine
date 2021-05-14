// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Policy/DisplayClusterProjectionPolicyBase.h"
#include "Policy/Domeprojection/DisplayClusterProjectionDomeprojectionViewAdapterBase.h"


/**
 * Domeprojection projection policy
 */
class FDisplayClusterProjectionDomeprojectionPolicyBase
	: public FDisplayClusterProjectionPolicyBase
{
public:
	FDisplayClusterProjectionDomeprojectionPolicyBase(const FString& ProjectionPolicyId, const struct FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy);

	virtual const FString GetTypeId() const
	{
		return DisplayClusterProjectionStrings::projection::Domeprojection;
	}

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterProjectionPolicy
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool HandleStartScene(class IDisplayClusterViewport* InViewport) override;
	virtual void HandleEndScene(class IDisplayClusterViewport* InViewport) override;

	virtual bool CalculateView(class IDisplayClusterViewport* InViewport, const uint32 InContextNum, FVector& InOutViewLocation, FRotator& InOutViewRotation, const FVector& ViewOffset, const float WorldToMeters, const float NCP, const float FCP) override;
	virtual bool GetProjectionMatrix(class IDisplayClusterViewport* InViewport, const uint32 InContextNum, FMatrix& OutPrjMatrix) override;

	virtual bool IsWarpBlendSupported() override;
	virtual void ApplyWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, const class IDisplayClusterViewportProxy* InViewportProxy) override;
	
	// Request additional targetable resources for domeprojection  external warpblend
	virtual bool ShouldUseAdditionalTargetableResource() const override
	{
		return true;
	}

protected:
	// Delegate view adapter instantiation to the RHI specific children
	virtual TUniquePtr<FDisplayClusterProjectionDomeprojectionViewAdapterBase> CreateViewAdapter(const FDisplayClusterProjectionDomeprojectionViewAdapterBase::FInitParams& InitParams) = 0;

private:
	// Parse Domeprojection related data from the nDisplay config file
	bool ReadConfigData(const FString& InViewportId, FString& OutFile, FString& OutOrigin, uint32& OutChannel);

private:
	FString OriginCompId;
	uint32 DomeprojectionChannel = 0;

	// RHI depended view adapter (different RHI require different DLL/API etc.)
	TUniquePtr<FDisplayClusterProjectionDomeprojectionViewAdapterBase> ViewAdapter;
};
