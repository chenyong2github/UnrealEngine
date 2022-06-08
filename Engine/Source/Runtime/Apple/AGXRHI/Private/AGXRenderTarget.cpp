// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXRenderTarget.cpp: AGX RHI render target implementation.
=============================================================================*/

#include "AGXRHIPrivate.h"
#include "ScreenRendering.h"
#include "AGXProfiler.h"
#include "ResolveShader.h"
#include "PipelineStateCache.h"
#include "Math/PackedVector.h"
#include "RHISurfaceDataConversion.h"

static FResolveRect GetDefaultRect(const FResolveRect& Rect, uint32 DefaultWidth, uint32 DefaultHeight)
{
	if (Rect.X1 >= 0 && Rect.X2 >= 0 && Rect.Y1 >= 0 && Rect.Y2 >= 0)
	{
		return Rect;
	}
	else
	{
		return FResolveRect(0, 0, DefaultWidth, DefaultHeight);
	}
}

int32 GAGXUseTexGetBytes = 1;
static FAutoConsoleVariableRef CVarAGXUseTexGetBytes(
								TEXT("rhi.AGX.UseTexGetBytes"),
								GAGXUseTexGetBytes,
								TEXT("If true prefer using -[MTLTexture getBytes:...] to retreive texture data, creating a temporary shared/managed texture to copy from private texture storage when required, rather than using a temporary MTLBuffer. This works around data alignment bugs on some GPU vendor's drivers and may be more appropriate on iOS. (Default: True)"),
								ECVF_RenderThreadSafe
								);

