// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	D3D12View.cpp: 
=============================================================================*/

#include "D3D12RHIPrivate.h"

static FORCEINLINE D3D12_SHADER_RESOURCE_VIEW_DESC GetVertexBufferSRVDesc(FD3D12Buffer* VertexBuffer, uint32& CreationStride, uint8 Format, uint32 StartOffsetBytes, uint32 NumElements)
{
	const uint32 BufferSize = VertexBuffer->GetSize();
	const uint64 BufferOffset = VertexBuffer->ResourceLocation.GetOffsetFromBaseOfResource();

	const uint32 FormatStride = GPixelFormats[Format].BlockBytes;

	const uint32 NumRequestedBytes = NumElements * FormatStride;
	const uint32 OffsetBytes = FMath::Min(StartOffsetBytes, BufferSize);
	const uint32 NumBytes = FMath::Min(NumRequestedBytes, BufferSize - OffsetBytes);

	D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc{};

	SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;

	if (VertexBuffer->GetUsage() & BUF_ByteAddressBuffer)
	{
		SRVDesc.Format = DXGI_FORMAT_R32_TYPELESS;
		SRVDesc.Buffer.Flags |= D3D12_BUFFER_SRV_FLAG_RAW;
		CreationStride = 4;
	}
	else
	{
		SRVDesc.Format = FindShaderResourceDXGIFormat((DXGI_FORMAT)GPixelFormats[Format].PlatformFormat, false);
		CreationStride = GPixelFormats[Format].BlockBytes;
	}

	SRVDesc.Buffer.FirstElement = (BufferOffset + OffsetBytes) / CreationStride;
	SRVDesc.Buffer.NumElements = NumBytes / CreationStride;

	return SRVDesc;
}

static FORCEINLINE D3D12_SHADER_RESOURCE_VIEW_DESC GetIndexBufferSRVDesc(FD3D12Buffer* IndexBuffer, uint32 StartOffsetBytes, uint32 NumElements)
{
	const uint32 Usage = IndexBuffer->GetUsage();
	const uint32 Width = IndexBuffer->GetSize();
	const uint32 CreationStride = IndexBuffer->GetStride();
	uint32 MaxElements = Width / CreationStride;
	StartOffsetBytes = FMath::Min(StartOffsetBytes, Width);
	uint32 StartElement = StartOffsetBytes / CreationStride;
	const FD3D12ResourceLocation& Location = IndexBuffer->ResourceLocation;

	D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc{};
	SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	if (Usage & BUF_ByteAddressBuffer)
	{
		check(CreationStride == 4u);
		SRVDesc.Format = DXGI_FORMAT_R32_TYPELESS;
		SRVDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
	}
	else
	{
		check(CreationStride == 2u || CreationStride == 4u);
		SRVDesc.Format = CreationStride == 2u ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
	}
	SRVDesc.Buffer.NumElements = FMath::Min(MaxElements - StartElement, NumElements);

	if (Location.GetResource())
	{
		// Create a Shader Resource View
		SRVDesc.Buffer.FirstElement = Location.GetOffsetFromBaseOfResource() / CreationStride + StartElement;
	}
	else
	{
		// Null underlying D3D12 resource should only be the case for dynamic resources
		check(Usage & BUF_AnyDynamic);
	}
	return SRVDesc;
}

template<typename TextureType>
FD3D12ShaderResourceView* CreateSRV(TextureType* Texture, D3D12_SHADER_RESOURCE_VIEW_DESC& Desc)
{
	if (Texture == nullptr)
	{
		return nullptr;
	}

	FD3D12Adapter* Adapter = Texture->GetParentDevice()->GetParentAdapter();

	return Adapter->CreateLinkedViews<TextureType, FD3D12ShaderResourceView>(Texture, [&Desc](TextureType* Texture)
	{
		return new FD3D12ShaderResourceView(Texture->GetParentDevice(), Desc, Texture->ResourceLocation);
	});
}

