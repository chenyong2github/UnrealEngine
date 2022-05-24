// Copyright Epic Games, Inc. All Rights Reserved.


#include "AGXRHIPrivate.h"
#include "AGXRHIStagingBuffer.h"
#include "RenderUtils.h"
#include "ClearReplacementShaders.h"
#include "AGXTransitionData.h"

// Constructor for buffers
FAGXResourceViewBase::FAGXResourceViewBase(FRHIBuffer* InBuffer, uint32 InStartOffsetBytes, uint32 InNumElements, EPixelFormat InFormat)
	: SourceBuffer     (ResourceCast(InBuffer))
	, bTexture         (false)
	, bSRGBForceDisable(false)
	, MipLevel         (0)
	, Reserved         (0)
	, NumMips          (0)
	, Format           (InFormat)
	, Stride           (0)
	, Offset           (InBuffer ? InStartOffsetBytes : 0)
{
	check(!bTexture);

	if (SourceBuffer)
	{
		SourceBuffer->AddRef();
	}

	EBufferUsageFlags Usage = SourceBuffer->GetUsage();
	if (EnumHasAnyFlags(Usage, BUF_VertexBuffer))
	{
		if (!SourceBuffer)
		{
			Stride = 0;
		}
		else
		{
			check(SourceBuffer->GetUsage() & BUF_ShaderResource);
			Stride = GPixelFormats[Format].BlockBytes;

			LinearTextureDesc = MakeUnique<FAGXLinearTextureDescriptor>(InStartOffsetBytes, InNumElements, Stride);
			SourceBuffer->CreateLinearTexture((EPixelFormat)Format, SourceBuffer, LinearTextureDesc.Get());
		}
	}
	else if (EnumHasAnyFlags(Usage, BUF_IndexBuffer))
	{
		if (!SourceBuffer)
		{
			Format = PF_R16_UINT;
			Stride = 0;
		}
		else
		{
			Format = (SourceBuffer->IndexType == mtlpp::IndexType::UInt16) ? PF_R16_UINT : PF_R32_UINT;
			Stride = SourceBuffer->GetStride();

			check(Stride == ((Format == PF_R16_UINT) ? 2 : 4));

			LinearTextureDesc = MakeUnique<FAGXLinearTextureDescriptor>(InStartOffsetBytes, InNumElements, Stride);
			SourceBuffer->CreateLinearTexture((EPixelFormat)Format, SourceBuffer, LinearTextureDesc.Get());
		}
	}
	else
	{
		check(EnumHasAnyFlags(Usage, BUF_StructuredBuffer));

		Format = PF_Unknown;
		Stride = SourceBuffer->GetStride();
	}
}

