// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Policy/DisplayClusterProjectionPolicyBase.h"

#include "IMPCDI.h"

#include "RHI.h"
#include "RHICommandList.h"
#include "RHIResources.h"
#include "RHIUtilities.h"

class USceneComponent;


/**
 * MPCDI projection policy
 */
class FDisplayClusterProjectionMPCDIPolicy
	: public FDisplayClusterProjectionPolicyBase
{
public:
	FDisplayClusterProjectionMPCDIPolicy(const FString& ViewportId);
	virtual ~FDisplayClusterProjectionMPCDIPolicy();

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

	virtual bool IsWarpBlendSupported() override;
	virtual void ApplyWarpBlend_RenderThread(const uint32 ViewIdx, FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, const FIntRect& ViewportRect) override;

protected:	
	bool InitializeResources_RenderThread();

private:
	FString OriginCompId;
	FIntPoint ViewportSize;

	IMPCDI& MPCDIAPI;
	IMPCDI::FRegionLocator WarpRef;

	struct FViewData
	{
		IMPCDI::FFrustum Frustum;
		FTexture2DRHIRef RTTexture;
	};

	TArray<FViewData> Views;

	mutable bool bIsRenderResourcesInitialized;
	FCriticalSection RenderingResourcesInitializationCS;
};
