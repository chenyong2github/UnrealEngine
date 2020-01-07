// Copyright Epic Games, Inc. All Rights Reserved.


#include "MetalRHIPrivate.h"
#include "MetalCommandBuffer.h"
#include "RenderUtils.h"
#include "ClearReplacementShaders.h"

static void ClearUAV(TRHICommandList_RecursiveHazardous<FMetalRHICommandContext>& RHICmdList, FMetalUnorderedAccessView* UnorderedAccessView, const void* ClearValue, bool bFloat);

FMetalShaderResourceView::FMetalShaderResourceView()
: TextureView(nullptr)
, MipLevel(0)
, NumMips(0)
, Format(0)
, Stride(0)
{
	
}

FMetalShaderResourceView::~FMetalShaderResourceView()
{
	if(TextureView)
	{
		FMetalSurface* Surface = GetMetalSurfaceFromRHITexture(SourceTexture);
		if (Surface)
		{
			Surface->SRVs.Remove(this);
		}
		
		if(TextureView->Texture)
		{
			TextureView->Texture = nil;
			
			TextureView->MSAATexture = nil;
		}
		delete TextureView;
		TextureView = nullptr;
	}
	
	SourceVertexBuffer = NULL;
	SourceTexture = NULL;
}

ns::AutoReleased<FMetalTexture> FMetalShaderResourceView::GetLinearTexture(bool const bUAV)
{
	ns::AutoReleased<FMetalTexture> NewLinearTexture;
	{
		if (IsValidRef(SourceVertexBuffer))
		{
			NewLinearTexture = SourceVertexBuffer->GetLinearTexture((EPixelFormat)Format);
			check(NewLinearTexture);
		}
		else if (IsValidRef(SourceIndexBuffer))
		{
			NewLinearTexture = SourceIndexBuffer->GetLinearTexture((EPixelFormat)Format);;
			check(NewLinearTexture);
		}
	}
	return NewLinearTexture;
}

FUnorderedAccessViewRHIRef FMetalDynamicRHI::RHICreateUnorderedAccessView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIStructuredBuffer* StructuredBuffer, bool bUseUAVCounter, bool bAppendBuffer)
{
	return GDynamicRHI->RHICreateUnorderedAccessView(StructuredBuffer, bUseUAVCounter, bAppendBuffer);
}

FUnorderedAccessViewRHIRef FMetalDynamicRHI::RHICreateUnorderedAccessView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture* Texture, uint32 MipLevel)
{
	FMetalSurface* Surface = (FMetalSurface*)Texture->GetTextureBaseRHI();
	FMetalTexture Tex = Surface->Texture;
	if (!(Tex.GetUsage() & mtlpp::TextureUsage::PixelFormatView))
	{
		FScopedRHIThreadStaller StallRHIThread(RHICmdList);
		return GDynamicRHI->RHICreateUnorderedAccessView(Texture, MipLevel);
	}
	else
	{
		return GDynamicRHI->RHICreateUnorderedAccessView(Texture, MipLevel);
	}
}

FUnorderedAccessViewRHIRef FMetalDynamicRHI::RHICreateUnorderedAccessView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIVertexBuffer* VertexBuffer, uint8 Format)
{
	FUnorderedAccessViewRHIRef Result = GDynamicRHI->RHICreateUnorderedAccessView(VertexBuffer, Format);
	if (IsRunningRHIInSeparateThread() && !RHICmdList.Bypass()) 	{ 		RHICmdList.RHIThreadFence(true); 	}
	return Result;
}

FUnorderedAccessViewRHIRef FMetalDynamicRHI::RHICreateUnorderedAccessView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIIndexBuffer* IndexBuffer, uint8 Format)
{
	FUnorderedAccessViewRHIRef Result = GDynamicRHI->RHICreateUnorderedAccessView(IndexBuffer, Format);
	if (IsRunningRHIInSeparateThread() && !RHICmdList.Bypass()) 	{ 		RHICmdList.RHIThreadFence(true); 	}
	return Result;
}

