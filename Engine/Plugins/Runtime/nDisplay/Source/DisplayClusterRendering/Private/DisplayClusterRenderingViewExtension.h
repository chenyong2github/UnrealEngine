// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"

class FSceneViewFamily;
class FTextureRenderTargetResource;
class IDisplayClusterProjectionPolicy;


/** 
 * Custom DisplayCluster view extension
 */
class FDisplayClusterRenderingViewExtension
	: public FSceneViewExtensionBase
{
public:
	FDisplayClusterRenderingViewExtension(const FAutoRegister& AutoRegister, FTextureRenderTargetResource* InLinkedRTT, IDisplayClusterProjectionPolicy* InProjectionPolicy);

public:
	//~Begin ISceneViewExtension interface
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override
	{ }

	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override
	{ }

	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override
	{ }

	virtual void PreRenderView_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView) override
	{ }

	virtual void PreRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily) override
	{ }

	virtual void PostRenderView_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView) override
	{ }

	virtual void PostRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily) override;

	virtual int32 GetPriority() const override
	{
		return -1;
	}

	virtual bool IsActiveThisFrame(class FViewport* InViewport) const override
	{
		return false;
	}
	//~End ISceneVIewExtension interface

public:
	FTextureRenderTargetResource* GetAssociatedRTT()  const
	{
		return LinkedRTT;
	}

	IDisplayClusterProjectionPolicy* GetAssociatedProjection() const
	{
		return ProjectionPolicy;
	}

private:
	/** Render target */
	FTextureRenderTargetResource* LinkedRTT = nullptr;
	/** Projection policy interface */
	IDisplayClusterProjectionPolicy* ProjectionPolicy = nullptr;
};
