// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Policy/DisplayClusterProjectionPolicyBase.h"
#include "DisplayClusterProjectionVIOSOWarper.h"

#include "RHI.h"
#include "RHICommandList.h"
#include "RHIResources.h"

/**
 * VIOSO projection policy
 */
class FDisplayClusterProjectionVIOSOPolicy
	: public FDisplayClusterProjectionPolicyBase
{
	enum class ERenderDevice : uint8
	{
		Unsupported = 0,
		D3D11,
		D3D12
	};

public:
	FDisplayClusterProjectionVIOSOPolicy(const FString& ViewportId, const FString& RHIName, const TMap<FString, FString>& Parameters);
	virtual ~FDisplayClusterProjectionVIOSOPolicy();

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
	FViosoPolicyConfiguration ViosoConfigData;

	FIntPoint ViewportSize;
	ERenderDevice RenderDevice = ERenderDevice::Unsupported;

	struct FViewData
	{
		bool IsValid();

		bool Initialize(ERenderDevice RenderDevice, const FViosoPolicyConfiguration& InConfigData);
		void DestroyVIOSO();

		bool UpdateVIOSO(const FVector& LocalLocation, const FRotator& LocalRotator, const float WorldToMeters, const float NCP, const float FCP);
		bool RenderVIOSO_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* ShaderResourceTexture, FRHITexture2D* RenderTargetTexture, const FViosoPolicyConfiguration& InConfigData);

	protected:
		bool InitializeVIOSO(FRHITexture2D* RenderTargetTexture, const FViosoPolicyConfiguration& InConfigData);

	public:
		FVector  ViewLocation;
		FRotator ViewRotation;
		FMatrix  ProjectionMatrix;

	private:
		ERenderDevice RenderDevice = ERenderDevice::Unsupported;
		FViosoWarper Warper;

		bool bInitialized = false;
		bool bDataInitialized = false;
	};

	TArray<FViewData> Views;
	FCriticalSection DllAccessCS;
};