FUnorderedAccessViewRHIRef FMetalDynamicRHI::RHICreateUnorderedAccessView(FRHIStructuredBuffer* StructuredBufferRHI, bool bUseUAVCounter, bool bAppendBuffer)
{
	@autoreleasepool {
	FMetalStructuredBuffer* StructuredBuffer = ResourceCast(StructuredBufferRHI);
	
	FMetalShaderResourceView* SRV = new FMetalShaderResourceView;
	SRV->SourceVertexBuffer = nullptr;
	SRV->SourceIndexBuffer = nullptr;
	SRV->TextureView = nullptr;
	SRV->SourceStructuredBuffer = StructuredBuffer;

	// create the UAV buffer to point to the structured buffer's memory
	FMetalUnorderedAccessView* UAV = new FMetalUnorderedAccessView;
	UAV->SourceView = SRV;
	return UAV;
	}
}

FUnorderedAccessViewRHIRef FMetalDynamicRHI::RHICreateUnorderedAccessView(FRHITexture* TextureRHI, uint32 MipLevel)
{
	@autoreleasepool {
	FMetalShaderResourceView* SRV = new FMetalShaderResourceView;
	SRV->SourceTexture = TextureRHI;
	
	FMetalSurface* Surface = GetMetalSurfaceFromRHITexture(TextureRHI);
	SRV->TextureView = Surface ? new FMetalSurface(*Surface, NSMakeRange(MipLevel, 1)) : nullptr;
	
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
	FMetalUnorderedAccessView* UAV = new FMetalUnorderedAccessView;
	UAV->SourceView = SRV;

	return UAV;
	}
}

FUnorderedAccessViewRHIRef FMetalDynamicRHI::RHICreateUnorderedAccessView(FRHIVertexBuffer* VertexBufferRHI, uint8 Format)
{
	@autoreleasepool {
	FMetalVertexBuffer* VertexBuffer = ResourceCast(VertexBufferRHI);
	
	FMetalShaderResourceView* SRV = new FMetalShaderResourceView;
	SRV->SourceVertexBuffer = VertexBuffer;
	SRV->TextureView = nullptr;
	SRV->SourceIndexBuffer = nullptr;
	SRV->SourceStructuredBuffer = nullptr;
	SRV->Format = Format;
	{
		check(VertexBuffer->GetUsage() & BUF_UnorderedAccess);
		VertexBuffer->CreateLinearTexture((EPixelFormat)Format, VertexBuffer);
	}
		
	// create the UAV buffer to point to the structured buffer's memory
	FMetalUnorderedAccessView* UAV = new FMetalUnorderedAccessView;
	UAV->SourceView = SRV;

	return UAV;
	}
}

FUnorderedAccessViewRHIRef FMetalDynamicRHI::RHICreateUnorderedAccessView(FRHIIndexBuffer* IndexBufferRHI, uint8 Format)
{
	@autoreleasepool {
		FMetalIndexBuffer* IndexBuffer = ResourceCast(IndexBufferRHI);
		
		FMetalShaderResourceView* SRV = new FMetalShaderResourceView;
		SRV->SourceVertexBuffer = nullptr;
		SRV->TextureView = nullptr;
		SRV->SourceIndexBuffer = IndexBuffer;
		SRV->SourceStructuredBuffer = nullptr;
		SRV->Format = Format;
		{
			check(IndexBuffer->GetUsage() & BUF_UnorderedAccess);
			IndexBuffer->CreateLinearTexture((EPixelFormat)Format, IndexBuffer);
		}
		
		// create the UAV buffer to point to the structured buffer's memory
		FMetalUnorderedAccessView* UAV = new FMetalUnorderedAccessView;
		UAV->SourceView = SRV;
		
		return UAV;
	}
}

FShaderResourceViewRHIRef FMetalDynamicRHI::CreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIVertexBuffer* VertexBuffer, uint32 Stride, uint8 Format)
{
	FShaderResourceViewRHIRef Result = GDynamicRHI->RHICreateShaderResourceView(VertexBuffer, Stride, Format);
	if (IsRunningRHIInSeparateThread() && !RHICmdList.Bypass()) 	{ 		RHICmdList.RHIThreadFence(true); 	}
	return Result;
}

