// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXTexture.cpp: AGX texture RHI implementation.
 =============================================================================*/

#include "AGXRHIPrivate.h"
#include "AGXProfiler.h" // for STAT_AGXTexturePageOffTime
#include "RenderUtils.h"
#include "Containers/ResourceArray.h"
#include "Misc/ScopeRWLock.h"
#include "AGXLLM.h"

volatile int64 FAGXSurface::ActiveUploads = 0;

int32 GAGXMaxOutstandingAsyncTexUploads = 100 * 1024 * 1024;
FAutoConsoleVariableRef CVarAGXMaxOutstandingAsyncTexUploads(TEXT("rhi.AGX.MaxOutstandingAsyncTexUploads"),
															 GAGXMaxOutstandingAsyncTexUploads,
															 TEXT("The maximum number of outstanding asynchronous texture uploads allowed to be pending in Metal. After the limit is reached the next upload will wait for all outstanding operations to complete and purge the waiting free-lists in order to reduce peak memory consumption. Defaults to 0 (infinite), set to a value > 0 limit the number."),
															 ECVF_ReadOnly | ECVF_RenderThreadSafe);

int32 GAGXForceIOSTexturesShared = 1;
FAutoConsoleVariableRef CVarAGXForceIOSTexturesShared(TEXT("rhi.AGX.ForceIOSTexturesShared"),
													  GAGXForceIOSTexturesShared,
													  TEXT("If true, forces all textures to be Shared on iOS"),
													  ECVF_RenderThreadSafe);

/** Given a pointer to a RHI texture that was created by the AGX RHI, returns a pointer to the FAGXTextureBase it encapsulates. */
FAGXSurface* AGXGetMetalSurfaceFromRHITexture(FRHITexture* Texture)
{
	return Texture
		? static_cast<FAGXSurface*>(Texture->GetTextureBaseRHI())
		: nullptr;
}

static bool IsRenderTarget(ETextureCreateFlags Flags)
{
	return EnumHasAnyFlags(Flags, TexCreate_RenderTargetable | TexCreate_ResolveTargetable | TexCreate_DepthStencilTargetable | TexCreate_DepthStencilResolveTarget);
}

static MTLTextureUsage ConvertFlagsToUsage(ETextureCreateFlags Flags)
{
	MTLTextureUsage Usage = MTLTextureUsageUnknown;
    if (EnumHasAnyFlags(Flags, TexCreate_ShaderResource | TexCreate_ResolveTargetable | TexCreate_DepthStencilTargetable))
	{
		Usage |= MTLTextureUsageShaderRead;
		Usage |= MTLTextureUsagePixelFormatView;
	}
	
	if (EnumHasAnyFlags(Flags, TexCreate_UAV))
	{
		Usage |= MTLTextureUsageShaderRead;
		Usage |= MTLTextureUsageShaderWrite;
		Usage |= MTLTextureUsagePixelFormatView;
	}
	
	// offline textures are normal shader read textures
	if (EnumHasAnyFlags(Flags, TexCreate_OfflineProcessed))
	{
		Usage |= MTLTextureUsageShaderRead;
	}

	//if the high level is doing manual resolves then the textures specifically markes as resolve targets
	//are likely to be used in a manual shader resolve by the high level and must be bindable as rendertargets.
	const bool bSeparateResolveTargets = FAGXCommandQueue::SupportsSeparateMSAAAndResolveTarget();
	const bool bResolveTarget = EnumHasAnyFlags(Flags, TexCreate_ResolveTargetable);
	if (EnumHasAnyFlags(Flags, TexCreate_RenderTargetable | TexCreate_DepthStencilTargetable | TexCreate_DepthStencilResolveTarget) || (bResolveTarget && bSeparateResolveTargets))
	{
		Usage |= MTLTextureUsageRenderTarget;
		Usage |= MTLTextureUsageShaderRead;
	}

	return Usage;
}

static bool IsPixelFormatCompressed(EPixelFormat Format)
{
	switch (Format)
	{
		case PF_DXT1:
		case PF_DXT3:
		case PF_DXT5:
		case PF_PVRTC2:
		case PF_PVRTC4:
		case PF_BC4:
		case PF_BC5:
		case PF_ETC2_RGB:
		case PF_ETC2_RGBA:
		case PF_ASTC_4x4:
		case PF_ASTC_6x6:
		case PF_ASTC_8x8:
		case PF_ASTC_10x10:
		case PF_ASTC_12x12:
		case PF_BC6H:
		case PF_BC7:
			return true;
		default:
			return false;
	}
}

static bool IsPixelFormatASTCCompressed(EPixelFormat Format)
{
	switch (Format)
	{
		case PF_ASTC_4x4:
		case PF_ASTC_6x6:
		case PF_ASTC_8x8:
		case PF_ASTC_10x10:
		case PF_ASTC_12x12:
			return true;
		default:
			return false;
	}
}

static bool IsPixelFormatPVRTCCompressed(EPixelFormat Format)
{
	switch (Format)
	{
		case PF_PVRTC2:
		case PF_PVRTC4:
		case PF_ETC2_RGB:
		case PF_ETC2_RGBA:
			return true;
		default:
			return false;
	}
}

void AGXSafeReleaseMetalTexture(FAGXSurface* Surface, FAGXTexture& Texture, bool bAVFoundationTexture)
{
	if(GIsAGXInitialized && GDynamicRHI)
	{
		if (!bAVFoundationTexture)
		{
			GetAGXDeviceContext().ReleaseTexture(Surface, Texture);
		}
		else
		{
			AGXSafeReleaseMetalObject([Texture.GetPtr() retain]);
		}
	}
}

void AGXSafeReleaseMetalTexture(FAGXSurface* Surface, FAGXTexture& Texture)
{
	if(GIsAGXInitialized && GDynamicRHI)
	{
		GetAGXDeviceContext().ReleaseTexture(Surface, Texture);
	}
}

#if PLATFORM_MAC
static MTLPixelFormat AGX_ToSRGBFormat_NonAppleMacGPU(MTLPixelFormat MTLFormat)
{
	switch (MTLFormat)
	{
		case MTLPixelFormatRGBA8Unorm:
			MTLFormat = MTLPixelFormatRGBA8Unorm_sRGB;
			break;
		case MTLPixelFormatBGRA8Unorm:
			MTLFormat = MTLPixelFormatBGRA8Unorm_sRGB;
			break;
		case MTLPixelFormatBC1_RGBA:
			MTLFormat = MTLPixelFormatBC1_RGBA_sRGB;
			break;
		case MTLPixelFormatBC2_RGBA:
			MTLFormat = MTLPixelFormatBC2_RGBA_sRGB;
			break;
		case MTLPixelFormatBC3_RGBA:
			MTLFormat = MTLPixelFormatBC3_RGBA_sRGB;
			break;
		case MTLPixelFormatBC7_RGBAUnorm:
			MTLFormat = MTLPixelFormatBC7_RGBAUnorm_sRGB;
			break;
		default:
			break;
	}
	return MTLFormat;
}
#endif

static MTLPixelFormat AGX_ToSRGBFormat_AppleGPU(MTLPixelFormat MTLFormat)
{
	switch (MTLFormat)
	{
		case MTLPixelFormatRGBA8Unorm:
			MTLFormat = MTLPixelFormatRGBA8Unorm_sRGB;
			break;
		case MTLPixelFormatBGRA8Unorm:
			MTLFormat = MTLPixelFormatBGRA8Unorm_sRGB;
			break;
		case MTLPixelFormatR8Unorm:
			MTLFormat = MTLPixelFormatR8Unorm_sRGB;
			break;
		case MTLPixelFormatPVRTC_RGBA_2BPP:
			MTLFormat = MTLPixelFormatPVRTC_RGBA_2BPP_sRGB;
			break;
		case MTLPixelFormatPVRTC_RGBA_4BPP:
			MTLFormat = MTLPixelFormatPVRTC_RGBA_4BPP_sRGB;
			break;
		case MTLPixelFormatASTC_4x4_LDR:
			MTLFormat = MTLPixelFormatASTC_4x4_sRGB;
			break;
		case MTLPixelFormatASTC_6x6_LDR:
			MTLFormat = MTLPixelFormatASTC_6x6_sRGB;
			break;
		case MTLPixelFormatASTC_8x8_LDR:
			MTLFormat = MTLPixelFormatASTC_8x8_sRGB;
			break;
		case MTLPixelFormatASTC_10x10_LDR:
			MTLFormat = MTLPixelFormatASTC_10x10_sRGB;
			break;
		case MTLPixelFormatASTC_12x12_LDR:
			MTLFormat = MTLPixelFormatASTC_12x12_sRGB;
			break;
#if PLATFORM_MAC
		// Fix for Apple silicon M1 macs that can support BC pixel formats even though they are Apple family GPUs.
		case MTLPixelFormatBC1_RGBA:
			MTLFormat = MTLPixelFormatBC1_RGBA_sRGB;
			break;
		case MTLPixelFormatBC2_RGBA:
			MTLFormat = MTLPixelFormatBC2_RGBA_sRGB;
			break;
		case MTLPixelFormatBC3_RGBA:
			MTLFormat = MTLPixelFormatBC3_RGBA_sRGB;
			break;
		case MTLPixelFormatBC7_RGBAUnorm:
			MTLFormat = MTLPixelFormatBC7_RGBAUnorm_sRGB;
			break;
#endif
		default:
			break;
	}
	return MTLFormat;
}

MTLPixelFormat AGXToSRGBFormat(MTLPixelFormat MTLFormat)
{
	if([GMtlDevice supportsFamily:MTLGPUFamilyApple1])
	{
		MTLFormat = AGX_ToSRGBFormat_AppleGPU(MTLFormat);
	}
#if PLATFORM_MAC
	else if([GMtlDevice supportsFamily:MTLGPUFamilyMac1])
	{
		MTLFormat = AGX_ToSRGBFormat_NonAppleMacGPU(MTLFormat);
	}
#endif
	
	return MTLFormat;
}

static inline uint32 ComputeLockIndex(uint32 MipIndex, uint32 ArrayIndex)
{
	check(MipIndex < MAX_uint16);
	check(ArrayIndex < MAX_uint16);
	return (MipIndex & MAX_uint16) | ((ArrayIndex & MAX_uint16) << 16);
}

void FAGXSurface::PrepareTextureView()
{
	// Recreate the texture to enable MTLTextureUsagePixelFormatView which must be off unless we definitely use this feature or we are throwing ~4% performance vs. Windows on the floor.
	MTLTextureUsage Usage = [Texture.GetPtr() usage];
	bool bMemoryLess = false;
#if PLATFORM_IOS
	bMemoryLess = ([Texture.GetPtr() storageMode] == MTLStorageModeMemoryless);
#endif
	if (!(Usage & MTLTextureUsagePixelFormatView) && !bMemoryLess)
	{
		check(ImageSurfaceRef == nullptr);
		
		check(Texture);
		const bool bMSAATextureIsTexture = MSAATexture == Texture;
		const bool bMSAAResolveTextureIsTexture = MSAAResolveTexture == Texture;
		if (MSAATexture && !bMSAATextureIsTexture)
		{
			FAGXTexture OldMSAATexture = MSAATexture;
			MSAATexture = Reallocate(MSAATexture, MTLTextureUsagePixelFormatView);
			AGXSafeReleaseMetalTexture(this, OldMSAATexture, ImageSurfaceRef != nullptr);
		}
		if (MSAAResolveTexture && !bMSAAResolveTextureIsTexture)
		{
			FAGXTexture OldMSAAResolveTexture = MSAAResolveTexture;
			MSAAResolveTexture = Reallocate(MSAAResolveTexture, MTLTextureUsagePixelFormatView);
			AGXSafeReleaseMetalTexture(this, OldMSAAResolveTexture, ImageSurfaceRef != nullptr);
		}
		
		FAGXTexture OldTexture = Texture;
		Texture = Reallocate(Texture, MTLTextureUsagePixelFormatView);
		AGXSafeReleaseMetalTexture(this, OldTexture, ImageSurfaceRef != nullptr);
		
		if (bMSAATextureIsTexture)
		{
			MSAATexture = Texture;
		}
		if (bMSAAResolveTextureIsTexture)
		{
			MSAAResolveTexture = Texture;
		}
	}
}

