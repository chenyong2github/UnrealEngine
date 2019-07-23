// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
		: FDisplayClusterRenderViewport(ViewportId, ViewportArea, ProjectionPolicy, ContextsAmount, InCameraId, false)
	{
	}

	FDisplayClusterRenderViewport(const FString& ViewportId, const FIntRect& ViewportArea, TSharedPtr<IDisplayClusterProjectionPolicy> ProjectionPolicy, uint8 ContextsAmount, const FString& InCameraId, bool IsRTT)
		: Id(ViewportId)
		, CameraId(InCameraId)
		, Area(ViewportArea)
		, Policy(ProjectionPolicy)
		, bRTT(IsRTT)
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

	FIntRect& GetArea()
	{ return Area; }

	const FIntRect& GetArea() const
	{ return Area; }

	bool IsRTT() const
	{ return bRTT; }

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
};
