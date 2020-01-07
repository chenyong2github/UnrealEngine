// Copyright Epic Games, Inc. All Rights Reserved.

#include "D3D11RHIPrivate.h"
#include "ClearReplacementShaders.h"

FUnorderedAccessViewRHIRef FD3D11DynamicRHI::RHICreateUnorderedAccessView(FRHIStructuredBuffer* StructuredBufferRHI, bool bUseUAVCounter, bool bAppendBuffer)
{
	FD3D11StructuredBuffer* StructuredBuffer = ResourceCast(StructuredBufferRHI);

	D3D11_BUFFER_DESC BufferDesc;
	StructuredBuffer->Resource->GetDesc(&BufferDesc);

	const bool bByteAccessBuffer = (BufferDesc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS) != 0;

	D3D11_UNORDERED_ACCESS_VIEW_DESC UAVDesc;
	UAVDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	UAVDesc.Format = DXGI_FORMAT_UNKNOWN;

	if (BufferDesc.MiscFlags & D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS)
	{
		UAVDesc.Format = DXGI_FORMAT_R32_UINT;
	}
	else if (bByteAccessBuffer)
	{
		UAVDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	}
		
	UAVDesc.Buffer.FirstElement = 0;

	// For byte access buffers and indirect draw argument buffers, GetDesc returns a StructureByteStride of 0 even though we created it with 4
	const uint32 EffectiveStride = BufferDesc.StructureByteStride == 0 ? 4 : BufferDesc.StructureByteStride;
	UAVDesc.Buffer.NumElements = BufferDesc.ByteWidth / EffectiveStride;
	UAVDesc.Buffer.Flags = 0;

	if (bUseUAVCounter)
	{
		UAVDesc.Buffer.Flags |= D3D11_BUFFER_UAV_FLAG_COUNTER;
	}

	if (bAppendBuffer)
	{
		UAVDesc.Buffer.Flags |= D3D11_BUFFER_UAV_FLAG_APPEND;
	}

	if (bByteAccessBuffer)
	{
		UAVDesc.Buffer.Flags |= D3D11_BUFFER_UAV_FLAG_RAW;
	}

	TRefCountPtr<ID3D11UnorderedAccessView> UnorderedAccessView;
	VERIFYD3D11RESULT_EX(Direct3DDevice->CreateUnorderedAccessView(StructuredBuffer->Resource,&UAVDesc,(ID3D11UnorderedAccessView**)UnorderedAccessView.GetInitReference()), Direct3DDevice);

	return new FD3D11UnorderedAccessView(UnorderedAccessView,StructuredBuffer);
}

FUnorderedAccessViewRHIRef FD3D11DynamicRHI::RHICreateUnorderedAccessView_RenderThread(
	class FRHICommandListImmediate& RHICmdList,
	FRHIStructuredBuffer* StructuredBuffer,
	bool bUseUAVCounter,
	bool bAppendBuffer)
{
	return RHICreateUnorderedAccessView(StructuredBuffer, bUseUAVCounter, bAppendBuffer);
}

FUnorderedAccessViewRHIRef FD3D11DynamicRHI::RHICreateUnorderedAccessView(FRHITexture* TextureRHI, uint32 MipLevel)
{
	FD3D11TextureBase* Texture = GetD3D11TextureFromRHITexture(TextureRHI);
	
	D3D11_UNORDERED_ACCESS_VIEW_DESC UAVDesc;

	if (TextureRHI->GetTexture3D() != NULL)
	{
		FD3D11Texture3D* Texture3D = (FD3D11Texture3D*)Texture;
		UAVDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
		UAVDesc.Texture3D.MipSlice = MipLevel;
		UAVDesc.Texture3D.FirstWSlice = 0;
		UAVDesc.Texture3D.WSize = Texture3D->GetSizeZ() >> MipLevel;
	}
	else if (TextureRHI->GetTexture2DArray() != NULL)
	{
		FD3D11Texture2DArray* Texture2DArray = (FD3D11Texture2DArray*)Texture;
		UAVDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
		UAVDesc.Texture2DArray.MipSlice = MipLevel;
		UAVDesc.Texture2DArray.FirstArraySlice = 0;
		UAVDesc.Texture2DArray.ArraySize = Texture2DArray->GetSizeZ();
	}
	else if (TextureRHI->GetTextureCube() != NULL)
	{
		FD3D11TextureCube* TextureCube = (FD3D11TextureCube*)Texture;
		UAVDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
		UAVDesc.Texture2DArray.MipSlice = MipLevel;
		UAVDesc.Texture2DArray.FirstArraySlice = 0;
		UAVDesc.Texture2DArray.ArraySize = TextureCube->GetSizeZ();
	}
	else
	{
		UAVDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		UAVDesc.Texture2D.MipSlice = MipLevel;
	}
	
	UAVDesc.Format = FindShaderResourceDXGIFormat((DXGI_FORMAT)GPixelFormats[TextureRHI->GetFormat()].PlatformFormat, false);

	TRefCountPtr<ID3D11UnorderedAccessView> UnorderedAccessView;
	VERIFYD3D11RESULT_EX(Direct3DDevice->CreateUnorderedAccessView(Texture->GetResource(),&UAVDesc,(ID3D11UnorderedAccessView**)UnorderedAccessView.GetInitReference()), Direct3DDevice);

	return new FD3D11UnorderedAccessView(UnorderedAccessView,Texture);
}

