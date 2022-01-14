// Copyright Epic Games, Inc. All Rights Reserved.


#include "AGXRHIPrivate.h"
#include "AGXRHIStagingBuffer.h"
#include "RenderUtils.h"
#include "ClearReplacementShaders.h"
#include "AGXTransitionData.h"

FAGXShaderResourceView::FAGXShaderResourceView()
	: TextureView(nullptr)
	, Offset(0)
	, MipLevel(0)
	, bSRGBForceDisable(0)
	, Reserved(0)
	, NumMips(0)
	, Format(0)
	, Stride(0)
	, LinearTextureDesc(nullptr)
{
	// void
}

FAGXShaderResourceView::~FAGXShaderResourceView()
{
	if (LinearTextureDesc)
	{
		delete LinearTextureDesc;
		LinearTextureDesc = nullptr;
	}
	
	if (TextureView)
	{
		FAGXSurface* Surface = AGXGetMetalSurfaceFromRHITexture(SourceTexture);
		if (Surface)
		{
			Surface->SRVs.Remove(this);
		}

		delete TextureView;
		TextureView = nullptr;
	}
	
	SourceVertexBuffer = NULL;
	SourceTexture = NULL;
}

void FAGXShaderResourceView::InitLinearTextureDescriptor(const FAGXLinearTextureDescriptor& InLinearTextureDescriptor)
{
	check(!LinearTextureDesc);
	LinearTextureDesc = new FAGXLinearTextureDescriptor(InLinearTextureDescriptor);
	check(LinearTextureDesc);
}

ns::AutoReleased<FAGXTexture> FAGXShaderResourceView::GetLinearTexture(bool const bUAV)
{
	ns::AutoReleased<FAGXTexture> NewLinearTexture;
	{
		if (IsValidRef(SourceVertexBuffer))
		{
			NewLinearTexture = SourceVertexBuffer->GetLinearTexture((EPixelFormat)Format, LinearTextureDesc);
		}
		else if (IsValidRef(SourceIndexBuffer))
		{
			NewLinearTexture = SourceIndexBuffer->GetLinearTexture((EPixelFormat)Format, LinearTextureDesc);
		}
	}
	return NewLinearTexture;
}

FUnorderedAccessViewRHIRef FAGXDynamicRHI::RHICreateUnorderedAccessView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIBuffer* Buffer, bool bUseUAVCounter, bool bAppendBuffer)
{
	return GDynamicRHI->RHICreateUnorderedAccessView(Buffer, bUseUAVCounter, bAppendBuffer);
}

FUnorderedAccessViewRHIRef FAGXDynamicRHI::RHICreateUnorderedAccessView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture* Texture, uint32 MipLevel, uint16 FirstArraySlice, uint16 NumArraySlices)
{
	FAGXSurface* Surface = (FAGXSurface*)Texture->GetTextureBaseRHI();
	FAGXTexture Tex = Surface->Texture;
	if (!(Tex.GetUsage() & mtlpp::TextureUsage::PixelFormatView))
	{
		FScopedRHIThreadStaller StallRHIThread(RHICmdList);
		return GDynamicRHI->RHICreateUnorderedAccessView(Texture, MipLevel, FirstArraySlice, NumArraySlices);
	}
	else
	{
		return GDynamicRHI->RHICreateUnorderedAccessView(Texture, MipLevel, FirstArraySlice, NumArraySlices);
	}
}

FUnorderedAccessViewRHIRef FAGXDynamicRHI::RHICreateUnorderedAccessView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIBuffer* Buffer, uint8 Format)
{
	FUnorderedAccessViewRHIRef Result = GDynamicRHI->RHICreateUnorderedAccessView(Buffer, Format);
	if (IsRunningRHIInSeparateThread() && !RHICmdList.Bypass()) 	{ 		RHICmdList.RHIThreadFence(true); 	}
	return Result;
}

