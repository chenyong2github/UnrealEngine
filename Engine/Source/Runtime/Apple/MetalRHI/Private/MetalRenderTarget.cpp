// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MetalRenderTarget.cpp: Metal render target implementation.
=============================================================================*/

#include "MetalRHIPrivate.h"
#include "ScreenRendering.h"
#include "MetalProfiler.h"
#include "MetalCommandBuffer.h"
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

int32 GMetalUseTexGetBytes = 1;
static FAutoConsoleVariableRef CVarMetalUseTexGetBytes(
								TEXT("rhi.Metal.UseTexGetBytes"),
								GMetalUseTexGetBytes,
								TEXT("If true prefer using -[MTLTexture getBytes:...] to retreive texture data, creating a temporary shared/managed texture to copy from private texture storage when required, rather than using a temporary MTLBuffer. This works around data alignment bugs on some GPU vendor's drivers and may be more appropriate on iOS. (Default: True)"),
								ECVF_RenderThreadSafe
								);

void FMetalRHICommandContext::RHICopyToResolveTarget(FRHITexture* SourceTextureRHI, FRHITexture* DestTextureRHI, const FResolveParams& ResolveParams)
{
	@autoreleasepool {
	if (!SourceTextureRHI || !DestTextureRHI)
	{
		// nothing to do if one of the textures is null!
		return;
	}
	if(SourceTextureRHI != DestTextureRHI)
	{
		FMetalSurface* Source = GetMetalSurfaceFromRHITexture(SourceTextureRHI);
		FMetalSurface* Destination = GetMetalSurfaceFromRHITexture(DestTextureRHI);
		
		switch (Source->Type)
		{
			case RRT_Texture2D:
				break;
			case RRT_TextureCube:
				check(Source->SizeZ == 6); // Arrays might not work yet.
				break;
			default:
				check(false); // Only Tex2D & Cube are tested to work so far!
				break;
		}
		switch (Destination->Type)
		{
			case RRT_Texture2D:
				break;
			case RRT_TextureCube:
				check(Destination->SizeZ == 6); // Arrays might not work yet.
				break;
			default:
				check(false); // Only Tex2D & Cube are tested to work so far!
				break;
		}
		
		mtlpp::Origin Origin(0, 0, 0);
		mtlpp::Size Size(0, 0, 1);
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
			
			Size.width = FMath::Max<uint32>(1, Source->SizeX >> ResolveParams.MipIndex);
			Size.height = FMath::Max<uint32>(1, Source->SizeY >> ResolveParams.MipIndex);
		}
		
		const bool bSrcCubemap  = Source->bIsCubemap;
		const bool bDestCubemap = Destination->bIsCubemap;
		
		uint32 DestIndex = ResolveParams.DestArrayIndex * (bDestCubemap ? 6 : 1) + (bDestCubemap ? uint32(ResolveParams.CubeFace) : 0);
		uint32 SrcIndex  = ResolveParams.SourceArrayIndex * (bSrcCubemap ? 6 : 1) + (bSrcCubemap ? uint32(ResolveParams.CubeFace) : 0);
		
		if(Profiler)
		{
			Profiler->RegisterGPUWork();
		}

		const bool bMSAASource = Source->MSAATexture;
        const bool bMSAADest = Destination->MSAATexture;
        const bool bDepthStencil = Source->PixelFormat == PF_DepthStencil;
		if (bMSAASource && !bMSAADest)
		{
			// Resolve required - Device must support this - Using Shader for resolve not supported amd NumSamples should be 1
			const bool bSupportsMSAADepthResolve = GetMetalDeviceContext().SupportsFeature(EMetalFeaturesMSAADepthResolve);
			const bool bSupportsMSAAStoreAndResolve = GetMetalDeviceContext().SupportsFeature(EMetalFeaturesMSAAStoreAndResolve);
			check( (!bDepthStencil && bSupportsMSAAStoreAndResolve) || (bDepthStencil && bSupportsMSAADepthResolve) );
			
			Context->CopyFromTextureToTexture(Source->MSAAResolveTexture, SrcIndex, ResolveParams.MipIndex, Origin, Size, Destination->Texture, DestIndex, ResolveParams.MipIndex, Origin);
		}
		else
		{
			Context->CopyFromTextureToTexture(Source->Texture, SrcIndex, ResolveParams.MipIndex, Origin, Size, Destination->Texture, DestIndex, ResolveParams.MipIndex, Origin);
		}
	}
	}
}

/** Helper for accessing R10G10B10A2 colors. */
struct FMetalR10G10B10A2
{
	uint32 R : 10;
	uint32 G : 10;
	uint32 B : 10;
	uint32 A : 2;
};

