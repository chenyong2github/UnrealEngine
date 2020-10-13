// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RHI.h"
#include "RHIResources.h"
#include "RHIUtilities.h"


/**
 * Base Domeprojection view adapter
 */
class FDisplayClusterProjectionDomeprojectionViewAdapterBase
{
public:
	struct FInitParams
	{
		FIntPoint ViewportSize;
		uint32 NumViews;
	};

public:
	FDisplayClusterProjectionDomeprojectionViewAdapterBase(const FInitParams& InitializationParams)
		: InitParams(InitializationParams)
	{ }

	virtual ~FDisplayClusterProjectionDomeprojectionViewAdapterBase() = 0
	{ }

public:
	virtual bool Initialize(const FString& File) = 0;
	
	virtual void Release()
	{ }

public:
	const FIntPoint& GetViewportSize() const
	{
		return InitParams.ViewportSize;
	}

	uint32 GetNumViews() const
	{
		return InitParams.NumViews;
	}

public:
	virtual bool CalculateView(const uint32 ViewIdx, const uint32 Channel, FVector& InOutViewLocation, FRotator& InOutViewRotation, const FVector& ViewOffset, const float WorldToMeters, const float NCP, const float FCP) = 0;
	virtual bool GetProjectionMatrix(const uint32 ViewIdx, const uint32 Channel, FMatrix& OutPrjMatrix) = 0;
	virtual bool ApplyWarpBlend_RenderThread(const uint32 ViewIdx, const uint32 Channel, FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, const FIntRect& ViewportRect) = 0;

private:
	const FInitParams InitParams;
};