FUnorderedAccessViewRHIRef FAGXDynamicRHI::RHICreateUnorderedAccessView(FRHIBuffer* BufferRHI, bool bUseUAVCounter, bool bAppendBuffer)
{
	@autoreleasepool {
	FAGXStructuredBuffer* StructuredBuffer = ResourceCast(BufferRHI);
	
	FAGXShaderResourceView* SRV = new FAGXShaderResourceView;
	SRV->SourceVertexBuffer = nullptr;
	SRV->SourceIndexBuffer = nullptr;
	SRV->TextureView = nullptr;
	SRV->SourceStructuredBuffer = StructuredBuffer;

	// create the UAV buffer to point to the structured buffer's memory
	FAGXUnorderedAccessView* UAV = new FAGXUnorderedAccessView;
	UAV->SourceView = SRV;
	return UAV;
	}
}

FUnorderedAccessViewRHIRef FAGXDynamicRHI::RHICreateUnorderedAccessView(FRHITexture* TextureRHI, uint32 MipLevel, uint16 FirstArraySlice, uint16 NumArraySlices)
{
	// Slice selection of a texture array still need to be implemented on AGX
	check(FirstArraySlice == 0 && NumArraySlices == 0);
	@autoreleasepool {
	FAGXShaderResourceView* SRV = new FAGXShaderResourceView;
	SRV->SourceTexture = TextureRHI;
	
	FAGXSurface* Surface = AGXGetMetalSurfaceFromRHITexture(TextureRHI);
	SRV->TextureView = Surface ? new FAGXSurface(*Surface, NSMakeRange(MipLevel, 1)) : nullptr;
	
	SRV->SourceVertexBuffer = nullptr;
	SRV->SourceIndexBuffer = nullptr;
	SRV->SourceStructuredBuffer = nullptr;
	
	SRV->MipLevel = MipLevel;
	SRV->NumMips = 1;
	SRV->Format = PF_Unknown;
		
	if (Surface)
	{
		Surface->SRVs.Add(SRV);
	}
		
	// create the UAV buffer to point to the structured buffer's memory
	FAGXUnorderedAccessView* UAV = new FAGXUnorderedAccessView;
	UAV->SourceView = SRV;

	return UAV;
	}
}

FUnorderedAccessViewRHIRef FAGXDynamicRHI::RHICreateUnorderedAccessView(FRHIBuffer* BufferRHI, uint8 Format)
{
	@autoreleasepool {
	FAGXResourceMultiBuffer* Buffer = ResourceCast(BufferRHI);
	
	FAGXShaderResourceView* SRV = new FAGXShaderResourceView;
	SRV->SourceVertexBuffer = EnumHasAnyFlags(BufferRHI->GetUsage(), BUF_VertexBuffer) ? Buffer : nullptr;
	SRV->TextureView = nullptr;
	SRV->SourceIndexBuffer = EnumHasAnyFlags(BufferRHI->GetUsage(), BUF_IndexBuffer) ? Buffer : nullptr;
	SRV->SourceStructuredBuffer = nullptr;
	SRV->Format = Format;
	{
		check(Buffer->GetUsage() & BUF_UnorderedAccess);
		Buffer->CreateLinearTexture((EPixelFormat)Format, Buffer);
	}
		
	// create the UAV buffer to point to the structured buffer's memory
	FAGXUnorderedAccessView* UAV = new FAGXUnorderedAccessView;
	UAV->SourceView = SRV;

	return UAV;
	}
}

FShaderResourceViewRHIRef FAGXDynamicRHI::CreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIBuffer* Buffer, uint32 Stride, uint8 Format)
{
	FShaderResourceViewRHIRef Result = GDynamicRHI->RHICreateShaderResourceView(Buffer, Stride, Format);
	if (IsRunningRHIInSeparateThread() && !RHICmdList.Bypass()) 	{ 		RHICmdList.RHIThreadFence(true); 	}
	return Result;
}

FShaderResourceViewRHIRef FAGXDynamicRHI::CreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, const FShaderResourceViewInitializer& Initializer)
{
	FShaderResourceViewRHIRef Result = GDynamicRHI->RHICreateShaderResourceView(Initializer);
	if (IsRunningRHIInSeparateThread() && !RHICmdList.Bypass()) { RHICmdList.RHIThreadFence(true); }
	return Result;
}

