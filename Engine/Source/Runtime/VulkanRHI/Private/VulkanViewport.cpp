// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VulkanViewport.cpp: Vulkan viewport RHI implementation.
=============================================================================*/

#include "VulkanRHIPrivate.h"
#include "VulkanSwapChain.h"
#include "VulkanPendingState.h"
#include "VulkanContext.h"
#include "GlobalShader.h"
#include "HAL/PlatformAtomics.h"
#include "Engine/RendererSettings.h"

extern FVulkanCommandBufferManager* GVulkanCommandBufferManager;

struct FRHICommandProcessDeferredDeletionQueue final : public FRHICommand<FRHICommandProcessDeferredDeletionQueue>
{
	FVulkanDevice* Device;
	FORCEINLINE_DEBUGGABLE FRHICommandProcessDeferredDeletionQueue(FVulkanDevice* InDevice)
		: Device(InDevice)
	{
	}

	void Execute(FRHICommandListBase& CmdList)
	{
		Device->GetDeferredDeletionQueue().ReleaseResources();
	}
};


FVulkanBackBuffer::FVulkanBackBuffer(FVulkanDevice& Device, FVulkanViewport* InViewport, EPixelFormat Format, uint32 SizeX, uint32 SizeY, uint32 UEFlags)
	: FVulkanTexture2D(Device, Format, SizeX, SizeY, 1, 1, VK_NULL_HANDLE, UEFlags, FRHIResourceCreateInfo())
	, Viewport(InViewport)
{
}

void FVulkanBackBuffer::ReleaseAcquiredImage()
{
	DefaultView.View = VK_NULL_HANDLE;
	DefaultView.ViewId = 0;
	Surface.Image = VK_NULL_HANDLE;
}

void FVulkanBackBuffer::ReleaseViewport()
{
	Viewport = nullptr;
	ReleaseAcquiredImage();
}

void FVulkanBackBuffer::OnGetBackBufferImage(FRHICommandListImmediate& RHICmdList)
{
	check(Viewport);
	if (GVulkanDelayAcquireImage == EDelayAcquireImageType::None)
	{
		FVulkanCommandListContext& Context = (FVulkanCommandListContext&)RHICmdList.GetContext();
		AcquireBackBufferImage(Context);
	}
}

void FVulkanBackBuffer::OnAdvanceBackBufferFrame(FRHICommandListImmediate& RHICmdList)
{
	check(Viewport);
	ReleaseAcquiredImage();
}

void FVulkanBackBuffer::OnTransitionResource(FVulkanCommandListContext& Context, EResourceTransitionAccess TransitionType)
{
	if (TransitionType == EResourceTransitionAccess::EWritable && GVulkanDelayAcquireImage == EDelayAcquireImageType::LazyAcquire)
	{
		AcquireBackBufferImage(Context);
	}
}

void FVulkanBackBuffer::AcquireBackBufferImage(FVulkanCommandListContext& Context)
{
	check(Viewport);
	if (Surface.Image == VK_NULL_HANDLE)
	{
		check(Viewport->AcquiredImageIndex == -1); //-V595
		
		Viewport->AcquireImageIndex(); //-V595
		// If swapchain got invalidated (OUT_OF_DATE etc) in the above call, we may end up not having a valid viewport pointer at this point. Abort the whole thing.
		if (Viewport == nullptr)
		{
			return;
		}

		int32 AcquiredImageIndex = Viewport->AcquiredImageIndex;
		check(AcquiredImageIndex >= 0 && AcquiredImageIndex < Viewport->TextureViews.Num());

		FVulkanTextureView& ImageView = Viewport->TextureViews[AcquiredImageIndex];

		Surface.Image = ImageView.Image;
		DefaultView.View = ImageView.View;
		DefaultView.ViewId = ImageView.ViewId;

		// right after acquiring image is in undefined state
		FTransitionAndLayoutManager& LayoutMgr = Context.GetTransitionAndLayoutManager();
		VkImageLayout& CurrentLayout = LayoutMgr.FindOrAddLayoutRW(ImageView.Image, VK_IMAGE_LAYOUT_UNDEFINED);
		CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		
		FVulkanCommandBufferManager* CmdBufferManager = Context.GetCommandBufferManager();
		FVulkanCmdBuffer* CmdBuffer = CmdBufferManager->GetActiveCmdBuffer();
		if (CmdBuffer->IsInsideRenderPass())
		{
			// This could happen due to a SetRT(AndClear) call lingering around (so emulated needs to be ended); however REAL render passes should already have been ended!
			checkf(!LayoutMgr.bInsideRealRenderPass, TEXT("Did not end Render Pass!"));
			LayoutMgr.EndEmulatedRenderPass(CmdBuffer);
		}
			
		// Wait for semaphore signal before writing to backbuffer image
		CmdBuffer->AddWaitSemaphore(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, Viewport->AcquiredSemaphore);
	}
}

FVulkanBackBuffer::~FVulkanBackBuffer()
{
	check(Surface.IsImageOwner() == false);
	// Clear flags so ~FVulkanTexture2D() doesn't try to re-destroy it
	Surface.UEFlags = 0;
	ReleaseAcquiredImage();
}

FVulkanViewport::FVulkanViewport(FVulkanDynamicRHI* InRHI, FVulkanDevice* InDevice, void* InWindowHandle, uint32 InSizeX, uint32 InSizeY, bool bInIsFullscreen, EPixelFormat InPreferredPixelFormat)
	: VulkanRHI::FDeviceChild(InDevice)
	, RHI(InRHI)
	, SizeX(InSizeX)
	, SizeY(InSizeY)
	, bIsFullscreen(bInIsFullscreen)
	, PixelFormat(InPreferredPixelFormat)
	, AcquiredImageIndex(-1)
	, SwapChain(nullptr)
	, WindowHandle(InWindowHandle)
	, PresentCount(0)
	, LockToVsync(1)
	, AcquiredSemaphore(nullptr)
{
	check(IsInGameThread());
	FMemory::Memzero(BackBufferImages);
	RHI->Viewports.Add(this);

	// Make sure Instance is created
	RHI->InitInstance();

	CreateSwapchain(nullptr);

	if (FVulkanPlatform::SupportsStandardSwapchain())
	{
		for (int32 Index = 0, NumBuffers = RenderingDoneSemaphores.Num(); Index < NumBuffers; ++Index)
		{
			RenderingDoneSemaphores[Index] = new VulkanRHI::FSemaphore(*InDevice);
			RenderingDoneSemaphores[Index]->AddRef();
		}
	}
}

