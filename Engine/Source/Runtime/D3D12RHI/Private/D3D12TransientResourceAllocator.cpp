// Copyright Epic Games, Inc. All Rights Reserved.

#include "D3D12RHIPrivate.h"
#include "D3D12TransientResourceAllocator.h"

D3D12_RESOURCE_STATES GetInitialResourceState(const D3D12_RESOURCE_DESC& InDesc)
{
	// Validate the creation state
	D3D12_RESOURCE_STATES State = D3D12_RESOURCE_STATE_COMMON;
	if (EnumHasAnyFlags(InDesc.Flags, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET))
	{
		State = D3D12_RESOURCE_STATE_RENDER_TARGET;
	}
	else if (EnumHasAnyFlags(InDesc.Flags, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))
	{
		State = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	}
	else if (EnumHasAnyFlags(InDesc.Flags, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS))
	{
		State = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	}
	check(State != D3D12_RESOURCE_STATE_COMMON);
	return State;
}

FD3D12TransientHeap::FD3D12TransientHeap(const FRHITransientHeapInitializer& Initializer, FD3D12Adapter* Adapter, FD3D12Device* Device, FRHIGPUMask VisibleNodeMask)
	: FRHITransientHeap(Initializer)
{
	D3D12_HEAP_FLAGS HeapFlags = D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES;

	if (Initializer.Flags != ERHITransientHeapFlags::AllowAll)
	{
		switch (Initializer.Flags)
		{
		case ERHITransientHeapFlags::AllowBuffers:
			HeapFlags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
			break;

		case ERHITransientHeapFlags::AllowTextures:
			HeapFlags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
			break;

		case ERHITransientHeapFlags::AllowRenderTargets:
			HeapFlags = D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
			break;
		}
	}

	D3D12_HEAP_PROPERTIES HeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	HeapProperties.CreationNodeMask = FRHIGPUMask::FromIndex(Device->GetGPUIndex()).GetNative();
	HeapProperties.VisibleNodeMask = VisibleNodeMask.GetNative();

	D3D12_HEAP_DESC Desc = {};
	Desc.SizeInBytes = Initializer.Size;
	Desc.Properties = HeapProperties;
	Desc.Alignment = 0;
	Desc.Flags = HeapFlags;

	if (Adapter->IsHeapNotZeroedSupported())
	{
		Desc.Flags |= FD3D12_HEAP_FLAG_CREATE_NOT_ZEROED;
	}

	ID3D12Heap* D3DHeap = nullptr;
	{
		ID3D12Device* D3DDevice = Device->GetDevice();

		LLM_PLATFORM_SCOPE(ELLMTag::GraphicsPlatform);

		// We are tracking allocations ourselves, so don't let XMemAlloc track these as well
		LLM_SCOPED_PAUSE_TRACKING_FOR_TRACKER(ELLMTracker::Default, ELLMAllocType::System);
		VERIFYD3D12RESULT(D3DDevice->CreateHeap(&Desc, IID_PPV_ARGS(&D3DHeap)));

#if PLATFORM_WINDOWS
		// Boost priority to make sure it's not paged out
		TRefCountPtr<ID3D12Device5> D3DDevice5;
		if (SUCCEEDED(D3DDevice->QueryInterface(IID_PPV_ARGS(D3DDevice5.GetInitReference()))))
		{
			ID3D12Pageable* Pageable = D3DHeap;
			D3D12_RESIDENCY_PRIORITY HeapPriority = D3D12_RESIDENCY_PRIORITY_HIGH;
			D3DDevice5->SetResidencyPriority(1, &Pageable, &HeapPriority);
		}
#endif // PLATFORM_WINDOWS
	}
	SetName(D3DHeap, L"TransientResourceAllocator Backing Heap");

	Heap = new FD3D12Heap(Device, VisibleNodeMask);
	Heap->SetHeap(D3DHeap);
	Heap->BeginTrackingResidency(Desc.SizeInBytes);
}

FD3D12TransientHeap::~FD3D12TransientHeap()
{
	LLM_SCOPED_PAUSE_TRACKING_FOR_TRACKER(ELLMTracker::Default, ELLMAllocType::System);
	Heap->Destroy();
}