FShaderResourceViewRHIRef FMetalDynamicRHI::CreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIIndexBuffer* Buffer)
{
	FShaderResourceViewRHIRef Result = GDynamicRHI->RHICreateShaderResourceView(Buffer);
	if (IsRunningRHIInSeparateThread() && !RHICmdList.Bypass()) 	{ 		RHICmdList.RHIThreadFence(true); 	}
	return Result;
}

FShaderResourceViewRHIRef FMetalDynamicRHI::RHICreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIVertexBuffer* VertexBuffer, uint32 Stride, uint8 Format)
{
	FShaderResourceViewRHIRef Result = GDynamicRHI->RHICreateShaderResourceView(VertexBuffer, Stride, Format);
	if (IsRunningRHIInSeparateThread() && !RHICmdList.Bypass()) 	{ 		RHICmdList.RHIThreadFence(true); 	}
	return Result;
}

FShaderResourceViewRHIRef FMetalDynamicRHI::RHICreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIIndexBuffer* Buffer)
{
	FShaderResourceViewRHIRef Result = GDynamicRHI->RHICreateShaderResourceView(Buffer);
	if (IsRunningRHIInSeparateThread() && !RHICmdList.Bypass()) 	{ 		RHICmdList.RHIThreadFence(true); 	}
	return Result;
}

FShaderResourceViewRHIRef FMetalDynamicRHI::RHICreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIStructuredBuffer* StructuredBuffer)
{
	return GDynamicRHI->RHICreateShaderResourceView(StructuredBuffer);
}