FShaderResourceViewRHIRef FD3D12DynamicRHI::RHICreateShaderResourceView(FRHITexture* Texture, const FRHITextureSRVCreateInfo& CreateInfo)
{
	D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	DXGI_FORMAT BaseTextureFormat = DXGI_FORMAT_UNKNOWN;
	FD3D12TextureBase* BaseTexture = nullptr;
	if (FD3D12Texture3D* Texture3D = FD3D12DynamicRHI::ResourceCast(Texture->GetTexture3D()))
	{
		const D3D12_RESOURCE_DESC& TextureDesc = Texture3D->GetResource()->GetDesc();
		BaseTextureFormat = TextureDesc.Format;
		BaseTexture = Texture3D;
		SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
		SRVDesc.Texture3D.MipLevels = CreateInfo.NumMipLevels;
		SRVDesc.Texture3D.MostDetailedMip = CreateInfo.MipLevel;
	}
	else if (FD3D12Texture2DArray* Texture2DArray = FD3D12DynamicRHI::ResourceCast(Texture->GetTexture2DArray()))
	{
		const D3D12_RESOURCE_DESC& TextureDesc = Texture2DArray->GetResource()->GetDesc();
		BaseTextureFormat = TextureDesc.Format;
		BaseTexture = Texture2DArray;
		SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
		SRVDesc.Texture2DArray.ArraySize = (CreateInfo.NumArraySlices == 0 ? TextureDesc.DepthOrArraySize : CreateInfo.NumArraySlices);
		SRVDesc.Texture2DArray.FirstArraySlice = CreateInfo.FirstArraySlice;
		SRVDesc.Texture2DArray.MipLevels = CreateInfo.NumMipLevels;
		SRVDesc.Texture2DArray.MostDetailedMip = CreateInfo.MipLevel;
	}
	else if (FD3D12TextureCube* TextureCube = FD3D12DynamicRHI::ResourceCast(Texture->GetTextureCube()))
	{
		const D3D12_RESOURCE_DESC& TextureDesc = TextureCube->GetResource()->GetDesc();
		BaseTextureFormat = TextureDesc.Format;
		BaseTexture = TextureCube;
		SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		SRVDesc.TextureCube.MipLevels = CreateInfo.NumMipLevels;
		SRVDesc.TextureCube.MostDetailedMip = CreateInfo.MipLevel;
	}
	else
	{
		FD3D12Texture2D* Texture2D = FD3D12DynamicRHI::ResourceCast(Texture->GetTexture2D());
		const D3D12_RESOURCE_DESC& TextureDesc = Texture2D->GetResource()->GetDesc();
		BaseTextureFormat = TextureDesc.Format;
		BaseTexture = Texture2D;

		if (TextureDesc.SampleDesc.Count > 1)
		{
			// MS textures can't have mips apparently, so nothing else to set.
			SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
		}
		else
		{
			SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			SRVDesc.Texture2D.MipLevels = CreateInfo.NumMipLevels;
			SRVDesc.Texture2D.MostDetailedMip = CreateInfo.MipLevel;
		}
	}

	// Allow input CreateInfo to override SRGB and/or format
	const bool bBaseSRGB = (Texture->GetFlags() & TexCreate_SRGB) != 0;
	const bool bSRGB = CreateInfo.SRGBOverride != SRGBO_ForceDisable && bBaseSRGB;
	const DXGI_FORMAT ViewTextureFormat = (CreateInfo.Format == PF_Unknown) ? BaseTextureFormat : (DXGI_FORMAT)GPixelFormats[CreateInfo.Format].PlatformFormat;
	SRVDesc.Format = FindShaderResourceDXGIFormat(ViewTextureFormat, bSRGB);

	switch (SRVDesc.ViewDimension)
	{
	case D3D12_SRV_DIMENSION_TEXTURE2D: SRVDesc.Texture2D.PlaneSlice = GetPlaneSliceFromViewFormat(BaseTextureFormat, SRVDesc.Format); break;
	case D3D12_SRV_DIMENSION_TEXTURE2DARRAY: SRVDesc.Texture2DArray.PlaneSlice = GetPlaneSliceFromViewFormat(BaseTextureFormat, SRVDesc.Format); break;
	default: break; // other view types don't support PlaneSlice
	}

	check(BaseTexture);
	return CreateSRV(BaseTexture, SRVDesc);
}