FVulkanViewport::~FVulkanViewport()
{
	RenderingBackBuffer = nullptr;
	
	if (RHIBackBuffer)
	{
		RHIBackBuffer->ReleaseViewport();
		RHIBackBuffer = nullptr;
	}
	
	if (FVulkanPlatform::SupportsStandardSwapchain())
	{
		for (int32 Index = 0, NumBuffers = RenderingDoneSemaphores.Num(); Index < NumBuffers; ++Index)
		{
			RenderingDoneSemaphores[Index]->Release();

			TextureViews[Index].Destroy(*Device);

			// FIXME: race condition on TransitionAndLayoutManager, could this be called from RT while RHIT is active?
			Device->NotifyDeletedImage(BackBufferImages[Index]);
			Device->NotifyDeletedRenderTarget(BackBufferImages[Index]); 
			BackBufferImages[Index] = VK_NULL_HANDLE;
		}

		SwapChain->Destroy(nullptr);
		delete SwapChain;
		SwapChain = nullptr;
	}

	RHI->Viewports.Remove(this);
}

int32 FVulkanViewport::DoAcquireImageIndex(FVulkanViewport* Viewport)
{
	return Viewport->AcquiredImageIndex = Viewport->SwapChain->AcquireImageIndex(&Viewport->AcquiredSemaphore);
}

bool FVulkanViewport::DoCheckedSwapChainJob(TFunction<int32(FVulkanViewport*)> SwapChainJob)
{
	int32 AttemptsPending = 4;
	int32 Status = SwapChainJob(this);

	while (Status < 0 && AttemptsPending > 0)
	{
		if (Status == (int32)FVulkanSwapChain::EStatus::OutOfDate)
		{
			UE_LOG(LogVulkanRHI, Verbose, TEXT("Swapchain is out of date! Trying to recreate the swapchain."));
		}
		else if (Status == (int32)FVulkanSwapChain::EStatus::SurfaceLost)
		{
			UE_LOG(LogVulkanRHI, Warning, TEXT("Swapchain surface lost! Trying to recreate the swapchain."));
		}
		else
		{
			check(0);
		}

		RecreateSwapchain(WindowHandle);

		// Swapchain creation pushes some commands - flush the command buffers now to begin with a fresh state
		Device->SubmitCommandsAndFlushGPU();
		Device->WaitUntilIdle();

		Status = SwapChainJob(this);

		--AttemptsPending;
	}

	return Status >= 0;
}

void FVulkanViewport::AcquireImageIndex()
{
	if (!DoCheckedSwapChainJob(DoAcquireImageIndex))
	{
		UE_LOG(LogVulkanRHI, Fatal, TEXT("Swapchain acquire image index failed!"));
	}
	check(AcquiredImageIndex != -1);
}

bool FVulkanViewport::TryAcquireImageIndex()
{
	int NewImageIndex = DoAcquireImageIndex(this);
	if (NewImageIndex != -1)
	{
		AcquiredImageIndex = NewImageIndex;
		return true;
	}

	return false;
}

FTexture2DRHIRef FVulkanViewport::GetBackBuffer(FRHICommandListImmediate& RHICmdList)
{
	check(IsInRenderingThread());

	// make sure we aren't in the middle of swapchain recreation (which can happen on e.g. RHI thread)
	FScopeLock LockSwapchain(&RecreatingSwapchain);

	if (FVulkanPlatform::SupportsStandardSwapchain() && GVulkanDelayAcquireImage != EDelayAcquireImageType::DelayAcquire)
	{
		check(RHICmdList.IsImmediate());
		check(RHIBackBuffer);
		
		RHICmdList.EnqueueLambda([this](FRHICommandListImmediate& CmdList)
		{
			this->RHIBackBuffer->OnGetBackBufferImage(CmdList);
		});

		return RHIBackBuffer.GetReference();
	}
	
	return RenderingBackBuffer.GetReference();
}

void FVulkanViewport::AdvanceBackBufferFrame(FRHICommandListImmediate& RHICmdList)
{
	check(IsInRenderingThread());

	if (FVulkanPlatform::SupportsStandardSwapchain() && GVulkanDelayAcquireImage != EDelayAcquireImageType::DelayAcquire)
	{
		check(RHIBackBuffer);
		
		RHICmdList.EnqueueLambda([this](FRHICommandListImmediate& CmdList)
		{
			this->RHIBackBuffer->OnAdvanceBackBufferFrame(CmdList);
		});
	}
}

void FVulkanViewport::WaitForFrameEventCompletion()
{
	if (FVulkanPlatform::RequiresWaitingForFrameCompletionEvent())
	{
		static FCriticalSection CS;
		FScopeLock ScopeLock(&CS);
		if (LastFrameCommandBuffer && LastFrameCommandBuffer->IsSubmitted())
		{
			// If last frame's fence hasn't been signaled already, wait for it here
			if (LastFrameFenceCounter == LastFrameCommandBuffer->GetFenceSignaledCounter())
			{
				if (!GWaitForIdleOnSubmit)
				{
					// The wait has already happened if GWaitForIdleOnSubmit is set
					LastFrameCommandBuffer->GetOwner()->GetMgr().WaitForCmdBuffer(LastFrameCommandBuffer);
				}
			}
		}
	}
}

void FVulkanViewport::IssueFrameEvent()
{
	if (FVulkanPlatform::RequiresWaitingForFrameCompletionEvent())
	{
		// The fence we need to wait on next frame is already there in the command buffer
		// that was just submitted in this frame's Present. Just grab that command buffer's
		// info to use next frame in WaitForFrameEventCompletion.
		FVulkanQueue* Queue = Device->GetGraphicsQueue();
		Queue->GetLastSubmittedInfo(LastFrameCommandBuffer, LastFrameFenceCounter);
	}
}


