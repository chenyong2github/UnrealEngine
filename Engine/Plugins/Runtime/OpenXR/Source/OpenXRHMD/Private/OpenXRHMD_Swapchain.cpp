// Copyright Epic Games, Inc. All Rights Reserved.

#include "OpenXRHMD_Swapchain.h"
#include "OpenXRCore.h"
#include "XRThreadUtils.h"

static TAutoConsoleVariable<int32> CVarOpenXRSwapchainRetryCount(
	TEXT("vr.OpenXRSwapchainRetryCount"),
	9,
	TEXT("Number of times the OpenXR plugin will attempt to wait for the next swapchain image."),
	ECVF_RenderThreadSafe);

FOpenXRSwapchain::FOpenXRSwapchain(TArray<FTextureRHIRef>&& InRHITextureSwapChain, const FTextureRHIRef & InRHITexture, XrSwapchain InHandle)
	: FXRSwapChain(MoveTemp(InRHITextureSwapChain), InRHITexture)
	, Handle(InHandle)
	, Acquired(false)
{
}

FOpenXRSwapchain::~FOpenXRSwapchain() 
{
	XR_ENSURE(xrDestroySwapchain(Handle));
}

// TODO: This function should be renamed to IncrementSwapChainIndex_RenderThread.
// Name change is currently blocked on runtimes still requiring this on the RHI thread.
void FOpenXRSwapchain::IncrementSwapChainIndex_RHIThread()
{
	check(IsInRenderingThread() || IsInRHIThread());

	if (Acquired)
	{
		return;
	}

	SCOPED_NAMED_EVENT(AcquireImage, FColor::Red);

	XrSwapchainImageAcquireInfo Info;
	Info.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;
	Info.next = nullptr;
	XR_ENSURE(xrAcquireSwapchainImage(Handle, &Info, &SwapChainIndex_RHIThread));

	GDynamicRHI->RHIAliasTextureResources((FTextureRHIRef&)RHITexture, (FTextureRHIRef&)RHITextureSwapChain[SwapChainIndex_RHIThread]);
	Acquired = true;
}

void FOpenXRSwapchain::WaitCurrentImage_RHIThread(int64 Timeout)
{
	check(IsInRenderingThread() || IsInRHIThread());

	SCOPED_NAMED_EVENT(WaitImage, FColor::Red);

	XrSwapchainImageWaitInfo WaitInfo;
	WaitInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
	WaitInfo.next = nullptr;
	WaitInfo.timeout = Timeout;

	XrResult WaitResult = XR_SUCCESS;
	int RetryCount = CVarOpenXRSwapchainRetryCount.GetValueOnAnyThread();
	do
	{
		XR_ENSURE(WaitResult = xrWaitSwapchainImage(Handle, &WaitInfo));
		if (WaitResult == XR_TIMEOUT_EXPIRED) //-V547
		{
			UE_LOG(LogHMD, Warning, TEXT("Timed out waiting on swapchain image %u! Attempts remaining %d."), SwapChainIndex_RHIThread, RetryCount);
		}
	} while (WaitResult == XR_TIMEOUT_EXPIRED && RetryCount-- > 0);

	if (WaitResult != XR_SUCCESS) //-V547
	{
		// We can't continue without acquiring a new swapchain image since we won't have an image available to render to.
		UE_LOG(LogHMD, Fatal, TEXT("Failed to wait on acquired swapchain image. This usually indicates a problem with the OpenXR runtime."));
	}
}

void FOpenXRSwapchain::ReleaseCurrentImage_RHIThread()
{
	check(IsInRenderingThread() || IsInRHIThread());

	if (!Acquired)
	{
		return;
	}

	SCOPED_NAMED_EVENT(ReleaseImage, FColor::Red);

	XrSwapchainImageReleaseInfo ReleaseInfo;
	ReleaseInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
	ReleaseInfo.next = nullptr;
	XR_ENSURE(xrReleaseSwapchainImage(Handle, &ReleaseInfo));

	Acquired = false;
}

