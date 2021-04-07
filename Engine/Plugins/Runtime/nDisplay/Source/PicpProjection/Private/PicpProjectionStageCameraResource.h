// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#include "RendererInterface.h"
#include "RHI.h"
#include "RHICommandList.h"
#include "RHIResources.h"

class FPicpProjectionStageCameraResource
{
public:
	struct FCameraData
	{
		/** Source texture viewport name */
		FString ViewportId;
		/** Source texture viewport rect */
		FIntRect ViewportRect;

		EPixelFormat Format;

		bool   bSRGB = false;
		uint32 NumMips = 1;
		FRHITexture* CustomCameraTexture = nullptr;
	};

	void InitializeStageCameraRTT(FRHICommandListImmediate& RHICmdList, const FCameraData& InCameraData);

	FRHITexture2D* GetStageCameraTexture()
	{
		return CameraRenderTarget.IsValid() ? CameraRenderTarget.GetReference() : nullptr;
	}

	bool UpdateStageCameraRTT(FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture);
	void DiscardStageCameraRTT();

protected:
	void ResampleCopyTextureImpl_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture* SrcTexture, FRHITexture* DstTexture, const FIntRect* SrcTextureRect, const FIntRect* DstTextureRect);

private:
	FCameraData                 CameraData;
	TRefCountPtr<FRHITexture2D> CameraRenderTarget;


	/** cached params etc. for use with mip generator */
	TRefCountPtr<IPooledRenderTarget> MipGenerationCache;
	uint32 CurrentNumMips = 1;

	bool bResourceUpdated = false;
};