/** Helper for accessing R16G16 colors. */
struct FMetalRG16
{
	uint16 R;
	uint16 G;
};

/** Helper for accessing R16G16B16A16 colors. */
struct FMetalRGBA16
{
	uint16 R;
	uint16 G;
	uint16 B;
	uint16 A;
};

void FMetalDynamicRHI::RHIReadSurfaceData(FRHITexture* TextureRHI, FIntRect InRect, TArray<FLinearColor>& OutData, FReadSurfaceDataFlags InFlags)
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

void FMetalDynamicRHI::RHIReadSurfaceData(FRHITexture* TextureRHI, FIntRect Rect, TArray<FColor>& OutData, FReadSurfaceDataFlags InFlags)
{
	@autoreleasepool {
	if (!ensure(TextureRHI))
	{
		OutData.Empty();
		OutData.AddZeroed(Rect.Width() * Rect.Height());
		return;
	}

	FMetalSurface* Surface = GetMetalSurfaceFromRHITexture(TextureRHI);

	// allocate output space
	const uint32 SizeX = Rect.Width();
	const uint32 SizeY = Rect.Height();
	OutData.Empty();
	OutData.AddUninitialized(SizeX * SizeY);
	
	FColor* OutDataPtr = OutData.GetData();
	mtlpp::Region Region(Rect.Min.X, Rect.Min.Y, SizeX, SizeY);
    
	FMetalTexture Texture = Surface->Texture;
    if(!Texture && (Surface->Flags & TexCreate_Presentable))
    {
        Texture = Surface->GetCurrentTexture();
    }
    if(!Texture)
    {
        UE_LOG(LogRHI, Error, TEXT("Trying to read from an uninitialised texture."));
        return;
    }

	if (GMetalUseTexGetBytes && Surface->PixelFormat != PF_DepthStencil && Surface->PixelFormat != PF_ShadowDepth)
	{
		SCOPE_CYCLE_COUNTER(STAT_MetalTexturePageOffTime);
		
		FMetalTexture TempTexture = nil;
		if (Texture.GetStorageMode() == mtlpp::StorageMode::Private)
		{
#if PLATFORM_MAC
			mtlpp::StorageMode StorageMode = mtlpp::StorageMode::Managed;
#else
			mtlpp::StorageMode StorageMode = mtlpp::StorageMode::Shared;
#endif
			mtlpp::PixelFormat MetalFormat = (mtlpp::PixelFormat)GPixelFormats[Surface->PixelFormat].PlatformFormat;
			mtlpp::TextureDescriptor Desc;
			Desc.SetTextureType(Texture.GetTextureType());
			Desc.SetPixelFormat(Texture.GetPixelFormat());
			Desc.SetWidth(SizeX);
			Desc.SetHeight(SizeY);
			Desc.SetDepth(1);
			Desc.SetMipmapLevelCount(1); // Only consider a single subresource and not the whole texture (like in the other RHIs)
			Desc.SetSampleCount(Texture.GetSampleCount());
			Desc.SetArrayLength(Texture.GetArrayLength());
			
			mtlpp::ResourceOptions GeneralResourceOption = (mtlpp::ResourceOptions)FMetalCommandQueue::GetCompatibleResourceOptions(mtlpp::ResourceOptions(((NSUInteger)Texture.GetCpuCacheMode() << mtlpp::ResourceCpuCacheModeShift) | ((NSUInteger)StorageMode << mtlpp::ResourceStorageModeShift) | mtlpp::ResourceOptions::HazardTrackingModeUntracked));
			Desc.SetResourceOptions(GeneralResourceOption);
			
			Desc.SetCpuCacheMode(Texture.GetCpuCacheMode());
			Desc.SetStorageMode(StorageMode);
			Desc.SetUsage(Texture.GetUsage());
			
			TempTexture = GetMetalDeviceContext().GetDevice().NewTexture(Desc);
			
			ImmediateContext.Context->CopyFromTextureToTexture(Texture, 0, InFlags.GetMip(), mtlpp::Origin(Region.origin), mtlpp::Size(Region.size), TempTexture, 0, 0, mtlpp::Origin(0, 0, 0));
			
			Texture = TempTexture;
			Region = mtlpp::Region(0, 0, SizeX, SizeY);
		}
#if PLATFORM_MAC
		if(Texture.GetStorageMode() == mtlpp::StorageMode::Managed)
		{
			// Synchronise the texture with the CPU
			ImmediateContext.Context->SynchronizeTexture(Texture, 0, InFlags.GetMip());
		}
#endif

		//kick the current command buffer.
		ImmediateContext.Context->SubmitCommandBufferAndWait();
		
		const uint32 Stride = GPixelFormats[Surface->PixelFormat].BlockBytes * SizeX;
		const uint32 BytesPerImage = Stride * SizeY;

		TArray<uint8> Data;
		Data.AddUninitialized(BytesPerImage);
		
		Texture.GetBytes(Data.GetData(), Stride, BytesPerImage, Region, 0, 0);
		
		ConvertSurfaceDataToFColor(Surface->PixelFormat, SizeX, SizeY, (uint8*)Data.GetData(), Stride, OutDataPtr, InFlags);
		
		if (TempTexture)
		{
			SafeReleaseMetalTexture(TempTexture);
		}
	}
	else
	{
		uint32 BytesPerPixel = (Surface->PixelFormat != PF_DepthStencil || !InFlags.GetOutputStencil()) ? GPixelFormats[Surface->PixelFormat].BlockBytes : 1;
		const uint32 Stride = BytesPerPixel * SizeX;
		const uint32 Alignment = PLATFORM_MAC ? 1u : 64u; // Mac permits natural row alignment (tightly-packed) but iOS does not.
		const uint32 AlignedStride = ((Stride - 1) & ~(Alignment - 1)) + Alignment;
		const uint32 BytesPerImage = AlignedStride * SizeY;
		FMetalBuffer Buffer = ((FMetalDeviceContext*)ImmediateContext.Context)->CreatePooledBuffer(FMetalPooledBufferArgs(ImmediateContext.Context->GetDevice(), BytesPerImage, BUF_Dynamic, mtlpp::StorageMode::Shared));
		{
			// Synchronise the texture with the CPU
			SCOPE_CYCLE_COUNTER(STAT_MetalTexturePageOffTime);
			
			if (Surface->PixelFormat != PF_DepthStencil)
			{
				ImmediateContext.Context->CopyFromTextureToBuffer(Texture, 0, InFlags.GetMip(), Region.origin, Region.size, Buffer, 0, AlignedStride, BytesPerImage, mtlpp::BlitOption::None);
			}
			else
			{
				if (!InFlags.GetOutputStencil())
				{
					ImmediateContext.Context->CopyFromTextureToBuffer(Texture, 0, InFlags.GetMip(), Region.origin, Region.size, Buffer, 0, AlignedStride, BytesPerImage, mtlpp::BlitOption::DepthFromDepthStencil);
				}
				else
				{
					ImmediateContext.Context->CopyFromTextureToBuffer(Texture, 0, InFlags.GetMip(), Region.origin, Region.size, Buffer, 0, AlignedStride, BytesPerImage, mtlpp::BlitOption::StencilFromDepthStencil);
				}
			}
			
			//kick the current command buffer.
			ImmediateContext.Context->SubmitCommandBufferAndWait();
			
			ConvertSurfaceDataToFColor(Surface->PixelFormat, SizeX, SizeY, (uint8*)Buffer.GetContents(), AlignedStride, OutDataPtr, InFlags);
		}
		((FMetalDeviceContext*)ImmediateContext.Context)->ReleaseBuffer(Buffer);
	}
	}
}