FShaderResourceViewRHIRef FAGXDynamicRHI::CreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIBuffer* Buffer)
{
	FShaderResourceViewRHIRef Result = GDynamicRHI->RHICreateShaderResourceView(Buffer);
	if (IsRunningRHIInSeparateThread() && !RHICmdList.Bypass()) 	{ 		RHICmdList.RHIThreadFence(true); 	}
	return Result;
}

FShaderResourceViewRHIRef FAGXDynamicRHI::RHICreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIBuffer* Buffer, uint32 Stride, uint8 Format)
{
	FShaderResourceViewRHIRef Result = GDynamicRHI->RHICreateShaderResourceView(Buffer, Stride, Format);
	if (IsRunningRHIInSeparateThread() && !RHICmdList.Bypass()) 	{ 		RHICmdList.RHIThreadFence(true); 	}
	return Result;
}

FShaderResourceViewRHIRef FAGXDynamicRHI::RHICreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, const FShaderResourceViewInitializer& Initializer)
{
	FShaderResourceViewRHIRef Result = GDynamicRHI->RHICreateShaderResourceView(Initializer);
	if (IsRunningRHIInSeparateThread() && !RHICmdList.Bypass()) 	{ 		RHICmdList.RHIThreadFence(true); 	}
	return Result;
}

FShaderResourceViewRHIRef FAGXDynamicRHI::RHICreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIBuffer* Buffer)
{
	FShaderResourceViewRHIRef Result = GDynamicRHI->RHICreateShaderResourceView(Buffer);
	if (IsRunningRHIInSeparateThread() && !RHICmdList.Bypass()) 	{ 		RHICmdList.RHIThreadFence(true); 	}
	return Result;
}

FShaderResourceViewRHIRef FAGXDynamicRHI::RHICreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture* Texture2DRHI, const FRHITextureSRVCreateInfo& CreateInfo)
{
	FAGXSurface* Surface = (FAGXSurface*)Texture2DRHI->GetTextureBaseRHI();
	FAGXTexture Tex = Surface->Texture;
	if (!(Tex.GetUsage() & mtlpp::TextureUsage::PixelFormatView))
	{
		FScopedRHIThreadStaller StallRHIThread(RHICmdList);
		return GDynamicRHI->RHICreateShaderResourceView(Texture2DRHI, CreateInfo);
	}
	else
	{
		return GDynamicRHI->RHICreateShaderResourceView(Texture2DRHI, CreateInfo);
	}
}

FShaderResourceViewRHIRef FAGXDynamicRHI::RHICreateShaderResourceView(FRHITexture* Texture2DRHI, const FRHITextureSRVCreateInfo& CreateInfo)
{
	@autoreleasepool {
		FAGXShaderResourceView* SRV = new FAGXShaderResourceView;
		SRV->SourceTexture = (FRHITexture*)Texture2DRHI;
		
		FAGXSurface* Surface = AGXGetMetalSurfaceFromRHITexture(Texture2DRHI);
		
		// Asking to make a SRV with PF_Unknown means to use the same format.
		// This matches the behavior of the DX11 RHI.
		EPixelFormat Format = (EPixelFormat) CreateInfo.Format;
		if(Surface && Format == PF_Unknown)
		{
			Format = Surface->PixelFormat;
		}
		
		const bool bSRGBForceDisable = (CreateInfo.SRGBOverride == SRGBO_ForceDisable);
		
		SRV->TextureView = Surface ? new FAGXSurface(*Surface, NSMakeRange(CreateInfo.MipLevel, CreateInfo.NumMipLevels), Format, bSRGBForceDisable) : nullptr;
		
		SRV->SourceVertexBuffer = nullptr;
		SRV->SourceIndexBuffer = nullptr;
		SRV->SourceStructuredBuffer = nullptr;
		
		SRV->MipLevel = CreateInfo.MipLevel;
		SRV->bSRGBForceDisable = bSRGBForceDisable;
		SRV->NumMips = CreateInfo.NumMipLevels;
		SRV->Format = CreateInfo.Format;
		
		if (Surface)
		{
			Surface->SRVs.Add(SRV);
		}
		
		return SRV;
	}
}