void FAGXRHICommandContext::RHICopyToResolveTarget(FRHITexture* SourceTextureRHI, FRHITexture* DestTextureRHI, const FResolveParams& ResolveParams)
{
	@autoreleasepool {
	if (!SourceTextureRHI || !DestTextureRHI)
	{
		// nothing to do if one of the textures is null!
		return;
	}
	if (SourceTextureRHI != DestTextureRHI)
	{
		FAGXSurface* Source = AGXGetMetalSurfaceFromRHITexture(SourceTextureRHI);
		FAGXSurface* Destination = AGXGetMetalSurfaceFromRHITexture(DestTextureRHI);
		
		const FRHITextureDesc& SourceDesc = Source->GetDesc();
		const FRHITextureDesc& DestinationDesc = Destination->GetDesc();

		// Only valid to have nil Metal textures when they are TexCreate_Presentable
		if (!Source->Texture)
		{
			// Source RHI texture is valid with no Presentable Metal texture - there is nothing to copy from
			check(EnumHasAnyFlags(SourceDesc.Flags, TexCreate_Presentable));
			return;
		}
		if (!Destination->Texture)
		{
			// Destination RHI texture is valid with no Presentable Metal texture - force fetch it now so we can complete the copy
			check(EnumHasAnyFlags(DestinationDesc.Flags, TexCreate_Presentable));
			Destination->GetDrawableTexture();
			if(!Destination->Texture)
			{
				UE_LOG(LogRHI, Error, TEXT("Drawable for destination texture resolve target unavailable"));
				return;
			}
		}

		checkf( SourceDesc.IsTexture2D()   || SourceDesc.IsTextureCube(), TEXT("Only Tex2D & Cube are tested to work so far!"));
		checkf(!SourceDesc.IsTextureCube() || SourceDesc.ArraySize == 1,  TEXT("Cube arrays might not work yet."));

		checkf( DestinationDesc.IsTexture2D()   || DestinationDesc.IsTextureCube(), TEXT("Only Tex2D & Cube are tested to work so far!"));
		checkf(!DestinationDesc.IsTextureCube() || DestinationDesc.ArraySize == 1,  TEXT("Cube arrays might not work yet."));

		MTLOrigin Origin = MTLOriginMake(0, 0, 0);
		MTLSize Size = MTLSizeMake(0, 0, 1);
		if (ResolveParams.Rect.IsValid())
		{
			// Partial copy
			Origin.x = ResolveParams.Rect.X1;
			Origin.y = ResolveParams.Rect.Y1;
			Size.width = ResolveParams.Rect.X2 - ResolveParams.Rect.X1;
			Size.height = ResolveParams.Rect.Y2 - ResolveParams.Rect.Y1;
		}
		else
		{
			// Whole of source copy
			Origin.x = 0;
			Origin.y = 0;
			
			Size.width = FMath::Max<uint32>(1, SourceDesc.Extent.X >> ResolveParams.MipIndex);
			Size.height = FMath::Max<uint32>(1, SourceDesc.Extent.Y >> ResolveParams.MipIndex);
			// clamp to a destination size
			Size.width = FMath::Min<uint32>(Size.width, DestinationDesc.Extent.X >> ResolveParams.MipIndex);
			Size.height = FMath::Min<uint32>(Size.height, DestinationDesc.Extent.Y >> ResolveParams.MipIndex);
		}
		
		const bool bSrcCubemap  = SourceDesc.IsTextureCube();
		const bool bDestCubemap = DestinationDesc.IsTextureCube();
		
		uint32 DestIndex = ResolveParams.DestArrayIndex * (bDestCubemap ? 6 : 1) + (bDestCubemap ? uint32(ResolveParams.CubeFace) : 0);
		uint32 SrcIndex  = ResolveParams.SourceArrayIndex * (bSrcCubemap ? 6 : 1) + (bSrcCubemap ? uint32(ResolveParams.CubeFace) : 0);
		
		if(Profiler)
		{
			Profiler->RegisterGPUWork();
		}

		const bool bMSAASource = Source->MSAATexture;
        const bool bMSAADest = Destination->MSAATexture;
        const bool bDepthStencil = SourceDesc.Format == PF_DepthStencil;
		if (bMSAASource && !bMSAADest)
		{
			// Resolve required - Device must support this - Using Shader for resolve not supported amd NumSamples should be 1
			const bool bSupportsMSAADepthResolve = GetAGXDeviceContext().SupportsFeature(EAGXFeaturesMSAADepthResolve);
			const bool bSupportsMSAAStoreAndResolve = GetAGXDeviceContext().SupportsFeature(EAGXFeaturesMSAAStoreAndResolve);
			check( (!bDepthStencil && bSupportsMSAAStoreAndResolve) || (bDepthStencil && bSupportsMSAADepthResolve) );
			
			Context->CopyFromTextureToTexture(Source->MSAAResolveTexture, SrcIndex, ResolveParams.MipIndex, Origin, Size, Destination->Texture, DestIndex, ResolveParams.MipIndex, Origin);
		}
		else if(Source->Texture.GetPixelFormat() == Destination->Texture.GetPixelFormat())
		{
			// Blit Copy for matching formats
			Context->CopyFromTextureToTexture(Source->Texture, SrcIndex, ResolveParams.MipIndex, Origin, Size, Destination->Texture, DestIndex, ResolveParams.MipIndex, Origin);
		}
		else
		{
			const FPixelFormatInfo& SourceFormatInfo = GPixelFormats[SourceDesc.Format];
			const FPixelFormatInfo& DestFormatInfo = GPixelFormats[DestinationDesc.Format];
			bool bUsingPixelFormatView = ([Source->Texture.GetPtr() usage] & MTLTextureUsagePixelFormatView) != 0;
			
			// Attempt to Resolve with a texture view - source Texture doesn't have to be created with MTLTextureUsagePixelFormatView for these cases e.g:
			// If we are resolving to/from sRGB linear color space within the same format OR using same bit length color format
			if	(	SourceFormatInfo.BlockBytes == DestFormatInfo.BlockBytes &&
					(bUsingPixelFormatView || SourceFormatInfo.NumComponents == DestFormatInfo.NumComponents)
				)
			{
				FAGXTexture SourceTextureView = Source->Texture.NewTextureView(Destination->Texture.GetPixelFormat(), Source->Texture.GetTextureType(), ns::Range(ResolveParams.MipIndex, 1), ns::Range(SrcIndex, 1));
				if(SourceTextureView)
				{
					Context->CopyFromTextureToTexture(SourceTextureView, 0, 0, Origin, Size, Destination->Texture, DestIndex, ResolveParams.MipIndex, Origin);
					AGXSafeReleaseMetalTexture(SourceTextureView);
				}
			}
		}

#if PLATFORM_MAC
		if((Destination->GPUReadback & FAGXSurface::EAGXGPUReadbackFlags::ReadbackRequested) != 0)
		{
			Context->GetCurrentRenderPass().SynchronizeTexture(Destination->Texture, DestIndex, ResolveParams.MipIndex);
		}
#endif
		
	}
	}
}