TUniquePtr<FD3D12TransientResourceSystem> FD3D12TransientResourceSystem::Create(FD3D12Adapter* ParentAdapter, FRHIGPUMask VisibleNodeMask)
{
	FRHITransientResourceSystemInitializer Initializer = FRHITransientResourceSystemInitializer::CreateDefault();
	Initializer.HeapAlignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;

	// Tier2 hardware is able to mix resource types onto the same heap.
	Initializer.bSupportsAllHeapFlags = ParentAdapter->GetResourceHeapTier() == D3D12_RESOURCE_HEAP_TIER_2;

	return TUniquePtr<FD3D12TransientResourceSystem>(new FD3D12TransientResourceSystem(Initializer, ParentAdapter, VisibleNodeMask));
}

FD3D12TransientResourceSystem::FD3D12TransientResourceSystem(const FRHITransientResourceSystemInitializer& Initializer, FD3D12Adapter* ParentAdapter, FRHIGPUMask InVisibleNodeMask)
	: FRHITransientResourceSystem(Initializer)
	, FD3D12AdapterChild(ParentAdapter)
	, VisibleNodeMask(InVisibleNodeMask)
{}

FRHITransientHeap* FD3D12TransientResourceSystem::CreateHeap(const FRHITransientHeapInitializer& HeapInitializer)
{
	return GetParentAdapter()->CreateLinkedObject<FD3D12TransientHeap>(VisibleNodeMask, [&](FD3D12Device* Device)
	{
		return new FD3D12TransientHeap(HeapInitializer, GetParentAdapter(), Device, VisibleNodeMask);
	});
}

FD3D12TransientResourceAllocator::FD3D12TransientResourceAllocator(FD3D12TransientResourceSystem& InParentSystem)
	: FD3D12AdapterChild(InParentSystem.GetParentAdapter())
	, Allocator(InParentSystem)
	, AllocationInfoQueryDevice(GetParentAdapter()->GetDevice(0))
{}

FRHITransientTexture* FD3D12TransientResourceAllocator::CreateTexture(const FRHITextureCreateInfo& InCreateInfo, const TCHAR* InDebugName)
{
	D3D12_RESOURCE_DESC Desc = FD3D12TextureBase::GetResourceDesc(InCreateInfo);
	D3D12_RESOURCE_ALLOCATION_INFO Info = AllocationInfoQueryDevice->GetResourceAllocationInfo(Desc);

	return Allocator.CreateTexture(InCreateInfo, InDebugName, Info.SizeInBytes, Info.Alignment,
		[&](const FRHITransientResourceAllocator::FResourceInitializer& Initializer)
	{
		ERHIAccess InitialState = ERHIAccess::UAVMask;

		if (EnumHasAnyFlags(InCreateInfo.Flags, TexCreate_RenderTargetable))
		{
			InitialState = ERHIAccess::RTV;
		}
		else if (EnumHasAnyFlags(InCreateInfo.Flags, TexCreate_DepthStencilTargetable))
		{
			InitialState = ERHIAccess::DSVWrite;
		}

		FRHIResourceCreateInfo ResourceCreateInfo(InDebugName, InCreateInfo.ClearValue);
		FResourceAllocatorAdapter ResourceAllocatorAdapter(GetParentAdapter(), static_cast<FD3D12TransientHeap&>(Initializer.Heap), Initializer.Allocation);
		FRHITexture* Texture = nullptr;

		const bool bTextureArray = InCreateInfo.ArraySize > 1;

		switch (InCreateInfo.Dimension)
		{
		case ETextureDimension::Texture2D:
		{
			const bool bCubeTexture = false;
			Texture = FD3D12DynamicRHI::GetD3DRHI()->CreateD3D12Texture2D<FD3D12BaseTexture2D>(nullptr, InCreateInfo.Extent.X, InCreateInfo.Extent.Y, 1, bTextureArray, bCubeTexture, InCreateInfo.Format, InCreateInfo.NumMips, InCreateInfo.NumSamples, InCreateInfo.Flags, InitialState, ResourceCreateInfo, &ResourceAllocatorAdapter);
		}
		break;
		case ETextureDimension::Texture3D:
		{
			Texture = FD3D12DynamicRHI::GetD3DRHI()->CreateD3D12Texture3D(nullptr, InCreateInfo.Extent.X, InCreateInfo.Extent.Y, InCreateInfo.Depth, InCreateInfo.Format, InCreateInfo.NumMips, InCreateInfo.Flags, InitialState, ResourceCreateInfo, &ResourceAllocatorAdapter);
		}
		break;
		case ETextureDimension::TextureCube:
		{
			const bool bCubeTexture = true;
			check(InCreateInfo.Extent.X == InCreateInfo.Extent.Y);
			Texture = FD3D12DynamicRHI::GetD3DRHI()->CreateD3D12Texture2D<FD3D12BaseTextureCube>(nullptr, InCreateInfo.Extent.X, InCreateInfo.Extent.Y, 6, bTextureArray, bCubeTexture, InCreateInfo.Format, InCreateInfo.NumMips, InCreateInfo.NumSamples, InCreateInfo.Flags, InitialState, ResourceCreateInfo, &ResourceAllocatorAdapter);
		}
		break;
		}

		// The D3D12_RESOURCE_DESC's are built in two different functions right now. This checks that they actually match what we expect.
#if DO_CHECK
		{
			CD3DX12_RESOURCE_DESC CreatedDesc(GetD3D12TextureFromRHITexture(Texture)->GetResource()->GetDesc());
			CD3DX12_RESOURCE_DESC DerivedDesc(Desc);
			check(CreatedDesc == DerivedDesc);
		}
#endif

		return new FRHITransientTexture(Texture, Initializer.Hash, InCreateInfo);
	});
}