FShaderResourceViewRHIRef FMetalDynamicRHI::RHICreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture* Texture2DRHI, const FRHITextureSRVCreateInfo& CreateInfo)
{
	FMetalSurface* Surface = (FMetalSurface*)Texture2DRHI->GetTextureBaseRHI();
	FMetalTexture Tex = Surface->Texture;
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

FShaderResourceViewRHIRef FMetalDynamicRHI::RHICreateShaderResourceView(FRHITexture* Texture2DRHI, const FRHITextureSRVCreateInfo& CreateInfo)
{
	@autoreleasepool {
		FMetalShaderResourceView* SRV = new FMetalShaderResourceView;
		SRV->SourceTexture = (FRHITexture*)Texture2DRHI;
		
		FMetalSurface* Surface = GetMetalSurfaceFromRHITexture(Texture2DRHI);
		
		// Asking to make a SRV with PF_Unknown means to use the same format.
		// This matches the behavior of the DX11 RHI.
		EPixelFormat Format = (EPixelFormat) CreateInfo.Format;
		if(Surface && Format == PF_Unknown)
		{
			Format = Surface->PixelFormat;
		}
		
		SRV->TextureView = Surface ? new FMetalSurface(*Surface, NSMakeRange(CreateInfo.MipLevel, CreateInfo.NumMipLevels), (EPixelFormat)(CreateInfo.Format == PF_Unknown ? Surface->PixelFormat : CreateInfo.Format)) : nullptr;
		
		SRV->SourceVertexBuffer = nullptr;
		SRV->SourceIndexBuffer = nullptr;
		SRV->SourceStructuredBuffer = nullptr;
		
		SRV->MipLevel = CreateInfo.MipLevel;
		SRV->NumMips = CreateInfo.NumMipLevels;
		SRV->Format = CreateInfo.Format;
		
		if (Surface)
		{
			Surface->SRVs.Add(SRV);
		}
		
		return SRV;
	}
}

FShaderResourceViewRHIRef FMetalDynamicRHI::RHICreateShaderResourceView(FRHIStructuredBuffer* StructuredBufferRHI)
{
	FMetalStructuredBuffer* StructuredBuffer = ResourceCast(StructuredBufferRHI);

	FMetalShaderResourceView* SRV = new FMetalShaderResourceView;
	SRV->SourceVertexBuffer = nullptr;
	SRV->SourceIndexBuffer = nullptr;
	SRV->TextureView = nullptr;
	SRV->SourceStructuredBuffer = StructuredBuffer;
	
	return SRV;
}

FShaderResourceViewRHIRef FMetalDynamicRHI::RHICreateShaderResourceView(FRHIVertexBuffer* VertexBufferRHI, uint32 Stride, uint8 Format)
{
	@autoreleasepool {
	if (!VertexBufferRHI)
	{
		FMetalShaderResourceView* SRV = new FMetalShaderResourceView;
		SRV->SourceVertexBuffer = nullptr;
		SRV->TextureView = nullptr;
		SRV->SourceIndexBuffer = nullptr;
		SRV->SourceStructuredBuffer = nullptr;
		SRV->Format = Format;
		SRV->Stride = 0;
		return SRV;
	}
	FMetalVertexBuffer* VertexBuffer = ResourceCast(VertexBufferRHI);
		
	FMetalShaderResourceView* SRV = new FMetalShaderResourceView;
	SRV->SourceVertexBuffer = VertexBuffer;
	SRV->TextureView = nullptr;
	SRV->SourceIndexBuffer = nullptr;
	SRV->SourceStructuredBuffer = nullptr;
	SRV->Format = Format;
	SRV->Stride = Stride;
	{
		check(Stride == GPixelFormats[Format].BlockBytes);
		check(VertexBuffer->GetUsage() & BUF_ShaderResource);
		
		VertexBuffer->CreateLinearTexture((EPixelFormat)Format, VertexBuffer);
	}
	
	return SRV;
	}
}

FShaderResourceViewRHIRef FMetalDynamicRHI::RHICreateShaderResourceView(FRHIIndexBuffer* BufferRHI)
{
	@autoreleasepool {
	if (!BufferRHI)
	{
		FMetalShaderResourceView* SRV = new FMetalShaderResourceView;
		SRV->SourceVertexBuffer = nullptr;
		SRV->TextureView = nullptr;
		SRV->SourceIndexBuffer = nullptr;
		SRV->SourceStructuredBuffer = nullptr;
		SRV->Format = PF_R16_UINT;
		SRV->Stride = 0;
		return SRV;
	}

	FMetalIndexBuffer* Buffer = ResourceCast(BufferRHI);
		
	FMetalShaderResourceView* SRV = new FMetalShaderResourceView;
	SRV->SourceVertexBuffer = nullptr;
	SRV->SourceIndexBuffer = Buffer;
	SRV->TextureView = nullptr;
	SRV->SourceStructuredBuffer = nullptr;
	SRV->Format = (Buffer->IndexType == mtlpp::IndexType::UInt16) ? PF_R16_UINT : PF_R32_UINT;
	{
		Buffer->CreateLinearTexture((EPixelFormat)SRV->Format, Buffer);
	}
	
	return SRV;
	}
}

void FMetalDynamicRHI::RHIUpdateShaderResourceView(FRHIShaderResourceView* SRVRHI, FRHIVertexBuffer* VertexBufferRHI, uint32 Stride, uint8 Format)
{
	check(SRVRHI);
	FMetalShaderResourceView* SRV = ResourceCast(SRVRHI);
	if (!VertexBufferRHI)
	{
		SRV->SourceVertexBuffer = nullptr;
		SRV->TextureView = nullptr;
		SRV->SourceIndexBuffer = nullptr;
		SRV->SourceStructuredBuffer = nullptr;
		SRV->Format = Format;
		SRV->Stride = Stride;
	}
	else if (SRV->SourceVertexBuffer != VertexBufferRHI)
	{
		FMetalVertexBuffer* VertexBuffer = ResourceCast(VertexBufferRHI);
		SRV->SourceVertexBuffer = VertexBuffer;
		SRV->TextureView = nullptr;
		SRV->SourceIndexBuffer = nullptr;
		SRV->SourceStructuredBuffer = nullptr;
		SRV->Format = Format;
		SRV->Stride = Stride;
	}
}

void FMetalDynamicRHI::RHIUpdateShaderResourceView(FRHIShaderResourceView* SRVRHI, FRHIIndexBuffer* IndexBufferRHI)
{
	check(SRVRHI);
	FMetalShaderResourceView* SRV = ResourceCast(SRVRHI);
	if (!IndexBufferRHI)
	{
		SRV->SourceVertexBuffer = nullptr;
		SRV->TextureView = nullptr;
		SRV->SourceIndexBuffer = nullptr;
		SRV->SourceStructuredBuffer = nullptr;
		SRV->Format = PF_R16_UINT;
		SRV->Stride = 0;
	}
	else if (SRV->SourceIndexBuffer != IndexBufferRHI)
	{
		FMetalIndexBuffer* IndexBuffer = ResourceCast(IndexBufferRHI);
		SRV->SourceVertexBuffer = nullptr;
		SRV->TextureView = nullptr;
		SRV->SourceIndexBuffer = IndexBuffer;
		SRV->SourceStructuredBuffer = nullptr;
		SRV->Format = (IndexBuffer->IndexType == mtlpp::IndexType::UInt16) ? PF_R16_UINT : PF_R32_UINT;
		SRV->Stride = 0;
	}
}

void FMetalRHICommandContext::RHIClearUAVFloat(FRHIUnorderedAccessView* UnorderedAccessViewRHI, const FVector4& Values)
{
	TRHICommandList_RecursiveHazardous<FMetalRHICommandContext> RHICmdList(this);
	ClearUAV(RHICmdList, ResourceCast(UnorderedAccessViewRHI), &Values, true);
}
void FMetalRHICommandContext::RHIClearUAVUint(FRHIUnorderedAccessView* UnorderedAccessViewRHI, const FUintVector4& Values)
{
	TRHICommandList_RecursiveHazardous<FMetalRHICommandContext> RHICmdList(this);
	ClearUAV(RHICmdList, ResourceCast(UnorderedAccessViewRHI), &Values, false);
}

void ClearUAV(TRHICommandList_RecursiveHazardous<FMetalRHICommandContext>& RHICmdList, FMetalUnorderedAccessView* UnorderedAccessView, const void* ClearValue, bool bFloat)
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
			uint32 NumElements = UnorderedAccessView->SourceView->SourceVertexBuffer->GetSize() / GPixelFormats[UnorderedAccessView->SourceView->Format].BlockBytes;
			ClearUAVShader_T<EClearReplacementResourceType::Buffer, 4, false>(RHICmdList, UnorderedAccessView, NumElements, 1, 1, ClearValue, ValueType);
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
			UE_LOG(LogRHI, Warning, TEXT("Metal RHI ClearUAV does not yet support clearing of a UAV without a SourceView."));
		}
	} // @autoreleasepool
}

