// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Projection/IDisplayClusterProjectionPolicy.h"
#include "IPicpProjection.h"
#include "Overlay/PicpProjectionOverlayRender.h"
#include "Misc/DisplayClusterObjectRef.h"
#include "PicpProjectionModule.h"

/**
 * Base class for nDisplay projection policies
 */
class FPicpProjectionPolicyBase
	: public IDisplayClusterProjectionPolicy
{
public:
	FPicpProjectionPolicyBase(FPicpProjectionModule& InPicpProjectionModule, const FString& ViewportId, const TMap<FString, FString>& InParameters);
	virtual ~FPicpProjectionPolicyBase() = 0;

	virtual void BeginWarpBlend_RenderThread(const uint32 ViewIdx, FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, const FIntRect& ViewportRect) override;
	virtual void EndWarpBlend_RenderThread(const uint32 ViewIdx, FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, const FIntRect& ViewportRect) override;

public:
	const FString& GetViewportId() const
	{
		return PolicyViewportId;
	}

	const TMap<FString, FString>& GetParameters() const
	{
		return Parameters;
	}

	void UpdateOverlayViewportData(FPicpProjectionOverlayFrameData& OverlayFrameData);
	void SetOverlayData_RenderThread(const FPicpProjectionOverlayViewportData* Source);
	void GetOverlayData_RenderThread(FPicpProjectionOverlayViewportData& Output);

	/** Update structure OutViewportOverlayData. Assign ref to camera textures
	* Valid inside BeginWarpBlend_RenderThread/EndWarpBlend_RenderThread scope
	*/
	void AssignStageCamerasTextures_RenderThread(FPicpProjectionOverlayViewportData& InOutViewportOverlayData);

protected:
	void InitializeOriginComponent(const FString& OriginCopmId);
	void ReleaseOriginComponent();

	const USceneComponent* const GetOriginComp() const
	{
		return PolicyOriginComponentRef.GetOrFindSceneComponent();
	}

	void SetViewportSize(const FIntPoint& InViewportSize);
	FIntPoint GetViewportSize() const
	{ 
		return ViewportSize; 
	}

private:
	FIntPoint ViewportSize;

	// Added 'Policy' prefix to avoid "... hides class name ..." warnings in child classes
	FString PolicyViewportId;
	FString PolicyOriginCompId;
	TMap<FString, FString> Parameters;
	FDisplayClusterSceneComponentRef PolicyOriginComponentRef;

	FPicpProjectionOverlayViewportData LocalOverlayViewportData;
	mutable FCriticalSection LocalOverlayViewportDataCS;

	FPicpProjectionModule& PicpProjectionModule;
};
