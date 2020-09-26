// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Device/DisplayClusterRenderViewContext.h"

class IDisplayClusterProjectionPolicy;


/**
 * Rendering viewport (sub-region of the main viewport)
 */
class FDisplayClusterRenderViewport
{
public:
	FDisplayClusterRenderViewport(
			const FString& ViewportId,
			const FIntRect& ViewportRect,
			TSharedPtr<IDisplayClusterProjectionPolicy> ProjectionPolicy,
			uint8 ContextsAmount,
			const FString& InCameraId,
			float InBufferRatio,
			bool AllowCrossGPUTransfer,
			int InGPUIndex,
			bool IsShared)
		: Id(ViewportId)
		, CameraId(InCameraId)
		, Rect(ViewportRect)
		, Policy(ProjectionPolicy)
		, BufferRatio(InBufferRatio)
		, bAllowCrossGPUTransfer(AllowCrossGPUTransfer)
		, GPUIndex(InGPUIndex)
		, bIsShared(IsShared)
	{
		check(ProjectionPolicy.IsValid());
		Contexts.AddDefaulted(ContextsAmount);
	}

	virtual ~FDisplayClusterRenderViewport()
	{ }

public:
	FString GetId() const
	{ return Id; }

	FString GetCameraId()
	{ return CameraId; }

	const FString& GetCameraId() const
	{ return CameraId; }

	void SetCameraId(const FString& InCameraId)
	{ CameraId = InCameraId; }

	TSharedPtr<IDisplayClusterProjectionPolicy>& GetProjectionPolicy()
	{ return Policy; }

	FIntRect& GetRect()
	{ return Rect; }

	const FIntRect& GetRect() const
	{ return Rect; }

	bool IsCrossGPUTransferAllowed() const
	{ return bAllowCrossGPUTransfer; }

	float GetBufferRatio() const
	{ return BufferRatio; }

	void SetBufferRatio(float Ratio)
	{ BufferRatio = Ratio; }

	int GetGPUIndex() const
	{ return GPUIndex; }

	void SetGPUIndex(int InGPUIndex)
	{ GPUIndex = InGPUIndex; }

	bool IsShared() const
	{ return bIsShared; }

	void SetShared(bool IsShared)
	{ bIsShared = IsShared; }

	FDisplayClusterRenderViewContext& GetContext(uint8 ContextNum)
	{
		check(ContextNum < Contexts.Num());
		return Contexts[ContextNum];
	}

	const FDisplayClusterRenderViewContext& GetContext(uint8 ContextNum) const
	{
		check(ContextNum < Contexts.Num());
		return Contexts[ContextNum];
	}

private:
	// Viewport ID
	FString Id;
	// Assigned camera. If empty, the currently active camera must be used
	FString CameraId;
	// 2D screen space rect for view projection
	FIntRect Rect;
	// Projection policy instance that serves this viewport
	TSharedPtr<IDisplayClusterProjectionPolicy> Policy;
	// Viewport contexts (left/center/right eyes)
	TArray<FDisplayClusterRenderViewContext> Contexts;
	// Viewport's buffer ratio
	float BufferRatio;
	// Cross GPU transfer for the viewport
	bool bAllowCrossGPUTransfer;
	// GPU index to bind the viewport
	int GPUIndex;
	// Is the viewport shared to outside via TextureShare
	bool bIsShared;
};
