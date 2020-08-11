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
	FDisplayClusterRenderViewport(const FString& ViewportId, const FIntRect& ViewportArea, TSharedPtr<IDisplayClusterProjectionPolicy> ProjectionPolicy, uint8 ContextsAmount, const FString& InCameraId)
		: FDisplayClusterRenderViewport(ViewportId, ViewportArea, ProjectionPolicy, ContextsAmount, InCameraId, 1.f, true, -1, false, false)
	{
	}

	FDisplayClusterRenderViewport(const FString& ViewportId, const FIntRect& ViewportArea, TSharedPtr<IDisplayClusterProjectionPolicy> ProjectionPolicy, uint8 ContextsAmount, const FString& InCameraId, float InBufferRatio, bool InbAllowCrossGPUTransfer, int InGPUIndex, bool InbIsRTT, bool InbIsShared)
		: Id(ViewportId)
		, CameraId(InCameraId)
		, Area(ViewportArea)
		, Policy(ProjectionPolicy)
		, bRTT(InbIsRTT)
		, bAllowCrossGPUTransfer(InbAllowCrossGPUTransfer)
	{
		check(ProjectionPolicy.IsValid());
		
		Contexts.AddDefaulted(ContextsAmount);
		
		SetBufferRatio(InBufferRatio);
		SetGPUIndex(InGPUIndex);
		SetShared(InbIsShared);
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

	FIntRect& GetArea()
	{ return Area; }

	const FIntRect& GetArea() const
	{ return Area; }

	bool IsRTT() const
	{ return bRTT; }

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
	{ return bShared; }

	void SetShared(bool IsShared)
	{ bShared = IsShared; }

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
	// 2D screen space area for view projection
	FIntRect Area;
	// Projection policy instance that serves this viewport
	TSharedPtr<IDisplayClusterProjectionPolicy> Policy;
	// Viewport contexts (left/center/right eyes)
	TArray<FDisplayClusterRenderViewContext> Contexts;
	// Is RTT viewport
	bool bRTT;
	// Viewport's buffer ratio
	float BufferRatio;
	// override GPUIndex
	int GPUIndex;
	// forward this UE4 Core flag for view
	bool bAllowCrossGPUTransfer;
	// allow viewport share to ext app
	bool bShared;
};