/** Helper for accessing R10G10B10A2 colors. */
struct FAGXR10G10B10A2
{
	uint32 R : 10;
	uint32 G : 10;
	uint32 B : 10;
	uint32 A : 2;
};

/** Helper for accessing R16G16 colors. */
struct FAGXRG16
{
	uint16 R;
	uint16 G;
};

/** Helper for accessing R16G16B16A16 colors. */
struct FAGXRGBA16
{
	uint16 R;
	uint16 G;
	uint16 B;
	uint16 A;
};

void FAGXDynamicRHI::RHIReadSurfaceData(FRHITexture* TextureRHI, FIntRect InRect, TArray<FLinearColor>& OutData, FReadSurfaceDataFlags InFlags)
{
	// Use our current surface read implementation and convert to linear - should refactor to make optimal
	TArray<FColor> OutDataUnConverted;
	RHIReadSurfaceData(TextureRHI, InRect, OutDataUnConverted, InFlags);

	OutData.Empty();
	OutData.AddUninitialized(OutDataUnConverted.Num());

	for (uint32 i = 0; i < OutDataUnConverted.Num(); ++i)
	{
		OutData[i] = OutDataUnConverted[i].ReinterpretAsLinear();
	}
}

static void ConvertSurfaceDataToFColor(EPixelFormat Format, uint32 Width, uint32 Height, uint8 *In, uint32 SrcPitch, FColor* Out, FReadSurfaceDataFlags InFlags)
{
	bool bLinearToGamma = InFlags.GetLinearToGamma();
	if (Format == PF_G16 || Format == PF_R16_UINT || Format == PF_R16_SINT)
	{
		ConvertRawR16DataToFColor(Width, Height, In, SrcPitch, Out);
	}
	else if (Format == PF_R8G8B8A8)
	{
		ConvertRawR8G8B8A8DataToFColor(Width, Height, In, SrcPitch, Out);
	}
	else if (Format == PF_B8G8R8A8)
	{
		ConvertRawB8G8R8A8DataToFColor(Width, Height, In, SrcPitch, Out);
	}
	else if (Format == PF_A2B10G10R10)
	{
		ConvertRawR10G10B10A2DataToFColor(Width, Height, In, SrcPitch, Out);
	}
	else if (Format == PF_FloatRGBA)
	{
		ConvertRawR16G16B16A16FDataToFColor(Width, Height, In, SrcPitch, Out, bLinearToGamma);
	}
	else if (Format == PF_FloatR11G11B10)
	{
		ConvertRawR11G11B10DataToFColor(Width, Height, In, SrcPitch, Out, bLinearToGamma);
	}
	else if (Format == PF_A32B32G32R32F)
	{
		ConvertRawR32G32B32A32DataToFColor(Width, Height, In, SrcPitch, Out, bLinearToGamma);
	}
	else if (Format == PF_A16B16G16R16)
	{
		ConvertRawR16G16B16A16DataToFColor(Width, Height, In, SrcPitch, Out);
	}
	else if (Format == PF_G16R16)
	{
		ConvertRawR16G16DataToFColor(Width, Height, In, SrcPitch, Out);
	}
	else if (Format == PF_DepthStencil)
	{
		ConvertRawD32S8DataToFColor(Width, Height, In, SrcPitch, Out, InFlags);
	}
	else
	{
		// not supported yet
		NOT_SUPPORTED("RHIReadSurfaceData Format");
	}
}

