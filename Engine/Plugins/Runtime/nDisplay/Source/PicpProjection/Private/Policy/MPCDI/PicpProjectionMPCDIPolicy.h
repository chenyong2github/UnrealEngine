// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

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
 * Adapter for the PICP MPCDI
 */
class FPicpProjectionMPCDIPolicy
	: public FPicpProjectionPolicyBase
{
public:
	enum class EWarpType : uint8
	{
		MPCDI= 0,
		Mesh
	};

	FPicpProjectionMPCDIPolicy(FPicpProjectionModule& InPicpProjectionModule, const FString& ViewportId, const TMap<FString, FString>& Parameters);
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

	void SetWarpTextureCapture(const uint32 ViewIdx, FRHITexture2D* target);
	IMPCDI::FFrustum GetWarpFrustum(const uint32 ViewIdx, bool bIsCaptureWarpTextureFrustum);

	virtual EWarpType GetWarpType() const
	{ return EWarpType::MPCDI; }

protected:
	FString OriginCompId;

	IPicpMPCDI& PicpMPCDIAPI;
	IMPCDI& MPCDIAPI;

	IMPCDI::FRegionLocator WarpRef;
	mutable FCriticalSection WarpRefCS;

	struct FViewData
	{
		IMPCDI::FFrustum Frustum;

		// Debug purpose:
		FTexture2DRHIRef ExtWarpTexture;
		IMPCDI::FFrustum ExtWarpFrustum;
		FViewData() : ExtWarpTexture(nullptr) {}
	};

	TArray<FViewData> Views;
};
