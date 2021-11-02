// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "ITextureShare.h"
#include "ITextureShareCore.h"
#include "TextureShareDisplayManager.h"

#include "TextureShareInstance.h"

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
	virtual bool CreateShare(const FString& ShareName, const FTextureShareSyncPolicy& SyncMode, ETextureShareProcess Process = ETextureShareProcess::Server, float SyncWaitTime = 0.03f) override;
	virtual bool ReleaseShare(const FString& ShareName) override;
	virtual bool GetShare(const FString& ShareName, TSharedPtr<ITextureShareItem>& OutShareItem) const override;

	virtual bool LinkSceneContextToShare(const TSharedPtr<ITextureShareItem>& ShareItem, int StereoscopicPass, bool bIsEnabled) override;
	virtual bool SetBackbufferRect(int StereoscopicPass, const FIntRect* BackbufferRect) override;

	virtual bool RegisterTexture(const TSharedPtr<ITextureShareItem>& ShareItem, const FString& InTextureName, const FIntPoint& InSize, EPixelFormat InFormat, ETextureShareSurfaceOp OperationType) override;

	virtual bool WriteToShare_RenderThread(FRHICommandListImmediate& RHICmdList, const TSharedPtr<ITextureShareItem>& ShareItem, const FString& TextureName, FRHITexture* SrcTexture, const FIntRect* SrcTextureRect = nullptr) override;
	virtual bool ReadFromShare_RenderThread(FRHICommandListImmediate& RHICmdList, const TSharedPtr<ITextureShareItem>& ShareItem, const FString& TextureName, FRHITexture* DstTexture, const FIntRect* DstTextureRect = nullptr) override;

	virtual bool SendTexture_RenderThread(FRHICommandListImmediate& RHICmdList, const TSharedPtr<ITextureShareItem>& ShareItem, const FString& TextureName, FRHITexture* SrcTexture, const FIntRect* SrcTextureRect = nullptr) override;
	virtual bool ReceiveTexture_RenderThread(FRHICommandListImmediate& RHICmdList, const TSharedPtr<ITextureShareItem>& ShareItem, const FString& TextureName, FRHITexture* DstTexture, const FIntRect* DstTextureRect = nullptr) override;

	virtual void CastTextureShareBPSyncPolicy(const struct FTextureShareBPSyncPolicy& InSyncPolicy, struct FTextureShareSyncPolicy& OutSyncPolicy) override;

public:
	// Rendered callback (capture scene textures)
	void OnBeginRenderViewFamily(FSceneViewFamily& InViewFamily);
	void OnResolvedSceneColor_RenderThread(FRHICommandListImmediate& RHICmdList, class FSceneRenderTargets& SceneContext, class FSceneViewFamily& ViewFamily);
	void OnPostRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, class FSceneViewFamily& ViewFamily);

private:
	void UpdateTextureSharesProxy();

	bool FindTextureShare(const FString& ShareName, TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe>& OutTextureShare) const;
	bool FindTextureShare(const TSharedPtr<ITextureShareItem>& ShareItem, TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe>& OutTextureShare) const;
	bool FindTextureShare(int32 InStereoscopicPass, TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe>& OutTextureShare) const;
		 
	bool FindTextureShare_RenderThread(const FString& ShareName, TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe>& OutTextureShare) const;
	bool FindTextureShare_RenderThread(const TSharedPtr<ITextureShareItem>& ShareItem, TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe>& OutTextureShare) const;
	bool FindTextureShare_RenderThread(int32 InStereoscopicPass, TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe>& OutTextureShare) const;

private:
	TSharedPtr<FTextureShareDisplayManager, ESPMode::ThreadSafe> DisplayManager;

	TArray<TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe>> TextureShares;
	TArray<TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe>> TextureSharesProxy;
};