FUnorderedAccessViewRHIRef FD3D11DynamicRHI::RHICreateUnorderedAccessView_RenderThread(
	class FRHICommandListImmediate& RHICmdList,
	FRHITexture* Texture,
	uint32 MipLevel)
{
	return RHICreateUnorderedAccessView(Texture, MipLevel);
}

FUnorderedAccessViewRHIRef FD3D11DynamicRHI::RHICreateUnorderedAccessView(FRHIVertexBuffer* VertexBufferRHI, uint8 Format)
{
	FD3D11VertexBuffer* VertexBuffer = ResourceCast(VertexBufferRHI);

	D3D11_BUFFER_DESC BufferDesc;
	VertexBuffer->Resource->GetDesc(&BufferDesc);

	const bool bByteAccessBuffer = (BufferDesc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS) != 0;

	D3D11_UNORDERED_ACCESS_VIEW_DESC UAVDesc;
	UAVDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	UAVDesc.Format = FindUnorderedAccessDXGIFormat((DXGI_FORMAT)GPixelFormats[Format].PlatformFormat);
	UAVDesc.Buffer.FirstElement = 0;

	UAVDesc.Buffer.NumElements = BufferDesc.ByteWidth / GPixelFormats[Format].BlockBytes;
	UAVDesc.Buffer.Flags = 0;

	if (bByteAccessBuffer)
	{
		UAVDesc.Buffer.Flags |= D3D11_BUFFER_UAV_FLAG_RAW;
		UAVDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	}

	TRefCountPtr<ID3D11UnorderedAccessView> UnorderedAccessView;
	VERIFYD3D11RESULT_EX(Direct3DDevice->CreateUnorderedAccessView(VertexBuffer->Resource,&UAVDesc,(ID3D11UnorderedAccessView**)UnorderedAccessView.GetInitReference()), Direct3DDevice);

	return new FD3D11UnorderedAccessView(UnorderedAccessView,VertexBuffer);
}

FUnorderedAccessViewRHIRef FD3D11DynamicRHI::RHICreateUnorderedAccessView_RenderThread(
	class FRHICommandListImmediate& RHICmdList,
	FRHIVertexBuffer* VertexBuffer,
	uint8 Format)
{
	return RHICreateUnorderedAccessView(VertexBuffer, Format);
}

FUnorderedAccessViewRHIRef FD3D11DynamicRHI::RHICreateUnorderedAccessView(FRHIIndexBuffer* IndexBufferRHI, uint8 Format)
{
	FD3D11IndexBuffer* IndexBuffer = ResourceCast(IndexBufferRHI);

	D3D11_BUFFER_DESC BufferDesc;
	IndexBuffer->Resource->GetDesc(&BufferDesc);

	const bool bByteAccessBuffer = (BufferDesc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS) != 0;

	D3D11_UNORDERED_ACCESS_VIEW_DESC UAVDesc;
	UAVDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	UAVDesc.Format = FindUnorderedAccessDXGIFormat((DXGI_FORMAT)GPixelFormats[Format].PlatformFormat);
	UAVDesc.Buffer.FirstElement = 0;

	UAVDesc.Buffer.NumElements = BufferDesc.ByteWidth / GPixelFormats[Format].BlockBytes;
	UAVDesc.Buffer.Flags = 0;

	if (bByteAccessBuffer)
	{
		UAVDesc.Buffer.Flags |= D3D11_BUFFER_UAV_FLAG_RAW;
		UAVDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	}

	TRefCountPtr<ID3D11UnorderedAccessView> UnorderedAccessView;
	VERIFYD3D11RESULT_EX(Direct3DDevice->CreateUnorderedAccessView(IndexBuffer->Resource, &UAVDesc, (ID3D11UnorderedAccessView**)UnorderedAccessView.GetInitReference()), Direct3DDevice);

	return new FD3D11UnorderedAccessView(UnorderedAccessView, IndexBuffer);
}