FComputeFenceRHIRef FMetalDynamicRHI::RHICreateComputeFence(const FName& Name)
{
	@autoreleasepool {
	return new FMetalComputeFence(Name);
	}
}

FMetalComputeFence::FMetalComputeFence(FName InName)
: FRHIComputeFence(InName)
, Fence(nullptr)
{}

FMetalComputeFence::~FMetalComputeFence()
{
	if (Fence)
		Fence->Release();
}

void FMetalComputeFence::Write(FMetalFence* InFence)
{
	check(!Fence);
	Fence = InFence;
	if (Fence)
		Fence->AddRef();
	
	FRHIComputeFence::WriteFence();
}

void FMetalComputeFence::Wait(FMetalContext& Context)
{
	if (Context.GetCurrentCommandBuffer())
	{
		Context.SubmitCommandsHint(EMetalSubmitFlagsNone);
	}
	Context.GetCurrentRenderPass().Begin(Fence);
	
	if (Fence)
		Fence->Release();
	
	Fence = nullptr;
}

void FMetalComputeFence::Reset()
{
	FRHIComputeFence::Reset();
	if (Fence)
		Fence->Release();

	Fence = nullptr;
}

void FMetalRHICommandContext::RHITransitionResources(EResourceTransitionAccess TransitionType, EResourceTransitionPipeline TransitionPipeline, FRHIUnorderedAccessView** InUAVs, int32 NumUAVs, FRHIComputeFence* WriteComputeFence)
{
	@autoreleasepool
	{
		if (TransitionType != EResourceTransitionAccess::EMetaData)
		{
			Context->TransitionResources(InUAVs, NumUAVs);
		}
		if (WriteComputeFence)
		{
			// Get the current render pass fence.
			TRefCountPtr<FMetalFence> const& MetalFence = Context->GetCurrentRenderPass().End();
			
			// Write it again as we may wait on this fence in two different encoders
			Context->GetCurrentRenderPass().Update(MetalFence);

			// Write it into the RHI object
			FMetalComputeFence* Fence = ResourceCast(WriteComputeFence);
			Fence->Write(MetalFence);
			if (GSupportsEfficientAsyncCompute)
			{
				this->RHISubmitCommandsHint();
			}
		}
	}
}