// Constructor for textures
FAGXResourceViewBase::FAGXResourceViewBase(
	  FRHITexture* InTexture
	, EPixelFormat InFormat
	, uint8 InMipLevel
	, uint8 InNumMipLevels
	, ERHITextureSRVOverrideSRGBType InSRGBOverride
	, uint32 InFirstArraySlice
	, uint32 InNumArraySlices
	, bool bInUAV
	)
	: SourceTexture    (ResourceCast(InTexture))
	, bTexture         (true)
	, bSRGBForceDisable(InSRGBOverride == SRGBO_ForceDisable)
	, MipLevel         (InMipLevel)
	, Reserved         (0)
	, NumMips          (InNumMipLevels)
	, Format           ((InTexture && InFormat == PF_Unknown) ? InTexture->GetDesc().Format : InFormat)
	, Stride           (0)
	, Offset           (0)
{
	if (SourceTexture)
	{
		SourceTexture->AddRef();

		id<MTLTexture> SourceTextureInternal = SourceTexture->Texture.GetPtr();

		// TODO: Apple Silicon supports memoryless
#if PLATFORM_IOS
		// Memoryless targets can't have texture views (SRVs or UAVs)
		if ([SourceTextureInternal storageMode] != MTLStorageModeMemoryless)
#endif
		{
			// Determine the appropriate metal format for the view.
			// This format will be non-sRGB. We convert to sRGB below if required.
			MTLPixelFormat MetalFormat = (MTLPixelFormat)GPixelFormats[Format].PlatformFormat;

			if (Format == PF_X24_G8)
			{
				// Stencil buffer view of a depth texture
				check(SourceTexture->GetDesc().Format == PF_DepthStencil);
				switch ([SourceTextureInternal pixelFormat])
				{
					default:
						checkNoEntry();
						break;

#if PLATFORM_MAC
					case MTLPixelFormatDepth24Unorm_Stencil8:
						MetalFormat = MTLPixelFormatX24_Stencil8;
						break;
#endif

					case MTLPixelFormatDepth32Float_Stencil8:
						MetalFormat = MTLPixelFormatX32_Stencil8;
						break;
				}
			}
			else
			{
				// Override the format's sRGB setting if appropriate
				if (EnumHasAnyFlags(SourceTexture->GetDesc().Flags, TexCreate_SRGB))
				{
					if (bSRGBForceDisable)
					{
#if PLATFORM_MAC
						// R8Unorm has been expanded in the source surface for sRGBA support - we need to expand to RGBA to enable compatible texture format view for non apple silicon macs
						if (Format == PF_G8 && [SourceTextureInternal pixelFormat] == MTLPixelFormatRGBA8Unorm_sRGB)
						{
							MetalFormat = MTLPixelFormatRGBA8Unorm;
						}
#endif
					}
					else
					{
						// Ensure we have the correct sRGB target format if we create a new texture view rather than using the source texture
						MetalFormat = AGXToSRGBFormat(MetalFormat);
					}
				}
			}

			// We can use the source texture directly if the view's format / mip count etc matches.
			bool bUseSourceTex =
				   MipLevel == 0
				&& NumMips == [SourceTextureInternal mipmapLevelCount]
				&& MetalFormat == [SourceTextureInternal pixelFormat]
				&& !(bInUAV && SourceTexture->GetDesc().IsTextureCube())	// @todo: Remove this once Cube UAV supported for all Metal Devices
				&& InFirstArraySlice == 0
				&& InNumArraySlices == 0;

			if (bUseSourceTex)
			{
				// SRV is exactly compatible with the original texture.
				TextureView = [SourceTextureInternal retain];
			}
			else
			{
				// Recreate the texture to enable MTLTextureUsagePixelFormatView which must be off unless we definitely use this feature or we are throwing ~4% performance vs. Windows on the floor.
				// @todo recreating resources like this will likely prevent us from making view creation multi-threaded.
				if (!([SourceTextureInternal usage] & MTLTextureUsagePixelFormatView))
				{
					SourceTexture->PrepareTextureView();
					SourceTextureInternal = SourceTexture->Texture.GetPtr();
				}

				const uint32 TextureSliceCount = [SourceTextureInternal arrayLength];
				const uint32 CubeSliceMultiplier = SourceTexture->GetDesc().IsTextureCube() ? 6 : 1;
				const uint32 NumArraySlices = (InNumArraySlices > 0 ? InNumArraySlices : TextureSliceCount) * CubeSliceMultiplier;
				
				// @todo: Remove this type swizzle once Cube UAV supported for all Metal Devices - SRV seem to want to stay as cube but UAV are expected to be 2DArray
				MTLTextureType TextureType = bInUAV && SourceTexture->GetDesc().IsTextureCube() ? MTLTextureType2DArray : [SourceTextureInternal textureType];

				// Assume a texture view of 1 slice into a multislice texture wants to be the non-array texture type
				// This doesn't really matter to Metal but will be very important when this texture is bound in the shader
				if (InNumArraySlices == 1)
				{
					switch (TextureType)
					{
					case MTLTextureType2DArray:
						TextureType = MTLTextureType2D;
						break;
					case MTLTextureTypeCubeArray:
						TextureType = MTLTextureTypeCube;
						break;
					default:
						// NOP
						break;
					}
				}

				TextureView = [SourceTextureInternal newTextureViewWithPixelFormat:MetalFormat
				                                                       textureType:TextureType
																	        levels:NSMakeRange(MipLevel, NumMips)
																			slices:NSMakeRange(InFirstArraySlice, NumArraySlices)];

#if METAL_DEBUG_OPTIONS
				[TextureView setLabel:[[SourceTextureInternal label] stringByAppendingString:@"_TextureView"]];
#endif
			}
		}
	}
	else
	{
		TextureView = nil;
	}
}