FVulkanFramebuffer::FVulkanFramebuffer(FVulkanDevice& Device, const FRHISetRenderTargetsInfo& InRTInfo, const FVulkanRenderTargetLayout& RTLayout, const FVulkanRenderPass& RenderPass)
	: Framebuffer(VK_NULL_HANDLE)
	, NumColorRenderTargets(InRTInfo.NumColorRenderTargets)
	, NumColorAttachments(0)
	, DepthStencilRenderTargetImage(VK_NULL_HANDLE)
	, FragmentDensityImage(VK_NULL_HANDLE)
{
	FMemory::Memzero(ColorRenderTargetImages);
	FMemory::Memzero(ColorResolveTargetImages);
		
	AttachmentTextureViews.Empty(RTLayout.GetNumAttachmentDescriptions());
	uint32 MipIndex = 0;

	const VkExtent3D& RTExtents = RTLayout.GetExtent3D();
	// Adreno does not like zero size RTs
	check(RTExtents.width != 0 && RTExtents.height != 0);
	uint32 NumLayers = RTExtents.depth;

	for (int32 Index = 0; Index < InRTInfo.NumColorRenderTargets; ++Index)
	{
		FRHITexture* RHITexture = InRTInfo.ColorRenderTarget[Index].Texture;
		if (!RHITexture)
		{
			continue;
		}

		FVulkanTextureBase* Texture = FVulkanTextureBase::Cast(RHITexture);
		// this could fire in case one of the textures is FVulkanBackBuffer and it has not acquired an image
		// with EDelayAcquireImageType::LazyAcquire acquire happens when texture transition to Writeable state
		// make sure you call TransitionResource(Writable, Tex) before using this texture as a render-target
		check(Texture->Surface.Image != VK_NULL_HANDLE);

		ColorRenderTargetImages[Index] = Texture->Surface.Image;
		MipIndex = InRTInfo.ColorRenderTarget[Index].MipIndex;

		FVulkanTextureView RTView;
		if (Texture->Surface.GetViewType() == VK_IMAGE_VIEW_TYPE_2D || Texture->Surface.GetViewType() == VK_IMAGE_VIEW_TYPE_2D_ARRAY)
		{
			RTView.Create(*Texture->Surface.Device, Texture->Surface.Image, Texture->Surface.GetViewType(), Texture->Surface.GetFullAspectMask(), Texture->Surface.PixelFormat, Texture->Surface.ViewFormat, MipIndex, 1, FMath::Max(0, (int32)InRTInfo.ColorRenderTarget[Index].ArraySliceIndex), Texture->Surface.GetNumberOfArrayLevels(), true);
		}
		else if (Texture->Surface.GetViewType() == VK_IMAGE_VIEW_TYPE_CUBE)
		{
			// Cube always renders one face at a time
			INC_DWORD_STAT(STAT_VulkanNumImageViews);
			RTView.Create(*Texture->Surface.Device, Texture->Surface.Image, VK_IMAGE_VIEW_TYPE_2D, Texture->Surface.GetFullAspectMask(), Texture->Surface.PixelFormat, Texture->Surface.ViewFormat, MipIndex, 1, InRTInfo.ColorRenderTarget[Index].ArraySliceIndex, 1, true);
		}
		else if (Texture->Surface.GetViewType() == VK_IMAGE_VIEW_TYPE_3D)
		{
			RTView.Create(*Texture->Surface.Device, Texture->Surface.Image, VK_IMAGE_VIEW_TYPE_2D_ARRAY, Texture->Surface.GetFullAspectMask(), Texture->Surface.PixelFormat, Texture->Surface.ViewFormat, MipIndex, 1, 0, Texture->Surface.Depth, true);
		}
		else
		{
			ensure(0);
		}

		AttachmentTextureViews.Add(RTView);
		AttachmentViewsToDelete.Add(RTView.View);

		++NumColorAttachments;

		if (InRTInfo.bHasResolveAttachments)
		{
			FRHITexture* ResolveRHITexture = InRTInfo.ColorResolveRenderTarget[Index].Texture;
			FVulkanTextureBase* ResolveTexture = FVulkanTextureBase::Cast(ResolveRHITexture);
			ColorResolveTargetImages[Index] = ResolveTexture->Surface.Image;

			//resolve attachments only supported for 2d/2d array textures
			FVulkanTextureView ResolveRTView;
			if (ResolveTexture->Surface.GetViewType() == VK_IMAGE_VIEW_TYPE_2D || ResolveTexture->Surface.GetViewType() == VK_IMAGE_VIEW_TYPE_2D_ARRAY)
			{
				ResolveRTView.Create(*ResolveTexture->Surface.Device, ResolveTexture->Surface.Image, ResolveTexture->Surface.GetViewType(), ResolveTexture->Surface.GetFullAspectMask(), ResolveTexture->Surface.PixelFormat, ResolveTexture->Surface.ViewFormat, 
					MipIndex, 1, FMath::Max(0, (int32)InRTInfo.ColorRenderTarget[Index].ArraySliceIndex), ResolveTexture->Surface.GetNumberOfArrayLevels(), true);
			}

			AttachmentTextureViews.Add(ResolveRTView);
			AttachmentViewsToDelete.Add(ResolveRTView.View);
		}
	}

	if (RTLayout.GetHasDepthStencil())
	{
		FVulkanTextureBase* Texture = FVulkanTextureBase::Cast(InRTInfo.DepthStencilRenderTarget.Texture);
		DepthStencilRenderTargetImage = Texture->Surface.Image;
		bool bHasStencil = (Texture->Surface.PixelFormat == PF_DepthStencil || Texture->Surface.PixelFormat == PF_X24_G8);
		check(Texture->PartialView);
		PartialDepthTextureView = *Texture->PartialView;

		ensure(Texture->Surface.GetViewType() == VK_IMAGE_VIEW_TYPE_2D || Texture->Surface.GetViewType() == VK_IMAGE_VIEW_TYPE_2D_ARRAY || Texture->Surface.GetViewType() == VK_IMAGE_VIEW_TYPE_CUBE);
		if (NumColorAttachments == 0 && Texture->Surface.GetViewType() == VK_IMAGE_VIEW_TYPE_CUBE)
		{
			FVulkanTextureView RTView;
			RTView.Create(*Texture->Surface.Device, Texture->Surface.Image, VK_IMAGE_VIEW_TYPE_2D_ARRAY, Texture->Surface.GetFullAspectMask(), Texture->Surface.PixelFormat, Texture->Surface.ViewFormat, MipIndex, 1, 0, 6, true);
			NumLayers = 6;
			AttachmentTextureViews.Add(RTView);
			AttachmentViewsToDelete.Add(RTView.View);
		}
		else
		{
			AttachmentTextureViews.Add(Texture->DefaultView);
		}
	}

	if (RTLayout.GetHasFragmentDensityAttachment() && Device.GetOptionalExtensions().HasEXTFragmentDensityMap)
	{
		FVulkanTextureBase* Texture = FVulkanTextureBase::Cast(InRTInfo.FoveationTexture);
		FragmentDensityImage = Texture->Surface.Image;

		ensure(Texture->Surface.GetViewType() == VK_IMAGE_VIEW_TYPE_2D || Texture->Surface.GetViewType() == VK_IMAGE_VIEW_TYPE_2D_ARRAY);

		FVulkanTextureView RTView;
		RTView.Create(*Texture->Surface.Device, Texture->Surface.Image, Texture->Surface.GetViewType(), Texture->Surface.GetFullAspectMask(), Texture->Surface.PixelFormat, Texture->Surface.ViewFormat, MipIndex, 1, 0, Texture->Surface.GetNumberOfArrayLevels(), true);

		AttachmentTextureViews.Add(RTView);
		AttachmentViewsToDelete.Add(RTView.View);
	}

	TArray<VkImageView> AttachmentViews;
	AttachmentViews.Empty(AttachmentTextureViews.Num());
	for (auto& TextureView : AttachmentTextureViews)
	{
		AttachmentViews.Add(TextureView.View);
	}

	VkFramebufferCreateInfo CreateInfo;
	ZeroVulkanStruct(CreateInfo, VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);
	CreateInfo.renderPass = RenderPass.GetHandle();
	CreateInfo.attachmentCount = AttachmentViews.Num();
	CreateInfo.pAttachments = AttachmentViews.GetData();
	CreateInfo.width  = RTExtents.width;
	CreateInfo.height = RTExtents.height;
	CreateInfo.layers = NumLayers;
	VERIFYVULKANRESULT_EXPANDED(VulkanRHI::vkCreateFramebuffer(Device.GetInstanceHandle(), &CreateInfo, VULKAN_CPU_ALLOCATOR, &Framebuffer));

	Extents.width = CreateInfo.width;
	Extents.height = CreateInfo.height;

	INC_DWORD_STAT(STAT_VulkanNumFrameBuffers);
}

