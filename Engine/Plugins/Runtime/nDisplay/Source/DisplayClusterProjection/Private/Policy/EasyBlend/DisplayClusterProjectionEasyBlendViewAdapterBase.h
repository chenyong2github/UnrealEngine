// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RHI.h"
#include "RHIResources.h"
#include "RHIUtilities.h"


/**
 * Base EasyBlend view adapter
 */
class FDisplayClusterProjectionEasyBlendViewAdapterBase
{
public:
	struct FInitParams
	{
		uint32 NumViews;
	};

public:
	FDisplayClusterProjectionEasyBlendViewAdapterBase(const FInitParams& InitializationParams)
		: InitParams(InitializationParams)
	{ }

	virtual ~FDisplayClusterProjectionEasyBlendViewAdapterBase() = default;

public:
	virtual bool Initialize(const FString& File) = 0;
	
	virtual void Release()
	{ }

public:
	uint32 GetNumViews() const
	{
		return InitParams.NumViews;
	}

public:
	virtual bool CalculateView(class IDisplayClusterViewport* InViewport, const uint32 InContextNum, FVector& InOutViewLocation, FRotator& InOutViewRotation, const FVector& ViewOffset, const float WorldToMeters, const float NCP, const float FCP) = 0;
	virtual bool GetProjectionMatrix(class IDisplayClusterViewport* InViewport, const uint32 InContextNum, FMatrix& OutPrjMatrix) = 0;
	virtual bool ApplyWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, const class IDisplayClusterViewportProxy* InViewportProxy) = 0;

private:
	const FInitParams InitParams;
};