void FAGXDynamicRHI::RHIReadSurfaceData(FRHITexture* TextureRHI, FIntRect Rect, TArray<FColor>& OutData, FReadSurfaceDataFlags InFlags)
{
	@autoreleasepool {
	if (!ensure(TextureRHI))
	{
		OutData.Empty();
		OutData.AddZeroed(Rect.Width() * Rect.Height());
		return;
	}

	FAGXSurface* Surface = AGXGetMetalSurfaceFromRHITexture(TextureRHI);

	// allocate output space
	const uint32 SizeX = Rect.Width();
	const uint32 SizeY = Rect.Height();
	OutData.Empty();
	OutData.AddUninitialized(SizeX * SizeY);
	
	FColor* OutDataPtr = OutData.GetData();
	MTLRegion Region = MTLRegionMake2D(Rect.Min.X, Rect.Min.Y, SizeX, SizeY);
    
	FAGXTexture Texture = Surface->Texture;
    if(!Texture && EnumHasAnyFlags(Surface->GetDesc().Flags, TexCreate_Presentable))
    {
        Texture = Surface->GetCurrentTexture();
    }
    if(!Texture)
    {
        UE_LOG(LogRHI, Error, TEXT("Trying to read from an uninitialised texture."));
        return;
    }

	if (GAGXUseTexGetBytes && Surface->GetDesc().Format != PF_DepthStencil && Surface->GetDesc().Format != PF_ShadowDepth)
	{
		SCOPE_CYCLE_COUNTER(STAT_AGXTexturePageOffTime);
		
		FAGXTexture TempTexture = nil;
		if ([Texture.GetPtr() storageMode] == MTLStorageModePrivate)
		{
#if PLATFORM_MAC
			MTLResourceOptions ResourceStorageMode = MTLResourceStorageModeManaged;
#else
			MTLResourceOptions ResourceStorageMode = MTLResourceStorageModeShared;
#endif
			MTLPixelFormat MetalFormat = (MTLPixelFormat)GPixelFormats[Surface->GetDesc().Format].PlatformFormat;
			mtlpp::TextureDescriptor Desc;
			Desc.SetTextureType(Texture.GetTextureType());
			Desc.SetPixelFormat(Texture.GetPixelFormat());
			Desc.SetWidth(SizeX);
			Desc.SetHeight(SizeY);
			Desc.SetDepth(1);
			Desc.SetMipmapLevelCount(1); // Only consider a single subresource and not the whole texture (like in the other RHIs)
			Desc.SetSampleCount(Texture.GetSampleCount());
			Desc.SetArrayLength(Texture.GetArrayLength());
			
			Desc.SetResourceOptions(mtlpp::ResourceOptions(([Texture.GetPtr() resourceOptions] & ~MTLResourceStorageModeMask) | ResourceStorageMode));
			
			Desc.SetUsage(Texture.GetUsage());
			
			TempTexture = GMtlppDevice.NewTexture(Desc);
			
			ImmediateContext.Context->CopyFromTextureToTexture(Texture, 0, InFlags.GetMip(), Region.origin, Region.size, TempTexture, 0, 0, MTLOriginMake(0, 0, 0));
			
			Texture = TempTexture;
			Region = MTLRegionMake2D(0, 0, SizeX, SizeY);
		}
#if PLATFORM_MAC
		if ([Texture.GetPtr() storageMode] == MTLStorageModeManaged)
		{
			// Synchronise the texture with the CPU
			ImmediateContext.Context->SynchronizeTexture(Texture, 0, InFlags.GetMip());
		}
#endif

		//kick the current command buffer.
		ImmediateContext.Context->SubmitCommandBufferAndWait();
		
		const uint32 Stride = GPixelFormats[Surface->GetDesc().Format].BlockBytes * SizeX;
		const uint32 BytesPerImage = Stride * SizeY;

		TArray<uint8> Data;
		Data.AddUninitialized(BytesPerImage);
		
		[Texture.GetPtr() getBytes:(void*)Data.GetData()
					   bytesPerRow:Stride
					 bytesPerImage:BytesPerImage
						fromRegion:Region
					   mipmapLevel:0
							 slice:0];
		
		ConvertSurfaceDataToFColor(Surface->GetDesc().Format, SizeX, SizeY, (uint8*)Data.GetData(), Stride, OutDataPtr, InFlags);
		
		if (TempTexture)
		{
			AGXSafeReleaseMetalTexture(TempTexture);
		}
	}
	else
	{
		uint32 BytesPerPixel = (Surface->GetDesc().Format != PF_DepthStencil || !InFlags.GetOutputStencil()) ? GPixelFormats[Surface->GetDesc().Format].BlockBytes : 1;
		const uint32 Stride = BytesPerPixel * SizeX;
		const uint32 Alignment = PLATFORM_MAC ? 1u : 64u; // Mac permits natural row alignment (tightly-packed) but iOS does not.
		const uint32 AlignedStride = ((Stride - 1) & ~(Alignment - 1)) + Alignment;
		const uint32 BytesPerImage = AlignedStride * SizeY;
		FAGXBuffer Buffer = ((FAGXDeviceContext*)ImmediateContext.Context)->CreatePooledBuffer(FAGXPooledBufferArgs(BytesPerImage, BUF_Dynamic, FAGXPooledBufferArgs::SharedStorageResourceOptions));
		{
			// Synchronise the texture with the CPU
			SCOPE_CYCLE_COUNTER(STAT_AGXTexturePageOffTime);
			
			if (Surface->GetDesc().Format != PF_DepthStencil)
			{
				ImmediateContext.Context->CopyFromTextureToBuffer(Texture, 0, InFlags.GetMip(), Region.origin, Region.size, Buffer, 0, AlignedStride, BytesPerImage, MTLBlitOptionNone);
			}
			else
			{
				if (!InFlags.GetOutputStencil())
				{
					ImmediateContext.Context->CopyFromTextureToBuffer(Texture, 0, InFlags.GetMip(), Region.origin, Region.size, Buffer, 0, AlignedStride, BytesPerImage, MTLBlitOptionDepthFromDepthStencil);
				}
				else
				{
					ImmediateContext.Context->CopyFromTextureToBuffer(Texture, 0, InFlags.GetMip(), Region.origin, Region.size, Buffer, 0, AlignedStride, BytesPerImage, MTLBlitOptionStencilFromDepthStencil);
				}
			}
			
			//kick the current command buffer.
			ImmediateContext.Context->SubmitCommandBufferAndWait();
			
			ConvertSurfaceDataToFColor(Surface->GetDesc().Format, SizeX, SizeY, (uint8*)Buffer.GetContents(), AlignedStride, OutDataPtr, InFlags);
		}
		((FAGXDeviceContext*)ImmediateContext.Context)->ReleaseBuffer(Buffer);
	}
	}
}