FAGXTexture FAGXSurface::Reallocate(FAGXTexture InTexture, MTLTextureUsage UsageModifier)
{
	id<MTLTexture> InMetalTexture = InTexture.GetPtr();

	MTLTextureDescriptor* TextureDescriptor = [[MTLTextureDescriptor alloc] init];
	TextureDescriptor.textureType      = InMetalTexture.textureType;
	TextureDescriptor.pixelFormat      = InMetalTexture.pixelFormat;
	TextureDescriptor.width            = InMetalTexture.width;
	TextureDescriptor.height           = InMetalTexture.height;
	TextureDescriptor.depth            = InMetalTexture.depth;
	TextureDescriptor.mipmapLevelCount = InMetalTexture.mipmapLevelCount;
	TextureDescriptor.sampleCount      = InMetalTexture.sampleCount;
	TextureDescriptor.arrayLength      = InMetalTexture.arrayLength;
	TextureDescriptor.resourceOptions  = InMetalTexture.resourceOptions;
	TextureDescriptor.usage            = (InMetalTexture.usage | UsageModifier);
	
	FAGXTexture NewTex = GetAGXDeviceContext().CreateTexture(this, TextureDescriptor);
	check(NewTex);
	
	[TextureDescriptor release];
	
	return NewTex;
}

void FAGXSurface::MakeAliasable(void)
{
	// TODO
}

uint8 AGXGetMetalPixelFormatKey(MTLPixelFormat Format)
{
	struct FAGXPixelFormatKeyMap
	{
		FRWLock Mutex;
		uint8 NextKey = 1; // 0 is reserved for MTLPixelFormatInvalid
		TMap<uint64, uint8> Map;

		uint8 Get(MTLPixelFormat Format)
		{
			FRWScopeLock Lock(Mutex, SLT_ReadOnly);
			uint8* Key = Map.Find((uint64)Format);
			if (Key == nullptr)
			{
				Lock.ReleaseReadOnlyLockAndAcquireWriteLock_USE_WITH_CAUTION();
				Key = Map.Find((uint64)Format);
				if (Key == nullptr)
				{
					Key = &Map.Add((uint64)Format, NextKey++);
					// only giving 6 bits to the key
					checkf(NextKey < 64, TEXT("Too many unique pixel formats to fit into the PipelineStateHash"));
				}
			}
			return *Key;
		}

		FAGXPixelFormatKeyMap()
		{
			// Add depth stencil formats first, so we don't have to use 6 bits for them in the pipeline hash
			Get(MTLPixelFormatDepth32Float);
			Get(MTLPixelFormatStencil8);
			Get(MTLPixelFormatDepth32Float_Stencil8);
#if PLATFORM_MAC
			Get(MTLPixelFormatDepth24Unorm_Stencil8);
			Get(MTLPixelFormatDepth16Unorm);
#endif
		}
	} static PixelFormatKeyMap;

	return PixelFormatKeyMap.Get(Format);
}

FAGXTextureDesc::FAGXTextureDesc(const FRHITextureDesc& InDesc)
	: bIsRenderTarget(IsRenderTarget(InDesc.Flags))
{
	PixelFormat = (MTLPixelFormat)GPixelFormats[InDesc.Format].PlatformFormat;

	if (EnumHasAnyFlags(InDesc.Flags, TexCreate_SRGB))
	{
		PixelFormat = AGXToSRGBFormat(PixelFormat);
	}

	// get a unique key for this surface's format
	FormatKey = AGXGetMetalPixelFormatKey(PixelFormat);

	if (InDesc.IsTextureCube())
	{
		Desc = new FMTLTextureDescriptor(
				[MTLTextureDescriptor textureCubeDescriptorWithPixelFormat:PixelFormat
																	  size:InDesc.Extent.X
																 mipmapped:(InDesc.NumMips > 1)],
				/* bRetain = */ true);
	}
	else if (InDesc.IsTexture3D())
	{
		MTLTextureDescriptor* TextureDescriptor = [[MTLTextureDescriptor alloc] init];
		TextureDescriptor.textureType      = MTLTextureType3D;
		TextureDescriptor.width            = InDesc.Extent.X;
		TextureDescriptor.height           = InDesc.Extent.Y;
		TextureDescriptor.depth            = InDesc.Depth;
		TextureDescriptor.pixelFormat      = PixelFormat;;
		TextureDescriptor.arrayLength      = 1;
		TextureDescriptor.mipmapLevelCount = 1;
		TextureDescriptor.sampleCount      = 1;

		Desc = new FMTLTextureDescriptor(TextureDescriptor, /* bRetain = */ false);
	}
	else
	{
		Desc = new FMTLTextureDescriptor(
				[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:PixelFormat
																   width:InDesc.Extent.X
																  height:InDesc.Extent.Y
															   mipmapped:(InDesc.NumMips > 1)],
				/* bRetain = */ true);
		[Desc->Get() setArrayLength:InDesc.ArraySize];
	}
	check(Desc.IsValid());

	MTLTextureDescriptor* TextureDescriptor = Desc->Get();

	// flesh out the descriptor
	if (InDesc.IsTextureArray())
	{
		TextureDescriptor.arrayLength = InDesc.ArraySize;
		if (InDesc.IsTextureCube())
		{
			if (FAGXCommandQueue::SupportsFeature(EAGXFeaturesCubemapArrays))
			{
				TextureDescriptor.textureType = MTLTextureTypeCubeArray;
			}
			else
			{
				TextureDescriptor.textureType = MTLTextureType2DArray;
				TextureDescriptor.arrayLength = (InDesc.ArraySize * 6);
			}
		}
		else
		{
			TextureDescriptor.textureType = MTLTextureType2DArray;
		}
	}

	TextureDescriptor.mipmapLevelCount = InDesc.NumMips;

	{
		MTLResourceOptions ResourceStorageMode;
		if (EnumHasAnyFlags(InDesc.Flags, TexCreate_CPUReadback) && !EnumHasAnyFlags(InDesc.Flags, TexCreate_RenderTargetable | TexCreate_DepthStencilTargetable | TexCreate_FastVRAM))
		{
#if PLATFORM_MAC
			ResourceStorageMode = MTLResourceStorageModeManaged;
#else
			ResourceStorageMode = MTLResourceStorageModeShared;
#endif
		}
		else if (EnumHasAnyFlags(InDesc.Flags, TexCreate_NoTiling) && !EnumHasAnyFlags(InDesc.Flags, TexCreate_FastVRAM | TexCreate_DepthStencilTargetable | TexCreate_RenderTargetable | TexCreate_UAV))
		{
#if PLATFORM_MAC
			ResourceStorageMode = MTLResourceStorageModeManaged;
#else
			ResourceStorageMode = MTLResourceStorageModeShared;
#endif
		}
		else if (EnumHasAnyFlags(InDesc.Flags, TexCreate_RenderTargetable | TexCreate_DepthStencilTargetable | TexCreate_ResolveTargetable | TexCreate_DepthStencilResolveTarget))
		{
			check(!(InDesc.Flags & TexCreate_CPUReadback));
#if PLATFORM_MAC
			ResourceStorageMode = MTLResourceStorageModePrivate;
#else
			if (GAGXForceIOSTexturesShared)
			{
				ResourceStorageMode = MTLResourceStorageModeShared;
			}
			else
			{
				ResourceStorageMode = MTLResourceStorageModePrivate;
			}
#endif
		}
		else
		{
			check(!(InDesc.Flags & TexCreate_CPUReadback));
#if PLATFORM_MAC
			ResourceStorageMode = MTLResourceStorageModePrivate;
#else
			if (GAGXForceIOSTexturesShared)
			{
				ResourceStorageMode = MTLResourceStorageModeShared;
			}
			// No private storage for PVRTC as it messes up the blit-encoder usage.
			// note: this is set to always be on and will be re-addressed in a future release
			else
			{
				if (IsPixelFormatPVRTCCompressed(InDesc.Format))
				{
					ResourceStorageMode = MTLResourceStorageModeShared;
				}
				else
				{
					ResourceStorageMode = MTLResourceStorageModePrivate;
				}
			}
#endif
		}

#if PLATFORM_IOS
		if (EnumHasAnyFlags(InDesc.Flags, TexCreate_Memoryless))
		{
			ensure(EnumHasAnyFlags(InDesc.Flags, (TexCreate_RenderTargetable | TexCreate_DepthStencilTargetable)));
			ensure(!EnumHasAnyFlags(InDesc.Flags, (TexCreate_CPUReadback | TexCreate_CPUWritable)));
			ensure(!EnumHasAnyFlags(InDesc.Flags, TexCreate_UAV));
			ResourceStorageMode = MTLResourceStorageModeMemoryless;
		}

		if (!FParse::Param(FCommandLine::Get(), TEXT("nomsaa")) && InDesc.NumSamples > 1)
		{
			if (GMaxRHIFeatureLevel < ERHIFeatureLevel::SM5)
			{
				ResourceStorageMode = MTLResourceStorageModeMemoryless;
				bMemoryless = true;
			}
		}
#endif

		TextureDescriptor.resourceOptions = (MTLResourceCPUCacheModeDefaultCache | ResourceStorageMode | MTLResourceHazardTrackingModeDefault);
		TextureDescriptor.usage = ConvertFlagsToUsage(InDesc.Flags);
	}
	
	if (!FParse::Param(FCommandLine::Get(), TEXT("nomsaa")))
	{
		if (InDesc.NumSamples > 1)
		{
			check(bIsRenderTarget);
			TextureDescriptor.textureType = MTLTextureType2DMultisample;

			// allow commandline to override
			uint32 NewNumSamples;
			if (FParse::Value(FCommandLine::Get(), TEXT("msaa="), NewNumSamples))
			{
				TextureDescriptor.sampleCount = NewNumSamples;
			}
			else
			{
				TextureDescriptor.sampleCount = InDesc.NumSamples;
			}
		}
	}
}