FShaderResourceViewRHIRef FAGXDynamicRHI::RHICreateShaderResourceView(FRHIBuffer* BufferRHI)
{
	@autoreleasepool {
	return RHICreateShaderResourceView(FShaderResourceViewInitializer(BufferRHI));
	}
}

FShaderResourceViewRHIRef FAGXDynamicRHI::RHICreateShaderResourceView(FRHIBuffer* BufferRHI, uint32 Stride, uint8 Format)
{
	check(GPixelFormats[Format].BlockBytes == Stride);

	@autoreleasepool
	{
		return RHICreateShaderResourceView(FShaderResourceViewInitializer(BufferRHI, EPixelFormat(Format)));
	}
}

FShaderResourceViewRHIRef FAGXDynamicRHI::RHICreateShaderResourceView(const FShaderResourceViewInitializer& Initializer)
{
	@autoreleasepool
	{
		switch(Initializer.GetType())
		{
			case FShaderResourceViewInitializer::EType::VertexBufferSRV:
			{
				FShaderResourceViewInitializer::FVertexBufferShaderResourceViewInitializer Desc = Initializer.AsVertexBufferSRV();
				
				FAGXVertexBuffer* VertexBuffer = ResourceCast(Desc.Buffer);
				
				FAGXShaderResourceView* SRV = new FAGXShaderResourceView;
				SRV->SourceVertexBuffer = VertexBuffer;
				SRV->TextureView = nullptr;
				SRV->SourceIndexBuffer = nullptr;
				SRV->SourceStructuredBuffer = nullptr;
				
				SRV->Format = Desc.Format;
				
				if(!VertexBuffer)
				{
					SRV->Offset = 0;
					SRV->Stride = 0;
				}
				else
				{
					check(VertexBuffer->GetUsage() & BUF_ShaderResource);
					uint32 Stride = GPixelFormats[Desc.Format].BlockBytes;
					
					SRV->Stride = Stride;
					SRV->Offset = Desc.StartOffsetBytes;
					
					FAGXLinearTextureDescriptor LinearTextureDesc(Desc.StartOffsetBytes, Desc.NumElements, Stride);
					SRV->InitLinearTextureDescriptor(LinearTextureDesc);
					
					VertexBuffer->CreateLinearTexture((EPixelFormat)Desc.Format, VertexBuffer, &LinearTextureDesc);
				}
				
				return SRV;
			} // VertexBufferSRV
				
			case FShaderResourceViewInitializer::EType::StructuredBufferSRV:
			{
				FShaderResourceViewInitializer::FStructuredBufferShaderResourceViewInitializer Desc = Initializer.AsStructuredBufferSRV();
				
				FAGXStructuredBuffer* StructuredBuffer = ResourceCast(Desc.Buffer);

				FAGXShaderResourceView* SRV = new FAGXShaderResourceView;
				SRV->SourceVertexBuffer = nullptr;
				SRV->SourceIndexBuffer = nullptr;
				SRV->TextureView = nullptr;
				SRV->SourceStructuredBuffer = StructuredBuffer;
				
				SRV->Offset = Desc.StartOffsetBytes;
				SRV->Format = 0;
				SRV->Stride = StructuredBuffer->GetStride();
				
				return SRV;
			} // StructuredBufferSRV
				
			case FShaderResourceViewInitializer::EType::IndexBufferSRV:
			{
				FShaderResourceViewInitializer::FIndexBufferShaderResourceViewInitializer Desc = Initializer.AsIndexBufferSRV();
				
				FAGXIndexBuffer* IndexBuffer = ResourceCast(Desc.Buffer);
				
				FAGXShaderResourceView* SRV = new FAGXShaderResourceView;
				SRV->SourceVertexBuffer = nullptr;
				SRV->SourceIndexBuffer = IndexBuffer;
				SRV->TextureView = nullptr;
				
				if(!IndexBuffer)
				{
					SRV->Format = PF_R16_UINT;
					SRV->Stride = 0;
					SRV->Offset = 0;
				}
				else
				{
					SRV->Format = (IndexBuffer->IndexType == mtlpp::IndexType::UInt16) ? PF_R16_UINT : PF_R32_UINT;
					
					const uint32 Stride = Desc.Buffer->GetStride();
					check(Stride == ((SRV->Format == PF_R16_UINT) ? 2 : 4));
					
					SRV->Offset = Desc.StartOffsetBytes;
					SRV->Stride = Stride;
					
					FAGXLinearTextureDescriptor LinearTextureDesc(Desc.StartOffsetBytes, Desc.NumElements, Stride);
					SRV->InitLinearTextureDescriptor(LinearTextureDesc);
					
					IndexBuffer->CreateLinearTexture((EPixelFormat)SRV->Format, IndexBuffer, &LinearTextureDesc);
				}
					  
				return SRV;
			} // IndexBufferSRV
					  
			default:
			{
				checkNoEntry();
				return nullptr;
			}
		}
	}
}