void FAGXDynamicRHI::RHIMapStagingSurface(FRHITexture* TextureRHI, FRHIGPUFence* FenceRHI, void*& OutData, int32& OutWidth, int32& OutHeight, uint32 GPUIndex)
{
	@autoreleasepool {
    FAGXSurface* Surface = AGXGetMetalSurfaceFromRHITexture(TextureRHI);
    
#if PLATFORM_MAC
	uint16 FencePoll = FenceRHI && FenceRHI->Poll() ? 1 : 0;
    Surface->GPUReadback |= FencePoll << FAGXSurface::EAGXGPUReadbackFlags::ReadbackFenceCompleteShift;
#endif
    
    uint32 Stride = 0;
    OutWidth = Surface->GetSizeX();
    OutHeight = Surface->GetSizeY();
    OutData = Surface->Lock(0, 0, RLM_ReadOnly, Stride);
    
#if PLATFORM_MAC
	Surface->GPUReadback = (Surface->Texture.GetPtr() && [Surface->Texture.GetPtr() storageMode] == MTLStorageModeManaged) ? FAGXSurface::EAGXGPUReadbackFlags::ReadbackRequested : 0;
#endif
	}
}

void FAGXDynamicRHI::RHIUnmapStagingSurface(FRHITexture* TextureRHI, uint32 GPUIndex)
{
	@autoreleasepool {
    	FAGXSurface* Surface = AGXGetMetalSurfaceFromRHITexture(TextureRHI);
	
    	Surface->Unlock(0, 0, false);
	}
}

