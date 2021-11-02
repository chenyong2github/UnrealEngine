// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "RHI.h"
#include "RHICommandList.h"
#include "RHIResources.h"

#include "ITextureShareCore.h"

class FTextureShareDisplayManager;
struct IPooledRenderTarget;

// TextureShareInstance Proxy data for render thread
class FTextureShareInstanceData
{
public:
	FTextureShareInstanceData()
	{ }

	FTextureShareInstanceData(const FTextureShareInstanceData& In)
	{
		operator=(In);
	}

	~FTextureShareInstanceData()
	{
		ResetSceneContext();
	}

	FORCEINLINE void operator=(const FTextureShareInstanceData& In)
	{
		RemoteAdditionalData = In.RemoteAdditionalData;

		bAllowRTTRect    = In.bAllowRTTRect;
		RTTRect          = In.RTTRect;
		FrameNumber      = In.FrameNumber;
		DisplayManager   = In.DisplayManager;
		StereoscopicPass = In.StereoscopicPass;
	}

public:
	/**
	 * Disable scene context capturing
	 */
	void ResetSceneContext();

	/**
	 * Send scene context textures from specified StereoscopicPass via share
	 *
	 * @param StereoscopicPass - Stereoscopic pass (full,left,right eye, or viewport code for DisplayCluster)
	 * @param InDisplayManager - Display Manager api
	 *
	 */
	void LinkSceneContext(int32 StereoscopicPass, const TSharedPtr<FTextureShareDisplayManager, ESPMode::ThreadSafe>& InDisplayManager);

	/**
	 * Use this rect as backbuffer rect, for stereoscopic pass capturing
	 *
	 * @param BackbufferRect   - viewport rect on backbuffer (null to disable for pass)
	 *
	 */
	void SetRTTRect(const FIntRect* InRTTRect);

	/**
	 * return backbuffer rect
	 *
	 * @return const ptr on backbuffer rect on null
	 *
	 */
	const FIntRect* GetRTTRect() const
	{
		return bAllowRTTRect ? &RTTRect : nullptr;
	}

public:
	// Additional data for current frame from remote process
	FTextureShareAdditionalData RemoteAdditionalData;

	// share custom region from RTT
	bool bAllowRTTRect = false;
	FIntRect RTTRect;

	// captured frame number. increases in HandleBeginNewFrame()
	uint32 FrameNumber = 0;

	// Capture textures from this stereoscopic pass
	TSharedPtr<FTextureShareDisplayManager, ESPMode::ThreadSafe> DisplayManager;
	int32 StereoscopicPass = -1;
};

// TextureShare interface for UE. Multithreading support
class FTextureShareInstance
{
public:
	FTextureShareInstance(const FString& InShareName, const TSharedPtr<ITextureShareItem>& InShareItem)
		: ShareName(InShareName), ShareItem(InShareItem)
	{ }

	~FTextureShareInstance();

	/**
	 * Create new TextureShareInstance object
	 *
	 * @param OutShareInstance- new instance
	 * @param ShareName - Unique share name (case insensitive)
	 * @param SyncMode  - Synchronization settings
	 * @param Process - Type of process
	 *
	 * @return True if the success
	 */
	static bool Create(TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe>& OutShareInstance, const FString& ShareName, const FTextureShareSyncPolicy& SyncMode, ETextureShareProcess Process);

public:
	/**
	 * Register/Update texture info for share
	 *
	 * @param InTextureName - unique texture name (case insensitive)
	 * @param InSize - Texture Size
	 * @param InFormat - Texture format
	 * @param OperationType - Surface operation type
	 *
	 * @return True if the success
	 */
	bool RegisterTexture(const FString& InTextureName, const FIntPoint& InSize, EPixelFormat InFormat, ETextureShareSurfaceOp OperationType);

