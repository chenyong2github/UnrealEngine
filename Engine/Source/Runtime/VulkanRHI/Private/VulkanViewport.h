// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VulkanViewport.h: Vulkan viewport RHI definitions.
=============================================================================*/

#pragma once

#include "VulkanResources.h"
#include "HAL/CriticalSection.h"

class FVulkanDynamicRHI;
class FVulkanSwapChain;
class FVulkanQueue;
class FVulkanViewport;

namespace VulkanRHI
{
	class FSemaphore;
}

class FVulkanBackBuffer : public FVulkanTexture2D
{
public:
	FVulkanBackBuffer(FVulkanDevice& Device, FVulkanViewport* InViewport, EPixelFormat Format, uint32 SizeX, uint32 SizeY, uint32 UEFlags);
	virtual ~FVulkanBackBuffer();
	
	virtual void OnTransitionResource(FVulkanCommandListContext& Context, EResourceTransitionAccess TransitionType) override final;

	void OnGetBackBufferImage(FRHICommandListImmediate& RHICmdList);
	void OnAdvanceBackBufferFrame(FRHICommandListImmediate& RHICmdList);

	void ReleaseViewport();

private:
	void AcquireBackBufferImage(FVulkanCommandListContext& Context);
	void ReleaseAcquiredImage();

private:
	FVulkanViewport* Viewport;
};


class FVulkanViewport : public FRHIViewport, public VulkanRHI::FDeviceChild
{
public:
	enum { NUM_BUFFERS = 3 };

	FVulkanViewport(FVulkanDynamicRHI* InRHI, FVulkanDevice* InDevice, void* InWindowHandle, uint32 InSizeX,uint32 InSizeY,bool bInIsFullscreen, EPixelFormat InPreferredPixelFormat);
	~FVulkanViewport();

	FTexture2DRHIRef GetBackBuffer(FRHICommandListImmediate& RHICmdList);
	void AdvanceBackBufferFrame(FRHICommandListImmediate& RHICmdList);

	void WaitForFrameEventCompletion();

	void IssueFrameEvent();

	inline FIntPoint GetSizeXY() const
	{
		return FIntPoint(SizeX, SizeY);
	}

	virtual void SetCustomPresent(FRHICustomPresent* InCustomPresent) override final
	{
		CustomPresent = InCustomPresent;
	}

	virtual FRHICustomPresent* GetCustomPresent() const override final
	{
		return CustomPresent;
	}

	virtual void Tick(float DeltaTime) override final;
	
	bool Present(FVulkanCommandListContext* Context, FVulkanCmdBuffer* CmdBuffer, FVulkanQueue* Queue, FVulkanQueue* PresentQueue, bool bLockToVsync);

	inline uint32 GetPresentCount() const
	{
		return PresentCount;
	}

	inline bool IsFullscreen() const
	{
		return bIsFullscreen;
	}

protected:
	// NUM_BUFFERS don't have to match exactly as the driver can require a minimum number larger than NUM_BUFFERS. Provide some slack
	TArray<VkImage, TInlineAllocator<NUM_BUFFERS*2>> BackBufferImages;
	TArray<VulkanRHI::FSemaphore*, TInlineAllocator<NUM_BUFFERS*2>> RenderingDoneSemaphores;
	TArray<FVulkanTextureView, TInlineAllocator<NUM_BUFFERS*2>> TextureViews;
	TRefCountPtr<FVulkanBackBuffer> RHIBackBuffer;

	// 'Dummy' back buffer
	TRefCountPtr<FVulkanTexture2D>	RenderingBackBuffer;
	
	/** narrow-scoped section that locks access to back buffer during its recreation*/
	FCriticalSection RecreatingSwapchain;

	FVulkanDynamicRHI* RHI;
	uint32 SizeX;
	uint32 SizeY;
	bool bIsFullscreen;
	EPixelFormat PixelFormat;
	int32 AcquiredImageIndex;
	FVulkanSwapChain* SwapChain;
	void* WindowHandle;
	uint32 PresentCount;

	int8 LockToVsync;

	// Just a pointer, not owned by this class
	VulkanRHI::FSemaphore* AcquiredSemaphore;

	FCustomPresentRHIRef CustomPresent;

	FVulkanCmdBuffer* LastFrameCommandBuffer = nullptr;
	uint64 LastFrameFenceCounter = 0;

	void CreateSwapchain(struct FVulkanSwapChainRecreateInfo* RecreateInfo);
	void DestroySwapchain(struct FVulkanSwapChainRecreateInfo* RecreateInfo);
	void AcquireImageIndex();
	bool TryAcquireImageIndex();

	void RecreateSwapchain(void* NewNativeWindow);
	void RecreateSwapchainFromRT(EPixelFormat PreferredPixelFormat);
	void Resize(uint32 InSizeX, uint32 InSizeY, bool bIsFullscreen, EPixelFormat PreferredPixelFormat);

	static int32 DoAcquireImageIndex(FVulkanViewport* Viewport);
	bool DoCheckedSwapChainJob(TFunction<int32(FVulkanViewport*)> SwapChainJob);

	friend class FVulkanDynamicRHI;
	friend class FVulkanCommandListContext;
	friend struct FRHICommandAcquireBackBuffer;
	friend class FVulkanBackBuffer;
};

template<>
struct TVulkanResourceTraits<FRHIViewport>
{
	typedef FVulkanViewport TConcreteType;
};