FShaderResourceViewRHIRef FD3D12DynamicRHI::RHICreateShaderResourceView(FRHIStructuredBuffer* StructuredBufferRHI)
{
	return FD3D12DynamicRHI::RHICreateShaderResourceView(FShaderResourceViewInitializer(StructuredBufferRHI));
}

FShaderResourceViewRHIRef FD3D12DynamicRHI::RHICreateShaderResourceView(FRHIVertexBuffer* VertexBufferRHI, uint32 Stride, uint8 Format)
{
	ensureMsgf(Stride == GPixelFormats[Format].BlockBytes, TEXT("provided stride: %i was not consitent with Pixelformat: %s"), Stride, GPixelFormats[Format].Name);
	return FD3D12DynamicRHI::RHICreateShaderResourceView(FShaderResourceViewInitializer(VertexBufferRHI, EPixelFormat(Format)));
}

uint64 FD3D12DynamicRHI::RHIGetMinimumAlignmentForBufferBackedSRV(EPixelFormat Format)
{
	return GPixelFormats[Format].BlockBytes;
}

FShaderResourceViewRHIRef FD3D12DynamicRHI::RHICreateShaderResourceView(const FShaderResourceViewInitializer& Initializer)
{
	FShaderResourceViewInitializer::FBufferShaderResourceViewInitializer Desc = Initializer.AsBufferSRV();

	if (!Desc.Buffer)
	{
		return GetAdapter().CreateLinkedObject<FD3D12ShaderResourceView>(FRHIGPUMask::All(), [](FD3D12Device* Device)
			{
				return new FD3D12ShaderResourceView(nullptr);
			});
	}

	FD3D12Buffer* Buffer = FD3D12DynamicRHI::ResourceCast(Desc.Buffer);

	switch (Initializer.GetType())
	{
		case FShaderResourceViewInitializer::EType::VertexBufferSRV:
		{
			struct FD3D12InitializeVertexBufferSRVRHICommand final : public FRHICommand<FD3D12InitializeVertexBufferSRVRHICommand>
			{
				FD3D12Buffer* VertexBuffer;
				FD3D12ShaderResourceView* SRV;

				uint32 StartOffsetBytes;
				uint32 NumElements;
				uint8 Format;

				FD3D12InitializeVertexBufferSRVRHICommand(FD3D12Buffer* InVertexBuffer, FD3D12ShaderResourceView* InSRV, uint32 InStartOffsetBytes, uint32 InNumElements, uint8 InFormat)
					: VertexBuffer(InVertexBuffer)
					, SRV(InSRV)
					, StartOffsetBytes(InStartOffsetBytes)
					, NumElements(InNumElements)
					, Format(InFormat)
				{}

				void Execute(FRHICommandListBase& RHICmdList)
				{
					uint32 CreationStride = GPixelFormats[Format].BlockBytes;
					D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = GetVertexBufferSRVDesc(VertexBuffer, CreationStride, Format, StartOffsetBytes, NumElements);

					// Create a Shader Resource View
					SRV->Initialize(SRVDesc, VertexBuffer->ResourceLocation, CreationStride, StartOffsetBytes);
					VertexBuffer->AddDynamicSRV(SRV);
				}
			};

			return GetAdapter().CreateLinkedViews<FD3D12Buffer, FD3D12ShaderResourceView>(Buffer,
				[Desc](FD3D12Buffer* Buffer)
				{
					check(Buffer);

					FD3D12ShaderResourceView* ShaderResourceView = new FD3D12ShaderResourceView(Buffer->GetParentDevice());
					
					uint32 Stride = GPixelFormats[Desc.Format].BlockBytes;
					FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
					if (ShouldDeferBufferLockOperation(&RHICmdList) && (Buffer->GetUsage() & BUF_AnyDynamic))
					{
						// We have to defer the SRV initialization to the RHI thread if the buffer is dynamic (and RHI threading is enabled), as dynamic buffers can be renamed.
						// Also insert an RHI thread fence to prevent parallel translate tasks running until this command has completed.
						ALLOC_COMMAND_CL(RHICmdList, FD3D12InitializeVertexBufferSRVRHICommand)(Buffer, ShaderResourceView, Desc.StartOffsetBytes, Desc.NumElements, Desc.Format);
						RHICmdList.RHIThreadFence(true);
					}
					else
					{
						// Run the command directly if we're bypassing RHI command list recording, or the buffer is not dynamic.
						FD3D12InitializeVertexBufferSRVRHICommand Command(Buffer, ShaderResourceView, Desc.StartOffsetBytes, Desc.NumElements, Desc.Format);
						Command.Execute(RHICmdList);
					}

					return ShaderResourceView;
				});
		}

		case FShaderResourceViewInitializer::EType::StructuredBufferSRV:
		{
			struct FD3D12InitializeStructuredBufferSRVRHICommand final : public FRHICommand<FD3D12InitializeStructuredBufferSRVRHICommand>
			{
				FD3D12Buffer* StructuredBuffer;
				FD3D12ShaderResourceView* SRV;
				uint32 StartOffsetBytes;
				uint32 NumElements;

				FD3D12InitializeStructuredBufferSRVRHICommand(FD3D12Buffer* InStructuredBuffer, FD3D12ShaderResourceView* InSRV, uint32 InStartOffsetBytes, uint32 InNumElements)
					: StructuredBuffer(InStructuredBuffer)
					, SRV(InSRV)
					, StartOffsetBytes(InStartOffsetBytes)
					, NumElements(InNumElements)
				{}

				void Execute(FRHICommandListBase& RHICmdList)
				{
					FD3D12ResourceLocation& Location = StructuredBuffer->ResourceLocation;

					const uint64 Offset = Location.GetOffsetFromBaseOfResource();
					const D3D12_RESOURCE_DESC& BufferDesc = Location.GetResource()->GetDesc();

					const uint32 BufferUsage = StructuredBuffer->GetUsage();
					const bool bByteAccessBuffer = (BufferUsage & BUF_ByteAddressBuffer) != 0;
					// Create a Shader Resource View
					D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
					SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

					// BufferDesc.StructureByteStride  is not getting patched through the D3D resource DESC structs, so use the RHI version as a hack
					uint32 Stride = StructuredBuffer->GetStride();
					uint32 MaxElements = Location.GetSize() / Stride;
					StartOffsetBytes = FMath::Min<uint32>(StartOffsetBytes, Location.GetSize());
					uint32 StartElement = StartOffsetBytes / Stride;

					if (bByteAccessBuffer)
					{
						SRVDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
						SRVDesc.Format = DXGI_FORMAT_R32_TYPELESS;
						Stride = 4;
					}
					else
					{
						SRVDesc.Buffer.StructureByteStride = Stride;
						SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
					}

					SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
					SRVDesc.Buffer.NumElements = FMath::Min<uint32>(MaxElements - StartElement, NumElements);
					SRVDesc.Buffer.FirstElement = Offset / Stride + StartElement;

					// Create a Shader Resource View
					SRV->Initialize(SRVDesc, StructuredBuffer->ResourceLocation, Stride, StartOffsetBytes);
					StructuredBuffer->AddDynamicSRV(SRV);
				}
			};

			return GetAdapter().CreateLinkedViews<FD3D12Buffer,
				FD3D12ShaderResourceView>(Buffer, [Desc](FD3D12Buffer* Buffer)
					{
						check(Buffer);

						FD3D12ShaderResourceView* ShaderResourceView = new FD3D12ShaderResourceView(Buffer->GetParentDevice());

						FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
						if (ShouldDeferBufferLockOperation(&RHICmdList) && (Buffer->GetUsage() & BUF_AnyDynamic))
						{
							// We have to defer the SRV initialization to the RHI thread if the buffer is dynamic (and RHI threading is enabled), as dynamic buffers can be renamed.
							// Also insert an RHI thread fence to prevent parallel translate tasks running until this command has completed.
							ALLOC_COMMAND_CL(RHICmdList, FD3D12InitializeStructuredBufferSRVRHICommand)(Buffer, ShaderResourceView, Desc.StartOffsetBytes, Desc.NumElements);
							RHICmdList.RHIThreadFence(true);
						}
						else
						{
							// Run the command directly if we're bypassing RHI command list recording, or the buffer is not dynamic.
							FD3D12InitializeStructuredBufferSRVRHICommand Command(Buffer, ShaderResourceView, Desc.StartOffsetBytes, Desc.NumElements);
							Command.Execute(RHICmdList);
						}

						return ShaderResourceView;
					});
		}

		case FShaderResourceViewInitializer::EType::IndexBufferSRV:
		{
			return GetAdapter().CreateLinkedViews<FD3D12Buffer, FD3D12ShaderResourceView>(Buffer,
			[Desc](FD3D12Buffer* Buffer)
			{
				check(Buffer);

				FD3D12ResourceLocation& Location = Buffer->ResourceLocation;
				const uint32 CreationStride = Buffer->GetStride();
				D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = GetIndexBufferSRVDesc(Buffer, Desc.StartOffsetBytes, Desc.NumElements);

				FD3D12ShaderResourceView* ShaderResourceView = new FD3D12ShaderResourceView(Buffer->GetParentDevice(), SRVDesc, Location, CreationStride);
				return ShaderResourceView;
			});
		}
	}

	checkNoEntry();
	return nullptr;
}