FAGXResourceViewBase::~FAGXResourceViewBase()
{
	if (TextureView)
	{
		FAGXTexture TmpWrapper(TextureView);
		AGXSafeReleaseMetalTexture(TmpWrapper);
		TextureView = nil;
	}
	
	if (bTexture)
	{
		if (SourceTexture)
		{
			SourceTexture->Release();
		}
	}
	else
	{
		if (SourceBuffer)
		{
			SourceBuffer->Release();
		}
	}
}

ns::AutoReleased<FAGXTexture> FAGXResourceViewBase::GetLinearTexture()
{
	ns::AutoReleased<FAGXTexture> NewLinearTexture;
	if (SourceBuffer)
	{
		NewLinearTexture = SourceBuffer->GetLinearTexture((EPixelFormat)Format, LinearTextureDesc.Get());
	}
	return NewLinearTexture;
}

FUnorderedAccessViewRHIRef FAGXDynamicRHI::RHICreateUnorderedAccessView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIBuffer* Buffer, bool bUseUAVCounter, bool bAppendBuffer)
{
	return this->RHICreateUnorderedAccessView(Buffer, bUseUAVCounter, bAppendBuffer);
}

FUnorderedAccessViewRHIRef FAGXDynamicRHI::RHICreateUnorderedAccessView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture* Texture, uint32 MipLevel, uint16 FirstArraySlice, uint16 NumArraySlices)
{
	FAGXSurface* Surface = AGXGetMetalSurfaceFromRHITexture(Texture);

	//
	// The FAGXResourceViewBase constructor for textures currently modifies the
	// underlying texture object via FMetalSurface::PrepareTextureView() to add
	// PixelFormatView support if it was not already created with it.
	//
	// Because of this, the following RHI thread stall is necessary. We will need
	// to clean this up in future before RHI functions can be completely thread-
	// safe.
	//

	if (!([Surface->Texture.GetPtr() usage] & MTLTextureUsagePixelFormatView))
	{
		FScopedRHIThreadStaller StallRHIThread(RHICmdList);
		return this->RHICreateUnorderedAccessView(Texture, MipLevel, FirstArraySlice, NumArraySlices);
	}
	else
	{
		return this->RHICreateUnorderedAccessView(Texture, MipLevel, FirstArraySlice, NumArraySlices);
	}
}

FUnorderedAccessViewRHIRef FAGXDynamicRHI::RHICreateUnorderedAccessView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIBuffer* Buffer, uint8 Format)
{
	FUnorderedAccessViewRHIRef Result = this->RHICreateUnorderedAccessView(Buffer, Format);
	if (IsRunningRHIInSeparateThread() && !RHICmdList.Bypass())
	{
		RHICmdList.RHIThreadFence(true);
	}
	return Result;
}

FUnorderedAccessViewRHIRef FAGXDynamicRHI::RHICreateUnorderedAccessView(FRHIBuffer* BufferRHI, bool bUseUAVCounter, bool bAppendBuffer)
{
	@autoreleasepool {
		return new FAGXUnorderedAccessView(BufferRHI, bUseUAVCounter, bAppendBuffer);
	}
}

FUnorderedAccessViewRHIRef FAGXDynamicRHI::RHICreateUnorderedAccessView(FRHITexture* TextureRHI, uint32 MipLevel, uint16 FirstArraySlice, uint16 NumArraySlices)
{
	@autoreleasepool {
		return new FAGXUnorderedAccessView(TextureRHI, MipLevel, FirstArraySlice, NumArraySlices);
	}
}

FUnorderedAccessViewRHIRef FAGXDynamicRHI::RHICreateUnorderedAccessView(FRHIBuffer* BufferRHI, uint8 Format)
{
	@autoreleasepool {
		return new FAGXUnorderedAccessView(BufferRHI, (EPixelFormat)Format);
	}
}

FShaderResourceViewRHIRef FAGXDynamicRHI::CreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIBuffer* Buffer, uint32 Stride, uint8 Format)
{
	FShaderResourceViewRHIRef Result = this->RHICreateShaderResourceView(Buffer, Stride, Format);
	if (IsRunningRHIInSeparateThread() && !RHICmdList.Bypass())
	{
		RHICmdList.RHIThreadFence(true);
	}
	return Result;
}