void FAGXDynamicRHI::RHIUpdateShaderResourceView(FRHIShaderResourceView* SRVRHI, FRHIBuffer* VertexBufferRHI, uint32 Stride, uint8 Format)
{
	check(SRVRHI);
	FAGXShaderResourceView* SRV = ResourceCast(SRVRHI);
	if (!VertexBufferRHI)
	{
		SRV->SourceVertexBuffer = nullptr;
		SRV->TextureView = nullptr;
		SRV->SourceIndexBuffer = nullptr;
		SRV->SourceStructuredBuffer = nullptr;
		SRV->Offset = 0;
		SRV->Format = Format;
		SRV->Stride = Stride;
	}
	else if (SRV->SourceVertexBuffer != VertexBufferRHI)
	{
		FAGXVertexBuffer* VertexBuffer = ResourceCast(VertexBufferRHI);
		SRV->SourceVertexBuffer = VertexBuffer;
		SRV->TextureView = nullptr;
		SRV->SourceIndexBuffer = nullptr;
		SRV->SourceStructuredBuffer = nullptr;
		SRV->Offset = 0;
		SRV->Format = Format;
		SRV->Stride = Stride;
	}
}

void FAGXDynamicRHI::RHIUpdateShaderResourceView(FRHIShaderResourceView* SRVRHI, FRHIBuffer* IndexBufferRHI)
{
	check(SRVRHI);
	FAGXShaderResourceView* SRV = ResourceCast(SRVRHI);
	if (!IndexBufferRHI)
	{
		SRV->SourceVertexBuffer = nullptr;
		SRV->TextureView = nullptr;
		SRV->SourceIndexBuffer = nullptr;
		SRV->SourceStructuredBuffer = nullptr;
		SRV->Offset = 0;
		SRV->Format = PF_R16_UINT;
		SRV->Stride = 0;
	}
	else if (SRV->SourceIndexBuffer != IndexBufferRHI)
	{
		FAGXIndexBuffer* IndexBuffer = ResourceCast(IndexBufferRHI);
		SRV->SourceVertexBuffer = nullptr;
		SRV->TextureView = nullptr;
		SRV->SourceIndexBuffer = IndexBuffer;
		SRV->SourceStructuredBuffer = nullptr;
		SRV->Offset = 0;
		SRV->Format = (IndexBuffer->IndexType == mtlpp::IndexType::UInt16) ? PF_R16_UINT : PF_R32_UINT;
		SRV->Stride = 0;
	}
}