void FMetalRHICommandContext::RHITransitionResources(EResourceTransitionAccess TransitionType, FRHITexture** InTextures, int32 NumTextures)
{
	@autoreleasepool
	{
		if (TransitionType != EResourceTransitionAccess::EMetaData)
		{
			Context->TransitionResources(InTextures, NumTextures);
		}
		if (TransitionType == EResourceTransitionAccess::EReadable)
		{
			const FResolveParams ResolveParams;
			for (int32 i = 0; i < NumTextures; ++i)
			{
				RHICopyToResolveTarget(InTextures[i], InTextures[i], ResolveParams);
			}
		}
	}
}

void FMetalRHICommandContext::RHIWaitComputeFence(FRHIComputeFence* InFence)
{
	@autoreleasepool {
	if (InFence)
	{
		checkf(InFence->GetWriteEnqueued(), TEXT("ComputeFence: %s waited on before being written. This will hang the GPU."), *InFence->GetName().ToString());
		FMetalComputeFence* Fence = ResourceCast(InFence);
		Fence->Wait(*Context);
	}
	}
}

void FMetalGPUFence::WriteInternal(mtlpp::CommandBuffer& CmdBuffer)
{
	Fence = CmdBuffer.GetCompletionFence();
	check(Fence);
}

void FMetalRHICommandContext::RHICopyToStagingBuffer(FRHIVertexBuffer* SourceBufferRHI, FRHIStagingBuffer* DestinationStagingBufferRHI, uint32 Offset, uint32 NumBytes)
{
	@autoreleasepool {
		check(DestinationStagingBufferRHI);

		FMetalStagingBuffer* MetalStagingBuffer = ResourceCast(DestinationStagingBufferRHI);
		ensureMsgf(!MetalStagingBuffer->bIsLocked, TEXT("Attempting to Copy to a locked staging buffer. This may have undefined behavior"));
		FMetalVertexBuffer* SourceBuffer = ResourceCast(SourceBufferRHI);
		FMetalBuffer& ReadbackBuffer = MetalStagingBuffer->ShadowBuffer;

		// Need a shadow buffer for this read. If it hasn't been allocated in our FStagingBuffer or if
		// it's not big enough to hold our readback we need to allocate.
		if (!ReadbackBuffer || ReadbackBuffer.GetLength() < NumBytes)
		{
			if (ReadbackBuffer)
			{
				SafeReleaseMetalBuffer(ReadbackBuffer);
			}
			FMetalPooledBufferArgs ArgsCPU(GetMetalDeviceContext().GetDevice(), NumBytes, BUF_Dynamic, mtlpp::StorageMode::Shared);
			ReadbackBuffer = GetMetalDeviceContext().CreatePooledBuffer(ArgsCPU);
		}

		// Inline copy from the actual buffer to the shadow
		GetMetalDeviceContext().CopyFromBufferToBuffer(SourceBuffer->Buffer, Offset, ReadbackBuffer, 0, NumBytes);
	}
}

void FMetalRHICommandContext::RHIWriteGPUFence(FRHIGPUFence* FenceRHI)
{
	@autoreleasepool {
		check(FenceRHI);
		FMetalGPUFence* Fence = ResourceCast(FenceRHI);
		Fence->WriteInternal(Context->GetCurrentCommandBuffer());
	}
}

FGPUFenceRHIRef FMetalDynamicRHI::RHICreateGPUFence(const FName &Name)
{
	@autoreleasepool {
	return new FMetalGPUFence(Name);
	}
}

void FMetalGPUFence::Clear()
{
	Fence = mtlpp::CommandBufferFence();
}

bool FMetalGPUFence::Poll() const
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
