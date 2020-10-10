// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Policy/DisplayClusterProjectionPolicyBase.h"
#include "Policy/Domeprojection/DisplayClusterProjectionDomeprojectionViewAdapterBase.h"


/**
 * Domeprojection projection policy
 */
class FDisplayClusterProjectionDomeprojectionPolicyBase
	: public FDisplayClusterProjectionPolicyBase
{
public:
	FDisplayClusterProjectionDomeprojectionPolicyBase(const FString& ViewportId, const TMap<FString, FString>& Parameters);

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
	virtual TUniquePtr<FDisplayClusterProjectionDomeprojectionViewAdapterBase> CreateViewAdapter(const FDisplayClusterProjectionDomeprojectionViewAdapterBase::FInitParams& InitParams) = 0;

private:
	// Parse Domeprojection related data from the nDisplay config file
	bool ReadConfigData(const FString& InViewportId, FString& OutFile, FString& OutOrigin, uint32& OutChannel);

private:
	FString OriginCompId;
	uint32 DomeprojectionChannel = 0;

	// RHI depended view adapter (different RHI require different DLL/API etc.)
	TUniquePtr<FDisplayClusterProjectionDomeprojectionViewAdapterBase> ViewAdapter;
};