FVulkanFramebuffer::~FVulkanFramebuffer()
{
	ensure(Framebuffer == VK_NULL_HANDLE);
}

void FVulkanFramebuffer::Destroy(FVulkanDevice& Device)
{
	VulkanRHI::FDeferredDeletionQueue& Queue = Device.GetDeferredDeletionQueue();
	
	// will be deleted in reverse order
	Queue.EnqueueResource(VulkanRHI::FDeferredDeletionQueue::EType::Framebuffer, Framebuffer);
	Framebuffer = VK_NULL_HANDLE;

	for (int32 Index = 0; Index < AttachmentViewsToDelete.Num(); ++Index)
	{
		DEC_DWORD_STAT(STAT_VulkanNumImageViews);
		Queue.EnqueueResource(VulkanRHI::FDeferredDeletionQueue::EType::ImageView, AttachmentViewsToDelete[Index]);
	}

	DEC_DWORD_STAT(STAT_VulkanNumFrameBuffers);
}

bool FVulkanFramebuffer::Matches(const FRHISetRenderTargetsInfo& InRTInfo) const
{
	if (NumColorRenderTargets != InRTInfo.NumColorRenderTargets)
	{
		return false;
	}

	{
		const FRHIDepthRenderTargetView& B = InRTInfo.DepthStencilRenderTarget;
		if (B.Texture)
		{
			VkImage AImage = DepthStencilRenderTargetImage;
			VkImage BImage = ((FVulkanTextureBase*)B.Texture->GetTextureBaseRHI())->Surface.Image;
			if (AImage != BImage)
			{
				return false;
			}
		}
	}

	{
		FRHITexture* Texture = InRTInfo.FoveationTexture;
		if (Texture)
		{
			VkImage AImage = FragmentDensityImage;
			VkImage BImage = ((FVulkanTextureBase*)Texture->GetTextureBaseRHI())->Surface.Image;
			if (AImage != BImage)
			{
				return false;
			}
		}
	}

	int32 AttachementIndex = 0;
	for (int32 Index = 0; Index < InRTInfo.NumColorRenderTargets; ++Index)
	{
		if (InRTInfo.bHasResolveAttachments)
		{
			const FRHIRenderTargetView& R = InRTInfo.ColorResolveRenderTarget[Index];
			if (R.Texture)
			{
				VkImage AImage = ColorResolveTargetImages[AttachementIndex];
				VkImage BImage = ((FVulkanTextureBase*)R.Texture->GetTextureBaseRHI())->Surface.Image;
				if (AImage != BImage)
				{
					return false;
				}
			}
		}

		const FRHIRenderTargetView& B = InRTInfo.ColorRenderTarget[Index];
		if (B.Texture)
		{
			VkImage AImage = ColorRenderTargetImages[AttachementIndex];
			VkImage BImage = ((FVulkanTextureBase*)B.Texture->GetTextureBaseRHI())->Surface.Image;
			if (AImage != BImage)
			{
				return false;
			}
			AttachementIndex++;
		}
	}

	return true;
}