FShaderResourceViewRHIRef FAGXDynamicRHI::CreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, const FShaderResourceViewInitializer& Initializer)
{
	FShaderResourceViewRHIRef Result = this->RHICreateShaderResourceView(Initializer);
	if (IsRunningRHIInSeparateThread() && !RHICmdList.Bypass())
	{
		RHICmdList.RHIThreadFence(true);
	}
	return Result;
}

FShaderResourceViewRHIRef FAGXDynamicRHI::CreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIBuffer* Buffer)
{
	FShaderResourceViewRHIRef Result = this->RHICreateShaderResourceView(Buffer);
	if (IsRunningRHIInSeparateThread() && !RHICmdList.Bypass())
	{
		RHICmdList.RHIThreadFence(true);
	}
	return Result;
}

FShaderResourceViewRHIRef FAGXDynamicRHI::RHICreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIBuffer* Buffer, uint32 Stride, uint8 Format)
{
	FShaderResourceViewRHIRef Result = this->RHICreateShaderResourceView(Buffer, Stride, Format);
	if (IsRunningRHIInSeparateThread() && !RHICmdList.Bypass())
	{
		RHICmdList.RHIThreadFence(true);
	}
	return Result;
}

FShaderResourceViewRHIRef FAGXDynamicRHI::RHICreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, const FShaderResourceViewInitializer& Initializer)
{
	FShaderResourceViewRHIRef Result = this->RHICreateShaderResourceView(Initializer);
	if (IsRunningRHIInSeparateThread() && !RHICmdList.Bypass())
	{
		RHICmdList.RHIThreadFence(true);
	}
	return Result;
}

FShaderResourceViewRHIRef FAGXDynamicRHI::RHICreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIBuffer* Buffer)
{
	FShaderResourceViewRHIRef Result = this->RHICreateShaderResourceView(Buffer);
	if (IsRunningRHIInSeparateThread() && !RHICmdList.Bypass())
	{
		RHICmdList.RHIThreadFence(true);
	}
	return Result;
}

FShaderResourceViewRHIRef FAGXDynamicRHI::RHICreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture* Texture2DRHI, const FRHITextureSRVCreateInfo& CreateInfo)
{
	FAGXSurface* Surface = AGXGetMetalSurfaceFromRHITexture(Texture2DRHI);

	//
	// The FMetalResourceViewBase constructor for textures currently modifies the
	// underlying texture object via FMetalSurface::PrepareTextureView() to add
	// PixelFormatView support if it was not already created with it.
	//
	// Because of this, the following RHI thread stall is necessary. We will need
	// to clean this up in future before RHI functions can be completely thread-
	// safe.
	//
	
	if (!([Surface->Texture.GetPtr() usage] & MTLTextureUsagePixelFormatView))
	{
		FScopedRHIThreadStaller StallRHIThread(RHICmdList);
		return this->RHICreateShaderResourceView(Texture2DRHI, CreateInfo);
	}
	else
	{
		return this->RHICreateShaderResourceView(Texture2DRHI, CreateInfo);
	}
}

FShaderResourceViewRHIRef FAGXDynamicRHI::RHICreateShaderResourceView(FRHITexture* Texture2DRHI, const FRHITextureSRVCreateInfo& CreateInfo)
{
	@autoreleasepool {
		return new FAGXShaderResourceView(Texture2DRHI, CreateInfo);
	}
}

FShaderResourceViewRHIRef FAGXDynamicRHI::RHICreateShaderResourceView(FRHIBuffer* BufferRHI)
{
	@autoreleasepool {
		return this->RHICreateShaderResourceView(FShaderResourceViewInitializer(BufferRHI));
	}
}

FShaderResourceViewRHIRef FAGXDynamicRHI::RHICreateShaderResourceView(FRHIBuffer* BufferRHI, uint32 Stride, uint8 Format)
{
	@autoreleasepool {
		check(GPixelFormats[Format].BlockBytes == Stride);
		return this->RHICreateShaderResourceView(FShaderResourceViewInitializer(BufferRHI, EPixelFormat(Format)));
	}
}