FUnorderedAccessViewRHIRef FD3D11DynamicRHI::RHICreateUnorderedAccessView_RenderThread(
	class FRHICommandListImmediate& RHICmdList,
	FRHIIndexBuffer* IndexBuffer,
	uint8 Format)
{
	return RHICreateUnorderedAccessView(IndexBuffer, Format);
}

FShaderResourceViewRHIRef FD3D11DynamicRHI::RHICreateShaderResourceView(FRHIStructuredBuffer* StructuredBufferRHI)
{
	FD3D11StructuredBuffer* StructuredBuffer = ResourceCast(StructuredBufferRHI);

	D3D11_BUFFER_DESC BufferDesc;
	StructuredBuffer->Resource->GetDesc(&BufferDesc);

	const bool bByteAccessBuffer = (BufferDesc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS) != 0;

	// Create a Shader Resource View
	D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc;

	if ( bByteAccessBuffer )
	{
		SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
		SRVDesc.BufferEx.NumElements = BufferDesc.ByteWidth / 4;
		SRVDesc.BufferEx.FirstElement = 0;
		SRVDesc.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
		SRVDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	}
	else
	{
		SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		SRVDesc.Buffer.FirstElement = 0;
	    SRVDesc.Buffer.NumElements = BufferDesc.ByteWidth / BufferDesc.StructureByteStride;
		SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
	}

	TRefCountPtr<ID3D11ShaderResourceView> ShaderResourceView;
	VERIFYD3D11RESULT_EX(Direct3DDevice->CreateShaderResourceView(StructuredBuffer->Resource, &SRVDesc, (ID3D11ShaderResourceView**)ShaderResourceView.GetInitReference()), Direct3DDevice);

	return new FD3D11ShaderResourceView(ShaderResourceView,StructuredBuffer);
}

FShaderResourceViewRHIRef FD3D11DynamicRHI::RHICreateShaderResourceView_RenderThread(
	class FRHICommandListImmediate& RHICmdList,
	FRHIStructuredBuffer* StructuredBuffer)
{
	return RHICreateShaderResourceView(StructuredBuffer);
}

static void CreateD3D11ShaderResourceViewOnBuffer(ID3D11Device* Direct3DDevice, ID3D11Buffer* Buffer, uint32 Stride, uint8 Format, ID3D11ShaderResourceView** OutSRV)
{
	D3D11_BUFFER_DESC BufferDesc;
	Buffer->GetDesc(&BufferDesc);

	// Create a Shader Resource View
	D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc;
	FMemory::Memzero(SRVDesc);
	SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	SRVDesc.Buffer.FirstElement = 0;

	SRVDesc.Format = FindShaderResourceDXGIFormat((DXGI_FORMAT)GPixelFormats[Format].PlatformFormat, false);
	SRVDesc.Buffer.NumElements = BufferDesc.ByteWidth / Stride;

	HRESULT hr = Direct3DDevice->CreateShaderResourceView(Buffer, &SRVDesc, OutSRV);
	if (FAILED(hr))
	{
		if (hr == E_OUTOFMEMORY)
		{
			// There appears to be a driver bug that causes SRV creation to fail with an OOM error and then succeed on the next call.
			hr = Direct3DDevice->CreateShaderResourceView(Buffer, &SRVDesc, OutSRV);
		}
		if (FAILED(hr))
		{
			UE_LOG(LogD3D11RHI, Error, TEXT("Failed to create shader resource view for buffer: ByteWidth=%d NumElements=%d Format=%s"), BufferDesc.ByteWidth, BufferDesc.ByteWidth / Stride, GPixelFormats[Format].Name);
			VerifyD3D11Result(hr, "Direct3DDevice->CreateShaderResourceView", __FILE__, __LINE__, Direct3DDevice);
		}
	}
}