FAGXSurface::FAGXSurface(FAGXTextureCreateDesc const& CreateDesc)
	: FRHITexture       (CreateDesc)
	, FormatKey         (CreateDesc.FormatKey)
	, Texture           (nil)
	, MSAATexture       (nil)
	, MSAAResolveTexture(nil)
	, TotalTextureSize  (0)
	, Viewport          (nullptr)
	, ImageSurfaceRef   (nullptr)
{
	FPlatformAtomics::InterlockedExchange(&Written, 0);

	check(CreateDesc.Extent.X > 0 && CreateDesc.Extent.Y > 0 && CreateDesc.NumMips > 0);

	// the special back buffer surface will be updated in GetMetalDeviceContext().BeginDrawingViewport - no need to set the texture here
	if (EnumHasAnyFlags(CreateDesc.Flags, TexCreate_Presentable))
	{
		return;
	}

	MTLTextureDescriptor* TextureDescriptor = CreateDesc.Desc->Get();

	FResourceBulkDataInterface* BulkData = CreateDesc.BulkData;

	// The bulk data interface can be used to create external textures for VR and media player.
	// Handle these first.
	if (BulkData != nullptr)
	{
		switch (BulkData->GetResourceType())
		{
		case FResourceBulkDataInterface::EBulkDataType::MediaTexture:
			{
				checkf(CreateDesc.NumMips == 1 && CreateDesc.ArraySize == 1, TEXT("Only handling bulk data with 1 mip and 1 array length"));
				ImageSurfaceRef = (CFTypeRef)BulkData->GetResourceBulkData();
				CFRetain(ImageSurfaceRef);
				
#if !COREVIDEO_SUPPORTS_METAL
				Texture = FAGXTexture([GMtlDevice newTextureWithDescriptor:CreateDesc.Desc->Get() iosurface:CVPixelBufferGetIOSurface((CVPixelBufferRef)ImageSurfaceRef) plane:0], ns::Ownership::Assign);
#else
				Texture = CVMetalTextureGetTexture((CVMetalTextureRef)ImageSurfaceRef);
#endif
				METAL_FATAL_ASSERT(Texture, TEXT("Failed to create texture, desc %s"), *FString([TextureDescriptor description]));

				BulkData->Discard();
				BulkData = nullptr;
			}
			break;

#if PLATFORM_MAC
		case FResourceBulkDataInterface::EBulkDataType::VREyeBuffer:
			{
				ImageSurfaceRef = (CFTypeRef)BulkData->GetResourceBulkData();
				CFRetain(ImageSurfaceRef);

				MTLTextureDescriptor* DescCopy = [TextureDescriptor copy];
				DescCopy.resourceOptions = ((DescCopy.resourceOptions & ~MTLResourceStorageModeMask) | MTLResourceStorageModeManaged);

				Texture = FAGXTexture([GMtlDevice newTextureWithDescriptor:DescCopy
																 iosurface:(IOSurfaceRef)ImageSurfaceRef
																	 plane:0], ns::Ownership::Assign);
				
				METAL_FATAL_ASSERT(Texture, TEXT("Failed to create texture, desc %s"), *FString([DescCopy description]));

				BulkData->Discard();
				BulkData = nullptr;

				[DescCopy release];
			}
			break;
#endif
		}
	}

	if (!Texture)
	{
		// Non VR/media texture case (i.e. a regular texture).
		// Create the actual texture resource.

		const bool bBufferCompatibleOption = (TextureDescriptor.textureType == MTLTextureType2D || TextureDescriptor.textureType == MTLTextureTypeTextureBuffer) &&
											  CreateDesc.NumMips == 1 && CreateDesc.ArraySize == 1 && CreateDesc.NumSamples == 1 && TextureDescriptor.depth == 1;

		if (bBufferCompatibleOption && (EnumHasAllFlags(CreateDesc.Flags, TexCreate_UAV | TexCreate_NoTiling) || EnumHasAllFlags(CreateDesc.Flags, TexCreate_AtomicCompatible)))
		{
			const uint32 MinimumByteAlignment = [GMtlDevice minimumLinearTextureAlignmentForPixelFormat:CreateDesc.PixelFormat];
			const NSUInteger BytesPerRow = Align(TextureDescriptor.width * GPixelFormats[CreateDesc.Format].BlockBytes, MinimumByteAlignment);

			// Backing buffer resource options must match the texture we are going to create from it
			FAGXPooledBufferArgs Args(BytesPerRow * TextureDescriptor.height, BUF_Dynamic, FAGXPooledBufferArgs::PrivateStorageResourceOptions);
			FAGXBuffer Buffer = GetAGXDeviceContext().CreatePooledBuffer(Args);

			Texture = mtlpp::Texture([Buffer.GetPtr() newTextureWithDescriptor:TextureDescriptor
																		offset:Buffer.GetOffset()
																   bytesPerRow:BytesPerRow], nullptr, ns::Ownership::Assign);
		}
		else
		{
			// If we are in here then either the texture description is not buffer compatable or these flags were not set
			// assert that these flag combinations are not set as they require a buffer backed texture and the texture description is not compatible with that
			checkf(!EnumHasAllFlags(CreateDesc.Flags, TexCreate_AtomicCompatible), TEXT("Requested buffer backed texture that breaks Metal linear texture limitations: %s"), *FString([TextureDescriptor description]));
			Texture = GetAGXDeviceContext().CreateTexture(this, TextureDescriptor);
		}
		
		METAL_FATAL_ASSERT(Texture, TEXT("Failed to create texture, desc %s"), *FString([TextureDescriptor description]));
	}

	if (BulkData)
	{
		// Regular texture has some bulk data to handle
		UE_LOG(LogAGX, Display, TEXT("Got a bulk data texture, with %d mips"), CreateDesc.NumMips);
		checkf(CreateDesc.NumMips == 1, TEXT("Only handling bulk data with 1 mip and 1 array length"));

		check(IsInRenderingThread());
		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

		// lock, copy, unlock
		uint32 Stride;
		void* LockedData = FAGXDynamicRHI::Get().LockTexture2D_RenderThread(RHICmdList, this, 0, RLM_WriteOnly, Stride, false);
		check(LockedData);
		FMemory::Memcpy(LockedData, BulkData->GetResourceBulkData(), BulkData->GetResourceBulkDataSize());
		FAGXDynamicRHI::Get().UnlockTexture2D_RenderThread(RHICmdList, this, 0, false);

		// bulk data can be unloaded now
		BulkData->Discard();
		BulkData = nullptr;
	}

	// calculate size of the texture
	TotalTextureSize = GetMemorySize();

	if (CreateDesc.NumSamples > 1 && !FParse::Param(FCommandLine::Get(), TEXT("nomsaa")))
	{
		MSAATexture = GetAGXDeviceContext().CreateTexture(this, TextureDescriptor);

		//device doesn't support HW depth resolve.  This case only valid on mobile renderer or
		//on Mac where RHISupportsSeparateMSAAAndResolveTextures is true.
		const bool bSupportsMSAADepthResolve = GetAGXDeviceContext().SupportsFeature(EAGXFeaturesMSAADepthResolve);
		const bool bDepthButNoResolveSupported = CreateDesc.Format == PF_DepthStencil && !bSupportsMSAADepthResolve;
		if (bDepthButNoResolveSupported)
		{
			Texture = MSAATexture;

			// we don't have the resolve texture, so we just update the memory size with the MSAA size
			TotalTextureSize = TotalTextureSize * CreateDesc.NumSamples;
		}
		else if (!CreateDesc.bMemoryless)
		{
			// an MSAA render target takes NumSamples more space, in addition to the resolve texture
			TotalTextureSize += TotalTextureSize * CreateDesc.NumSamples;
		}

		if (MSAATexture != Texture)
		{
			check(!MSAAResolveTexture);

			//if bSupportsSeparateMSAAAndResolve then the high level expect to binds the MSAA when binding shader params.
			const bool bSupportsSeparateMSAAAndResolve = FAGXCommandQueue::SupportsSeparateMSAAAndResolveTarget();
			if (bSupportsSeparateMSAAAndResolve)
			{
				MSAAResolveTexture = Texture;
				Texture = MSAATexture;
			}
			else
			{
				MSAAResolveTexture = Texture;
			}
		}

		// We always require an MSAAResolveTexture if MSAATexture is active.
		check(!MSAATexture || MSAAResolveTexture || bDepthButNoResolveSupported);

		if (MSAATexture.GetPtr() == nil)
		{
			UE_LOG(LogAGX, Warning, TEXT("Failed to create MSAA texture with descriptor: %s"), *FString([TextureDescriptor description]));
		}
	}

	// create a stencil buffer if needed
	if (CreateDesc.Format == PF_DepthStencil)
	{
		// 1 byte per texel
		TotalTextureSize += CreateDesc.Extent.X * CreateDesc.Extent.Y;
	}

	// track memory usage

	if (CreateDesc.bIsRenderTarget)
	{
		GCurrentRendertargetMemorySize += Align(TotalTextureSize, 1024) / 1024;
	}
	else
	{
		GCurrentTextureMemorySize += Align(TotalTextureSize, 1024) / 1024;
	}

#if STATS
	if (CreateDesc.IsTextureCube())
	{
		if (CreateDesc.bIsRenderTarget)
		{
			INC_MEMORY_STAT_BY(STAT_RenderTargetMemoryCube, TotalTextureSize);
		}
		else
		{
			INC_MEMORY_STAT_BY(STAT_TextureMemoryCube, TotalTextureSize);
		}
	}
	else if (CreateDesc.IsTexture3D())
	{
		if (CreateDesc.bIsRenderTarget)
		{
			INC_MEMORY_STAT_BY(STAT_RenderTargetMemory3D, TotalTextureSize);
		}
		else
		{
			INC_MEMORY_STAT_BY(STAT_TextureMemory3D, TotalTextureSize);
		}
	}
	else
	{
		if (CreateDesc.bIsRenderTarget)
		{
			INC_MEMORY_STAT_BY(STAT_RenderTargetMemory2D, TotalTextureSize);
		}
		else
		{
			INC_MEMORY_STAT_BY(STAT_TextureMemory2D, TotalTextureSize);
		}
	}
#endif
}

@interface FAGXDeferredStats : FApplePlatformObject
{
@public
	uint64 TextureSize;
	ETextureDimension Dimension;
	bool bIsRenderTarget;
}
@end

@implementation FAGXDeferredStats
APPLE_PLATFORM_OBJECT_ALLOC_OVERRIDES(FAGXDeferredStats)
-(void)dealloc
{
#if STATS
	if (Dimension == ETextureDimension::TextureCube || Dimension == ETextureDimension::TextureCubeArray)
	{
		if (bIsRenderTarget)
		{
			DEC_MEMORY_STAT_BY(STAT_RenderTargetMemoryCube, TextureSize);
		}
		else
		{
			DEC_MEMORY_STAT_BY(STAT_TextureMemoryCube, TextureSize);
		}
	}
	else if (Dimension == ETextureDimension::Texture3D)
	{
		if (bIsRenderTarget)
		{
			DEC_MEMORY_STAT_BY(STAT_RenderTargetMemory3D, TextureSize);
		}
		else
		{
			DEC_MEMORY_STAT_BY(STAT_TextureMemory3D, TextureSize);
		}
	}
	else
	{
		if (bIsRenderTarget)
		{
			DEC_MEMORY_STAT_BY(STAT_RenderTargetMemory2D, TextureSize);
		}
		else
		{
			DEC_MEMORY_STAT_BY(STAT_TextureMemory2D, TextureSize);
		}
	}
#endif
	if (bIsRenderTarget)
	{
		GCurrentRendertargetMemorySize -= Align(TextureSize, 1024) / 1024;
	}
	else
	{
		GCurrentTextureMemorySize -= Align(TextureSize, 1024) / 1024;
	}
	[super dealloc];
}
@end

FAGXSurface::~FAGXSurface()
{
	bool const bIsRenderTarget = IsRenderTarget(GetDesc().Flags);
	
	if (MSAATexture.GetPtr())
	{
		if (Texture.GetPtr() != MSAATexture.GetPtr())
		{
			AGXSafeReleaseMetalTexture(this, MSAATexture, false);
		}
	}
	
	
	//do the same as above.  only do a [release] if it's the same as texture.
	if (MSAAResolveTexture.GetPtr())
	{
		if (Texture.GetPtr() != MSAAResolveTexture.GetPtr())
		{
			AGXSafeReleaseMetalTexture(this, MSAAResolveTexture, false);
		}
	}
	
	if (!(GetDesc().Flags & TexCreate_Presentable) && Texture.GetPtr())
	{
		AGXSafeReleaseMetalTexture(this, Texture, (ImageSurfaceRef != nullptr));
	}
	
	MSAATexture = nil;
	MSAAResolveTexture = nil;
	Texture = nil;
	
	// track memory usage
	FAGXDeferredStats* Block = [FAGXDeferredStats new];
	Block->Dimension = GetDesc().Dimension;
	Block->TextureSize = TotalTextureSize;
	Block->bIsRenderTarget = bIsRenderTarget;
	AGXSafeReleaseMetalObject(Block);
	
	if(ImageSurfaceRef)
	{
		// CFArray can contain CFType objects and is toll-free bridged with NSArray
		CFArrayRef Temp = CFArrayCreate(kCFAllocatorSystemDefault, &ImageSurfaceRef, 1, &kCFTypeArrayCallBacks);
		AGXSafeReleaseMetalObject((NSArray*)Temp);
		CFRelease(ImageSurfaceRef);
	}
	
	ImageSurfaceRef = nullptr;
}

id <MTLBuffer> FAGXSurface::AllocSurface(uint32 MipIndex, uint32 ArrayIndex, EResourceLockMode LockMode, uint32& DestStride, bool SingleLayer /*= false*/)
{
	check(IsInRenderingThread());

	// get size and stride
	uint32 MipBytes = GetMipSize(MipIndex, &DestStride, SingleLayer);
	
	// allocate some temporary memory
	// This should really be pooled and texture transfers should be their own pool
	id <MTLBuffer> Buffer = [GMtlDevice newBufferWithLength:MipBytes options:MTLResourceStorageModeShared];
	Buffer.label = @"Temporary Surface Backing";
	
	// Note: while the lock is active, this map owns the backing store.
	const uint32 LockIndex = ComputeLockIndex(MipIndex, ArrayIndex);
	GRHILockTracker.Lock(this, Buffer, LockIndex, MipBytes, LockMode, false);
	
#if PLATFORM_MAC
	// Expand R8_sRGB into RGBA8_sRGB for Mac.
	if (   GetDesc().Format == PF_G8
		&& GetDesc().Dimension == ETextureDimension::Texture2D
		&& EnumHasAnyFlags(GetDesc().Flags, TexCreate_SRGB)
		&& LockMode == RLM_WriteOnly
		&& (MTLPixelFormat)Texture.GetPixelFormat() == MTLPixelFormatRGBA8Unorm_sRGB)
	{
		DestStride = FMath::Max<uint32>(GetDesc().Extent.X >> MipIndex, 1);
	}
#endif
	
	check(Buffer);
	
	return Buffer;
}