FShaderResourceViewRHIRef FAGXDynamicRHI::RHICreateShaderResourceView(const FShaderResourceViewInitializer& Initializer)
{
	@autoreleasepool {
		return new FAGXShaderResourceView(Initializer);
	}
}

void FAGXDynamicRHI::RHIUpdateShaderResourceView(FRHIShaderResourceView* SRVRHI, FRHIBuffer* BufferRHI, uint32 Stride, uint8 Format)
{
	check(SRVRHI);
	FAGXShaderResourceView* SRV = ResourceCast(SRVRHI);
	check(!SRV->bTexture);

	FAGXResourceMultiBuffer* OldBuffer = SRV->SourceBuffer;

	SRV->SourceBuffer = ResourceCast(BufferRHI);
	SRV->Stride = Stride;
	SRV->Format = Format;

	if (SRV->SourceBuffer)
	{
		SRV->SourceBuffer->AddRef();
	}

	if (OldBuffer)
	{
		OldBuffer->Release();
	}
}

void FAGXDynamicRHI::RHIUpdateShaderResourceView(FRHIShaderResourceView* SRVRHI, FRHIBuffer* BufferRHI)
{
	check(SRVRHI);
	FAGXShaderResourceView* SRV = ResourceCast(SRVRHI);
	check(!SRV->bTexture);

	FAGXResourceMultiBuffer* OldBuffer = SRV->SourceBuffer;

	SRV->SourceBuffer = ResourceCast(BufferRHI);
	SRV->Stride = 0;

	SRV->Format = SRV->SourceBuffer && SRV->SourceBuffer->IndexType != mtlpp::IndexType::UInt16
		? PF_R32_UINT
		: PF_R16_UINT;

	if (SRV->SourceBuffer)
	{
		SRV->SourceBuffer->AddRef();
	}

	if (OldBuffer)
	{
		OldBuffer->Release();
	}
}

#if UE_METAL_RHI_SUPPORT_CLEAR_UAV_WITH_BLIT_ENCODER
void FAGXRHICommandContext::ClearUAVWithBlitEncoder(FRHIUnorderedAccessView* UnorderedAccessViewRHI, EAGXRHIClearUAVType Type, uint32 Pattern)
{
	SCOPED_AUTORELEASE_POOL;

	FAGXResourceMultiBuffer* SourceBuffer = ResourceCast(UnorderedAccessViewRHI)->GetSourceBuffer();
	FAGXBuffer Buffer = SourceBuffer->GetCurrentBuffer();
	uint32 Size = SourceBuffer->GetSize();

	check(Type != EAGXRHIClearUAVType::VertexBuffer || EnumHasAnyFlags(SourceBuffer->GetUsage(), BUF_ByteAddressBuffer));

	uint32 AlignedSize = Align(Size, BufferOffsetAlignment);
	FAGXPooledBufferArgs Args(AlignedSize, BUF_Dynamic, FAGXPooledBufferArgs::SharedStorageResourceOptions);
	FAGXBuffer Temp = GetAGXDeviceContext().CreatePooledBuffer(Args);
	uint32* ContentBytes = (uint32*)Temp.GetContents();
	for (uint32 Element = 0; Element < (AlignedSize >> 2); ++Element)
	{
		ContentBytes[Element] = Pattern;
	}
	Context->CopyFromBufferToBuffer(Temp, 0, Buffer, 0, Size);
	GetAGXDeviceContext().ReleaseBuffer(Temp);
}
#endif // UE_METAL_RHI_SUPPORT_CLEAR_UAV_WITH_BLIT_ENCODER