FShaderResourceViewRHIRef FD3D11DynamicRHI::RHICreateShaderResourceView(FRHIVertexBuffer* VertexBufferRHI, uint32 Stride, uint8 Format)
{
	FD3D11VertexBuffer* VertexBuffer = ResourceCast(VertexBufferRHI);
	if (!VertexBufferRHI || !VertexBuffer->Resource)
	{
		return new FD3D11ShaderResourceView(nullptr, nullptr);
	}

	TRefCountPtr<ID3D11ShaderResourceView> ShaderResourceView;
	CreateD3D11ShaderResourceViewOnBuffer(Direct3DDevice, VertexBuffer->Resource, Stride, Format, ShaderResourceView.GetInitReference());

	return new FD3D11ShaderResourceView(ShaderResourceView,VertexBuffer);
}

FShaderResourceViewRHIRef FD3D11DynamicRHI::CreateShaderResourceView_RenderThread(
	class FRHICommandListImmediate& RHICmdList,
	FRHIVertexBuffer* VertexBuffer,
	uint32 Stride,
	uint8 Format)
{
	return RHICreateShaderResourceView(VertexBuffer, Stride, Format);
}

void FD3D11DynamicRHI::RHIUpdateShaderResourceView(FRHIShaderResourceView* SRV, FRHIVertexBuffer* VertexBufferRHI, uint32 Stride, uint8 Format)
{
	check(SRV);
	FD3D11ShaderResourceView* SRVD3D11 = ResourceCast(SRV);
	if (!VertexBufferRHI)
	{
		SRVD3D11->Rename(nullptr, nullptr);
	}
	else
	{
		FD3D11VertexBuffer* VertexBuffer = ResourceCast(VertexBufferRHI);
		check(VertexBuffer->Resource);

		TRefCountPtr<ID3D11ShaderResourceView> ShaderResourceView;
		CreateD3D11ShaderResourceViewOnBuffer(Direct3DDevice, VertexBuffer->Resource, Stride, Format, ShaderResourceView.GetInitReference());

		SRVD3D11->Rename(ShaderResourceView, VertexBuffer);
	}
}

void FD3D11DynamicRHI::RHIUpdateShaderResourceView(FRHIShaderResourceView* SRV, FRHIIndexBuffer* IndexBufferRHI)
{
	check(SRV);
	FD3D11ShaderResourceView* SRVD3D11 = ResourceCast(SRV);
	if (!IndexBufferRHI)
	{
		SRVD3D11->Rename(nullptr, nullptr);
	}
	else
	{
		FD3D11IndexBuffer* IndexBuffer = ResourceCast(IndexBufferRHI);
		check(IndexBuffer->Resource);

		const uint32 Stride = IndexBuffer->GetStride();
		const EPixelFormat Format = Stride == 2 ? PF_R16_UINT : PF_R32_UINT;
		TRefCountPtr<ID3D11ShaderResourceView> ShaderResourceView;
		CreateD3D11ShaderResourceViewOnBuffer(Direct3DDevice, IndexBuffer->Resource, Stride, Format, ShaderResourceView.GetInitReference());

		SRVD3D11->Rename(ShaderResourceView, IndexBuffer);
	}
}

FShaderResourceViewRHIRef FD3D11DynamicRHI::RHICreateShaderResourceView(FRHIIndexBuffer* BufferRHI)
{
	if (!BufferRHI)
	{
		return new FD3D11ShaderResourceView(nullptr, nullptr);
	}
	FD3D11IndexBuffer* Buffer = ResourceCast(BufferRHI);
	check(Buffer);
	check(Buffer->Resource);

	// The stride in bytes of the index buffer; must be 2 or 4
	const uint32 Stride = BufferRHI->GetStride();
	check(Stride == 2 || Stride == 4);
	const EPixelFormat Format = (Stride == 2) ? PF_R16_UINT : PF_R32_UINT;
	TRefCountPtr<ID3D11ShaderResourceView> ShaderResourceView;
	CreateD3D11ShaderResourceViewOnBuffer(Direct3DDevice, Buffer->Resource, Stride, Format, ShaderResourceView.GetInitReference());

	return new FD3D11ShaderResourceView(ShaderResourceView, Buffer);
}

