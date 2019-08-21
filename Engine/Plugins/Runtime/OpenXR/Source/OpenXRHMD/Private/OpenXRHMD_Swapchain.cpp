// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "OpenXRHMD_Swapchain.h"
#include "OpenXRHMDPrivate.h"
#include "OpenXRHMDPrivateRHI.h"

FOpenXRSwapchain::FOpenXRSwapchain(TArray<FTextureRHIRef>&& InRHITextureSwapChain, const FTextureRHIRef & InRHITexture, XrSwapchain InHandle) :
	FXRSwapChain(MoveTemp(InRHITextureSwapChain), InRHITexture),
	Handle(InHandle), 
	IsAcquired(false)
	
{
	IncrementSwapChainIndex_RHIThread((int64)XR_NO_DURATION);
}

void FOpenXRSwapchain::IncrementSwapChainIndex_RHIThread(int64 Timeout)
{
	check(IsInRenderingThread());

	if (IsAcquired)
		return;
	
	XrSwapchainImageAcquireInfo Info;
	Info.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;
	Info.next = nullptr;
	XR_ENSURE(xrAcquireSwapchainImage(Handle, &Info, &SwapChainIndex_RHIThread));

	IsAcquired = true;

	XrSwapchainImageWaitInfo WaitInfo;
	WaitInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
	WaitInfo.next = nullptr;
	WaitInfo.timeout = Timeout;
	XR_ENSURE(xrWaitSwapchainImage(Handle, &WaitInfo));

	GDynamicRHI->RHIAliasTextureResources(RHITexture, RHITextureSwapChain[SwapChainIndex_RHIThread]);
}

void FOpenXRSwapchain::ReleaseCurrentImage_RHIThread()
{
	check(IsInRenderingThread() || IsInRHIThread());

	if (!IsAcquired)
		return;

	XrSwapchainImageReleaseInfo ReleaseInfo;
	ReleaseInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
	ReleaseInfo.next = nullptr;
	XR_ENSURE(xrReleaseSwapchainImage(Handle, &ReleaseInfo));

	IsAcquired = false;
}

void FOpenXRSwapchain::ReleaseResources_RHIThread()
{
	FXRSwapChain::ReleaseResources_RHIThread();
	xrDestroySwapchain(Handle);
}

uint8 GetNearestSupportedSwapchainFormat(XrSession InSession, uint8 RequestedFormat, TFunction<uint32(uint8)> ToPlatformFormat = nullptr)
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

	// Search for an 8bpc fallback format in order of preference (first element in the array has the high preference).
	uint8 FallbackFormat = 0;
	uint32 FallbackPlatformFormat = 0;
	for (int64_t Format : Formats)
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

	if (!FallbackFormat)
	{
		UE_LOG(LogHMD, Warning, TEXT("No compatible swapchain format found!"));
		return PF_Unknown;
	}

	UE_LOG(LogHMD, Warning, TEXT("Swapchain format not supported (%d), falling back to runtime preferred format (%d)."), PlatformFormat, FallbackPlatformFormat);
	return FallbackFormat;
}