void FMetalDynamicRHI::RHIMapStagingSurface(FRHITexture* TextureRHI, FRHIGPUFence* FenceRHI, void*& OutData, int32& OutWidth, int32& OutHeight, uint32 GPUIndex)
{
	@autoreleasepool {
    FMetalSurface* Surface = GetMetalSurfaceFromRHITexture(TextureRHI);
    FMetalTexture2D* Texture = (FMetalTexture2D*)TextureRHI->GetTexture2D();
    
    uint32 Stride = 0;
    OutWidth = Texture->GetSizeX();
    OutHeight = Texture->GetSizeY();
    OutData = Surface->Lock(0, 0, RLM_ReadOnly, Stride);
	}
}

void FMetalDynamicRHI::RHIUnmapStagingSurface(FRHITexture* TextureRHI, uint32 GPUIndex)
{
	@autoreleasepool {
    	FMetalSurface* Surface = GetMetalSurfaceFromRHITexture(TextureRHI);
	
    	Surface->Unlock(0, 0, false);
	}
}

void FMetalDynamicRHI::RHIReadSurfaceFloatData(FRHITexture* TextureRHI, FIntRect Rect, TArray<FFloat16Color>& OutData, ECubeFace CubeFace,int32 ArrayIndex,int32 MipIndex)
{
	@autoreleasepool {
	FMetalSurface* Surface = GetMetalSurfaceFromRHITexture(TextureRHI);
	
    FMetalTexture Texture = Surface->Texture;
    if(!Texture && (Surface->Flags & TexCreate_Presentable))
    {
		Texture = Surface->GetCurrentTexture();
    }
    if(!Texture)
    {
        UE_LOG(LogRHI, Error, TEXT("Trying to read from an uninitialised texture."));
        return;
    }
    
	// verify the input image format (but don't crash)
	if (Surface->PixelFormat != PF_FloatRGBA)
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
	
	mtlpp::Region Region = mtlpp::Region(Rect.Min.X, Rect.Min.Y, SizeX, SizeY);
	
	// function wants details about the destination, not the source
	const uint32 Stride = GPixelFormats[Surface->PixelFormat].BlockBytes * SizeX;
	const uint32 Alignment = PLATFORM_MAC ? 1u : 64u; // Mac permits natural row alignment (tightly-packed) but iOS does not.
	const uint32 AlignedStride = ((Stride - 1) & ~(Alignment - 1)) + Alignment;
	const uint32 BytesPerImage = AlignedStride  * SizeY;
	int32 FloatBGRADataSize = BytesPerImage;
	FMetalBuffer Buffer = ((FMetalDeviceContext*)ImmediateContext.Context)->CreatePooledBuffer(FMetalPooledBufferArgs(ImmediateContext.Context->GetDevice(), FloatBGRADataSize, BUF_Dynamic, mtlpp::StorageMode::Shared));
	{
		// Synchronise the texture with the CPU
		SCOPE_CYCLE_COUNTER(STAT_MetalTexturePageOffTime);
		
		ImmediateContext.Context->CopyFromTextureToBuffer(Texture, ArrayIndex, MipIndex, Region.origin, Region.size, Buffer, 0, AlignedStride, BytesPerImage, mtlpp::BlitOption::None);
		
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
	
	((FMetalDeviceContext*)ImmediateContext.Context)->ReleaseBuffer(Buffer);
	}
}

void FMetalDynamicRHI::RHIRead3DSurfaceFloatData(FRHITexture* TextureRHI,FIntRect InRect,FIntPoint ZMinMax,TArray<FFloat16Color>& OutData)
{
	@autoreleasepool {
	FMetalSurface* Surface = GetMetalSurfaceFromRHITexture(TextureRHI);
	
	FMetalTexture Texture = Surface->Texture;
	if(!Texture)
	{
		UE_LOG(LogRHI, Error, TEXT("Trying to read from an uninitialised texture."));
		return;
	}
	
	// verify the input image format (but don't crash)
	if (Surface->PixelFormat != PF_FloatRGBA)
	{
		UE_LOG(LogRHI, Log, TEXT("Trying to read non-FloatRGBA surface."));
	}
	
	// allocate output space
	const uint32 SizeX = InRect.Width();
	const uint32 SizeY = InRect.Height();
	const uint32 SizeZ = ZMinMax.Y - ZMinMax.X;
	OutData.Empty();
	OutData.AddUninitialized(SizeX * SizeY * SizeZ);
	
	mtlpp::Region Region = mtlpp::Region(InRect.Min.X, InRect.Min.Y, ZMinMax.X, SizeX, SizeY, SizeZ);
	
	// function wants details about the destination, not the source
	const uint32 Stride = GPixelFormats[Surface->PixelFormat].BlockBytes * SizeX;
	const uint32 Alignment = PLATFORM_MAC ? 1u : 64u; // Mac permits natural row alignment (tightly-packed) but iOS does not.
	const uint32 AlignedStride = ((Stride - 1) & ~(Alignment - 1)) + Alignment;
	const uint32 BytesPerImage = AlignedStride  * SizeY;
	int32 FloatBGRADataSize = BytesPerImage * SizeZ;
	FMetalBuffer Buffer = ((FMetalDeviceContext*)ImmediateContext.Context)->CreatePooledBuffer(FMetalPooledBufferArgs(ImmediateContext.Context->GetDevice(), FloatBGRADataSize, BUF_Dynamic, mtlpp::StorageMode::Shared));
	{
		// Synchronise the texture with the CPU
		SCOPE_CYCLE_COUNTER(STAT_MetalTexturePageOffTime);
		
		ImmediateContext.Context->CopyFromTextureToBuffer(Texture, 0, 0, Region.origin, Region.size, Buffer, 0, AlignedStride, BytesPerImage, mtlpp::BlitOption::None);
		
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
	
	((FMetalDeviceContext*)ImmediateContext.Context)->ReleaseBuffer(Buffer);
	}
}
