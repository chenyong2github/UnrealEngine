// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Policy/EasyBlend/DisplayClusterProjectionEasyBlendViewAdapterBase.h"

#include "EasyBlendSDKFrustum.h"
#include "EasyBlendSDKDXStructs.h"


class FDisplayClusterProjectionEasyBlendViewAdapterDX11
	: public FDisplayClusterProjectionEasyBlendViewAdapterBase
{
public:
	FDisplayClusterProjectionEasyBlendViewAdapterDX11(const FDisplayClusterProjectionEasyBlendViewAdapterBase::FInitParams& InitParams);
	virtual ~FDisplayClusterProjectionEasyBlendViewAdapterDX11();

public:
	virtual bool Initialize(const FString& File) override;

public:
	virtual bool CalculateView(const uint32 ViewIdx, FVector& InOutViewLocation, FRotator& InOutViewRotation, const FVector& ViewOffset, const float WorldToMeters, const float NCP, const float FCP) override;
	virtual bool GetProjectionMatrix(const uint32 ViewIdx, FMatrix& OutPrjMatrix) override;
	virtual bool ApplyWarpBlend_RenderThread(const uint32 ViewIdx, FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, const FIntRect& ViewportRect) override;

private:
	bool InitializeResources_RenderThread();
	void LoadViewportTexture_RenderThread(const uint32 ViewIdx, FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, const FIntRect& ViewportRect);
	void SaveViewportTexture_RenderThread(const uint32 ViewIdx, FRHICommandListImmediate& RHICmdList, FRHITexture2D* DstTexture, const FIntRect& ViewportRect);

private:
	float ZNear;
	float ZFar;

	struct ViewData
	{
		TUniquePtr<EasyBlendSDKDX_Mesh> EasyBlendMeshData;

		FTexture2DRHIRef TargetableTexture;
		FTexture2DRHIRef ShaderResourceTexture;

		bool bIsMeshInitialized = false;
		
		FVector EyeLocation = FVector::ZeroVector;

		ViewData()
		{
			EasyBlendMeshData.Reset(new EasyBlendSDKDX_Mesh);
		}

		~ViewData() = default;
	};

	TArray<ViewData> Views;

	bool bIsRenderResourcesInitialized = false;
	FCriticalSection RenderingResourcesInitializationCS;
	FCriticalSection DllAccessCS;
};