void FAGXSurface::UpdateSurfaceAndDestroySourceBuffer(id <MTLBuffer> SourceBuffer, uint32 MipIndex, uint32 ArrayIndex)
{
#if STATS
	uint64 Start = FPlatformTime::Cycles64();
#endif
	check(SourceBuffer);
	
	uint32 Stride;
	uint32 BytesPerImage = GetMipSize(MipIndex, &Stride, true);
	
	MTLRegion Region;
	if (GetDesc().IsTexture3D())
	{
		// upload the texture to the texture slice
		Region = MTLRegionMake3D(
			0, 0, 0,
			FMath::Max<uint32>(GetDesc().Extent.X >> MipIndex, 1),
			FMath::Max<uint32>(GetDesc().Extent.Y >> MipIndex, 1),
			FMath::Max<uint32>(GetDesc().Depth    >> MipIndex, 1)
		);
	}
	else
	{
		// upload the texture to the texture slice
		Region = MTLRegionMake2D(
			0, 0,
			FMath::Max<uint32>(GetDesc().Extent.X >> MipIndex, 1),
			FMath::Max<uint32>(GetDesc().Extent.Y >> MipIndex, 1)
		);
	}

#if PLATFORM_MAC
	// Expand R8_sRGB into RGBA8_sRGB for Mac.
	if (   GetDesc().Format == PF_G8
		&& GetDesc().Dimension == ETextureDimension::Texture2D
		&& EnumHasAnyFlags(GetDesc().Flags, TexCreate_SRGB)
		&& (MTLPixelFormat)Texture.GetPixelFormat() == MTLPixelFormatRGBA8Unorm_sRGB)
	{
		TArray<uint8> Data;
		uint8* ExpandedMem = (uint8*) SourceBuffer.contents;
		check(ExpandedMem);
		Data.Append(ExpandedMem, BytesPerImage);
		uint32 SrcStride = FMath::Max<uint32>(GetDesc().Extent.X >> MipIndex, 1);
		for(uint y = 0; y < FMath::Max<uint32>(GetDesc().Extent.Y >> MipIndex, 1); y++)
		{
			uint8* RowDest = ExpandedMem;
			for(uint x = 0; x < FMath::Max<uint32>(GetDesc().Extent.X >> MipIndex, 1); x++)
			{
				*(RowDest++) = Data[(y * SrcStride) + x];
				*(RowDest++) = Data[(y * SrcStride) + x];
				*(RowDest++) = Data[(y * SrcStride) + x];
				*(RowDest++) = Data[(y * SrcStride) + x];
			}
			ExpandedMem = (ExpandedMem + Stride);
		}
	}
#endif
	
	if ([Texture.GetPtr() storageMode] == MTLStorageModePrivate)
	{
		SCOPED_AUTORELEASE_POOL;
		
		FAGXBuffer Buffer(SourceBuffer);
		
		int64 Size = BytesPerImage * Region.size.depth * FMath::Max(1u, ArrayIndex);
		
		int64 Count = FPlatformAtomics::InterlockedAdd(&ActiveUploads, Size);
		
		bool const bWait = ((GetAGXDeviceContext().GetNumActiveContexts() == 1) && (GAGXMaxOutstandingAsyncTexUploads > 0) && (Count >= GAGXMaxOutstandingAsyncTexUploads));
		
		MTLBlitOption Options = MTLBlitOptionNone;
#if !PLATFORM_MAC
		if ((MTLPixelFormat)Texture.GetPixelFormat() >= MTLPixelFormatPVRTC_RGB_2BPP && (MTLPixelFormat)Texture.GetPixelFormat() <= MTLPixelFormatPVRTC_RGBA_4BPP_sRGB)
		{
			Options = MTLBlitOptionRowLinearPVRTC;
		}
#endif
		
		if(GetAGXDeviceContext().AsyncCopyFromBufferToTexture(Buffer, 0, Stride, BytesPerImage, Region.size, Texture, ArrayIndex, MipIndex, Region.origin, Options))
		{
			mtlpp::CommandBufferHandler ScheduledHandler = nil;
	#if STATS
			int64* Cycles = new int64;
			FPlatformAtomics::InterlockedExchange(Cycles, 0);
			ScheduledHandler = [Cycles](mtlpp::CommandBuffer const&)
			{
				FPlatformAtomics::InterlockedExchange(Cycles, FPlatformTime::Cycles64());
			};
			mtlpp::CommandBufferHandler CompletionHandler = [SourceBuffer, Size, Cycles](mtlpp::CommandBuffer const&)
	#else
			mtlpp::CommandBufferHandler CompletionHandler = [SourceBuffer, Size](mtlpp::CommandBuffer const&)
	#endif
			{
				FPlatformAtomics::InterlockedAdd(&ActiveUploads, -Size);
	#if STATS
				int64 Taken = FPlatformTime::Cycles64() - *Cycles;
				delete Cycles;
				FPlatformAtomics::InterlockedAdd(&GAGXTexturePageOnTime, Taken);
	#endif
				[SourceBuffer release];
			};
			GetAGXDeviceContext().SubmitAsyncCommands(ScheduledHandler, CompletionHandler, bWait);
			
		}
		else
		{
			mtlpp::CommandBufferHandler CompletionHandler = [SourceBuffer, Size](mtlpp::CommandBuffer const&)
			{
				FPlatformAtomics::InterlockedAdd(&ActiveUploads, -Size);
				[SourceBuffer release];
			};
			GetAGXDeviceContext().GetCurrentRenderPass().AddCompletionHandler(CompletionHandler);
		}
		
		INC_DWORD_STAT_BY(STAT_AGXTextureMemUpdate, Size);
		
		if (bWait)
		{
			GetAGXDeviceContext().ClearFreeList();
		}
	}
	else
	{
#if !PLATFORM_MAC
		if ((MTLPixelFormat)Texture.GetPixelFormat() >= MTLPixelFormatPVRTC_RGB_2BPP && (MTLPixelFormat)Texture.GetPixelFormat() <= MTLPixelFormatPVRTC_RGBA_4BPP_sRGB) // @todo Calculate correct strides and byte-counts
		{
			Stride = 0;
			BytesPerImage = 0;
		}
#endif
		
		[Texture.GetPtr() replaceRegion:Region
							mipmapLevel:MipIndex
								  slice:ArrayIndex
							  withBytes:(const void*)[SourceBuffer contents]
							bytesPerRow:Stride
						  bytesPerImage:BytesPerImage];

		[SourceBuffer release];
		
		INC_DWORD_STAT_BY(STAT_AGXTextureMemUpdate, BytesPerImage);
	}
	
	FPlatformAtomics::InterlockedExchange(&Written, 1);
	
#if STATS
	GAGXTexturePageOnTime += (FPlatformTime::Cycles64() - Start);
#endif
}

void* FAGXSurface::Lock(uint32 MipIndex, uint32 ArrayIndex, EResourceLockMode LockMode, uint32& DestStride, bool SingleLayer /*= false*/)
{
	// get size and stride
	uint32 MipBytes = GetMipSize(MipIndex, &DestStride, false);
	
	// allocate some temporary memory
	id <MTLBuffer> Buffer = AllocSurface(MipIndex, ArrayIndex, LockMode, DestStride, SingleLayer);
	FAGXBuffer SourceData(Buffer);
	
	switch(LockMode)
	{
		case RLM_ReadOnly:
		{
			SCOPE_CYCLE_COUNTER(STAT_AGXTexturePageOffTime);
			
			FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
			const bool bIssueImmediateCommands = RHICmdList.Bypass() || IsInRHIThread();
			
			MTLRegion Region;
			if (GetDesc().IsTexture3D())
			{
				// upload the texture to the texture slice
				Region = MTLRegionMake3D(
					0, 0, 0,
					FMath::Max<uint32>(GetDesc().Extent.X >> MipIndex, 1),
					FMath::Max<uint32>(GetDesc().Extent.Y >> MipIndex, 1),
					FMath::Max<uint32>(GetDesc().Depth    >> MipIndex, 1));
			}
			else
			{
				// upload the texture to the texture slice
				Region = MTLRegionMake2D(
					0, 0,
					FMath::Max<uint32>(GetDesc().Extent.X >> MipIndex, 1),
					FMath::Max<uint32>(GetDesc().Extent.Y >> MipIndex, 1)
				);
			}
			
			if ([Texture.GetPtr() storageMode] == MTLStorageModePrivate)
			{
				// If we are running with command lists or the RHI thread is enabled we have to execute GFX commands in that context.
				auto CopyTexToBuf =
				[this, &ArrayIndex, &MipIndex, &Region, &SourceData, &DestStride, &MipBytes](FRHICommandListImmediate& RHICmdList)
				{
					GetAGXDeviceContext().CopyFromTextureToBuffer(this->Texture, ArrayIndex, MipIndex, Region.origin, Region.size, SourceData, 0, DestStride, MipBytes, MTLBlitOptionNone);
					//kick the current command buffer.
					GetAGXDeviceContext().SubmitCommandBufferAndWait();
				};
				
				if (bIssueImmediateCommands)
				{
					CopyTexToBuf(RHICmdList);
				}
				else
				{
					RHICmdList.EnqueueLambda(MoveTemp(CopyTexToBuf));
					RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
				}
			}
			else
			{
#if PLATFORM_MAC
				if((GPUReadback & EAGXGPUReadbackFlags::ReadbackRequestedAndComplete) != EAGXGPUReadbackFlags::ReadbackRequestedAndComplete)
				{
					// A previous texture sync has not been done, need the data now, request texture sync and kick the current command buffer.
					auto SyncReadbackToCPU =
					[this, &ArrayIndex, &MipIndex](FRHICommandListImmediate& RHICmdList)
					{
						GetAGXDeviceContext().SynchronizeTexture(this->Texture, ArrayIndex, MipIndex);
						GetAGXDeviceContext().SubmitCommandBufferAndWait();
					};
					
					// Similar to above. If we are in a context where we have command lists or the RHI thread we must execute
					// commands there. Otherwise we can just do this directly.
					if (bIssueImmediateCommands)
					{
						SyncReadbackToCPU(RHICmdList);
					}
					else
					{
						RHICmdList.EnqueueLambda(MoveTemp(SyncReadbackToCPU));
						RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
					}
				}
#endif
				
				// This block breaks the texture atlas system in Ocean, which depends on nonzero strides coming back from compressed textures. Turning off.
#if 0
				if (GetDesc().Format == PF_PVRTC2 || GetDesc().Format == PF_PVRTC4)
				{
					// for compressed textures metal debug RT expects 0 for rowBytes and imageBytes.
					DestStride = 0;
					MipBytes = 0;
				}
#endif
				uint32 BytesPerRow = DestStride;
				if (GetDesc().Format == PF_PVRTC2 || GetDesc().Format == PF_PVRTC4)
				{
					// for compressed textures metal debug RT expects 0 for rowBytes and imageBytes.
					BytesPerRow = 0;
					MipBytes = 0;
				}

				[Texture.GetPtr() getBytes:(void*)((uint8*)[SourceData.GetPtr() contents] + SourceData.GetOffset())
							   bytesPerRow:BytesPerRow
							 bytesPerImage:MipBytes
								fromRegion:Region
							   mipmapLevel:MipIndex
									 slice:ArrayIndex];
			}
			
#if PLATFORM_MAC
			// Pack RGBA8_sRGB into R8_sRGB for Mac.
			if (   GetDesc().Format == PF_G8
				&& GetDesc().Dimension == ETextureDimension::Texture2D
				&& EnumHasAnyFlags(GetDesc().Flags, TexCreate_SRGB)
				&& (MTLPixelFormat)Texture.GetPixelFormat() == MTLPixelFormatRGBA8Unorm_sRGB)
			{
				TArray<uint8> Data;
				uint8* ExpandedMem = (uint8*)SourceData.GetContents();
				Data.Append(ExpandedMem, MipBytes);
				uint32 SrcStride = DestStride;
				DestStride = FMath::Max<uint32>(GetDesc().Extent.X >> MipIndex, 1);
				for(uint y = 0; y < FMath::Max<uint32>(GetDesc().Extent.Y >> MipIndex, 1); y++)
				{
					uint8* RowDest = ExpandedMem;
					for(uint x = 0; x < FMath::Max<uint32>(GetDesc().Extent.X >> MipIndex, 1); x++)
					{
						*(RowDest++) = Data[(y * SrcStride) + (x * 4)];
					}
					ExpandedMem = (ExpandedMem + DestStride);
				}
			}
#endif
			
			break;
		}
		case RLM_WriteOnly:
		{
			break;
		}
		default:
			check(false);
			break;
	}
	
	return SourceData.GetContents();
}

