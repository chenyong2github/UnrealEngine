// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Policy/DisplayClusterProjectionPolicyBase.h"

class FDisplayClusterProjectionLinkPolicy
	: public FDisplayClusterProjectionPolicyBase
{
public:
	FDisplayClusterProjectionLinkPolicy(const FString& ProjectionPolicyId, const struct FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy);
	virtual ~FDisplayClusterProjectionLinkPolicy();

	virtual const FString GetTypeId() const
	{ return DisplayClusterProjectionStrings::projection::Link; }

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterProjectionPolicy
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool HandleStartScene(class IDisplayClusterViewport* InViewport) override
	{
		// no resources
		return true;
	}

	virtual void HandleEndScene(class IDisplayClusterViewport* InViewport) override
	{ }

	// Return values from linked parent viewport
	// Important note: before doing this, the parent viewports must be updated.
	virtual bool CalculateView(class IDisplayClusterViewport* InViewport, const uint32 InContextNum, FVector& InOutViewLocation, FRotator& InOutViewRotation, const FVector& ViewOffset, const float WorldToMeters, const float NCP, const float FCP) override;
	virtual bool GetProjectionMatrix(class IDisplayClusterViewport* InViewport, const uint32 InContextNum, FMatrix& OutPrjMatrix) override;

	virtual bool IsWarpBlendSupported() override
	{
		return false;
	}

	virtual bool ShouldUseSourceTextureWithMips() const override
	{ return true; }
};