// Tear down and recreate swapchain and related resources.
void FVulkanViewport::RecreateSwapchain(void* NewNativeWindow)
{
	FScopeLock LockSwapchain(&RecreatingSwapchain);

	FVulkanSwapChainRecreateInfo RecreateInfo = { VK_NULL_HANDLE, VK_NULL_HANDLE };
	DestroySwapchain(&RecreateInfo);
	WindowHandle = NewNativeWindow;
	CreateSwapchain(&RecreateInfo);
	check(RecreateInfo.Surface == VK_NULL_HANDLE);
	check(RecreateInfo.SwapChain == VK_NULL_HANDLE);
}

void FVulkanViewport::Tick(float DeltaTime)
{
	check(IsInGameThread());

	if(SwapChain && FPlatformAtomics::AtomicRead(&LockToVsync) != SwapChain->DoesLockToVsync())
	{
		FlushRenderingCommands();
		ENQUEUE_RENDER_COMMAND(UpdateVsync)(
			[this](FRHICommandListImmediate& RHICmdList)
		{
			RecreateSwapchainFromRT(PixelFormat);
		});
		FlushRenderingCommands();
	}
}

void FVulkanViewport::Resize(uint32 InSizeX, uint32 InSizeY, bool bInIsFullscreen, EPixelFormat PreferredPixelFormat)
{
	SizeX = InSizeX;
	SizeY = InSizeY;
	bIsFullscreen = bInIsFullscreen;

	RecreateSwapchainFromRT(PreferredPixelFormat);
}

void FVulkanViewport::RecreateSwapchainFromRT(EPixelFormat PreferredPixelFormat)
{
	check(IsInRenderingThread());

	// TODO: should flush RHIT commands here?
	
	FVulkanSwapChainRecreateInfo RecreateInfo = { VK_NULL_HANDLE, VK_NULL_HANDLE};
	DestroySwapchain(&RecreateInfo);
	PixelFormat = PreferredPixelFormat;
	CreateSwapchain(&RecreateInfo);
	check(RecreateInfo.Surface == VK_NULL_HANDLE);
	check(RecreateInfo.SwapChain == VK_NULL_HANDLE);
}

