// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once


#include "Policy/DisplayClusterProjectionPolicyBase.h"
#include "Policy/PicpProjectionPolicyBase.h"

#include "IMPCDI.h"
#include "IPicpMPCDI.h"

#include "RHI.h"
#include "RHICommandList.h"
#include "RHIResources.h"
#include "RHIUtilities.h"


class FPicpProjectionViewportBase;
class USceneComponent;


/**
 * Adapter for the MPCDI module
 */
class FPicpProjectionMPCDIPolicy
	: public FPicpProjectionPolicyBase
{
public:
	FPicpProjectionMPCDIPolicy(const FString& ViewportId);
	virtual ~FPicpProjectionMPCDIPolicy();

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

	void UpdateOverlayViewportData(FPicpProjectionOverlayFrameData& OverlayFrameData);
	void SetOverlayData_RenderThread(const FPicpProjectionOverlayViewportData* Source);

	void SetWarpTextureCapture(const uint32 ViewIdx, FRHITexture2D* target);
	IMPCDI::FFrustum GetWarpFrustum(const uint32 ViewIdx, bool bIsCaptureWarpTextureFrustum);

protected:	
	bool InitializeResources_RenderThread();	

private:
	FString OriginCompId;
	FIntPoint ViewportSize;

	IPicpMPCDI& PicpMPCDIAPI;

	IMPCDI& MPCDIAPI;
	IMPCDI::FRegionLocator WarpRef;

	FPicpProjectionOverlayViewportData OverlayViewportData;

	struct FViewData
	{
		IMPCDI::FFrustum Frustum;
		FTexture2DRHIRef RTTexture;

		// Debug purpose:
		FTexture2DRHIRef ExtWarpTexture;
		IMPCDI::FFrustum ExtWarpFrustum;
		FViewData() : RTTexture(nullptr), ExtWarpTexture(nullptr) {}
	};

	TArray<FViewData> Views;

	mutable bool bIsRenderResourcesInitialized = false;
	mutable FCriticalSection RenderingResourcesInitializationCS;
};