void FAGXDynamicRHI::RHIReadSurfaceFloatData(FRHITexture* TextureRHI, FIntRect Rect, TArray<FFloat16Color>& OutData, ECubeFace CubeFace,int32 ArrayIndex,int32 MipIndex)
{
	@autoreleasepool {
	FAGXSurface* Surface = AGXGetMetalSurfaceFromRHITexture(TextureRHI);
	
    FAGXTexture Texture = Surface->Texture;
    if(!Texture && EnumHasAnyFlags(Surface->GetDesc().Flags, TexCreate_Presentable))
    {
		Texture = Surface->GetCurrentTexture();
    }
    if(!Texture)
    {
        UE_LOG(LogRHI, Error, TEXT("Trying to read from an uninitialised texture."));
        return;
    }
    
	// verify the input image format (but don't crash)
	if (Surface->GetDesc().Format != PF_FloatRGBA)
	{
		UE_LOG(LogRHI, Log, TEXT("Trying to read non-FloatRGBA surface."));
	}

	if (TextureRHI->GetTextureCube())
	{
		// adjust index to account for cubemaps as texture arrays
		ArrayIndex *= CubeFace_MAX;
		ArrayIndex += GetMetalCubeFace(CubeFace);
	}
	
	// allocate output space
	const uint32 SizeX = Rect.Width();
	const uint32 SizeY = Rect.Height();
	OutData.Empty();
	OutData.AddUninitialized(SizeX * SizeY);
	
	MTLRegion Region = MTLRegionMake2D(Rect.Min.X, Rect.Min.Y, SizeX, SizeY);
	
	// function wants details about the destination, not the source
	const uint32 Stride = GPixelFormats[Surface->GetDesc().Format].BlockBytes * SizeX;
	const uint32 Alignment = PLATFORM_MAC ? 1u : 64u; // Mac permits natural row alignment (tightly-packed) but iOS does not.
	const uint32 AlignedStride = ((Stride - 1) & ~(Alignment - 1)) + Alignment;
	const uint32 BytesPerImage = AlignedStride  * SizeY;
	int32 FloatBGRADataSize = BytesPerImage;
	FAGXBuffer Buffer = ((FAGXDeviceContext*)ImmediateContext.Context)->CreatePooledBuffer(FAGXPooledBufferArgs(FloatBGRADataSize, BUF_Dynamic, FAGXPooledBufferArgs::SharedStorageResourceOptions));
	{
		// Synchronise the texture with the CPU
		SCOPE_CYCLE_COUNTER(STAT_AGXTexturePageOffTime);
		
		ImmediateContext.Context->CopyFromTextureToBuffer(Texture, ArrayIndex, MipIndex, Region.origin, Region.size, Buffer, 0, AlignedStride, BytesPerImage, MTLBlitOptionNone);
		
		//kick the current command buffer.
		ImmediateContext.Context->SubmitCommandBufferAndWait();
	}
	
	uint8* DataPtr = (uint8*)Buffer.GetContents();
	FFloat16Color* OutDataPtr = OutData.GetData();
	if (Alignment > 1u)
	{
		for (uint32 Row = 0; Row < SizeY; Row++)
		{
			FFloat16Color* FloatBGRAData = (FFloat16Color*)DataPtr;
			FMemory::Memcpy(OutDataPtr, FloatBGRAData, Stride);
			DataPtr += AlignedStride;
			OutDataPtr += SizeX;
		}
	}
	else
	{
		FFloat16Color* FloatBGRAData = (FFloat16Color*)DataPtr;
		FMemory::Memcpy(OutDataPtr, FloatBGRAData, FloatBGRADataSize);
	}
	
	((FAGXDeviceContext*)ImmediateContext.Context)->ReleaseBuffer(Buffer);
	}
}