FShaderResourceViewRHIRef FD3D11DynamicRHI::CreateShaderResourceView_RenderThread(
	class FRHICommandListImmediate& RHICmdList,
	FRHIIndexBuffer* Buffer)
{
	return RHICreateShaderResourceView(Buffer);
}

void FD3D11DynamicRHI::ClearUAV(TRHICommandList_RecursiveHazardous<FD3D11DynamicRHI>& RHICmdList, FD3D11UnorderedAccessView* UnorderedAccessView, const void* ClearValues, bool bFloat)
{
	D3D11_UNORDERED_ACCESS_VIEW_DESC UAVDesc;
	UnorderedAccessView->View->GetDesc(&UAVDesc);

	// Only structured buffers can have an unknown format
	check(UAVDesc.ViewDimension == D3D11_UAV_DIMENSION_BUFFER || UAVDesc.Format != DXGI_FORMAT_UNKNOWN);

	EClearReplacementValueType ValueType = EClearReplacementValueType::Float;
	switch (UAVDesc.Format)
	{
	case DXGI_FORMAT_R32G32B32A32_SINT:
	case DXGI_FORMAT_R32G32B32_SINT:
	case DXGI_FORMAT_R16G16B16A16_SINT:
	case DXGI_FORMAT_R32G32_SINT:
	case DXGI_FORMAT_R8G8B8A8_SINT:
	case DXGI_FORMAT_R16G16_SINT:
	case DXGI_FORMAT_R32_SINT:
	case DXGI_FORMAT_R8G8_SINT:
	case DXGI_FORMAT_R16_SINT:
	case DXGI_FORMAT_R8_SINT:
		ValueType = EClearReplacementValueType::Int32;
		break;

	case DXGI_FORMAT_R32G32B32A32_UINT:
	case DXGI_FORMAT_R32G32B32_UINT:
	case DXGI_FORMAT_R16G16B16A16_UINT:
	case DXGI_FORMAT_R32G32_UINT:
	case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
	case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
	case DXGI_FORMAT_R10G10B10A2_UINT:
	case DXGI_FORMAT_R8G8B8A8_UINT:
	case DXGI_FORMAT_R16G16_UINT:
	case DXGI_FORMAT_R32_UINT:
	case DXGI_FORMAT_D24_UNORM_S8_UINT:
	case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
	case DXGI_FORMAT_R8G8_UINT:
	case DXGI_FORMAT_R16_UINT:
	case DXGI_FORMAT_R8_UINT:
		ValueType = EClearReplacementValueType::Uint32;
		break;
	}

	ensureMsgf((UAVDesc.Format == DXGI_FORMAT_UNKNOWN) || (bFloat == (ValueType == EClearReplacementValueType::Float)), TEXT("Attempt to clear a UAV using the wrong RHIClearUAV function. Float vs Integer mismatch."));

	if (UAVDesc.ViewDimension == D3D11_UAV_DIMENSION_BUFFER)
	{
		if (UAVDesc.Format == DXGI_FORMAT_UNKNOWN)
		{
			// Structured buffer. Use the clear function on the immediate context, since we can't use a general purpose shader for these.
			RHICmdList.RunOnContext([UnorderedAccessView, ClearValues](auto& Context)
			{
				Context.Direct3DDeviceIMContext->ClearUnorderedAccessViewUint(UnorderedAccessView->View, *reinterpret_cast<const UINT(*)[4]>(ClearValues));
				Context.GPUProfilingData.RegisterGPUWork(1);
			});
		}
		else
		{
			ClearUAVShader_T<EClearReplacementResourceType::Buffer, 4, false>(RHICmdList, UnorderedAccessView, UAVDesc.Buffer.NumElements, 1, 1, ClearValues, ValueType);
		}
	}
	else
	{
		if (UAVDesc.ViewDimension == D3D11_UAV_DIMENSION_TEXTURE2D)
		{
			FD3D11Texture2D* Texture2D = static_cast<FD3D11Texture2D*>(UnorderedAccessView->Resource.GetReference());
			FIntVector Size = Texture2D->GetSizeXYZ();

			uint32 Width  = Size.X >> UAVDesc.Texture2D.MipSlice;
			uint32 Height = Size.Y >> UAVDesc.Texture2D.MipSlice;
			ClearUAVShader_T<EClearReplacementResourceType::Texture2D, 4, false>(RHICmdList, UnorderedAccessView, Width, Height, 1, ClearValues, ValueType);
		}
		else if (UAVDesc.ViewDimension == D3D11_UAV_DIMENSION_TEXTURE2DARRAY)
		{
			FD3D11Texture2DArray* Texture2DArray = static_cast<FD3D11Texture2DArray*>(UnorderedAccessView->Resource.GetReference());
			FIntVector Size = Texture2DArray->GetSizeXYZ();

			uint32 Width = Size.X >> UAVDesc.Texture2DArray.MipSlice;
			uint32 Height = Size.Y >> UAVDesc.Texture2DArray.MipSlice;
			ClearUAVShader_T<EClearReplacementResourceType::Texture2DArray, 4, false>(RHICmdList, UnorderedAccessView, Width, Height, UAVDesc.Texture2DArray.ArraySize, ClearValues, ValueType);
		}
		else if (UAVDesc.ViewDimension == D3D11_UAV_DIMENSION_TEXTURE3D)
		{
			FD3D11Texture3D* Texture3D = static_cast<FD3D11Texture3D*>(UnorderedAccessView->Resource.GetReference());
			FIntVector Size = Texture3D->GetSizeXYZ();

			// @todo - is WSize / mip index handling here correct?
			uint32 Width = Size.X >> UAVDesc.Texture2DArray.MipSlice;
			uint32 Height = Size.Y >> UAVDesc.Texture2DArray.MipSlice;
			ClearUAVShader_T<EClearReplacementResourceType::Texture3D, 4, false>(RHICmdList, UnorderedAccessView, Width, Height, UAVDesc.Texture3D.WSize, ClearValues, ValueType);
		}
		else
		{
			ensure(0);
		}
	}
}

