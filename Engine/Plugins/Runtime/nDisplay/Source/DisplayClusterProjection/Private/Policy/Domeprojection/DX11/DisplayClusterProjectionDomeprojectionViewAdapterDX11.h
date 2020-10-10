// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Policy/Domeprojection/DisplayClusterProjectionDomeprojectionViewAdapterBase.h"

THIRD_PARTY_INCLUDES_START
#include "dpTypes.h"
THIRD_PARTY_INCLUDES_END


class FDisplayClusterProjectionDomeprojectionViewAdapterDX11
	: public FDisplayClusterProjectionDomeprojectionViewAdapterBase
{
public:
	FDisplayClusterProjectionDomeprojectionViewAdapterDX11(const FDisplayClusterProjectionDomeprojectionViewAdapterBase::FInitParams& InitParams);
	virtual ~FDisplayClusterProjectionDomeprojectionViewAdapterDX11();

public:
	virtual bool Initialize(const FString& File) override;

public:
	virtual bool CalculateView(const uint32 ViewIdx, const uint32 Channel, FVector& InOutViewLocation, FRotator& InOutViewRotation, const FVector& ViewOffset, const float WorldToMeters, const float NCP, const float FCP) override;
	virtual bool GetProjectionMatrix(const uint32 ViewIdx, const uint32 Channel, FMatrix& OutPrjMatrix) override;
	virtual bool ApplyWarpBlend_RenderThread(const uint32 ViewIdx, const uint32 Channel, FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, const FIntRect& ViewportRect) override;

private:
	bool InitializeResources_RenderThread();
	void LoadViewportTexture_RenderThread(const uint32 ViewIdx, FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, const FIntRect& ViewportRect);
	void SaveViewportTexture_RenderThread(const uint32 ViewIdx, FRHICommandListImmediate& RHICmdList, FRHITexture2D* DstTexture, const FIntRect& ViewportRect);

private:
	float ZNear;
	float ZFar;

	struct ViewData
	{
		ViewData() = default;
		~ViewData() = default;

		FTexture2DRHIRef TargetableTexture;
		FTexture2DRHIRef ShaderResourceTexture;

		FIntPoint ViewportSize;
		dpCamera Camera;
	};

	TArray<ViewData> Views;

	bool bIsRenderResourcesInitialized = false;
	FCriticalSection RenderingResourcesInitializationCS;
	FCriticalSection DllAccessCS;

	dpContext* Context;
};