XrSwapchain CreateSwapchain(XrSession InSession, uint32 PlatformFormat, uint32 SizeX, uint32 SizeY, uint32 NumMips, uint32 NumSamples, uint32 TargetableTextureFlags)
{
	XrSwapchainUsageFlags Usage = 0;
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
	info.createFlags = 0;
	info.usageFlags = Usage;
	info.format = PlatformFormat;
	info.sampleCount = NumSamples;
	info.width = SizeX;
	info.height = SizeY;
	info.faceCount = 1;
	info.arraySize = 1;
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
FXRSwapChainPtr CreateSwapchain_D3D11(XrSession InSession, uint8 Format, uint32 SizeX, uint32 SizeY, uint32 NumMips, uint32 NumSamples, uint32 Flags, uint32 TargetableTextureFlags)
{
	TFunction<uint32(uint8)> ToPlatformFormat = [Flags](uint8 InFormat)
	{
		// We need to convert typeless to typed formats to create a swapchain
		DXGI_FORMAT PlatformFormat = (DXGI_FORMAT)GPixelFormats[InFormat].PlatformFormat;
		PlatformFormat = FindDepthStencilDXGIFormat(PlatformFormat);
		PlatformFormat = FindShaderResourceDXGIFormat(PlatformFormat, Flags & TexCreate_SRGB);
		return PlatformFormat;
	};

	Format = GetNearestSupportedSwapchainFormat(InSession, Format, ToPlatformFormat);
	if (!Format)
	{
		return nullptr;
	}

	XrSwapchain Swapchain = CreateSwapchain(InSession, ToPlatformFormat(Format), SizeX, SizeY, NumMips, NumSamples, TargetableTextureFlags);
	if (!Swapchain)
	{
		return nullptr;
	}

	const bool bDepthStencil = (PF_DepthStencil == Format);

	FD3D11DynamicRHI* DynamicRHI = static_cast<FD3D11DynamicRHI*>(GDynamicRHI);
	TArray<FTextureRHIRef> TextureChain;
	// @todo: Once things settle down, the chain target will be created below in the CreateXRSwapChain call, via an RHI "CreateAliasedTexture" call.
	TArray<XrSwapchainImageD3D11KHR> Images = EnumerateImages<XrSwapchainImageD3D11KHR>(Swapchain, XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR);
	FTextureRHIRef ChainTarget = static_cast<FTextureRHIRef>(DynamicRHI->RHICreateTexture2DFromResource(GPixelFormats[Format].UnrealFormat, TargetableTextureFlags, bDepthStencil ? FClearValueBinding::DepthFar : FClearValueBinding::Black, Images[0].texture));
	for (const auto& Image : Images)
	{
		TextureChain.Add(static_cast<FTextureRHIRef>(DynamicRHI->RHICreateTexture2DFromResource(GPixelFormats[Format].UnrealFormat, TargetableTextureFlags, bDepthStencil ? FClearValueBinding::DepthFar : FClearValueBinding::Black, Image.texture)));
	}

	return CreateXRSwapChain<FOpenXRSwapchain>(MoveTemp(TextureChain), ChainTarget, Swapchain);
}
#endif

#ifdef XR_USE_GRAPHICS_API_D3D12
FXRSwapChainPtr CreateSwapchain_D3D12(XrSession InSession, uint8 Format, uint32 SizeX, uint32 SizeY, uint32 NumMips, uint32 NumSamples, uint32 Flags, uint32 TargetableTextureFlags)
{
	TFunction<uint32(uint8)> ToPlatformFormat = [Flags](uint8 InFormat)
	{
		// We need to convert typeless to typed formats to create a swapchain
		DXGI_FORMAT PlatformFormat = (DXGI_FORMAT)GPixelFormats[InFormat].PlatformFormat;
		PlatformFormat = FindDepthStencilDXGIFormat_D3D12(PlatformFormat);
		PlatformFormat = FindShaderResourceDXGIFormat_D3D12(PlatformFormat, Flags & TexCreate_SRGB);
		return PlatformFormat;
	};

	Format = GetNearestSupportedSwapchainFormat(InSession, Format, ToPlatformFormat);
	if (!Format)
	{
		return nullptr;
	}

	XrSwapchain Swapchain = CreateSwapchain(InSession, ToPlatformFormat(Format), SizeX, SizeY, NumMips, NumSamples, TargetableTextureFlags);
	if (!Swapchain)
	{
		return nullptr;
	}

	FD3D12DynamicRHI* DynamicRHI = static_cast<FD3D12DynamicRHI*>(GDynamicRHI);
	TArray<FTextureRHIRef> TextureChain;
	// @todo: Once things settle down, the chain target will be created below in the CreateXRSwapChain call, via an RHI "CreateAliasedTexture" call.
	TArray<XrSwapchainImageD3D12KHR> Images = EnumerateImages<XrSwapchainImageD3D12KHR>(Swapchain, XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR);
	FTextureRHIRef ChainTarget = static_cast<FTextureRHIRef>(DynamicRHI->RHICreateTexture2DFromResource(GPixelFormats[Format].UnrealFormat, TargetableTextureFlags, FClearValueBinding::Black, Images[0].texture));
	for (const auto& Image : Images)
	{
		TextureChain.Add(static_cast<FTextureRHIRef>(DynamicRHI->RHICreateTexture2DFromResource(GPixelFormats[Format].UnrealFormat, TargetableTextureFlags, FClearValueBinding::Black, Image.texture)));
	}

	return CreateXRSwapChain<FOpenXRSwapchain>(MoveTemp(TextureChain), ChainTarget, Swapchain);
}
#endif

#ifdef XR_USE_GRAPHICS_API_OPENGL
FXRSwapChainPtr CreateSwapchain_OpenGL(XrSession InSession, uint8 Format, uint32 SizeX, uint32 SizeY, uint32 NumMips, uint32 NumSamples, uint32 Flags, uint32 TargetableTextureFlags)
{
	Format = GetNearestSupportedSwapchainFormat(InSession, Format);
	if (!Format)
	{
		return nullptr;
	}

	XrSwapchain Swapchain = CreateSwapchain(InSession, GPixelFormats[Format].PlatformFormat, SizeX, SizeY, NumMips, NumSamples, TargetableTextureFlags);
	if (!Swapchain)
	{
		return nullptr;
	}

	TArray<FTextureRHIRef> TextureChain;
	FOpenGLDynamicRHI* DynamicRHI = static_cast<FOpenGLDynamicRHI*>(GDynamicRHI);
	// @todo: Once things settle down, the chain target will be created below in the CreateXRSwapChain call, via an RHI "CreateAliasedTexture" call.
	TArray<XrSwapchainImageOpenGLKHR> Images = EnumerateImages<XrSwapchainImageOpenGLKHR>(Swapchain, XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR);
	FTextureRHIRef ChainTarget = static_cast<FTextureRHIRef>(DynamicRHI->RHICreateTexture2DFromResource(GPixelFormats[Format].UnrealFormat, SizeX, SizeY, NumMips, NumSamples, 1, FClearValueBinding::Black, Images[0].image, Flags));
	for (const auto& Image : Images)
	{
		TextureChain.Add(static_cast<FTextureRHIRef>(DynamicRHI->RHICreateTexture2DFromResource(GPixelFormats[Format].UnrealFormat, SizeX, SizeY, NumMips, NumSamples, 1, FClearValueBinding::Black, Image.image, Flags)));
	}

	return CreateXRSwapChain<FOpenXRSwapchain>(MoveTemp(TextureChain), ChainTarget, Swapchain);
}
#endif

#ifdef XR_USE_GRAPHICS_API_VULKAN
FXRSwapChainPtr CreateSwapchain_Vulkan(XrSession InSession, uint8 Format, uint32 SizeX, uint32 SizeY, uint32 NumMips, uint32 NumSamples, uint32 Flags, uint32 TargetableTextureFlags)
{
	TFunction<uint32(uint8)> ToPlatformFormat = [Flags](uint8 InFormat)
	{
		return UEToVkTextureFormat(GPixelFormats[InFormat].UnrealFormat, Flags & TexCreate_SRGB);
	};
	Format = GetNearestSupportedSwapchainFormat(InSession, Format, ToPlatformFormat);
	if (!Format)
	{
		return nullptr;
	}

	XrSwapchain Swapchain = CreateSwapchain(InSession, ToPlatformFormat(Format), SizeX, SizeY, NumMips, NumSamples, TargetableTextureFlags);
	if (!Swapchain)
	{
		return nullptr;
	}

	TArray<FTextureRHIRef> TextureChain;
	FVulkanDynamicRHI* DynamicRHI = static_cast<FVulkanDynamicRHI*>(GDynamicRHI);
	// @todo: Once things settle down, the chain target will be created below in the CreateXRSwapChain call, via an RHI "CreateAliasedTexture" call.
	TArray<XrSwapchainImageVulkanKHR> Images = EnumerateImages<XrSwapchainImageVulkanKHR>(Swapchain, XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR);
	FTextureRHIRef ChainTarget = static_cast<FTextureRHIRef>(DynamicRHI->RHICreateTexture2DFromResource(GPixelFormats[Format].UnrealFormat, SizeX, SizeY, NumMips, NumSamples, Images[0].image, Flags));
	for (const auto& Image : Images)
	{
		TextureChain.Add(static_cast<FTextureRHIRef>(DynamicRHI->RHICreateTexture2DFromResource(GPixelFormats[Format].UnrealFormat, SizeX, SizeY, NumMips, NumSamples, Image.image, Flags)));
	}

	return CreateXRSwapChain<FOpenXRSwapchain>(MoveTemp(TextureChain), ChainTarget, Swapchain);
}
#endif