void FD3D11DynamicRHI::RHIClearUAVFloat(FRHIUnorderedAccessView* UnorderedAccessViewRHI, const FVector4& Values)
{
	TRHICommandList_RecursiveHazardous<FD3D11DynamicRHI> RHICmdList(this);
	ClearUAV(RHICmdList, ResourceCast(UnorderedAccessViewRHI), &Values, true);
}

void FD3D11DynamicRHI::RHIClearUAVUint(FRHIUnorderedAccessView* UnorderedAccessViewRHI, const FUintVector4& Values)
{
	TRHICommandList_RecursiveHazardous<FD3D11DynamicRHI> RHICmdList(this);
	ClearUAV(RHICmdList, ResourceCast(UnorderedAccessViewRHI), &Values, false);
}

void FD3D11DynamicRHI::RHIBindDebugLabelName(FRHIUnorderedAccessView* UnorderedAccessViewRHI, const TCHAR* Name)
{
#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
	FD3D11UnorderedAccessView* UAV = ResourceCast(UnorderedAccessViewRHI);
	UAV->View->SetPrivateData(WKPDID_D3DDebugObjectName, FCString::Strlen(Name) + 1, TCHAR_TO_ANSI(Name));
#endif
}

FD3D11StagingBuffer::~FD3D11StagingBuffer()
{
	if (StagedRead)
	{
		StagedRead.SafeRelease();
	}
}

void* FD3D11StagingBuffer::Lock(uint32 Offset, uint32 NumBytes)
{
	check(!bIsLocked);
	bIsLocked = true;
	if (StagedRead)
	{
		// Map the staging buffer's memory for reading.
		D3D11_MAPPED_SUBRESOURCE MappedSubresource;
		VERIFYD3D11RESULT(Context->Map(StagedRead, 0, D3D11_MAP_READ, 0, &MappedSubresource));

		return (void*)((uint8*)MappedSubresource.pData + Offset);
	}
	else
	{
		return nullptr;
	}
}

void FD3D11StagingBuffer::Unlock()
{
	check(bIsLocked);
	bIsLocked = false;
	if (StagedRead)
	{
		Context->Unmap(StagedRead, 0);
	}
}