void FAGXRHICommandContext::RHIClearUAVFloat(FRHIUnorderedAccessView* UnorderedAccessViewRHI, const FVector4f& Values)
{
#if UE_METAL_RHI_SUPPORT_CLEAR_UAV_WITH_BLIT_ENCODER
	FAGXUnorderedAccessView* UAV = ResourceCast(UnorderedAccessViewRHI);
	if (!UAV->bTexture && EnumHasAnyFlags(UAV->GetSourceBuffer()->GetUsage(), BUF_StructuredBuffer))
	{
		ClearUAVWithBlitEncoder(UnorderedAccessViewRHI, EAGXRHIClearUAVType::StructuredBuffer, *(uint32*)&Values.X);
	}
	else
#endif // UE_METAL_RHI_SUPPORT_CLEAR_UAV_WITH_BLIT_ENCODER
	{
		TRHICommandList_RecursiveHazardous<FAGXRHICommandContext> RHICmdList(this);
		ClearUAV(RHICmdList, ResourceCast(UnorderedAccessViewRHI), &Values, true);
	}
}

void FAGXRHICommandContext::RHIClearUAVUint(FRHIUnorderedAccessView* UnorderedAccessViewRHI, const FUintVector4& Values)
{
#if UE_METAL_RHI_SUPPORT_CLEAR_UAV_WITH_BLIT_ENCODER
	FAGXUnorderedAccessView* UAV = ResourceCast(UnorderedAccessViewRHI);
	if (!UAV->bTexture && EnumHasAnyFlags(UAV->GetSourceBuffer()->GetUsage(), BUF_StructuredBuffer))
	{
		ClearUAVWithBlitEncoder(UnorderedAccessViewRHI, EAGXRHIClearUAVType::StructuredBuffer, *(uint32*)&Values.X);
	}
	else
#endif // UE_METAL_RHI_SUPPORT_CLEAR_UAV_WITH_BLIT_ENCODER
	{
		TRHICommandList_RecursiveHazardous<FAGXRHICommandContext> RHICmdList(this);
		ClearUAV(RHICmdList, ResourceCast(UnorderedAccessViewRHI), &Values, false);
	}
}

void FAGXRHICommandContext::ClearUAV(TRHICommandList_RecursiveHazardous<FAGXRHICommandContext>& RHICmdList, FAGXUnorderedAccessView* UnorderedAccessView, const void* ClearValue, bool bFloat)
{
	@autoreleasepool {
		EClearReplacementValueType ValueType = bFloat ? EClearReplacementValueType::Float : EClearReplacementValueType::Uint32;

		// The Metal validation layer will complain about resources with a
		// signed format bound against an unsigned data format type as the
		// shader parameter.
		switch (GPixelFormats[UnorderedAccessView->Format].UnrealFormat)
		{
			case PF_R32_SINT:
			case PF_R16_SINT:
			case PF_R16G16B16A16_SINT:
				ValueType = EClearReplacementValueType::Int32;
				break;
				
			default:
				break;
		}

		if (UnorderedAccessView->bTexture)
		{
			FAGXSurface* Texture = UnorderedAccessView->GetSourceTexture();
			FIntVector SizeXYZ = Texture->GetSizeXYZ();

			if (FRHITexture2D* Texture2D = Texture->GetTexture2D())
			{
				ClearUAVShader_T<EClearReplacementResourceType::Texture2D, 4, false>(RHICmdList, UnorderedAccessView, SizeXYZ.X, SizeXYZ.Y, SizeXYZ.Z, ClearValue, ValueType);
			}
			else if (FRHITexture2DArray* Texture2DArray = Texture->GetTexture2DArray())
			{
				ClearUAVShader_T<EClearReplacementResourceType::Texture2DArray, 4, false>(RHICmdList, UnorderedAccessView, SizeXYZ.X, SizeXYZ.Y, SizeXYZ.Z, ClearValue, ValueType);
			}
			else if (FRHITexture3D* Texture3D = Texture->GetTexture3D())
			{
				ClearUAVShader_T<EClearReplacementResourceType::Texture3D, 4, false>(RHICmdList, UnorderedAccessView, SizeXYZ.X, SizeXYZ.Y, SizeXYZ.Z, ClearValue, ValueType);
			}
			else if (FRHITextureCube* TextureCube = Texture->GetTextureCube())
			{
				ClearUAVShader_T<EClearReplacementResourceType::Texture2DArray, 4, false>(RHICmdList, UnorderedAccessView, SizeXYZ.X, SizeXYZ.Y, SizeXYZ.Z, ClearValue, ValueType);
			}
			else
			{
				ensure(0);
			}
		}
		else
		{
			FAGXResourceMultiBuffer* SourceBuffer = UnorderedAccessView->GetSourceBuffer();

#if UE_METAL_RHI_SUPPORT_CLEAR_UAV_WITH_BLIT_ENCODER
			if (EnumHasAnyFlags(SourceBuffer->GetUsage(), BUF_ByteAddressBuffer))
			{
				ClearUAVWithBlitEncoder(UnorderedAccessView, EAGXRHIClearUAVType::VertexBuffer, *(const uint32*)ClearValue);
			}
			else
#endif // UE_METAL_RHI_SUPPORT_CLEAR_UAV_WITH_BLIT_ENCODER
			{
				uint32 NumElements = SourceBuffer->GetSize() / GPixelFormats[UnorderedAccessView->Format].BlockBytes;
				ClearUAVShader_T<EClearReplacementResourceType::Buffer, 4, false>(RHICmdList, UnorderedAccessView, NumElements, 1, 1, ClearValue, ValueType);
			}
		}
	} // @autoreleasepool
}