void FD3D12DynamicRHI::RHIUpdateShaderResourceView(FRHIShaderResourceView* SRV, FRHIBuffer* BufferRHI, uint32 Stride, uint8 Format)
{
	check(SRV);
	if (BufferRHI)
	{
		FD3D12Buffer* Buffer = ResourceCast(BufferRHI);
		FD3D12ShaderResourceView* SRVD3D12 = ResourceCast(SRV);
		D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = GetVertexBufferSRVDesc(Buffer, Stride, Format, 0, UINT32_MAX);

		// Rename the SRV to view on the new vertex buffer
		for (auto It = MakeDualLinkedObjectIterator(Buffer, SRVD3D12); It; ++It)
		{
			Buffer = It.GetFirst();
			SRVD3D12 = It.GetSecond();

			FD3D12Device* ParentDevice = Buffer->GetParentDevice();
			SRVD3D12->Initialize(ParentDevice, SRVDesc, Buffer->ResourceLocation, Stride);
			Buffer->AddDynamicSRV(SRVD3D12);
		}
	}
}

void FD3D12DynamicRHI::RHIUpdateShaderResourceView(FRHIShaderResourceView* SRV, FRHIBuffer* BufferRHI)
{
	check(SRV);
	if (BufferRHI)
	{
		FD3D12Buffer* Buffer = ResourceCast(BufferRHI);
		FD3D12ShaderResourceView* SRVD3D12 = ResourceCast(SRV);
		D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = GetIndexBufferSRVDesc(Buffer, 0, UINT32_MAX);
		const uint32 Stride = Buffer->GetStride();

		// Rename the SRV to view on the new index buffer
		for (auto It = MakeDualLinkedObjectIterator(Buffer, SRVD3D12); It; ++It)
		{
			Buffer = It.GetFirst();
			SRVD3D12 = It.GetSecond();

			FD3D12Device* ParentDevice = Buffer->GetParentDevice();
			SRVD3D12->Initialize(ParentDevice, SRVDesc, Buffer->ResourceLocation, Stride);
		}
	}
}