uint8 FOpenXRSwapchain::GetNearestSupportedSwapchainFormat(XrSession InSession, uint8 RequestedFormat, TFunction<uint32(uint8)> ToPlatformFormat /*= nullptr*/)
{
	if (!ToPlatformFormat)
	{
		ToPlatformFormat = [](uint8 InFormat) { return GPixelFormats[InFormat].PlatformFormat; };
	}

	uint32_t FormatsCount = 0;
	XR_ENSURE(xrEnumerateSwapchainFormats(InSession, 0, &FormatsCount, nullptr));

	TArray<int64_t> Formats;
	Formats.SetNum(FormatsCount);
	XR_ENSURE(xrEnumerateSwapchainFormats(InSession, (uint32_t)Formats.Num(), &FormatsCount, Formats.GetData()));
	ensure(FormatsCount == Formats.Num());

	// Return immediately if the runtime supports the exact format being requested.
	uint32 PlatformFormat = ToPlatformFormat(RequestedFormat);
	if (Formats.Contains(PlatformFormat))
	{
		return RequestedFormat;
	}

	// Search for a fallback format in order of preference (first element in the array has the highest preference).
	uint8 FallbackFormat = 0;
	uint32 FallbackPlatformFormat = 0;
	for (int64_t Format : Formats)
	{
		if (RequestedFormat == PF_DepthStencil)
		{
			if (Format == ToPlatformFormat(PF_D24))
			{
				FallbackFormat = PF_D24;
				FallbackPlatformFormat = Format;
				break;
			}
		}
		else
		{
			if (Format == ToPlatformFormat(PF_B8G8R8A8))
			{
				FallbackFormat = PF_B8G8R8A8;
				FallbackPlatformFormat = Format;
				break;
			}
			else if (Format == ToPlatformFormat(PF_R8G8B8A8))
			{
				FallbackFormat = PF_R8G8B8A8;
				FallbackPlatformFormat = Format;
				break;
			}
		}
	}

	if (!FallbackFormat)
	{
		UE_LOG(LogHMD, Warning, TEXT("No compatible swapchain format found!"));
		return PF_Unknown;
	}

	UE_LOG(LogHMD, Warning, TEXT("Swapchain format not supported (%d), falling back to runtime preferred format (%d)."), PlatformFormat, FallbackPlatformFormat);
	return FallbackFormat;
}

XrSwapchain FOpenXRSwapchain::CreateSwapchain(XrSession InSession, uint32 PlatformFormat, uint32 SizeX, uint32 SizeY, uint32 ArraySize, uint32 NumMips, uint32 NumSamples, ETextureCreateFlags Flags, ETextureCreateFlags TargetableTextureFlags)
{
	// Need a mutable format so we can reinterpret an sRGB format into a linear format
	XrSwapchainUsageFlags Usage = XR_SWAPCHAIN_USAGE_MUTABLE_FORMAT_BIT;
	if (TargetableTextureFlags & TexCreate_RenderTargetable)
	{
		Usage |= XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
	}
	if (TargetableTextureFlags & TexCreate_DepthStencilTargetable)
	{
		Usage |= XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	}
	if (TargetableTextureFlags & TexCreate_ShaderResource)
	{
		Usage |= XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
	}
	if (TargetableTextureFlags & TexCreate_UAV)
	{
		Usage |= XR_SWAPCHAIN_USAGE_UNORDERED_ACCESS_BIT;
	}

	XrSwapchain Swapchain;
	XrSwapchainCreateInfo info;
	info.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
	info.next = nullptr;
	info.createFlags = Flags & TexCreate_Dynamic ? 0 : XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT;
	info.usageFlags = Usage;
	info.format = PlatformFormat;
	info.sampleCount = NumSamples;
	info.width = SizeX;
	info.height = SizeY;
	info.faceCount = 1;
	info.arraySize = ArraySize;
	info.mipCount = NumMips;
	if (!XR_ENSURE(xrCreateSwapchain(InSession, &info, &Swapchain)))
	{
		return XR_NULL_HANDLE;
	}
	return Swapchain;
}

template<typename T>
TArray<T> EnumerateImages(XrSwapchain InSwapchain, XrStructureType InType)
{
	TArray<T> Images;
	uint32_t ChainCount;
	xrEnumerateSwapchainImages(InSwapchain, 0, &ChainCount, nullptr);
	Images.AddZeroed(ChainCount);
	for (auto& Image : Images)
	{
		Image.type = InType;
	}
	XR_ENSURE(xrEnumerateSwapchainImages(InSwapchain, ChainCount, &ChainCount, reinterpret_cast<XrSwapchainImageBaseHeader*>(Images.GetData())));
	return Images;
}