void FAGXSurface::Unlock(uint32 MipIndex, uint32 ArrayIndex, bool bTryAsync)
{
	check(IsInRenderingThread());
	
	const uint32 LockIndex = ComputeLockIndex(MipIndex, ArrayIndex);
	FRHILockTracker::FLockParams Params = GRHILockTracker.Unlock(this, LockIndex);
	
	id <MTLBuffer> SourceData = (id <MTLBuffer>) Params.Buffer;
	if(bTryAsync)
	{
		AsyncUnlock(SourceData, MipIndex, ArrayIndex);
	}
	else
	{
		UpdateSurfaceAndDestroySourceBuffer(SourceData, MipIndex, ArrayIndex);
	}
}

void* FAGXSurface::AsyncLock(class FRHICommandListImmediate& RHICmdList, uint32 MipIndex, uint32 ArrayIndex, EResourceLockMode LockMode, uint32& DestStride, bool bNeedsDefaultRHIFlush)
{
	bool bDirectLock = (LockMode == RLM_ReadOnly || !GIsRHIInitialized);
	
	void* BufferData = nullptr;
	
	// Never flush for writing, it is unnecessary
	if (bDirectLock)
	{
		if (bNeedsDefaultRHIFlush)
		{
			// @todo Not all read locks need to flush either, but that'll require resource use tracking
			QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_LockTexture2D_Flush);
			RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
		}
		BufferData = Lock(MipIndex, ArrayIndex, LockMode, DestStride);
	}
	else
	{
		id <MTLBuffer> Buffer = AllocSurface(MipIndex, ArrayIndex, LockMode, DestStride);
		check(Buffer);
		
		BufferData = Buffer.contents;
	}
	
	check(BufferData);
	
	return BufferData;
}

struct FAGXRHICommandUnlockTextureUpdate final : public FRHICommand<FAGXRHICommandUnlockTextureUpdate>
{
	FAGXSurface* Surface;
	id <MTLBuffer> UpdateData;
	uint32 MipIndex;
	
	FORCEINLINE_DEBUGGABLE FAGXRHICommandUnlockTextureUpdate(FAGXSurface* InSurface, id <MTLBuffer> InUpdateData, uint32 InMipIndex)
	: Surface(InSurface)
	, UpdateData(InUpdateData)
	, MipIndex(InMipIndex)
	{
		[UpdateData retain];
	}
	
	void Execute(FRHICommandListBase& CmdList)
	{
		Surface->UpdateSurfaceAndDestroySourceBuffer(UpdateData, MipIndex, 0);
	}
	
	virtual ~FAGXRHICommandUnlockTextureUpdate()
	{
		[UpdateData release];
	}
};

void FAGXSurface::AsyncUnlock(id <MTLBuffer> SourceData, uint32 MipIndex, uint32 ArrayIndex)
{
	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
	if (RHICmdList.Bypass() || !IsRunningRHIInSeparateThread())
	{
		UpdateSurfaceAndDestroySourceBuffer(SourceData, MipIndex, ArrayIndex);
	}
	else
	{
		new (RHICmdList.AllocCommand<FAGXRHICommandUnlockTextureUpdate>()) FAGXRHICommandUnlockTextureUpdate(this, SourceData, MipIndex);
	}
}

uint32 FAGXSurface::GetMipSize(uint32 MipIndex, uint32* Stride, bool bSingleLayer)
{
	EPixelFormat PixelFormat = GetDesc().Format;

	// DXT/BC formats on Mac actually do have mip-tails that are smaller than the block size, they end up being uncompressed.
	bool const bPixelFormatASTC = IsPixelFormatASTCCompressed(PixelFormat);
	
	// Calculate the dimensions of the mip-map.
	const uint32 BlockSizeX = GPixelFormats[PixelFormat].BlockSizeX;
	const uint32 BlockSizeY = GPixelFormats[PixelFormat].BlockSizeY;
	const uint32 BlockBytes = GPixelFormats[PixelFormat].BlockBytes;
	const uint32 Alignment = 1u; // Apparently we always want natural row alignment (tightly-packed) even though the docs say iOS doesn't support it - this may be because we don't upload texture data from one contiguous buffer.
	const uint32 UnalignedMipSizeX = FMath::Max<uint32>(GetDesc().Extent.X >> MipIndex, BlockSizeX);
	const uint32 UnalignedMipSizeY = FMath::Max<uint32>(GetDesc().Extent.Y >> MipIndex, BlockSizeY);
	const uint32 MipSizeX = (bPixelFormatASTC) ? AlignArbitrary(UnalignedMipSizeX, BlockSizeX) : UnalignedMipSizeX;
	const uint32 MipSizeY = (bPixelFormatASTC) ? AlignArbitrary(UnalignedMipSizeY, BlockSizeY) : UnalignedMipSizeY;
	
	const uint32 MipSizeZ = bSingleLayer ? 1 : FMath::Max<uint32>(GetDesc().Depth >> MipIndex, 1u);
	uint32 NumBlocksX = (MipSizeX + BlockSizeX - 1) / BlockSizeX;
	uint32 NumBlocksY = (MipSizeY + BlockSizeY - 1) / BlockSizeY;
	if (PixelFormat == PF_PVRTC2 || PixelFormat == PF_PVRTC4)
	{
		// PVRTC has minimum 2 blocks width and height
		NumBlocksX = FMath::Max<uint32>(NumBlocksX, 2);
		NumBlocksY = FMath::Max<uint32>(NumBlocksY, 2);
	}
#if PLATFORM_MAC
	else if (PixelFormat == PF_G8 && EnumHasAnyFlags(GetDesc().Flags, TexCreate_SRGB) && (MTLPixelFormat)Texture.GetPixelFormat() == MTLPixelFormatRGBA8Unorm_sRGB)
	{
		// RGBA_sRGB is the closest match - so expand the data.
		NumBlocksX *= 4;
	}
#endif
	
	const uint32 MipStride = NumBlocksX * BlockBytes;
	const uint32 AlignedStride = ((MipStride - 1) & ~(Alignment - 1)) + Alignment;
	
	const uint32 MipBytes = AlignedStride * NumBlocksY * MipSizeZ;
	
	if (Stride)
	{
		*Stride = AlignedStride;
	}
	
	return MipBytes;
}

uint32 FAGXSurface::GetMemorySize()
{
	// if already calculated, no need to do it again
	if (TotalTextureSize != 0)
	{
		return TotalTextureSize;
	}
	
	if (Texture.GetPtr() == nil)
	{
		return 0;
	}
	
	uint32 TotalSize = 0;
	for (uint32 MipIndex = 0; MipIndex < Texture.GetMipmapLevelCount(); MipIndex++)
	{
		TotalSize += GetMipSize(MipIndex, NULL, false);
	}
	
	return TotalSize;
}

uint32 FAGXSurface::GetNumFaces()
{
	return GetDesc().Depth * GetDesc().ArraySize;
}

void FAGXSurface::GetDrawableTexture()
{
	if (!Texture && EnumHasAnyFlags(GetDesc().Flags, TexCreate_Presentable))
	{
		check(Viewport);
		Texture = Viewport->GetDrawableTexture(EAGXViewportAccessRHI);
	}
}

id<MTLTexture> FAGXSurface::GetCurrentTexture()
{
	id<MTLTexture> MtlTexture = nil;
	if (Viewport && EnumHasAnyFlags(GetDesc().Flags, TexCreate_Presentable))
	{
		check(Viewport);
		MtlTexture = Viewport->GetCurrentTexture(EAGXViewportAccessRHI);
	}
	return MtlTexture;
}


/*-----------------------------------------------------------------------------
 Texture allocator support.
 -----------------------------------------------------------------------------*/

void FAGXDynamicRHI::RHIGetTextureMemoryStats(FTextureMemoryStats& OutStats)
{
	if(MemoryStats.TotalGraphicsMemory > 0)
	{
		OutStats.DedicatedVideoMemory = MemoryStats.DedicatedVideoMemory;
		OutStats.DedicatedSystemMemory = MemoryStats.DedicatedSystemMemory;
		OutStats.SharedSystemMemory = MemoryStats.SharedSystemMemory;
		OutStats.TotalGraphicsMemory = MemoryStats.TotalGraphicsMemory;
	}
	else
	{
		OutStats.DedicatedVideoMemory = 0;
		OutStats.DedicatedSystemMemory = 0;
		OutStats.SharedSystemMemory = 0;
		OutStats.TotalGraphicsMemory = 0;
	}
	
	OutStats.AllocatedMemorySize = int64(GCurrentTextureMemorySize) * 1024;
	OutStats.LargestContiguousAllocation = OutStats.AllocatedMemorySize;
	OutStats.TexturePoolSize = GTexturePoolSize;
	OutStats.PendingMemoryAdjustment = 0;
}

bool FAGXDynamicRHI::RHIGetTextureMemoryVisualizeData( FColor* /*TextureData*/, int32 /*SizeX*/, int32 /*SizeY*/, int32 /*Pitch*/, int32 /*PixelSize*/ )
{
	NOT_SUPPORTED("RHIGetTextureMemoryVisualizeData");
	return false;
}

uint32 FAGXDynamicRHI::RHIComputeMemorySize(FRHITexture* TextureRHI)
{
	@autoreleasepool {
		if(!TextureRHI)
		{
			return 0;
		}
		
		return AGXGetMetalSurfaceFromRHITexture(TextureRHI)->GetMemorySize();
	}
}

/*-----------------------------------------------------------------------------
 2D texture support.
 -----------------------------------------------------------------------------*/

FTextureRHIRef FAGXDynamicRHI::RHICreateTexture_RenderThread(class FRHICommandListImmediate& RHICmdList, const FRHITextureCreateDesc& CreateDesc)
{
	@autoreleasepool{
		return this->RHICreateTexture(CreateDesc);
	}
}

FTextureRHIRef FAGXDynamicRHI::RHICreateTexture(const FRHITextureCreateDesc& CreateDesc)
{
	@autoreleasepool{
		return new FAGXSurface(CreateDesc);
	}
}

FTexture2DRHIRef FAGXDynamicRHI::RHIAsyncCreateTexture2D(uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, ERHIAccess InResourceState, void** InitialMipData, uint32 NumInitialMips)
{
	UE_LOG(LogAGX, Fatal, TEXT("RHIAsyncCreateTexture2D is not supported"));
	return FTexture2DRHIRef();
}

void FAGXDynamicRHI::RHICopySharedMips(FRHITexture2D* DestTexture2D, FRHITexture2D* SrcTexture2D)
{
	NOT_SUPPORTED("RHICopySharedMips");
}

void FAGXDynamicRHI::RHIGenerateMips(FRHITexture* SourceSurfaceRHI)
{
	@autoreleasepool {
		FAGXSurface* Surf = AGXGetMetalSurfaceFromRHITexture(SourceSurfaceRHI);
		if (Surf && Surf->Texture)
		{
			ImmediateContext.GetInternalContext().AsyncGenerateMipmapsForTexture(Surf->Texture);
		}
	}
}

FTexture2DRHIRef FAGXDynamicRHI::AsyncReallocateTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture2D, int32 NewMipCount, int32 NewSizeX, int32 NewSizeY, FThreadSafeCounter* RequestStatus)
{
	return this->RHIAsyncReallocateTexture2D(Texture2D, NewMipCount, NewSizeX, NewSizeY, RequestStatus);
}

ETextureReallocationStatus FAGXDynamicRHI::FinalizeAsyncReallocateTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture2D, bool bBlockUntilCompleted)
{
	// No need to flush - does nothing
	return this->RHIFinalizeAsyncReallocateTexture2D(Texture2D, bBlockUntilCompleted);
}

ETextureReallocationStatus FAGXDynamicRHI::CancelAsyncReallocateTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture2D, bool bBlockUntilCompleted)
{
	// No need to flush - does nothing
	return this->RHICancelAsyncReallocateTexture2D(Texture2D, bBlockUntilCompleted);
}