void FAGXRHICommandContext::RHIBeginTransitions(TArrayView<const FRHITransition*> Transitions)
{
	for (auto Transition : Transitions)
	{
		Transition->GetPrivateData<FAGXTransitionData>()->BeginResourceTransitions();
	}
}

void FAGXRHICommandContext::RHIEndTransitions(TArrayView<const FRHITransition*> Transitions)
{
	for (auto Transition : Transitions)
	{
		Transition->GetPrivateData<FAGXTransitionData>()->EndResourceTransitions();
	}
}

void FAGXGPUFence::WriteInternal(mtlpp::CommandBuffer& CmdBuffer)
{
	Fence = CmdBuffer.GetCompletionFence();
	check(Fence);
}

void FAGXRHICommandContext::RHICopyToStagingBuffer(FRHIBuffer* SourceBufferRHI, FRHIStagingBuffer* DestinationStagingBufferRHI, uint32 Offset, uint32 NumBytes)
{
	@autoreleasepool {
		check(DestinationStagingBufferRHI);

		FAGXRHIStagingBuffer* AGXStagingBuffer = ResourceCast(DestinationStagingBufferRHI);
		ensureMsgf(!AGXStagingBuffer->bIsLocked, TEXT("Attempting to Copy to a locked staging buffer. This may have undefined behavior"));
		FAGXVertexBuffer* SourceBuffer = ResourceCast(SourceBufferRHI);
		FAGXBuffer& ReadbackBuffer = AGXStagingBuffer->ShadowBuffer;

		// Need a shadow buffer for this read. If it hasn't been allocated in our FStagingBuffer or if
		// it's not big enough to hold our readback we need to allocate.
		if (!ReadbackBuffer || ReadbackBuffer.GetLength() < NumBytes)
		{
			if (ReadbackBuffer)
			{
				AGXSafeReleaseMetalBuffer(ReadbackBuffer);
			}
			FAGXPooledBufferArgs ArgsCPU(NumBytes, BUF_Dynamic, FAGXPooledBufferArgs::SharedStorageResourceOptions);
			ReadbackBuffer = GetAGXDeviceContext().CreatePooledBuffer(ArgsCPU);
		}

		// Inline copy from the actual buffer to the shadow
		GetAGXDeviceContext().CopyFromBufferToBuffer(SourceBuffer->GetCurrentBuffer(), Offset, ReadbackBuffer, 0, NumBytes);
	}
}

void FAGXRHICommandContext::RHIWriteGPUFence(FRHIGPUFence* FenceRHI)
{
	@autoreleasepool {
		check(FenceRHI);
		FAGXGPUFence* Fence = ResourceCast(FenceRHI);
		Fence->WriteInternal(Context->GetCurrentCommandBuffer());
	}
}

FGPUFenceRHIRef FAGXDynamicRHI::RHICreateGPUFence(const FName &Name)
{
	@autoreleasepool {
	return new FAGXGPUFence(Name);
	}
}

void FAGXGPUFence::Clear()
{
	Fence = mtlpp::CommandBufferFence();
}

bool FAGXGPUFence::Poll() const
{
	if (Fence)
	{
		return Fence.Wait(0);
	}
	else
	{
		return false;
	}
}