#ifdef XR_USE_GRAPHICS_API_D3D11
FXRSwapChainPtr CreateSwapchain_D3D11(XrSession InSession, uint8 Format, uint32 SizeX, uint32 SizeY, uint32 ArraySize, uint32 NumMips, uint32 NumSamples, ETextureCreateFlags Flags, ETextureCreateFlags TargetableTextureFlags, const FClearValueBinding& ClearValueBinding)
{
	TFunction<uint32(uint8)> ToPlatformFormat = [](uint8 InFormat)
	{
		// We need to convert typeless to typed formats to create a swapchain
		DXGI_FORMAT PlatformFormat = (DXGI_FORMAT)GPixelFormats[InFormat].PlatformFormat;
		PlatformFormat = FindDepthStencilDXGIFormat(PlatformFormat);

		// UE4 renders a gamma-corrected image so we need to use an sRGB format if available
		PlatformFormat = FindShaderResourceDXGIFormat(PlatformFormat, true);
		return PlatformFormat;
	};

	Format = FOpenXRSwapchain::GetNearestSupportedSwapchainFormat(InSession, Format, ToPlatformFormat);
	if (!Format)
	{
		return nullptr;
	}

	XrSwapchain Swapchain = FOpenXRSwapchain::CreateSwapchain(InSession, ToPlatformFormat(Format), SizeX, SizeY, ArraySize, NumMips, NumSamples, Flags, TargetableTextureFlags);
	if (!Swapchain)
	{
		return nullptr;
	}

	TArray<FTextureRHIRef> TextureChain;
	TArray<XrSwapchainImageD3D11KHR> Images = EnumerateImages<XrSwapchainImageD3D11KHR>(Swapchain, XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR);
	for (const auto& Image : Images)
	{
		TextureChain.Add(static_cast<FTextureRHIRef>(GD3D11RHI->RHICreateTexture2DArrayFromResource(GPixelFormats[Format].UnrealFormat, TargetableTextureFlags, ClearValueBinding, Image.texture)));
	}
	FTextureRHIRef ChainTarget = static_cast<FTextureRHIRef>(GDynamicRHI->RHICreateAliasedTexture((FTextureRHIRef&)TextureChain[0]));

	return CreateXRSwapChain<FOpenXRSwapchain>(MoveTemp(TextureChain), ChainTarget, Swapchain);
}
#endif

#ifdef XR_USE_GRAPHICS_API_D3D12
FXRSwapChainPtr CreateSwapchain_D3D12(XrSession InSession, uint8 Format, uint32 SizeX, uint32 SizeY, uint32 ArraySize, uint32 NumMips, uint32 NumSamples, ETextureCreateFlags Flags, ETextureCreateFlags TargetableTextureFlags, const FClearValueBinding& ClearValueBinding)
{
	TFunction<uint32(uint8)> ToPlatformFormat = [](uint8 InFormat)
	{
		// We need to convert typeless to typed formats to create a swapchain
		DXGI_FORMAT PlatformFormat = (DXGI_FORMAT)GPixelFormats[InFormat].PlatformFormat;
		PlatformFormat = FindDepthStencilDXGIFormat_D3D12(PlatformFormat);

		// UE4 renders a gamma-corrected image so we need to use an sRGB format if available
		PlatformFormat = FindShaderResourceDXGIFormat_D3D12(PlatformFormat, true);
		return PlatformFormat;
	};

	Format = FOpenXRSwapchain::GetNearestSupportedSwapchainFormat(InSession, Format, ToPlatformFormat);
	if (!Format)
	{
		return nullptr;
	}

	XrSwapchain Swapchain = FOpenXRSwapchain::CreateSwapchain(InSession, ToPlatformFormat(Format), SizeX, SizeY, ArraySize, NumMips, NumSamples, Flags, TargetableTextureFlags);
	if (!Swapchain)
	{
		return nullptr;
	}

	FD3D12DynamicRHI* DynamicRHI = FD3D12DynamicRHI::GetD3DRHI();
	TArray<FTextureRHIRef> TextureChain;
	TArray<XrSwapchainImageD3D12KHR> Images = EnumerateImages<XrSwapchainImageD3D12KHR>(Swapchain, XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR);
	for (const auto& Image : Images)
	{
		TextureChain.Add(static_cast<FTextureRHIRef>(DynamicRHI->RHICreateTexture2DArrayFromResource(GPixelFormats[Format].UnrealFormat, TargetableTextureFlags, ClearValueBinding, Image.texture)));
	}
	FTextureRHIRef ChainTarget = static_cast<FTextureRHIRef>(GDynamicRHI->RHICreateAliasedTexture((FTextureRHIRef&)TextureChain[0]));

	return CreateXRSwapChain<FOpenXRSwapchain>(MoveTemp(TextureChain), ChainTarget, Swapchain);
}
#endif

