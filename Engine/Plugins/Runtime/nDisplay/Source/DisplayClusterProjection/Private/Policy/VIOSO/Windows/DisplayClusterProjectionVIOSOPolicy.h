// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Policy/DisplayClusterProjectionPolicyBase.h"
#include "Policy/VIOSO/Windows/DisplayClusterProjectionVIOSOWarper.h"

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
	FDisplayClusterProjectionVIOSOPolicy(const FString& ProjectionPolicyId, const struct FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy);
	virtual ~FDisplayClusterProjectionVIOSOPolicy();

	virtual const FString GetTypeId() const
	{ return DisplayClusterProjectionStrings::projection::VIOSO; }

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
	{ return true; }

	virtual bool ShouldUseSourceTextureWithMips() const override
	{ return true; }

protected:
	bool ImplApplyWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, const class IDisplayClusterViewportProxy* InViewportProxy);
	void ImplRelease();

protected:
	FViosoPolicyConfiguration ViosoConfigData;

	//FIntPoint ViewportSize;
	ERenderDevice RenderDevice = ERenderDevice::Unsupported;

	struct FViewData
	{
		bool IsValid();

		bool Initialize(ERenderDevice RenderDevice, const FViosoPolicyConfiguration& InConfigData);
		void DestroyVIOSO();

		bool UpdateVIOSO(class IDisplayClusterViewport* InViewport, const uint32 InContextNum, const FVector& LocalLocation, const FRotator& LocalRotator, const float WorldToMeters, const float NCP, const float FCP);
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