FShaderResourceViewRHIRef FD3D12DynamicRHI::RHICreateShaderResourceView_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture* Texture, const FRHITextureSRVCreateInfo& CreateInfo)
{
	return RHICreateShaderResourceView(Texture, CreateInfo);
}

FShaderResourceViewRHIRef FD3D12DynamicRHI::RHICreateShaderResourceView_RenderThread(FRHICommandListImmediate& RHICmdList, FRHIBuffer* BufferRHI, uint32 Stride, uint8 Format)
{
	return RHICreateShaderResourceView(BufferRHI, Stride, Format);
}

FShaderResourceViewRHIRef FD3D12DynamicRHI::RHICreateShaderResourceView_RenderThread(FRHICommandListImmediate& RHICmdList, const FShaderResourceViewInitializer& Initializer)
{
	return RHICreateShaderResourceView(Initializer);
}

FShaderResourceViewRHIRef FD3D12DynamicRHI::CreateShaderResourceView_RenderThread(FRHICommandListImmediate& RHICmdList, FRHIBuffer* BufferRHI, uint32 Stride, uint8 Format)
{
	return RHICreateShaderResourceView_RenderThread(RHICmdList, BufferRHI, Stride, Format);
}

FShaderResourceViewRHIRef FD3D12DynamicRHI::CreateShaderResourceView_RenderThread(FRHICommandListImmediate& RHICmdList, FRHIIndexBuffer* Buffer)
{
	return RHICreateShaderResourceView(Buffer);
}

