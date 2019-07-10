// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Policy/DisplayClusterProjectionPolicyBase.h"
#include "Policy/EasyBlend/DisplayClusterProjectionEasyBlendViewAdapterBase.h"

class USceneComponent;
class FDisplayClusterProjectionEasyBlendViewportBase;


/**
 * EasyBlend projection policy
 */
class FDisplayClusterProjectionEasyBlendPolicyBase
	: public FDisplayClusterProjectionPolicyBase
{
public:
	FDisplayClusterProjectionEasyBlendPolicyBase(const FString& ViewportId);
	virtual ~FDisplayClusterProjectionEasyBlendPolicyBase();

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
	// Delegate view adapter instantiation to the RHI specific children
	virtual TSharedPtr<FDisplayClusterProjectionEasyBlendViewAdapterBase> CreateViewAdapter(const FDisplayClusterProjectionEasyBlendViewAdapterBase::FInitParams& InitParams) = 0;

private:
	// Parse EasyBlend related data from the nDisplay config file
	bool ReadConfigData(const FString& InViewportId, FString& OutFile, FString& OutOrigin, float& OutGeometryScale);

private:
	FString OriginCompId;
	float EasyBlendScale = 1.f;

	// RHI depended view adapter (different RHI require different DLL/API etc.)
	TSharedPtr<FDisplayClusterProjectionEasyBlendViewAdapterBase> ViewAdapter;
};