void FAGXDynamicRHI::RHIRead3DSurfaceFloatData(FRHITexture* TextureRHI,FIntRect InRect,FIntPoint ZMinMax,TArray<FFloat16Color>& OutData)
{
	@autoreleasepool {
	FAGXSurface* Surface = AGXGetMetalSurfaceFromRHITexture(TextureRHI);
	
	FAGXTexture Texture = Surface->Texture;
	if(!Texture)
	{
		UE_LOG(LogRHI, Error, TEXT("Trying to read from an uninitialised texture."));
		return;
	}
	
	// verify the input image format (but don't crash)
	if (Surface->GetDesc().Format != PF_FloatRGBA)
	{
		UE_LOG(LogRHI, Log, TEXT("Trying to read non-FloatRGBA surface."));
	}
	
	// allocate output space
	const uint32 SizeX = InRect.Width();
	const uint32 SizeY = InRect.Height();
	const uint32 SizeZ = ZMinMax.Y - ZMinMax.X;
	OutData.Empty();
	OutData.AddUninitialized(SizeX * SizeY * SizeZ);
	
	MTLRegion Region = MTLRegionMake3D(InRect.Min.X, InRect.Min.Y, ZMinMax.X, SizeX, SizeY, SizeZ);
	
	// function wants details about the destination, not the source
	const uint32 Stride = GPixelFormats[Surface->GetDesc().Format].BlockBytes * SizeX;
	const uint32 Alignment = PLATFORM_MAC ? 1u : 64u; // Mac permits natural row alignment (tightly-packed) but iOS does not.
	const uint32 AlignedStride = ((Stride - 1) & ~(Alignment - 1)) + Alignment;
	const uint32 BytesPerImage = AlignedStride  * SizeY;
	int32 FloatBGRADataSize = BytesPerImage * SizeZ;
	FAGXBuffer Buffer = ((FAGXDeviceContext*)ImmediateContext.Context)->CreatePooledBuffer(FAGXPooledBufferArgs(FloatBGRADataSize, BUF_Dynamic, FAGXPooledBufferArgs::SharedStorageResourceOptions));
	{
		// Synchronise the texture with the CPU
		SCOPE_CYCLE_COUNTER(STAT_AGXTexturePageOffTime);
		
		ImmediateContext.Context->CopyFromTextureToBuffer(Texture, 0, 0, Region.origin, Region.size, Buffer, 0, AlignedStride, BytesPerImage, MTLBlitOptionNone);
		
		//kick the current command buffer.
		ImmediateContext.Context->SubmitCommandBufferAndWait();
	}
	
	uint8* DataPtr = (uint8*)Buffer.GetContents();
	FFloat16Color* OutDataPtr = OutData.GetData();
	if (Alignment > 1u)
	{
		for (uint32 Image = 0; Image < SizeZ; Image++)
		{
			for (uint32 Row = 0; Row < SizeY; Row++)
			{
				FFloat16Color* FloatBGRAData = (FFloat16Color*)DataPtr;
				FMemory::Memcpy(OutDataPtr, FloatBGRAData, Stride);
				DataPtr += AlignedStride;
				OutDataPtr += SizeX;
			}
		}
	}
	else
	{
		FFloat16Color* FloatBGRAData = (FFloat16Color*)DataPtr;
		FMemory::Memcpy(OutDataPtr, FloatBGRAData, FloatBGRADataSize);
	}
	
	((FAGXDeviceContext*)ImmediateContext.Context)->ReleaseBuffer(Buffer);
	}
}