#ifdef XR_USE_GRAPHICS_API_OPENGL
FXRSwapChainPtr CreateSwapchain_OpenGL(XrSession InSession, uint8 Format, uint32 SizeX, uint32 SizeY, uint32 ArraySize, uint32 NumMips, uint32 NumSamples, ETextureCreateFlags Flags, ETextureCreateFlags TargetableTextureFlags, const FClearValueBinding& ClearValueBinding)
{
	Format = FOpenXRSwapchain::GetNearestSupportedSwapchainFormat(InSession, Format);
	if (!Format)
	{
		return nullptr;
	}

	XrSwapchain Swapchain = FOpenXRSwapchain::CreateSwapchain(InSession, GPixelFormats[Format].PlatformFormat, SizeX, SizeY, ArraySize, NumMips, NumSamples, Flags, TargetableTextureFlags);
	if (!Swapchain)
	{
		return nullptr;
	}

	TArray<FTextureRHIRef> TextureChain;
	FOpenGLDynamicRHI* DynamicRHI = static_cast<FOpenGLDynamicRHI*>(GDynamicRHI);
	TArray<XrSwapchainImageOpenGLKHR> Images = EnumerateImages<XrSwapchainImageOpenGLKHR>(Swapchain, XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR);
	for (const auto& Image : Images)
	{
		TextureChain.Add(static_cast<FTextureRHIRef>(DynamicRHI->RHICreateTexture2DArrayFromResource(GPixelFormats[Format].UnrealFormat, SizeX, SizeY, ArraySize, NumMips, NumSamples, 1, ClearValueBinding, Image.image, TargetableTextureFlags)));
	}
	FTextureRHIRef ChainTarget = static_cast<FTextureRHIRef>(GDynamicRHI->RHICreateAliasedTexture((FTextureRHIRef&)TextureChain[0]));

	return CreateXRSwapChain<FOpenXRSwapchain>(MoveTemp(TextureChain), ChainTarget, Swapchain);
}
#endif

#ifdef XR_USE_GRAPHICS_API_VULKAN
FXRSwapChainPtr CreateSwapchain_Vulkan(XrSession InSession, uint8 Format, uint32 SizeX, uint32 SizeY, uint32 ArraySize, uint32 NumMips, uint32 NumSamples, ETextureCreateFlags Flags, ETextureCreateFlags TargetableTextureFlags, const FClearValueBinding& ClearValueBinding)
{
	TFunction<uint32(uint8)> ToPlatformFormat = [](uint8 InFormat)
	{
		// UE4 renders a gamma-corrected image so we need to use an sRGB format if available
		return UEToVkTextureFormat(GPixelFormats[InFormat].UnrealFormat, true);
	};
	Format = FOpenXRSwapchain::GetNearestSupportedSwapchainFormat(InSession, Format, ToPlatformFormat);
	if (!Format)
	{
		return nullptr;
	}

	XrSwapchain Swapchain = FOpenXRSwapchain::CreateSwapchain(InSession, ToPlatformFormat(Format), SizeX, SizeY, ArraySize, NumMips, NumSamples, Flags, TargetableTextureFlags);
	if (!Swapchain)
	{
		return nullptr;
	}

	TArray<FTextureRHIRef> TextureChain;
	TArray<XrSwapchainImageVulkanKHR> Images = EnumerateImages<XrSwapchainImageVulkanKHR>(Swapchain, XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR);
	for (const auto& Image : Images)
	{
		TextureChain.Add(static_cast<FTextureRHIRef>(GVulkanRHI->RHICreateTexture2DArrayFromResource(GPixelFormats[Format].UnrealFormat, SizeX, SizeY, ArraySize, NumMips, NumSamples, Image.image, TargetableTextureFlags)));
	}
	FTextureRHIRef ChainTarget = static_cast<FTextureRHIRef>(GDynamicRHI->RHICreateAliasedTexture((FTextureRHIRef&)TextureChain[0]));

	return CreateXRSwapChain<FOpenXRSwapchain>(MoveTemp(TextureChain), ChainTarget, Swapchain);
}
#endif