FTexture2DRHIRef FAGXDynamicRHI::RHIAsyncReallocateTexture2D(FRHITexture2D* OldTextureRHI, int32 NewMipCount, int32 NewSizeX, int32 NewSizeY, FThreadSafeCounter* RequestStatus)
{
	@autoreleasepool {

		check(IsInRenderingThread());
		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

		FAGXSurface* OldTexture = ResourceCast(OldTextureRHI);

		FRHITextureDesc Desc = OldTexture->GetDesc();
		Desc.Extent = FIntPoint(NewSizeX, NewSizeY);
		Desc.NumMips = NewMipCount;

		FRHITextureCreateDesc CreateDesc(
			Desc,
			RHIGetDefaultResourceState(Desc.Flags, false),
			TEXT("RHIAsyncReallocateTexture2D")
		);
		
		FAGXSurface* NewTexture = new FAGXSurface(CreateDesc);

		// Copy shared mips
		RHICmdList.EnqueueLambda([this, OldTexture, NewSizeX, NewSizeY, NewTexture, RequestStatus](FRHICommandListImmediate& RHICmdList)
		{
			FAGXContext& Context = ImmediateContext.GetInternalContext();

			// figure out what mips to schedule
			const uint32 NumSharedMips = FMath::Min(OldTexture->GetNumMips(), NewTexture->GetNumMips());
			const uint32 SourceMipOffset = OldTexture->GetNumMips() - NumSharedMips;
			const uint32 DestMipOffset = NewTexture->GetNumMips() - NumSharedMips;

			const uint32 BlockSizeX = GPixelFormats[OldTexture->GetFormat()].BlockSizeX;
			const uint32 BlockSizeY = GPixelFormats[OldTexture->GetFormat()].BlockSizeY;

			// only handling straight 2D textures here
			uint32 SliceIndex = 0;
			MTLOrigin Origin = MTLOriginMake(0, 0, 0);

			FAGXTexture Tex = OldTexture->Texture;

			// DXT/BC formats on Mac actually do have mip-tails that are smaller than the block size, they end up being uncompressed.
			bool const bPixelFormatASTC = IsPixelFormatASTCCompressed(OldTexture->GetFormat());

			bool bAsync = true;
			for (uint32 MipIndex = 0; MipIndex < NumSharedMips; ++MipIndex)
			{
				const uint32 UnalignedMipSizeX = FMath::Max<uint32>(1, NewSizeX >> (MipIndex + DestMipOffset));
				const uint32 UnalignedMipSizeY = FMath::Max<uint32>(1, NewSizeY >> (MipIndex + DestMipOffset));
				const uint32 MipSizeX = FMath::Max<uint32>(1, NewSizeX >> (MipIndex + DestMipOffset));
				const uint32 MipSizeY = FMath::Max<uint32>(1, NewSizeY >> (MipIndex + DestMipOffset));

				bAsync &= Context.AsyncCopyFromTextureToTexture(OldTexture->Texture, SliceIndex, MipIndex + SourceMipOffset, Origin, MTLSizeMake(MipSizeX, MipSizeY, 1), NewTexture->Texture, SliceIndex, MipIndex + DestMipOffset, Origin);
			}

			// when done, decrement the counter to indicate it's safe
			mtlpp::CommandBufferHandler CompletionHandler = [Tex](mtlpp::CommandBuffer const&)
			{
			};

			if (bAsync)
			{
				// kck it off!
				Context.SubmitAsyncCommands(nil, CompletionHandler, false);
			}
			else
			{
				Context.GetCurrentRenderPass().AddCompletionHandler(CompletionHandler);
			}

			// Like D3D mark this as complete immediately.
			RequestStatus->Decrement();

			FAGXSurface* Source = AGXGetMetalSurfaceFromRHITexture(OldTexture);
			Source->MakeAliasable();
		});
		
		return NewTexture;
	}
}

ETextureReallocationStatus FAGXDynamicRHI::RHIFinalizeAsyncReallocateTexture2D(FRHITexture2D* Texture2D, bool bBlockUntilCompleted )
{
	return TexRealloc_Succeeded;
}

ETextureReallocationStatus FAGXDynamicRHI::RHICancelAsyncReallocateTexture2D(FRHITexture2D* Texture2D, bool bBlockUntilCompleted )
{
	return TexRealloc_Failed;
}

void* FAGXDynamicRHI::LockTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture, uint32 MipIndex, EResourceLockMode LockMode, uint32& DestStride, bool bLockWithinMiptail, bool bNeedsDefaultRHIFlush)
{
	@autoreleasepool {
		check(IsInRenderingThread());
		
		FAGXSurface* TextureMTL = ResourceCast(Texture);
		
		void* BufferData = TextureMTL->AsyncLock(RHICmdList, MipIndex, 0, LockMode, DestStride, bNeedsDefaultRHIFlush);
		
		return BufferData;
	}
}

void FAGXDynamicRHI::UnlockTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture, uint32 MipIndex, bool bLockWithinMiptail, bool bNeedsDefaultRHIFlush)
{
	@autoreleasepool
	{
		check(IsInRenderingThread());
		
		FAGXSurface* TextureMTL = ResourceCast(Texture);
		TextureMTL->Unlock(MipIndex, 0, true);
	}
}


void* FAGXDynamicRHI::RHILockTexture2D(FRHITexture2D* TextureRHI,uint32 MipIndex,EResourceLockMode LockMode,uint32& DestStride,bool bLockWithinMiptail)
{
	@autoreleasepool {
		FAGXSurface* Texture = ResourceCast(TextureRHI);
		return Texture->Lock(MipIndex, 0, LockMode, DestStride);
	}
}

void FAGXDynamicRHI::RHIUnlockTexture2D(FRHITexture2D* TextureRHI,uint32 MipIndex,bool bLockWithinMiptail)
{
	@autoreleasepool {
		FAGXSurface* Texture = ResourceCast(TextureRHI);
		Texture->Unlock(MipIndex, 0, false);
	}
}

void* FAGXDynamicRHI::RHILockTexture2DArray(FRHITexture2DArray* TextureRHI,uint32 TextureIndex,uint32 MipIndex,EResourceLockMode LockMode,uint32& DestStride,bool bLockWithinMiptail)
{
	@autoreleasepool {
		FAGXSurface* Texture = ResourceCast(TextureRHI);
		return Texture->Lock(MipIndex, TextureIndex, LockMode, DestStride);
	}
}

void FAGXDynamicRHI::RHIUnlockTexture2DArray(FRHITexture2DArray* TextureRHI,uint32 TextureIndex,uint32 MipIndex,bool bLockWithinMiptail)
{
	@autoreleasepool {
		FAGXSurface* Texture = ResourceCast(TextureRHI);
		Texture->Unlock(MipIndex, TextureIndex, false);
	}
}

#if PLATFORM_MAC
static void InternalExpandR8ToStandardRGBA(uint32* pDest, const struct FUpdateTextureRegion2D& UpdateRegion, uint32& InOutSourcePitch, const uint8* pSrc)
{
	const uint32 ExpandedPitch = UpdateRegion.Width * sizeof(uint32);
	
	for(uint y = 0; y < UpdateRegion.Height; y++)
	{
		for(uint x = 0; x < UpdateRegion.Width; x++)
		{
			uint8 Value = pSrc[(y * InOutSourcePitch) + x];
			*(pDest++) = (Value | (Value << 8) | (Value << 16) | (Value << 24));
		}
	}
	
	InOutSourcePitch = ExpandedPitch;
}
#endif

static FAGXBuffer Internal_CreateBufferAndCopyTexture2DUpdateRegionData(FRHITexture2D* TextureRHI, const struct FUpdateTextureRegion2D& UpdateRegion, uint32& InOutSourcePitch, const uint8* SourceData)
{
	FAGXBuffer OutBuffer;

	FAGXSurface* Texture = ResourceCast(TextureRHI);	

#if PLATFORM_MAC
	// Expand R8_sRGB into RGBA8_sRGB for Mac.
	if (   Texture->GetFormat() == PF_G8
		&& EnumHasAnyFlags(Texture->GetFlags(), TexCreate_SRGB)
		&& (MTLPixelFormat)Texture->Texture.GetPixelFormat() == MTLPixelFormatRGBA8Unorm_sRGB)
	{
		const uint32 ExpandedBufferSize = UpdateRegion.Height * UpdateRegion.Width * sizeof(uint32);
		OutBuffer = GetAGXDeviceContext().CreatePooledBuffer(FAGXPooledBufferArgs(ExpandedBufferSize, BUF_Static, FAGXPooledBufferArgs::SharedStorageResourceOptions));
		InternalExpandR8ToStandardRGBA((uint32*)OutBuffer.GetContents(), UpdateRegion, InOutSourcePitch, SourceData);
	}
	else
#endif
	{
		const FPixelFormatInfo& FormatInfo = GPixelFormats[TextureRHI->GetFormat()];
		
		const uint32 BufferSize = UpdateRegion.Height * InOutSourcePitch;
		OutBuffer = GetAGXDeviceContext().CreatePooledBuffer(FAGXPooledBufferArgs(BufferSize, BUF_Static, FAGXPooledBufferArgs::SharedStorageResourceOptions));

		uint32 CopyPitch = FMath::DivideAndRoundUp(UpdateRegion.Width, (uint32)FormatInfo.BlockSizeX) * FormatInfo.BlockBytes;
		check(CopyPitch <= InOutSourcePitch);
		
		uint8* pDestRow = (uint8*)OutBuffer.GetContents();
		uint8* pSourceRow = (uint8*)SourceData;
		const uint32 NumRows = UpdateRegion.Height / (uint32)FormatInfo.BlockSizeY;

		// Limit copy to line by line by update region pitch otherwise we can go off the end of source data on the last row
		for (uint32 i = 0;i < NumRows;++i)
		{
			FMemory::Memcpy(pDestRow, pSourceRow, CopyPitch);
			pSourceRow += InOutSourcePitch;
			pDestRow += InOutSourcePitch;
		}
	}

	return OutBuffer;
}

static void InternalUpdateTexture2D(FAGXContext& Context, FRHITexture2D* TextureRHI, uint32 MipIndex, FUpdateTextureRegion2D const& UpdateRegion, uint32 SourcePitch, FAGXBuffer Buffer)
{
	FAGXSurface* Texture = ResourceCast(TextureRHI);
	FAGXTexture Tex = Texture->Texture;
	
	MTLRegion Region = MTLRegionMake2D(UpdateRegion.DestX, UpdateRegion.DestY, UpdateRegion.Width, UpdateRegion.Height);
	
	if ([Tex.GetPtr() storageMode] == MTLStorageModePrivate)
	{
		SCOPED_AUTORELEASE_POOL;
		
		const FPixelFormatInfo& FormatInfo = GPixelFormats[TextureRHI->GetFormat()];
		const uint32 NumRows = UpdateRegion.Height / (uint32)FormatInfo.BlockSizeY;
		uint32 BytesPerImage = SourcePitch * NumRows;
		
		MTLBlitOption Options = MTLBlitOptionNone;
#if !PLATFORM_MAC
		if ((MTLPixelFormat)Tex.GetPixelFormat() >= MTLPixelFormatPVRTC_RGB_2BPP && (MTLPixelFormat)Tex.GetPixelFormat() <= MTLPixelFormatPVRTC_RGBA_4BPP_sRGB)
		{
			Options = MTLBlitOptionRowLinearPVRTC;
		}
#endif
		if(Context.AsyncCopyFromBufferToTexture(Buffer, 0, SourcePitch, BytesPerImage, Region.size, Tex, 0, MipIndex, Region.origin, Options))
		{
			Context.SubmitAsyncCommands(nil, nil, false);
		}
	}
	else
	{
		[Tex.GetPtr() replaceRegion:Region
						mipmapLevel:MipIndex
						  withBytes:(const void*)((uint8*)[Buffer.GetPtr() contents] + Buffer.GetOffset())
						bytesPerRow:SourcePitch];
	}

	FPlatformAtomics::InterlockedExchange(&Texture->Written, 1);
}

struct FAGXRHICommandUpdateTexture2D final : public FRHICommand<FAGXRHICommandUpdateTexture2D>
{
	FAGXContext& Context;
	FRHITexture2D* Texture;
	uint32 MipIndex;
	FUpdateTextureRegion2D UpdateRegion;
	uint32 SourcePitch;
	FAGXBuffer SourceBuffer;

	FORCEINLINE_DEBUGGABLE FAGXRHICommandUpdateTexture2D(FAGXContext& InContext, FRHITexture2D* InTexture, uint32 InMipIndex, FUpdateTextureRegion2D InUpdateRegion, uint32 InSourcePitch, const uint8* SourceData)
	: Context(InContext)
	, Texture(InTexture)
	, MipIndex(InMipIndex)
	, UpdateRegion(InUpdateRegion)
	, SourcePitch(InSourcePitch)
	{
		SourceBuffer = Internal_CreateBufferAndCopyTexture2DUpdateRegionData(Texture, UpdateRegion, SourcePitch, SourceData);
	}
	
	void Execute(FRHICommandListBase& CmdList)
	{		
		InternalUpdateTexture2D(Context, Texture, MipIndex, UpdateRegion, SourcePitch, SourceBuffer);
		GetAGXDeviceContext().ReleaseBuffer(SourceBuffer);
		INC_DWORD_STAT_BY(STAT_AGXTextureMemUpdate, UpdateRegion.Height * SourcePitch);
	}
};