	/**
	 * Send (and register\update texture if need) RHI texture to shared object texture
	 * > Call inside BeginFrame_RenderThread()\EndFrame_RenderThread()
	 * > Texture info updated automatically from sourceTHI. Previos texture settings reset by this function call
	 *
	 * @param TextureName - unique texture name (case insensitive)
	 * @param SrcTexture - Source RHI texture
	 * @param SrcTextureRect - Source Region (or nullptr)
	 *
	 * @return True if the success
	 */
	bool SendTexture_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& TextureName, FRHITexture* SrcTexture, const FIntRect* SrcTextureRect = nullptr);

	/**
	 * Send (and register\update texture if need) RHI texture to shared object texture
	 * > Call inside BeginFrame_RenderThread()\EndFrame_RenderThread()
	 * > Texture info updated automatically from sourceTHI. Previos texture settings reset by this function call
	 *
	 * @param TextureName - unique texture name (case insensitive)
	 * @param PooledRenderTargetRef - Source pooled RTT
	 *
	 * @return True if the success
	 */
	bool SendTexture_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& TextureName, const TRefCountPtr<IPooledRenderTarget>& PooledRenderTargetRef);

	/**
	 * Receive (and register\update texture if need) RHI texture from shared object texture
	 * > Call inside BeginFrame_RenderThread()\EndFrame_RenderThread()
	 * > Texture info updated automatically from destinationRHI. Previos texture settings reset by this function call
	 *
	 * @param TextureName - unique texture name (case insensitive)
	 * @param DstTexture - Destination RHI texture
	 * @param DstTextureRect - Destination Region (or nullptr)
	 *
	 * @return True if the success
	 */
	bool ReceiveTexture_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& TextureName, FRHITexture* DstTexture, const FIntRect* DstTextureRect = nullptr);

	/**
	 * Write texture region to shared object texture.
	 * > Call inside BeginFrame_RenderThread()\EndFrame_RenderThread()
	 * > Texture must be registered, before use this function
	 *
	 * @param InTextureName - unique texture name (case insensitive)
	 * @param SrcTexture - Source RHI texture
	 * @param SrcTextureRect - Source Region (or nullptr)
	 *
	 * @return True if the success
	 */
	bool WriteToShare_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& InTextureName, FRHITexture* SrcTexture, const FIntRect* SrcTextureRect);

	/**
	 * Read from shared object texture to destination texture region
	 * > Call inside BeginFrame_RenderThread()\EndFrame_RenderThread()
	 * > Texture must be registered, before use this function
	 *
	 * @param InTextureName - unique texture name (case insensitive)
	 * @param DstTexture - Destination RHI texture
	 * @param DstTextureRect - Destination Region (or nullptr)
	 *
	 * @return True if the success
	 */
	bool ReadFromShare_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& InTextureName, FRHITexture* DstTexture, const FIntRect* DstTextureRect);

	/**
	 * Update proxy data on render thread
	 */
	void UpdateData_RenderThread(const FTextureShareInstanceData& ProxyData);

	/**
	 * Handle new frame on game thread
	 */
	void HandleBeginNewFrame();

public:
	const FTextureShareInstanceData& GetDataConstRef() const
	{
		check(IsInGameThread());

		return InstanceData;
	}

	const FTextureShareInstanceData& GetDataConstRef_RenderThread() const
	{
		check(IsInRenderingThread());

		return InstanceData_RenderThread;
	}

	FORCEINLINE bool IsValid() const
	{
		return ShareItem.IsValid();
	}

	FORCEINLINE bool Equals(const TSharedPtr<ITextureShareItem>& InShareItem) const
	{
		return ShareItem == InShareItem;
	}

	FORCEINLINE bool Equals(const FString& InShareName) const
	{
		return ShareName.Equals(InShareName, ESearchCase::IgnoreCase);
	}

	FORCEINLINE const TSharedPtr<ITextureShareItem>& GetTextureShareItem() const
	{
		return ShareItem;
	}

protected:
	friend class FTextureShareModule;
	
	/**
	 * Send scene context textures from specified StereoscopicPass via share
	 *
	 * @param StereoscopicPass - Stereoscopic pass (full,left,right eye, or viewport code for DisplayCluster)
	 * @param InDisplayManager - display manager for capture
	 *
	 */
	void LinkSceneContext(int32 StereoscopicPass, const TSharedPtr<FTextureShareDisplayManager, ESPMode::ThreadSafe>& InDisplayManager)
	{
		ImplGetData().LinkSceneContext(StereoscopicPass, InDisplayManager);
	}

	/**
	 * Use this rect as backbuffer rect, for stereoscopic pass
	 *
	 * @param BackbufferRect   - viewport rect on backbuffer (null to disable for pass)
	 *
	 */
	void SetRTTRect(const FIntRect* BackbufferRect)
	{
		ImplGetData().SetRTTRect(BackbufferRect);
	}

	/**
	* Send internal textures from scene context
	*
	* @param SceneContext - scene context api
	* @param ViewFamily - view family data
	*
	* @return True if the success
	*/
	bool SendSceneContext_RenderThread(FRHICommandListImmediate& RHICmdList, class FSceneRenderTargets& SceneContext, class FSceneViewFamily& ViewFamily);

	/**
	* Send backbuffer texture
	*
	* @param ViewFamily - view family data
	*
	* @return True if the success
	*/
	bool SendPostRender_RenderThread(FRHICommandListImmediate& RHICmdList, class FSceneViewFamily& ViewFamily);

private:
	FTextureShareInstanceData& ImplGetData()
	{
		check(IsInGameThread());

		return InstanceData;
	}

	FTextureShareInstanceData& ImplGetData_RenderThread()
	{
		check(IsInRenderingThread());

		return InstanceData_RenderThread;
	}

public:
	const FString ShareName;

private:
	// Single thread texture share object
	TSharedPtr<ITextureShareItem> ShareItem;

	// Instance data for game and render threads
	FTextureShareInstanceData InstanceData;
	FTextureShareInstanceData InstanceData_RenderThread;
};
