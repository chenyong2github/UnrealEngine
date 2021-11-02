// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SceneViewExtension.h"

#include "CoreMinimal.h"
#include "UObject/GCObject.h"


class FViewport;
class FSceneViewFamily;
class FTextureShareDisplayManager;

/** 
 * View extension applying an OCIO Display Look to the viewport we're attached to
 */
class FTextureShareDisplayExtension : public FSceneViewExtensionBase
{
public:
	FTextureShareDisplayExtension(const FAutoRegister& AutoRegister, FTextureShareDisplayManager& InTextureShareDisplayManager, FViewport* AssociatedViewport);

	//~ Begin ISceneViewExtension interface	
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {};
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {};

	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override;
	virtual void PreRenderView_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView) override {};

	virtual void PreRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily) override;
	virtual void PostRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily) override;

	virtual int32 GetPriority() const override { return -1; }
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;
	//~End ISceneVIewExtension interface

public:
	/** Returns the viewport this extension is currently attached to */
	FViewport* GetAssociatedViewport() { return LinkedViewport; }

private:
	FTextureShareDisplayManager& TextureShareDisplayManager;

	/** Viewport to which we are attached */
	FViewport* LinkedViewport = nullptr;
};