void FAGXDynamicRHI::UpdateTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture, uint32 MipIndex, const struct FUpdateTextureRegion2D& UpdateRegion, uint32 SourcePitch, const uint8* SourceData)
{
	@autoreleasepool {
		if (RHICmdList.Bypass() || !IsRunningRHIInSeparateThread())
		{
			this->RHIUpdateTexture2D(Texture, MipIndex, UpdateRegion, SourcePitch, SourceData);
		}
		else
		{
			new (RHICmdList.AllocCommand<FAGXRHICommandUpdateTexture2D>()) FAGXRHICommandUpdateTexture2D(ImmediateContext.GetInternalContext(), Texture, MipIndex, UpdateRegion, SourcePitch, SourceData);
		}
	}
}

void FAGXDynamicRHI::RHIUpdateTexture2D(FRHITexture2D* TextureRHI, uint32 MipIndex, const struct FUpdateTextureRegion2D& UpdateRegion, uint32 SourcePitch, const uint8* SourceData)
{
	@autoreleasepool 
	{
		FAGXSurface* Texture = ResourceCast(TextureRHI);
		FAGXTexture Tex = Texture->Texture;
		
		if([Tex.GetPtr() storageMode] == MTLStorageModePrivate)
		{
			FAGXBuffer Buffer = Internal_CreateBufferAndCopyTexture2DUpdateRegionData(TextureRHI, UpdateRegion, SourcePitch, SourceData);
			InternalUpdateTexture2D(ImmediateContext.GetInternalContext(), TextureRHI, MipIndex, UpdateRegion, SourcePitch, Buffer);
			GetAGXDeviceContext().ReleaseBuffer(Buffer);
		}
		else
		{
#if PLATFORM_MAC
			TArray<uint32> ExpandedData;
			if (Texture->GetFormat() == PF_G8 && EnumHasAnyFlags(Texture->GetFlags(), TexCreate_SRGB) && (MTLPixelFormat)Tex.GetPixelFormat() == MTLPixelFormatRGBA8Unorm_sRGB)
			{
				ExpandedData.AddZeroed(UpdateRegion.Height * UpdateRegion.Width);
				InternalExpandR8ToStandardRGBA((uint32*)ExpandedData.GetData(), UpdateRegion, SourcePitch, SourceData);
				SourceData = (uint8*)ExpandedData.GetData();
			}
#endif
			MTLRegion Region = MTLRegionMake2D(UpdateRegion.DestX, UpdateRegion.DestY, UpdateRegion.Width, UpdateRegion.Height);

			[Tex.GetPtr() replaceRegion:Region
							mipmapLevel:MipIndex
							  withBytes:(const void*)SourceData
							bytesPerRow:SourcePitch];

			FPlatformAtomics::InterlockedExchange(&Texture->Written, 1);
		}
		
		INC_DWORD_STAT_BY(STAT_AGXTextureMemUpdate, UpdateRegion.Height*SourcePitch);
	}
}

static FAGXBuffer Internal_CreateBufferAndCopyTexture3DUpdateRegionData(FRHITexture3D* TextureRHI, const struct FUpdateTextureRegion3D& UpdateRegion, uint32 SourceRowPitch, uint32 SourceDepthPitch, const uint8* SourceData)
{
	FAGXSurface* Texture = ResourceCast(TextureRHI);
	
	const uint32 BufferSize = SourceDepthPitch * UpdateRegion.Depth;
	FAGXBuffer OutBuffer = GetAGXDeviceContext().CreatePooledBuffer(FAGXPooledBufferArgs(BufferSize, BUF_Static, FAGXPooledBufferArgs::SharedStorageResourceOptions));

	const FPixelFormatInfo& FormatInfo = GPixelFormats[TextureRHI->GetFormat()];
	uint32 CopyPitch = FMath::DivideAndRoundUp(UpdateRegion.Width, (uint32)FormatInfo.BlockSizeX) * FormatInfo.BlockBytes;
	
	check(FormatInfo.BlockSizeZ == 1);
	check(CopyPitch <= SourceRowPitch);
	
	uint8_t* DestData = (uint8_t*)OutBuffer.GetContents();
	const uint32 NumRows = UpdateRegion.Height / (uint32)FormatInfo.BlockSizeY;
	
	// Perform safe line copy
	for (uint32 i = 0;i < UpdateRegion.Depth;++i)
	{
		const uint8* pSourceRowData = SourceData + (SourceDepthPitch * i);
		uint8* pDestRowData = DestData + (SourceDepthPitch * i);

		for (uint32 j = 0;j < NumRows;++j)
		{
			FMemory::Memcpy(pDestRowData, pSourceRowData, CopyPitch);
			pSourceRowData += SourceRowPitch;
			pDestRowData += SourceRowPitch;
		}
	}
	
	return OutBuffer;
}


static void InternalUpdateTexture3D(FAGXContext& Context, FRHITexture3D* TextureRHI, uint32 MipIndex, const FUpdateTextureRegion3D& UpdateRegion, uint32 SourceRowPitch, uint32 SourceDepthPitch, FAGXBuffer Buffer)
{
	FAGXSurface* Texture = ResourceCast(TextureRHI);
	FAGXTexture Tex = Texture->Texture;
	
	MTLRegion Region = MTLRegionMake3D(UpdateRegion.DestX, UpdateRegion.DestY, UpdateRegion.DestZ, UpdateRegion.Width, UpdateRegion.Height, UpdateRegion.Depth);
	
	if ([Tex.GetPtr() storageMode] == MTLStorageModePrivate)
	{
		const FPixelFormatInfo& FormatInfo = GPixelFormats[TextureRHI->GetFormat()];
		const uint32 NumRows = UpdateRegion.Height / (uint32)FormatInfo.BlockSizeY;
		const uint32 BytesPerImage = SourceRowPitch * NumRows;

		MTLBlitOption Options = MTLBlitOptionNone;
#if !PLATFORM_MAC
		if ((MTLPixelFormat)Tex.GetPixelFormat() >= MTLPixelFormatPVRTC_RGB_2BPP && (MTLPixelFormat)Tex.GetPixelFormat() <= MTLPixelFormatPVRTC_RGBA_4BPP_sRGB)
		{
			Options = MTLBlitOptionRowLinearPVRTC;
		}
#endif
		if(Context.AsyncCopyFromBufferToTexture(Buffer, 0, SourceRowPitch, BytesPerImage, Region.size, Tex, 0, MipIndex, Region.origin, Options))
		{
			Context.SubmitAsyncCommands(nil, nil, false);
		}
	}
	else
	{
		[Tex.GetPtr() replaceRegion:Region
						mipmapLevel:MipIndex
							  slice:0
						  withBytes:(const void*)((uint8*)[Buffer.GetPtr() contents] + Buffer.GetOffset())
						bytesPerRow:SourceRowPitch
					  bytesPerImage:SourceDepthPitch];
	}

	FPlatformAtomics::InterlockedExchange(&Texture->Written, 1);
}

struct FAGXDynamicRHIUpdateTexture3DCommand final : public FRHICommand<FAGXDynamicRHIUpdateTexture3DCommand>
{
	FAGXContext& Context;
	FRHITexture3D* DestinationTexture;
	uint32 MipIndex;
	FUpdateTextureRegion3D UpdateRegion;
	uint32 SourceRowPitch;
	uint32 SourceDepthPitch;
	FAGXBuffer Buffer;
	
	FORCEINLINE_DEBUGGABLE FAGXDynamicRHIUpdateTexture3DCommand(FAGXContext& InContext, FRHITexture3D* TextureRHI, uint32 InMipIndex, const struct FUpdateTextureRegion3D& InUpdateRegion, uint32 InSourceRowPitch, uint32 InSourceDepthPitch, const uint8* SourceData)
	: Context(InContext)
	, DestinationTexture(TextureRHI)
	, MipIndex(InMipIndex)
	, UpdateRegion(InUpdateRegion)
	, SourceRowPitch(InSourceRowPitch)
	, SourceDepthPitch(InSourceDepthPitch)
	{
		Buffer = Internal_CreateBufferAndCopyTexture3DUpdateRegionData(TextureRHI, UpdateRegion, SourceRowPitch, SourceDepthPitch, SourceData);
	}
	
	void Execute(FRHICommandListBase& CmdList)
	{
		InternalUpdateTexture3D(Context, DestinationTexture, MipIndex, UpdateRegion, SourceRowPitch, SourceDepthPitch, Buffer);
		GetAGXDeviceContext().ReleaseBuffer(Buffer);
		INC_DWORD_STAT_BY(STAT_AGXTextureMemUpdate, UpdateRegion.Height * UpdateRegion.Width * SourceDepthPitch);
	}
};

void FAGXDynamicRHI::UpdateTexture3D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture3D* Texture, uint32 MipIndex, const struct FUpdateTextureRegion3D& UpdateRegion, uint32 SourceRowPitch, uint32 SourceDepthPitch, const uint8* SourceData)
{
	if (RHICmdList.Bypass() || !IsRunningRHIInSeparateThread())
	{
		RHIUpdateTexture3D(Texture, MipIndex, UpdateRegion, SourceRowPitch, SourceDepthPitch, SourceData);
	}
	else
	{
		new (RHICmdList.AllocCommand<FAGXDynamicRHIUpdateTexture3DCommand>()) FAGXDynamicRHIUpdateTexture3DCommand(ImmediateContext.GetInternalContext(), Texture, MipIndex, UpdateRegion, SourceRowPitch, SourceDepthPitch, SourceData);
	}
}

FUpdateTexture3DData FAGXDynamicRHI::BeginUpdateTexture3D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture3D* Texture, uint32 MipIndex, const struct FUpdateTextureRegion3D& UpdateRegion)
{
	check(IsInRenderingThread());
	
	const int32 FormatSize = PixelFormatBlockBytes[Texture->GetFormat()];
	const int32 RowPitch = UpdateRegion.Width * FormatSize;
	const int32 DepthPitch = UpdateRegion.Width * UpdateRegion.Height * FormatSize;
	
	SIZE_T MemorySize = DepthPitch * UpdateRegion.Depth;
	uint8* Data = (uint8*)FMemory::Malloc(MemorySize);
	
	return FUpdateTexture3DData(Texture, MipIndex, UpdateRegion, RowPitch, DepthPitch, Data, MemorySize, GFrameNumberRenderThread);
}

void FAGXDynamicRHI::EndUpdateTexture3D_RenderThread(class FRHICommandListImmediate& RHICmdList, FUpdateTexture3DData& UpdateData)
{
	check(IsInRenderingThread());
	check(GFrameNumberRenderThread == UpdateData.FrameNumber);
	
	if (RHICmdList.Bypass() || !IsRunningRHIInSeparateThread())
	{
		GDynamicRHI->RHIUpdateTexture3D(UpdateData.Texture, UpdateData.MipIndex, UpdateData.UpdateRegion, UpdateData.RowPitch, UpdateData.DepthPitch, UpdateData.Data);
	}
	else
	{
		new (RHICmdList.AllocCommand<FAGXDynamicRHIUpdateTexture3DCommand>()) FAGXDynamicRHIUpdateTexture3DCommand(ImmediateContext.GetInternalContext(), UpdateData.Texture, UpdateData.MipIndex, UpdateData.UpdateRegion, UpdateData.RowPitch, UpdateData.DepthPitch, UpdateData.Data);
	}
	
	FMemory::Free(UpdateData.Data);
	UpdateData.Data = nullptr;
}