void FVulkanViewport::CreateSwapchain(FVulkanSwapChainRecreateInfo* RecreateInfo)
{
	if (FVulkanPlatform::SupportsStandardSwapchain())
	{
		uint32 DesiredNumBackBuffers = NUM_BUFFERS;

		TArray<VkImage> Images;
		SwapChain = new FVulkanSwapChain(
			RHI->Instance, *Device, WindowHandle,
			PixelFormat, SizeX, SizeY, bIsFullscreen,
			&DesiredNumBackBuffers,
			Images,
			LockToVsync,
			RecreateInfo
		);

		checkf(Images.Num() >= NUM_BUFFERS, TEXT("We wanted at least %i images, actual Num: %i"), NUM_BUFFERS, Images.Num());
		BackBufferImages.SetNum(Images.Num());
		RenderingDoneSemaphores.SetNum(Images.Num());
		TextureViews.SetNum(Images.Num());

		FVulkanCmdBuffer* CmdBuffer = Device->GetImmediateContext().GetCommandBufferManager()->GetUploadCmdBuffer();
		ensure(CmdBuffer->IsOutsideRenderPass());

		for (int32 Index = 0; Index < Images.Num(); ++Index)
		{
			BackBufferImages[Index] = Images[Index];
			TextureViews[Index].Create(*Device, Images[Index], VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT, PixelFormat, UEToVkTextureFormat(PixelFormat, false), 0, 1, 0, 1);

			// Clear the swapchain to avoid a validation warning, and transition to ColorAttachment
			{
				VkImageSubresourceRange Range;
				FMemory::Memzero(Range);
				Range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				Range.baseMipLevel = 0;
				Range.levelCount = 1;
				Range.baseArrayLayer = 0;
				Range.layerCount = 1;

				VkClearColorValue Color;
				FMemory::Memzero(Color);
				VulkanRHI::ImagePipelineBarrier(CmdBuffer->GetHandle(), Images[Index], EImageLayoutBarrier::Undefined, EImageLayoutBarrier::TransferDest, Range);
				VulkanRHI::vkCmdClearColorImage(CmdBuffer->GetHandle(), Images[Index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &Color, 1, &Range);
				VulkanRHI::ImagePipelineBarrier(CmdBuffer->GetHandle(), Images[Index], EImageLayoutBarrier::TransferDest, EImageLayoutBarrier::ColorAttachment, Range);
			}

#if VULKAN_ENABLE_DRAW_MARKERS
			if (Device->GetDebugMarkerSetObjectName())
			{
				VulkanRHI::SetDebugMarkerName(Device->GetDebugMarkerSetObjectName(), Device->GetInstanceHandle(), BackBufferImages[Index], "RenderingBackBuffer");
			}
#endif
		}
		
		Device->GetImmediateContext().GetCommandBufferManager()->SubmitUploadCmdBuffer();

		RHIBackBuffer = new FVulkanBackBuffer(*Device, this, PixelFormat, SizeX, SizeY, TexCreate_RenderTargetable | TexCreate_ShaderResource);
	}
	else
	{
		PixelFormat = FVulkanPlatform::GetPixelFormatForNonDefaultSwapchain();
		if (RecreateInfo != nullptr)
		{
			if(RecreateInfo->SwapChain)
			{
				VulkanRHI::vkDestroySwapchainKHR(Device->GetInstanceHandle(), RecreateInfo->SwapChain, VULKAN_CPU_ALLOCATOR);
				RecreateInfo->SwapChain = VK_NULL_HANDLE;
			}
			if (RecreateInfo->Surface)
			{
				VulkanRHI::vkDestroySurfaceKHR(RHI->Instance, RecreateInfo->Surface, VULKAN_CPU_ALLOCATOR);
				RecreateInfo->Surface = VK_NULL_HANDLE;
			}
		}
	}

	if (!FVulkanPlatform::SupportsStandardSwapchain() || GVulkanDelayAcquireImage == EDelayAcquireImageType::DelayAcquire)
	{
		RenderingBackBuffer = new FVulkanTexture2D(*Device, PixelFormat, SizeX, SizeY, 1, 1, TexCreate_RenderTargetable | TexCreate_ShaderResource, FRHIResourceCreateInfo());
#if VULKAN_ENABLE_DRAW_MARKERS
		if (Device->GetDebugMarkerSetObjectName())
		{
			VulkanRHI::SetDebugMarkerName(Device->GetDebugMarkerSetObjectName(), Device->GetInstanceHandle(), RenderingBackBuffer->Surface.Image, "RenderingBackBuffer");
		}
#endif
	}

	AcquiredImageIndex = -1;
}

void FVulkanViewport::DestroySwapchain(FVulkanSwapChainRecreateInfo* RecreateInfo)
{
	// Submit all command buffers here
	Device->SubmitCommandsAndFlushGPU();
	Device->WaitUntilIdle();
	
	RenderingBackBuffer = nullptr;
	
	if (RHIBackBuffer)
	{
		RHIBackBuffer->ReleaseViewport();
		RHIBackBuffer = nullptr;
	}
		
	if (FVulkanPlatform::SupportsStandardSwapchain() && SwapChain)
	{
		for (int32 Index = 0, NumBuffers = BackBufferImages.Num(); Index < NumBuffers; ++Index)
		{
			TextureViews[Index].Destroy(*Device);
			Device->NotifyDeletedImage(BackBufferImages[Index]);
			Device->NotifyDeletedRenderTarget(BackBufferImages[Index]);
			BackBufferImages[Index] = VK_NULL_HANDLE;
		}
		
		Device->GetDeferredDeletionQueue().ReleaseResources(true);

		SwapChain->Destroy(RecreateInfo);
		delete SwapChain;
		SwapChain = nullptr;

		Device->GetDeferredDeletionQueue().ReleaseResources(true);
	}

	AcquiredImageIndex = -1;
}

inline static void CopyImageToBackBuffer(FVulkanCmdBuffer* CmdBuffer, bool bSourceReadOnly, VkImage SrcSurface, VkImage DstSurface, int32 SizeX, int32 SizeY, int32 WindowSizeX, int32 WindowSizeY)
{
	VulkanRHI::FPendingBarrier Barriers;
	int32 SourceIndex = Barriers.AddImageBarrier(SrcSurface, VK_IMAGE_ASPECT_COLOR_BIT, 1);
	int32 DestIndex = Barriers.AddImageBarrier(DstSurface, VK_IMAGE_ASPECT_COLOR_BIT, 1);

	// Prepare for copy
	Barriers.SetTransition(SourceIndex,
		bSourceReadOnly ? EImageLayoutBarrier::PixelShaderRead : EImageLayoutBarrier::ColorAttachment,
		EImageLayoutBarrier::TransferSource);
	Barriers.SetTransition(DestIndex, EImageLayoutBarrier::Undefined, EImageLayoutBarrier::TransferDest);
	Barriers.Execute(CmdBuffer);

	VulkanRHI::DebugHeavyWeightBarrier(CmdBuffer->GetHandle(), 32);

	if (SizeX != WindowSizeX || SizeY != WindowSizeY)
	{
		VkImageBlit Region;
		FMemory::Memzero(Region);
		Region.srcOffsets[0].x = 0;
		Region.srcOffsets[0].y = 0;
		Region.srcOffsets[0].z = 0;
		Region.srcOffsets[1].x = SizeX;
		Region.srcOffsets[1].y = SizeY;
		Region.srcOffsets[1].z = 1;
		Region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		Region.srcSubresource.layerCount = 1;
		Region.dstOffsets[0].x = 0;
		Region.dstOffsets[0].y = 0;
		Region.dstOffsets[0].z = 0;
		Region.dstOffsets[1].x = WindowSizeX;
		Region.dstOffsets[1].y = WindowSizeY;
		Region.dstOffsets[1].z = 1;
		Region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		Region.dstSubresource.baseArrayLayer = 0;
		Region.dstSubresource.layerCount = 1;
		VulkanRHI::vkCmdBlitImage(CmdBuffer->GetHandle(),
			SrcSurface, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			DstSurface, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &Region, VK_FILTER_LINEAR);
	}
	else
	{
		VkImageCopy Region;
		FMemory::Memzero(Region);
		Region.extent.width = SizeX;
		Region.extent.height = SizeY;
		Region.extent.depth = 1;
		Region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		//Region.srcSubresource.baseArrayLayer = 0;
		Region.srcSubresource.layerCount = 1;
		//Region.srcSubresource.mipLevel = 0;
		Region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		//Region.dstSubresource.baseArrayLayer = 0;
		Region.dstSubresource.layerCount = 1;
		//Region.dstSubresource.mipLevel = 0;
		VulkanRHI::vkCmdCopyImage(CmdBuffer->GetHandle(),
			SrcSurface, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			DstSurface, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &Region);
	}

	// Prepare for present
	Barriers.ResetStages();
	Barriers.SetTransition(SourceIndex,
		EImageLayoutBarrier::TransferSource,
		bSourceReadOnly ? EImageLayoutBarrier::PixelShaderRead : EImageLayoutBarrier::ColorAttachment);
	Barriers.SetTransition(DestIndex, EImageLayoutBarrier::TransferDest, EImageLayoutBarrier::Present);
	Barriers.Execute(CmdBuffer);
}

bool FVulkanViewport::Present(FVulkanCommandListContext* Context, FVulkanCmdBuffer* CmdBuffer, FVulkanQueue* Queue, FVulkanQueue* PresentQueue, bool bLockToVsync)
{
	FPlatformAtomics::AtomicStore(&LockToVsync, bLockToVsync ? 1 : 0);
	bool bFailedToDelayAcquireBackbuffer = false;

	//Transition back buffer to presentable and submit that command
	check(CmdBuffer->IsOutsideRenderPass());

	if (FVulkanPlatform::SupportsStandardSwapchain())
	{
		if (GVulkanDelayAcquireImage == EDelayAcquireImageType::DelayAcquire && RenderingBackBuffer)
		{
			SCOPE_CYCLE_COUNTER(STAT_VulkanAcquireBackBuffer);
			// swapchain can go out of date, do not crash at this point
			if (LIKELY(TryAcquireImageIndex()))
			{
				uint32 WindowSizeX = FMath::Min(SizeX, SwapChain->InternalWidth);
				uint32 WindowSizeY = FMath::Min(SizeY, SwapChain->InternalHeight);

				Context->RHIPushEvent(TEXT("CopyImageToBackBuffer"), FColor::Blue);
				CopyImageToBackBuffer(CmdBuffer, true, RenderingBackBuffer->Surface.Image, BackBufferImages[AcquiredImageIndex], SizeX, SizeY, WindowSizeX, WindowSizeY);
				Context->RHIPopEvent();
			}
			else
			{
				bFailedToDelayAcquireBackbuffer = true;
			}
		}
		else
		{
			check(AcquiredImageIndex != -1);
			check(RHIBackBuffer != nullptr && RHIBackBuffer->Surface.Image == BackBufferImages[AcquiredImageIndex]);

			VkImageLayout& Layout = Context->GetTransitionAndLayoutManager().FindOrAddLayoutRW(BackBufferImages[AcquiredImageIndex], VK_IMAGE_LAYOUT_UNDEFINED);
			VulkanRHI::ImagePipelineBarrier(CmdBuffer->GetHandle(), BackBufferImages[AcquiredImageIndex], VulkanRHI::GetImageLayoutFromVulkanLayout(Layout), EImageLayoutBarrier::Present, VulkanRHI::SetupImageSubresourceRange());
			Layout = VK_IMAGE_LAYOUT_UNDEFINED;
		}
	}

	CmdBuffer->End();
	GVulkanCommandBufferManager->FlushResetQueryPools();
	if (FVulkanPlatform::SupportsStandardSwapchain())
	{
		if (LIKELY(!bFailedToDelayAcquireBackbuffer))
		{
			if (GVulkanDelayAcquireImage == EDelayAcquireImageType::DelayAcquire)
			{
				CmdBuffer->AddWaitSemaphore(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, AcquiredSemaphore);
			}
			Queue->Submit(CmdBuffer, RenderingDoneSemaphores[AcquiredImageIndex]->GetHandle());
		}
		else
		{
			// failing to do the delayacquire can only happen if we were in this mode to begin with
			check(GVulkanDelayAcquireImage == EDelayAcquireImageType::DelayAcquire);

			UE_LOG(LogVulkanRHI, Log, TEXT("AcquireNextImage() failed due to the outdated swapchain, not even attempting to present."));

			// cannot just throw out this command buffer (needs to be submitted or other checks fail)
			Queue->Submit(CmdBuffer);
			RecreateSwapchain(WindowHandle);

			// Swapchain creation pushes some commands - flush the command buffers now to begin with a fresh state
			Device->SubmitCommandsAndFlushGPU();
			Device->WaitUntilIdle();

			// early exit
			return (int32)FVulkanSwapChain::EStatus::Healthy;
		}
	}
	else
	{
		Queue->Submit(CmdBuffer);
	}

	//Flush all commands
	//check(0);

	//#todo-rco: Proper SyncInterval bLockToVsync ? RHIConsoleVariables::SyncInterval : 0
	int32 SyncInterval = 0;
	bool bNeedNativePresent = true;

	const bool bHasCustomPresent = IsValidRef(CustomPresent);
	if (bHasCustomPresent)
	{
		SCOPE_CYCLE_COUNTER(STAT_VulkanCustomPresentTime);
		bNeedNativePresent = CustomPresent->Present(SyncInterval);
	}

	bool bResult = false;
	if (bNeedNativePresent && (!FVulkanPlatform::SupportsStandardSwapchain() || GVulkanDelayAcquireImage == EDelayAcquireImageType::DelayAcquire || RHIBackBuffer != nullptr))
	{
		// Present the back buffer to the viewport window.
		auto SwapChainJob = [Queue, PresentQueue](FVulkanViewport* Viewport)
		{
			// May happend if swapchain was recreated in DoCheckedSwapChainJob()
			if (Viewport->AcquiredImageIndex == -1)
			{
				// Skip present silently if image has not been acquired
				return (int32)FVulkanSwapChain::EStatus::Healthy;
			}
			return (int32)Viewport->SwapChain->Present(Queue, PresentQueue, Viewport->RenderingDoneSemaphores[Viewport->AcquiredImageIndex]);
		};
		if (FVulkanPlatform::SupportsStandardSwapchain() && !DoCheckedSwapChainJob(SwapChainJob))
		{
			UE_LOG(LogVulkanRHI, Fatal, TEXT("Swapchain present failed!"));
			bResult = false;
		}
		else
		{
			bResult = true;
		}

		if (bHasCustomPresent)
		{
			CustomPresent->PostPresent();
		}
	}

	if (FVulkanPlatform::RequiresWaitingForFrameCompletionEvent() && !bHasCustomPresent)
	{
		// Wait for the GPU to finish rendering the previous frame before finishing this frame.
		WaitForFrameEventCompletion();
		IssueFrameEvent();
	}

	// If the input latency timer has been triggered, block until the GPU is completely
	// finished displaying this frame and calculate the delta time.
	//if (GInputLatencyTimer.RenderThreadTrigger)
	//{
	//	WaitForFrameEventCompletion();
	//	uint32 EndTime = FPlatformTime::Cycles();
	//	GInputLatencyTimer.DeltaTime = EndTime - GInputLatencyTimer.StartTime;
	//	GInputLatencyTimer.RenderThreadTrigger = false;
	//}

	FVulkanCommandBufferManager* ImmediateCmdBufMgr = Device->GetImmediateContext().GetCommandBufferManager();
	// PrepareForNewActiveCommandBuffer might be called by swapchain re-creation routine. Skip prepare if we already have an open active buffer.
	if (ImmediateCmdBufMgr->GetActiveCmdBuffer() && !ImmediateCmdBufMgr->GetActiveCmdBuffer()->HasBegun())
	{
		ImmediateCmdBufMgr->PrepareForNewActiveCommandBuffer();
	}

	AcquiredImageIndex = -1;

	++PresentCount;
	++GVulkanRHI->TotalPresentCount;

	return bResult;
}

/*=============================================================================
 *	The following RHI functions must be called from the main thread.
 *=============================================================================*/
FViewportRHIRef FVulkanDynamicRHI::RHICreateViewport(void* WindowHandle, uint32 SizeX, uint32 SizeY, bool bIsFullscreen, EPixelFormat PreferredPixelFormat)
{
	check( IsInGameThread() );

	// Use a default pixel format if none was specified	
	if (PreferredPixelFormat == PF_Unknown)
	{
		static const auto* CVarDefaultBackBufferPixelFormat = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DefaultBackBufferPixelFormat"));
		PreferredPixelFormat = EDefaultBackBufferPixelFormat::Convert2PixelFormat(EDefaultBackBufferPixelFormat::FromInt(CVarDefaultBackBufferPixelFormat->GetValueOnAnyThread()));
	}

	return new FVulkanViewport(this, Device, WindowHandle, SizeX, SizeY, bIsFullscreen, PreferredPixelFormat);
}

void FVulkanDynamicRHI::RHIResizeViewport(FRHIViewport* ViewportRHI, uint32 SizeX, uint32 SizeY, bool bIsFullscreen, EPixelFormat PreferredPixelFormat)
{
	check(IsInGameThread());
	FVulkanViewport* Viewport = ResourceCast(ViewportRHI);

	// Use a default pixel format if none was specified	
	if (PreferredPixelFormat == PF_Unknown)
	{
		static const auto* CVarDefaultBackBufferPixelFormat = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DefaultBackBufferPixelFormat"));
		PreferredPixelFormat = EDefaultBackBufferPixelFormat::Convert2PixelFormat(EDefaultBackBufferPixelFormat::FromInt(CVarDefaultBackBufferPixelFormat->GetValueOnAnyThread()));
	}

	if (Viewport->GetSizeXY() != FIntPoint(SizeX, SizeY) || Viewport->IsFullscreen() != bIsFullscreen)
	{
		FlushRenderingCommands();

		ENQUEUE_RENDER_COMMAND(ResizeViewport)(
			[Viewport, SizeX, SizeY, bIsFullscreen, PreferredPixelFormat](FRHICommandListImmediate& RHICmdList)
			{
				Viewport->Resize(SizeX, SizeY, bIsFullscreen, PreferredPixelFormat);
			});
		FlushRenderingCommands();
	}
}

void FVulkanDynamicRHI::RHIResizeViewport(FRHIViewport* ViewportRHI, uint32 SizeX, uint32 SizeY, bool bIsFullscreen)
{
	check(IsInGameThread());
	FVulkanViewport* Viewport = ResourceCast(ViewportRHI);

	if (Viewport->GetSizeXY() != FIntPoint(SizeX, SizeY))
	{
		FlushRenderingCommands();

		ENQUEUE_RENDER_COMMAND(ResizeViewport)(
			[Viewport, SizeX, SizeY, bIsFullscreen](FRHICommandListImmediate& RHICmdList)
			{
				Viewport->Resize(SizeX, SizeY, bIsFullscreen, PF_Unknown);
			});
		FlushRenderingCommands();
	}
}

void FVulkanDynamicRHI::RHITick(float DeltaTime)
{
	check(IsInGameThread());
	FVulkanDevice* VulkanDevice = GetDevice();
	static bool bRequestNULLPixelShader = true;
	bool bRequested = bRequestNULLPixelShader;
	ENQUEUE_RENDER_COMMAND(TempFrameReset)(
		[VulkanDevice, bRequested](FRHICommandListImmediate& RHICmdList)
		{
			if (bRequested)
			{
				//work around layering violation
				TShaderMapRef<FNULLPS>(GetGlobalShaderMap(GMaxRHIFeatureLevel))->GetPixelShader();
			}

			VulkanDevice->GetImmediateContext().GetTempFrameAllocationBuffer().Reset();

			// Destroy command buffers here when using Delay; when not delaying we'll delete after Acquire
			if (GVulkanDelayAcquireImage == EDelayAcquireImageType::DelayAcquire)
			{
				VulkanDevice->GetImmediateContext().GetCommandBufferManager()->FreeUnusedCmdBuffers();
			}
		});

	if (bRequestNULLPixelShader)
	{
		bRequestNULLPixelShader = false;
	}
}

FTexture2DRHIRef FVulkanDynamicRHI::RHIGetViewportBackBuffer(FRHIViewport* ViewportRHI)
{
	check(IsInRenderingThread());
	check(ViewportRHI);
	FVulkanViewport* Viewport = ResourceCast(ViewportRHI);

	if (Viewport->SwapChain)
	{
		Viewport->SwapChain->RenderThreadPacing();
	}

	return Viewport->GetBackBuffer(FRHICommandListExecutor::GetImmediateCommandList());
}

void FVulkanDynamicRHI::RHIAdvanceFrameForGetViewportBackBuffer(FRHIViewport* ViewportRHI)
{
	check(IsInRenderingThread());
	check(ViewportRHI);
	FVulkanViewport* Viewport = ResourceCast(ViewportRHI);
	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

	Viewport->AdvanceBackBufferFrame(RHICmdList);

	if (RHICmdList.Bypass() || !IsRunningRHIInSeparateThread())
	{
		FRHICommandProcessDeferredDeletionQueue Cmd(Device);
		Cmd.Execute(RHICmdList);
	}
	else
	{
		check(IsInRenderingThread());
		ALLOC_COMMAND_CL(RHICmdList, FRHICommandProcessDeferredDeletionQueue)(Device);
	}
}

void FVulkanCommandListContext::RHISetViewport(float MinX, float MinY, float MinZ, float MaxX, float MaxY, float MaxZ)
{
	PendingGfxState->SetViewport(MinX, MinY, MinZ, MaxX, MaxY, MaxZ);
}

void FVulkanCommandListContext::RHISetMultipleViewports(uint32 Count, const FViewportBounds* Data)
{
	VULKAN_SIGNAL_UNIMPLEMENTED();
}

void FVulkanCommandListContext::RHISetScissorRect(bool bEnable, uint32 MinX, uint32 MinY, uint32 MaxX, uint32 MaxY)
{
	PendingGfxState->SetScissor(bEnable, MinX, MinY, MaxX, MaxY);
}
