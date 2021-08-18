// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#include "RHI.h"
#include "RHIResources.h"
#include "RHICommandList.h"

#include "Containers/TextureShareCoreGenericContainers.h"

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

class ITextureShareItem;

class ITextureShare
	: public IModuleInterface
{
public:
	static constexpr auto ModuleName = TEXT("TextureShare");
public:
	/**
	 * Singleton-like access to this module's interface.  This is just for convenience!
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */

	static inline ITextureShare& Get()
	{
		return FModuleManager::LoadModuleChecked<ITextureShare>(ITextureShare::ModuleName);
	}

	/**
	 * Checks to see if this module is loaded and ready.  It is only valid to call Get() if IsAvailable() returns true.
	 *
	 * @return True if the module is loaded and ready to use
	 */
	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded(ITextureShare::ModuleName);
	}

public:

	/**
	 * Create TextureShare object
	 *
	 * @param ShareName - Unique share name (case insensitive)
	 * @param SyncMode  - Synchronization settings
	 *
	 * @return True if the success
	 */
	virtual bool CreateShare(const FString& ShareName, const FTextureShareSyncPolicy& SyncMode, ETextureShareProcess Process = ETextureShareProcess::Server, float SyncWaitTime = 0.03f) = 0;

	/** 
	 * Delete TextureShare object
	 *
	 * @param ShareName - Unique share name (case insensitive)
	 *
	 * @return True if the success
	 */
	virtual bool ReleaseShare(const FString& ShareName) = 0;

	/**
	 * Get ITextureShareItem low-level api object
	 *
	 * @param ShareName - Unique share name (case insensitive)
	 *
	 * @return sharedPtr to TextureShareItem object
	 */
	virtual bool GetShare(const FString& ShareName, TSharedPtr<ITextureShareItem>& OutShareItem) const = 0;

	/**
	 * Send scene context textures from specified StereoscopicPass via share
	 *
	 * @param ShareName - Unique share name (case insensitive)
	 * @param StereoscopicPass - Stereoscopic pass (full,left,right eye, or viewport code for DisplayCluster)
	 * @param bIsEnabled - Activate
	 *
	 * @return True if the success
	 */
	virtual bool LinkSceneContextToShare(const TSharedPtr<ITextureShareItem>& ShareItem, int StereoscopicPass, bool bIsEnabled) = 0;

	/**
	 * Use this rect as backbuffer rect, for stereoscopic pass
	 *
	 * @param StereoscopicPass - EStereoscopicPass, map viewport
	 * @param BackbufferRect   - viewport rect on backbuffer (null to disable for pass)
	 *
	 * @return True if the success
	 */
	virtual bool SetBackbufferRect(int StereoscopicPass, const FIntRect* BackbufferRect) = 0;

	/**
	 * Register/Update texture info for share
	 *
	 * @param ShareItem - share object
	 * @param InTextureName - unique texture name (case insensitive)
	 * @param InSize - Texture Size
	 * @param InFormat - Texture format
	 *
	 * @return True if the success
	 */
	virtual bool RegisterTexture(const TSharedPtr<ITextureShareItem>& ShareItem, const FString& InTextureName, const FIntPoint& InSize, EPixelFormat InFormat, ETextureShareSurfaceOp OperationType) = 0;

	/**
	 * Write texture region to shared object texture.
	 * > Call inside BeginFrame_RenderThread()\EndFrame_RenderThread()
	 * > Texture must be registered, before use this function
	 *
	 * @param ShareItem - share object
	 * @param TextureName - unique texture name (case insensitive)
	 * @param SrcTexture - Source RHI texture
	 * @param SrcTextureRect - Source Region (or nullptr)
	 *
	 * @return True if the success
	 */
	virtual bool WriteToShare_RenderThread(FRHICommandListImmediate& RHICmdList, const TSharedPtr<ITextureShareItem>& ShareItem, const FString& TextureName, FRHITexture* SrcTexture, const FIntRect* SrcTextureRect = nullptr) = 0;

	/**
	 * Read from shared object texture to destination texture region
	 * > Call inside BeginFrame_RenderThread()\EndFrame_RenderThread()
	 * > Texture must be registered, before use this function
	 *
	 * @param ShareItem - share object
	 * @param TextureName - unique texture name (case insensitive)
	 * @param DstTexture - Destination RHI texture
	 * @param DstTextureRect - Destination Region (or nullptr)
	 *
	 * @return True if the success
	 */
	virtual bool ReadFromShare_RenderThread(FRHICommandListImmediate& RHICmdList, const TSharedPtr<ITextureShareItem>& ShareItem, const FString& TextureName, FRHITexture* DstTexture, const FIntRect* DstTextureRect = nullptr) = 0;

	/**
	 * Send (and register\update texture if need) RHI texture to shared object texture
	 * > Call inside BeginFrame_RenderThread()\EndFrame_RenderThread()
	 * > Texture info updated automatically from sourceTHI. Previos texture settings reset by this function call
	 *
	 * @param ShareItem - share object
	 * @param TextureName - unique texture name (case insensitive)
	 * @param SrcTexture - Source RHI texture
	 *
	 * @return True if the success
	 */
	virtual bool SendTexture_RenderThread(FRHICommandListImmediate& RHICmdList, const TSharedPtr<ITextureShareItem>& ShareItem, const FString& TextureName, FRHITexture* SrcTexture, const FIntRect* SrcTextureRect = nullptr) = 0;

	/**
	 * Receive (and register\update texture if need) RHI texture from shared object texture
	 * > Call inside BeginFrame_RenderThread()\EndFrame_RenderThread()
	 * > Texture info updated automatically from destinationRHI. Previos texture settings reset by this function call
	 *
	 * @param ShareItem - share object
	 * @param TextureName - unique texture name (case insensitive)
	 * @param DstTexture - Destination RHI texture
	 *
	 * @return True if the success
	 */
	virtual bool ReceiveTexture_RenderThread(FRHICommandListImmediate& RHICmdList, const TSharedPtr<ITextureShareItem>& ShareItem, const FString& TextureName, FRHITexture* DstTexture, const FIntRect* DstTextureRect = nullptr) = 0;

	/**
	 * Cast internal structures bp<->cpp
	 *
	 */
	virtual void CastTextureShareBPSyncPolicy(const struct FTextureShareBPSyncPolicy& InSyncPolicy, struct FTextureShareSyncPolicy& OutSyncPolicy) = 0;
};
