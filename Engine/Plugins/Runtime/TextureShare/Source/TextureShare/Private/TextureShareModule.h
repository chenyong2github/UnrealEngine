// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "ITextureShare.h"
#include "ITextureShareCore.h"
#include "TextureShareDisplayManager.h"

struct IPooledRenderTarget;

class FTextureShareModule 
	: public ITextureShare
{
public:

	FTextureShareModule();
	virtual ~FTextureShareModule();

	//////////////////////////////////////////////////////////////////////////////////////////////
	// IModuleInterface
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// TextureShare
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool CreateShare(const FString& ShareName, const FTextureShareSyncPolicy& SyncMode, ETextureShareProcess Process = ETextureShareProcess::Server) override;
	virtual bool ReleaseShare(const FString& ShareName) override;
	virtual bool GetShare(const FString& ShareName, TSharedPtr<ITextureShareItem>& OutShareItem) const override;

	virtual bool LinkSceneContextToShare(const TSharedPtr<ITextureShareItem>& ShareItem, int StereoscopicPass, bool bIsEnabled) override;
	virtual bool SetBackbufferRect(int StereoscopicPass, FIntRect* BackbufferRect) override;

	virtual bool RegisterTexture(const TSharedPtr<ITextureShareItem>& ShareItem, const FString& InTextureName, const FIntPoint& InSize, EPixelFormat InFormat, ETextureShareSurfaceOp OperationType) override;

	virtual bool WriteToShare_RenderThread(FRHICommandListImmediate& RHICmdList, const TSharedPtr<ITextureShareItem>& ShareItem, const FString& TextureName, FRHITexture* SrcTexture, const FIntRect* SrcTextureRect = nullptr) override;
	virtual bool ReadFromShare_RenderThread(FRHICommandListImmediate& RHICmdList, const TSharedPtr<ITextureShareItem>& ShareItem, const FString& TextureName, FRHITexture* DstTexture, const FIntRect* DstTextureRect = nullptr) override;

	virtual bool SendTexture_RenderThread(FRHICommandListImmediate& RHICmdList, const TSharedPtr<ITextureShareItem>& ShareItem, const FString& TextureName, FRHITexture* SrcTexture, const FIntRect* SrcTextureRect = nullptr) override;
	virtual bool ReceiveTexture_RenderThread(FRHICommandListImmediate& RHICmdList, const TSharedPtr<ITextureShareItem>& ShareItem, const FString& TextureName, FRHITexture* DstTexture, const FIntRect* DstTextureRect = nullptr) override;

	// Rendered callback (capture scene textures)
	void OnResolvedSceneColor_RenderThread(FRHICommandListImmediate& RHICmdList, class FSceneRenderTargets& SceneContext, class FSceneViewFamily& ViewFamily);
	void OnPostRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, class FSceneViewFamily& ViewFamily);

protected:
	bool SendTexture_RenderThread(FRHICommandListImmediate& RHICmdList, const TSharedPtr<ITextureShareItem>& ShareItem, const FString& TextureName, const TRefCountPtr<IPooledRenderTarget>& PooledRenderTargetRef);

protected:
	bool SendSceneContext_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& ShareName, class FSceneRenderTargets& SceneContext, class FSceneViewFamily& ViewFamily);
	bool SendPostRender_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& ShareName, class FSceneViewFamily& ViewFamily);

	void RemoveSceneContextCallback(const FString& ShareName);
	

protected:
	ETextureShareDevice GetTextureShareDeviceType() const;

private:
	void ReleaseSharedResources();

private:
	ITextureShareCore& ShareCoreAPI;
	TSharedPtr<FTextureShareDisplayManager, ESPMode::ThreadSafe> DisplayManager;

	mutable FCriticalSection DataGuard;
	/** Map Share name to stereoscopic pass */
	mutable TMap<FString, int> TextureShareSceneContextCallback;
	/** Use SubRect for stereoscopic pass (nDisplay viewport purpose) */
	mutable TMap<int, FIntRect> BackbufferRects;
};