FShaderResourceViewRHIRef FD3D12DynamicRHI::CreateShaderResourceView_RenderThread(FRHICommandListImmediate& RHICmdList, const FShaderResourceViewInitializer& Initializer)
{
	return RHICreateShaderResourceView_RenderThread(RHICmdList, Initializer);
}

FShaderResourceViewRHIRef FD3D12DynamicRHI::RHICreateShaderResourceView_RenderThread(FRHICommandListImmediate& RHICmdList, FRHIBuffer* BufferRHI)
{
	return RHICreateShaderResourceView(BufferRHI);
}

FShaderResourceViewRHIRef FD3D12DynamicRHI::RHICreateShaderResourceViewWriteMask_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture2D)
{
	return RHICreateShaderResourceViewWriteMask(Texture2D);
}

#if USE_STATIC_ROOT_SIGNATURE
void FD3D12ConstantBufferView::AllocateHeapSlot()
{
	if (!OfflineDescriptorHandle.ptr)
	{
	    FD3D12OfflineDescriptorManager& DescriptorAllocator = GetParentDevice()->GetViewDescriptorAllocator<D3D12_CONSTANT_BUFFER_VIEW_DESC>();
	    OfflineDescriptorHandle = DescriptorAllocator.AllocateHeapSlot(OfflineHeapIndex);
	    check(OfflineDescriptorHandle.ptr != 0);
	}
}

void FD3D12ConstantBufferView::FreeHeapSlot()
{
	if (OfflineDescriptorHandle.ptr)
	{
		FD3D12OfflineDescriptorManager& DescriptorAllocator = GetParentDevice()->GetViewDescriptorAllocator<D3D12_CONSTANT_BUFFER_VIEW_DESC>();
		DescriptorAllocator.FreeHeapSlot(OfflineDescriptorHandle, OfflineHeapIndex);
		OfflineDescriptorHandle.ptr = 0;
	}
}

void FD3D12ConstantBufferView::Create(D3D12_GPU_VIRTUAL_ADDRESS GPUAddress, const uint32 AlignedSize)
{
	Desc.BufferLocation = GPUAddress;
	Desc.SizeInBytes = AlignedSize;
	GetParentDevice()->GetDevice()->CreateConstantBufferView(&Desc, OfflineDescriptorHandle);
}
#endif
