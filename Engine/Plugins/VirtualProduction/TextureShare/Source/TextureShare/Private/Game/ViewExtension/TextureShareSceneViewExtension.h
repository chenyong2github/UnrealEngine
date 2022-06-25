// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "SceneViewExtension.h"
#include "Containers/TextureShareCoreEnums.h"

class ITextureShareObjectProxy;
struct FTextureShareCoreViewDesc;

/**
 * Convenience type definition of a function that gives an opinion of whether the scene view extension should be active in the given context for the current frame.
 */
using TFunctionTextureShareViewExtension = TFunction<void(FRHICommandListImmediate& RHICmdList, class FTextureShareSceneViewExtension& InViewExtension)>;
using TFunctionTextureShareOnBackBufferReadyToPresent = TFunction<void(FRHICommandListImmediate& RHICmdList, class FTextureShareSceneViewExtension& InViewExtension, const FTexture2DRHIRef& InBackbuffer)>;

/**
 * A view extension to handle a multi-threaded renderer for a TextureShare object.
 */
class TEXTURESHARE_API FTextureShareSceneViewExtension : public FSceneViewExtensionBase
{
private:
	// A quick and dirty way to determine which TS ViewExtension (sub)class this is. Every subclass should implement it.
	virtual FName GetRTTI() const { return TEXT("FTextureShareSceneViewExtension"); }

public:
	FTextureShareSceneViewExtension(const FAutoRegister&, const TSharedRef<ITextureShareObjectProxy, ESPMode::ThreadSafe>& InObjectProxy, FViewport* InLinkedViewport);
	virtual ~FTextureShareSceneViewExtension();

public:
	//~ Begin ISceneViewExtension interface
	virtual int32 GetPriority() const override { return -1; }

	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {};

	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override;
	virtual void PreRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily) override;
	virtual void PostRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily) override;

	virtual void OnResolvedSceneColor_RenderThread(FRDGBuilder& GraphBuilder, const FSceneTextures& SceneTextures);
	virtual void OnBackBufferReadyToPresent_RenderThread(SWindow&, const FTexture2DRHIRef&);

	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;
	//~End ISceneVIewExtension interface

public:
	virtual void GetSceneViewData_RenderThread(const FSceneView& InSceneView, const FTextureShareCoreViewDesc& InViewDesc);
	virtual void GetTextureShareCoreSceneViewData_RenderThread(const FSceneViewFamily& InViewFamily, const FSceneView& InSceneView, struct FTextureShareCoreSceneViewData& OutData) const;
	virtual void ShareSceneViewColors_RenderThread(FRDGBuilder& GraphBuilder, const FSceneTextures& SceneTextures, const FSceneView& InSceneView, const FTextureShareCoreViewDesc& InViewDesc);

public:
	// Returns true if the given object is of the same type.
	bool IsA(const FTextureShareSceneViewExtension&& Other) const
	{
		return GetRTTI() == Other.GetRTTI();
	}

	const TSharedRef<ITextureShareObjectProxy, ESPMode::ThreadSafe>& GetObjectProxy() const
	{
		return ObjectProxy;
	}
	
	FSceneViewFamily* GetViewFamily_RenderThread() const
	{
		check(IsInRenderingThread());

		return ViewFamily_RenderThread;
	}

	void Initialize(const TSharedPtr<FTextureShareSceneViewExtension, ESPMode::ThreadSafe> InViewExtension);

	void SetPreRenderViewFamilyFunction(TFunctionTextureShareViewExtension* In);
	void SetPostRenderViewFamilyFunction(TFunctionTextureShareViewExtension* In);
	void SetOnBackBufferReadyToPresentFunction(TFunctionTextureShareOnBackBufferReadyToPresent* In);

	void SetEnableObjectProxySync(bool bEnabled);

	bool IsStereoRenderingAllowed() const;

	FViewport* GetLinkedViewport() const;
	void SetLinkedViewport(FViewport* InLinkedViewport);

protected:
	mutable FCriticalSection DataCS;

	/** Viewport to which we are attached */
	FViewport* LinkedViewport;

	bool bEnableObjectProxySync = false;

	bool bEnabled = true;
	bool bEnabled_RenderThread = true;

	TFunctionTextureShareViewExtension PreRenderViewFamilyFunction;
	TFunctionTextureShareViewExtension PostRenderViewFamilyFunction;

	TFunctionTextureShareOnBackBufferReadyToPresent OnBackBufferReadyToPresentFunction;
	
	/** TextureShare proxy owner*/
	TSharedRef<ITextureShareObjectProxy, ESPMode::ThreadSafe> ObjectProxy;

	/** Runtime value */
	FSceneViewFamily* ViewFamily_RenderThread = nullptr;
};