void FAGXDynamicRHI::RHIUpdateTexture3D(FRHITexture3D* TextureRHI,uint32 MipIndex,const FUpdateTextureRegion3D& UpdateRegion,uint32 SourceRowPitch,uint32 SourceDepthPitch, const uint8* SourceData)
{
	@autoreleasepool {
	
		FAGXSurface* Texture = ResourceCast(TextureRHI);
		FAGXTexture Tex = Texture->Texture;
		
#if PLATFORM_MAC
		checkf(!(Texture->GetFormat() == PF_G8 && EnumHasAnyFlags(Texture->GetFlags(), TexCreate_SRGB) && (MTLPixelFormat)Tex.GetPixelFormat() == MTLPixelFormatRGBA8Unorm_sRGB), TEXT("AGXRHI does not support PF_G8_sRGB on 3D, array or cube textures as it requires manual, CPU-side expansion to RGBA8_sRGB which is expensive!"));
#endif
		if ([Tex.GetPtr() storageMode] == MTLStorageModePrivate)
		{
			SCOPED_AUTORELEASE_POOL;
			FAGXBuffer IntermediateBuffer = Internal_CreateBufferAndCopyTexture3DUpdateRegionData(TextureRHI, UpdateRegion, SourceRowPitch, SourceDepthPitch, SourceData);
			InternalUpdateTexture3D(ImmediateContext.GetInternalContext(), TextureRHI, MipIndex, UpdateRegion, SourceRowPitch, SourceDepthPitch, IntermediateBuffer);
			GetAGXDeviceContext().ReleaseBuffer(IntermediateBuffer);
		}
		else
		{
			MTLRegion Region = MTLRegionMake3D(UpdateRegion.DestX, UpdateRegion.DestY, UpdateRegion.DestZ, UpdateRegion.Width, UpdateRegion.Height, UpdateRegion.Depth);

			[Tex.GetPtr() replaceRegion:Region
							mipmapLevel:MipIndex
								  slice:0
							  withBytes:(const void*)SourceData
							bytesPerRow:SourceRowPitch
						  bytesPerImage:SourceDepthPitch];

			FPlatformAtomics::InterlockedExchange(&Texture->Written, 1);
		}
		
		INC_DWORD_STAT_BY(STAT_AGXTextureMemUpdate, UpdateRegion.Height * UpdateRegion.Width * SourceDepthPitch);
	}
}

/*-----------------------------------------------------------------------------
 Cubemap texture support.
 -----------------------------------------------------------------------------*/
void* FAGXDynamicRHI::RHILockTextureCubeFace(FRHITextureCube* TextureCubeRHI,uint32 FaceIndex,uint32 ArrayIndex,uint32 MipIndex,EResourceLockMode LockMode,uint32& DestStride,bool bLockWithinMiptail)
{
	@autoreleasepool {
		FAGXSurface* TextureCube = ResourceCast(TextureCubeRHI);
		uint32 MetalFace = GetMetalCubeFace((ECubeFace)FaceIndex);
		return TextureCube->Lock(MipIndex, MetalFace + (6 * ArrayIndex), LockMode, DestStride, true);
	}
}

void FAGXDynamicRHI::RHIUnlockTextureCubeFace(FRHITextureCube* TextureCubeRHI,uint32 FaceIndex,uint32 ArrayIndex,uint32 MipIndex,bool bLockWithinMiptail)
{
	@autoreleasepool {
		FAGXSurface* TextureCube = ResourceCast(TextureCubeRHI);
		uint32 MetalFace = GetMetalCubeFace((ECubeFace)FaceIndex);
		TextureCube->Unlock(MipIndex, MetalFace + (ArrayIndex * 6), false);
	}
}

void FAGXDynamicRHI::RHIBindDebugLabelName(FRHITexture* TextureRHI, const TCHAR* Name)
{
	@autoreleasepool {
		FAGXSurface* Surf = AGXGetMetalSurfaceFromRHITexture(TextureRHI);
		if(Surf->Texture)
		{
			Surf->Texture.SetLabel(FString(Name).GetNSString());
		}
		if(Surf->MSAATexture)
		{
			Surf->MSAATexture.SetLabel(FString(Name).GetNSString());
		}
	}
}

void FAGXDynamicRHI::RHIVirtualTextureSetFirstMipInMemory(FRHITexture2D* TextureRHI, uint32 FirstMip)
{
	NOT_SUPPORTED("RHIVirtualTextureSetFirstMipInMemory");
}

void FAGXDynamicRHI::RHIVirtualTextureSetFirstMipVisible(FRHITexture2D* TextureRHI, uint32 FirstMip)
{
	NOT_SUPPORTED("RHIVirtualTextureSetFirstMipVisible");
}

inline bool AGXRHICopyTexutre_IsTextureFormatCompatible(EPixelFormat SrcFmt, EPixelFormat DstFmt)
{
	//
	// For now, we only support copies between textures of mismatching
	// formats if they are of size-compatible internal formats.  This allows us
	// to copy from uncompressed to compressed textures, specifically in support
	// of the runtime virtual texture system.  Note that copies of compatible
	// formats incur the cost of an extra copy, as we must copy from the source
	// texture to a temporary buffer and finally to the destination texture.
	//
	return ((SrcFmt == DstFmt) || (GPixelFormats[SrcFmt].BlockBytes == GPixelFormats[DstFmt].BlockBytes));
}

void FAGXRHICommandContext::RHICopyTexture(FRHITexture* SourceTextureRHI, FRHITexture* DestTextureRHI, const FRHICopyTextureInfo& CopyInfo)
{
	@autoreleasepool {
		check(SourceTextureRHI);
		check(DestTextureRHI);
		
		FAGXSurface* MetalSrcTexture = AGXGetMetalSurfaceFromRHITexture(SourceTextureRHI);
		FAGXSurface* MetalDestTexture = AGXGetMetalSurfaceFromRHITexture(DestTextureRHI);
		
		const bool TextureFormatExactMatch = (SourceTextureRHI->GetFormat() == DestTextureRHI->GetFormat());
		const bool TextureFormatCompatible = AGXRHICopyTexutre_IsTextureFormatCompatible(SourceTextureRHI->GetFormat(), DestTextureRHI->GetFormat());
		
		if (TextureFormatExactMatch || TextureFormatCompatible)
		{
			const FIntVector Size = CopyInfo.Size == FIntVector::ZeroValue ? MetalSrcTexture->GetDesc().GetSize() >> CopyInfo.SourceMipIndex : CopyInfo.Size;

			FAGXTexture SrcTexture;

			if (TextureFormatExactMatch)
			{
				MTLTextureUsage Usage = [MetalSrcTexture->Texture.GetPtr() usage];
				if (Usage & MTLTextureUsagePixelFormatView)
				{
					ns::Range Slices(0, MetalSrcTexture->Texture.GetArrayLength() * (MetalSrcTexture->GetDesc().IsTextureCube() ? 6 : 1));
					if (MetalSrcTexture->Texture.GetPixelFormat() != MetalDestTexture->Texture.GetPixelFormat())
					{
						SrcTexture = MetalSrcTexture->Texture.NewTextureView(MetalDestTexture->Texture.GetPixelFormat(), MetalSrcTexture->Texture.GetTextureType(), ns::Range(0, MetalSrcTexture->Texture.GetMipmapLevelCount()), Slices);
					}
				}
				if (!SrcTexture)
				{
					SrcTexture = MetalSrcTexture->Texture;
				}
			}
			
			for (uint32 SliceIndex = 0; SliceIndex < CopyInfo.NumSlices; ++SliceIndex)
			{
				uint32 SourceSliceIndex = CopyInfo.SourceSliceIndex + SliceIndex;
				uint32 DestSliceIndex = CopyInfo.DestSliceIndex + SliceIndex;

				for (uint32 MipIndex = 0; MipIndex < CopyInfo.NumMips; ++MipIndex)
				{
					uint32 SourceMipIndex = CopyInfo.SourceMipIndex + MipIndex;
					uint32 DestMipIndex = CopyInfo.DestMipIndex + MipIndex;
					MTLSize SourceSize = MTLSizeMake(FMath::Max(Size.X >> MipIndex, 1), FMath::Max(Size.Y >> MipIndex, 1), FMath::Max(Size.Z >> MipIndex, 1));
					MTLSize DestSize = SourceSize;

					MTLOrigin SourceOrigin = MTLOriginMake(CopyInfo.SourcePosition.X >> MipIndex, CopyInfo.SourcePosition.Y >> MipIndex, CopyInfo.SourcePosition.Z >> MipIndex);
					MTLOrigin DestinationOrigin = MTLOriginMake(CopyInfo.DestPosition.X >> MipIndex, CopyInfo.DestPosition.Y >> MipIndex, CopyInfo.DestPosition.Z >> MipIndex);

					if (TextureFormatCompatible)
					{
						DestSize.width  *= GPixelFormats[MetalDestTexture->GetDesc().Format].BlockSizeX;
						DestSize.height *= GPixelFormats[MetalDestTexture->GetDesc().Format].BlockSizeY;
					}

					// Account for create with TexCreate_SRGB flag which could make these different
					if (TextureFormatExactMatch && (SrcTexture.GetPixelFormat() == MetalDestTexture->Texture.GetPixelFormat()))
					{
						GetInternalContext().CopyFromTextureToTexture(SrcTexture, SourceSliceIndex, SourceMipIndex, SourceOrigin, SourceSize, MetalDestTexture->Texture, DestSliceIndex, DestMipIndex, DestinationOrigin);
					}
					else
					{
						//
						// In the case of compatible texture formats or pixel
						// format mismatch (like linear vs. sRGB), then we must
						// achieve the copy by going through a buffer object.
						//
						const bool BlockSizeMatch = (GPixelFormats[MetalSrcTexture->GetDesc().Format].BlockSizeX == GPixelFormats[MetalDestTexture->GetDesc().Format].BlockSizeX);
						const uint32 BytesPerPixel = (MetalSrcTexture->GetDesc().Format != PF_DepthStencil) ? GPixelFormats[MetalSrcTexture->GetDesc().Format].BlockBytes : 1;
						const uint32 Stride = BytesPerPixel * SourceSize.width;
#if PLATFORM_MAC
						const uint32 Alignment = 1u;
#else
						// don't mess with alignment if we copying between formats with a different block size
						const uint32 Alignment = BlockSizeMatch ? 64u : 1u;
#endif
						const uint32 AlignedStride = ((Stride - 1) & ~(Alignment - 1)) + Alignment;
						const uint32 BytesPerImage = AlignedStride *  SourceSize.height;
						const uint32 DataSize = BytesPerImage * SourceSize.depth;
						
						FAGXBuffer Buffer = GetAGXDeviceContext().CreatePooledBuffer(FAGXPooledBufferArgs(DataSize, BUF_Dynamic, FAGXPooledBufferArgs::SharedStorageResourceOptions));
						
						check(Buffer);
						
						MTLBlitOption Options = MTLBlitOptionNone;
#if !PLATFORM_MAC
						if ((MTLPixelFormat)MetalSrcTexture->Texture.GetPixelFormat() >= MTLPixelFormatPVRTC_RGB_2BPP && (MTLPixelFormat)MetalSrcTexture->Texture.GetPixelFormat() <= MTLPixelFormatPVRTC_RGBA_4BPP_sRGB)
						{
							Options = MTLBlitOptionRowLinearPVRTC;
						}
#endif
						GetInternalContext().CopyFromTextureToBuffer(MetalSrcTexture->Texture, SourceSliceIndex, SourceMipIndex, SourceOrigin, SourceSize, Buffer, 0, AlignedStride, BytesPerImage, Options);
						GetInternalContext().CopyFromBufferToTexture(Buffer, 0, Stride, BytesPerImage, DestSize, MetalDestTexture->Texture, DestSliceIndex, DestMipIndex, DestinationOrigin, Options);
						
						GetAGXDeviceContext().ReleaseBuffer(Buffer);
					}
				}
			}
			
			if (SrcTexture && (SrcTexture != MetalSrcTexture->Texture))
			{
				AGXSafeReleaseMetalTexture(SrcTexture);
			}
		}
		else
		{
			UE_LOG(LogAGX, Error, TEXT("RHICopyTexture Source (UnrealEngine %d: MTL %d) <-> Destination (UnrealEngine %d: MTL %d) texture format mismatch"), (uint32)SourceTextureRHI->GetFormat(), (uint32)MetalSrcTexture->Texture.GetPixelFormat(), (uint32)DestTextureRHI->GetFormat(), (uint32)MetalDestTexture->Texture.GetPixelFormat());
		}
	}
}

void FAGXRHICommandContext::RHICopyBufferRegion(FRHIBuffer* DstBufferRHI, uint64 DstOffset, FRHIBuffer* SrcBufferRHI, uint64 SrcOffset, uint64 NumBytes)
{
	if (!DstBufferRHI || !SrcBufferRHI || DstBufferRHI == SrcBufferRHI || !NumBytes)
	{
		return;
	}

	@autoreleasepool {
		FAGXResourceMultiBuffer* DstBuffer = ResourceCast(DstBufferRHI);
		FAGXResourceMultiBuffer* SrcBuffer = ResourceCast(SrcBufferRHI);

		check(DstBuffer && SrcBuffer);
		check(!DstBuffer->Data && !SrcBuffer->Data);
		check(DstOffset + NumBytes <= DstBufferRHI->GetSize() && SrcOffset + NumBytes <= SrcBufferRHI->GetSize());

		GetInternalContext().CopyFromBufferToBuffer(SrcBuffer->GetCurrentBuffer(), SrcOffset, DstBuffer->GetCurrentBuffer(), DstOffset, NumBytes);
	}
}