#if UE_METAL_RHI_SUPPORT_CLEAR_UAV_WITH_BLIT_ENCODER
void FAGXRHICommandContext::ClearUAVWithBlitEncoder(FRHIUnorderedAccessView* UnorderedAccessViewRHI, EAGXRHIClearUAVType Type, uint32 Pattern)
{
	SCOPED_AUTORELEASE_POOL;

	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
	FAGXUnorderedAccessView* UnorderedAccessView = ResourceCast(UnorderedAccessViewRHI);
	FAGXBuffer Buffer;
	uint32 Size = 0;

	switch (Type)
	{
		case EAGXRHIClearUAVType::VertexBuffer:
			check(EnumHasAnyFlags(UnorderedAccessView->SourceView->SourceVertexBuffer->GetUsage(), BUF_ByteAddressBuffer));
			Buffer = UnorderedAccessView->SourceView->SourceVertexBuffer->GetCurrentBuffer();
			Size = UnorderedAccessView->SourceView->SourceVertexBuffer->GetSize();
			break;

		case EAGXRHIClearUAVType::StructuredBuffer:
			Buffer = UnorderedAccessView->SourceView->SourceStructuredBuffer->GetCurrentBuffer();
			Size = UnorderedAccessView->SourceView->SourceStructuredBuffer->GetSize();
			break;
	};

	uint32 AlignedSize = Align(Size, BufferOffsetAlignment);
	FAGXPooledBufferArgs Args(AlignedSize, BUF_Dynamic, mtlpp::StorageMode::Shared);
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
	FAGXUnorderedAccessView* UnorderedAccessView = ResourceCast(UnorderedAccessViewRHI);
	if (UnorderedAccessView->SourceView->SourceStructuredBuffer)
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
	FAGXUnorderedAccessView* UnorderedAccessView = ResourceCast(UnorderedAccessViewRHI);
	if (UnorderedAccessView->SourceView->SourceStructuredBuffer)
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
		switch (GPixelFormats[UnorderedAccessView->SourceView->Format].UnrealFormat)
		{
			case PF_R32_SINT:
			case PF_R16_SINT:
			case PF_R16G16B16A16_SINT:
				ValueType = EClearReplacementValueType::Int32;
				break;
				
			default:
				break;
		}

		if (UnorderedAccessView->SourceView->SourceVertexBuffer)
		{
#if UE_METAL_RHI_SUPPORT_CLEAR_UAV_WITH_BLIT_ENCODER
			if (EnumHasAnyFlags(UnorderedAccessView->SourceView->SourceVertexBuffer->GetUsage(), BUF_ByteAddressBuffer))
			{
				ClearUAVWithBlitEncoder(UnorderedAccessView, EAGXRHIClearUAVType::VertexBuffer, *(const uint32*)ClearValue);
			}
			else
#endif // UE_METAL_RHI_SUPPORT_CLEAR_UAV_WITH_BLIT_ENCODER
			{
				uint32 NumElements = UnorderedAccessView->SourceView->SourceVertexBuffer->GetSize() / GPixelFormats[UnorderedAccessView->SourceView->Format].BlockBytes;
				ClearUAVShader_T<EClearReplacementResourceType::Buffer, 4, false>(RHICmdList, UnorderedAccessView, NumElements, 1, 1, ClearValue, ValueType);
			}
		}
		else if (UnorderedAccessView->SourceView->SourceTexture)
		{
			FIntVector SizeXYZ = UnorderedAccessView->SourceView->SourceTexture->GetSizeXYZ();

			if (FRHITexture2D* Texture2D = UnorderedAccessView->SourceView->SourceTexture->GetTexture2D())
			{
				ClearUAVShader_T<EClearReplacementResourceType::Texture2D, 4, false>(RHICmdList, UnorderedAccessView, SizeXYZ.X, SizeXYZ.Y, SizeXYZ.Z, ClearValue, ValueType);
			}
			else if (FRHITexture2DArray* Texture2DArray = UnorderedAccessView->SourceView->SourceTexture->GetTexture2DArray())
			{
				ClearUAVShader_T<EClearReplacementResourceType::Texture2DArray, 4, false>(RHICmdList, UnorderedAccessView, SizeXYZ.X, SizeXYZ.Y, SizeXYZ.Z, ClearValue, ValueType);
			}
			else if (FRHITexture3D* Texture3D = UnorderedAccessView->SourceView->SourceTexture->GetTexture3D())
			{
				ClearUAVShader_T<EClearReplacementResourceType::Texture3D, 4, false>(RHICmdList, UnorderedAccessView, SizeXYZ.X, SizeXYZ.Y, SizeXYZ.Z, ClearValue, ValueType);
			}
			else if (FRHITextureCube* TextureCube = UnorderedAccessView->SourceView->SourceTexture->GetTextureCube())
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
			// TODO: ensure(0);
			UE_LOG(LogRHI, Warning, TEXT("AGX RHI ClearUAV does not yet support clearing of a UAV without a SourceView."));
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
			FAGXPooledBufferArgs ArgsCPU(NumBytes, BUF_Dynamic, mtlpp::StorageMode::Shared);
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
