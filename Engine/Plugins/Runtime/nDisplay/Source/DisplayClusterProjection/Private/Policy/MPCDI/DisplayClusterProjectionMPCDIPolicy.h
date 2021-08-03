// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Policy/DisplayClusterProjectionPolicyBase.h"

#include "WarpBlend/IDisplayClusterWarpBlend.h"
#include "WarpBlend/DisplayClusterWarpContext.h"

/**
 * MPCDI projection policy
 */
class FDisplayClusterProjectionMPCDIPolicy
	: public FDisplayClusterProjectionPolicyBase
{
public:
	enum class EWarpType : uint8
	{
		mpcdi = 0,
		mesh
	};

public:
	FDisplayClusterProjectionMPCDIPolicy(const FString& ProjectionPolicyId, const struct FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy);
	virtual ~FDisplayClusterProjectionMPCDIPolicy();

	virtual const FString GetTypeId() const
	{ return DisplayClusterProjectionStrings::projection::MPCDI; }

public:
	virtual EWarpType GetWarpType() const
	{
		return EWarpType::mpcdi;
	}

	// This policy can support ICVFX rendering
	virtual bool ShouldSupportICVFX() const
	{
		return true;
	}

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

	virtual bool ShouldUseSourceTextureWithMips() const override
	{
		// Support input texture with mips
		return true;
	}

	virtual void UpdateProxyData(class IDisplayClusterViewport* InViewport) override;

protected:
	bool CreateWarpBlendFromConfig();
	void ImplRelease();

protected:
	FString OriginCompId;
	
	TSharedPtr<IDisplayClusterWarpBlend, ESPMode::ThreadSafe> WarpBlendInterface;
	TArray<FDisplayClusterWarpContext> WarpBlendContexts;

	TSharedPtr<IDisplayClusterWarpBlend, ESPMode::ThreadSafe> WarpBlendInterface_Proxy;
	TArray<FDisplayClusterWarpContext> WarpBlendContexts_Proxy;

private:
	bool bInvalidConfiguration = false;
	bool bIsPreviewMeshEnabled = false;

#if WITH_EDITOR
protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterProjectionPolicyPreview
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool HasPreviewMesh() override
	{
		return true;
	}
	virtual class UMeshComponent* GetOrCreatePreviewMeshComponent(class IDisplayClusterViewport* InViewport, bool& bOutIsRootActorComponent) override;

	void ReleasePreviewMeshComponent();

private:
	FDisplayClusterSceneComponentRef PreviewMeshComponentRef;
#endif
};