void FD3D12TransientResourceAllocator::FResourceAllocatorAdapter::AllocateResource(
	uint32 GPUIndex, D3D12_HEAP_TYPE, const D3D12_RESOURCE_DESC& InDesc, uint64 InSize, uint32, ED3D12ResourceStateMode InResourceStateMode,
	D3D12_RESOURCE_STATES InCreateState, const D3D12_CLEAR_VALUE* InClearValue, const TCHAR* InName, FD3D12ResourceLocation& ResourceLocation)
{
	FD3D12Resource* NewResource = nullptr;
	VERIFYD3D12RESULT(GetParentAdapter()->CreatePlacedResource(InDesc, Heap.GetLinkedObject(GPUIndex)->Get(), Allocation.Offset, InCreateState, InResourceStateMode, D3D12_RESOURCE_STATE_TBD, InClearValue, &NewResource, InName));

	check(!ResourceLocation.IsValid());
	ResourceLocation.AsHeapAliased(NewResource);
	ResourceLocation.SetSize(InSize);
}

FRHITransientBuffer* FD3D12TransientResourceAllocator::CreateBuffer(const FRHIBufferCreateInfo& InCreateInfo, const TCHAR* InDebugName)
{
	D3D12_RESOURCE_DESC Desc;
	uint32 Alignment;
	EBufferUsageFlags BufferUsage = InCreateInfo.Usage;
	FD3D12Buffer::GetResourceDescAndAlignment(InCreateInfo.Size, InCreateInfo.Stride, BufferUsage, Desc, Alignment);

	return Allocator.CreateBuffer(InCreateInfo, InDebugName, Desc.Width, Alignment,
		[&](const FRHITransientResourceAllocator::FResourceInitializer& Initializer)
	{
		FResourceAllocatorAdapter ResourceAllocatorAdapter(GetParentAdapter(), static_cast<FD3D12TransientHeap&>(Initializer.Heap), Initializer.Allocation);
		FRHIResourceCreateInfo ResourceCreateInfo(InDebugName);

		FD3D12Buffer* Buffer = FD3D12DynamicRHI::GetD3DRHI()->CreateD3D12Buffer(nullptr, InCreateInfo.Size, BufferUsage, InCreateInfo.Stride, ERHIAccess::UAVMask, ResourceCreateInfo, &ResourceAllocatorAdapter);

		// The D3D12_RESOURCE_DESC's are built in two different functions right now. This checks that they actually match what we expect.
#if DO_CHECK
		{
			CD3DX12_RESOURCE_DESC CreatedDesc(Buffer->GetResource()->GetDesc());
			CD3DX12_RESOURCE_DESC DerivedDesc(Desc);
			check(CreatedDesc == DerivedDesc);
		}
#endif

		return new FRHITransientBuffer(Buffer, Initializer.Hash, InCreateInfo);
	});
}

void FD3D12TransientResourceAllocator::DeallocateMemory(FRHITransientTexture* InTexture)
{
	Allocator.DeallocateMemory(InTexture);
}

void FD3D12TransientResourceAllocator::DeallocateMemory(FRHITransientBuffer* InBuffer)
{
	Allocator.DeallocateMemory(InBuffer);
}

void FD3D12TransientResourceAllocator::Freeze(FRHICommandListImmediate& RHICmdList)
{
	Allocator.Freeze(RHICmdList);
}