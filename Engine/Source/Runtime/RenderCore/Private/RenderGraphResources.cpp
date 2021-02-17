// Copyright Epic Games, Inc. All Rights Reserved.

#include "RenderGraphResources.h"
#include "RenderGraphPass.h"
#include "RenderGraphPrivate.h"

#if RDG_ENABLE_DEBUG
void FRDGResource::MarkResourceAsUsed()
{
	ValidateRHIAccess();
	DebugData.bIsActuallyUsedByPass = true;
}

void FRDGUniformBuffer::MarkResourceAsUsed()
{
	FRDGResource::MarkResourceAsUsed();

	// Individual resources can't be culled from a uniform buffer, so we have to mark them all as used.
	ParameterStruct.Enumerate([](FRDGParameter Parameter)
	{
		if (FRDGResourceRef Resource = Parameter.GetAsResource())
		{
			Resource->MarkResourceAsUsed();
		}
	});
}
#endif

FRDGParentResource::FRDGParentResource(const TCHAR* InName, const ERDGParentResourceType InType)
	: FRDGResource(InName)
	, Type(InType)
	, bExternal(0)
	, bExtracted(0)
	, bTransient(0)
	, bLastOwner(1)
	// Culling logic runs only when immediate mode is off.
	, bCulled(1)
	, bUsedByAsyncComputePass(0)
{}

bool FRDGSubresourceState::IsTransitionRequired(const FRDGSubresourceState& Previous, const FRDGSubresourceState& Next)
{
	check(Next.Access != ERHIAccess::Unknown);

	if (Previous.Access != Next.Access || Previous.Pipeline != Next.Pipeline)
	{
		return true;
	}
	else if (EnumHasAnyFlags(Previous.Access, ERHIAccess::UAVMask) && EnumHasAnyFlags(Next.Access, ERHIAccess::UAVMask))
	{
		if (!GRDGOverlapUAVs)
		{
			return true;
		}

		const FRDGViewHandle PreviousUniqueHandle = Previous.NoUAVBarrierFilter.GetUniqueHandle();
		const FRDGViewHandle NextUniqueHandle = Next.NoUAVBarrierFilter.GetUniqueHandle();

		// Previous / Next have the same non-null no-barrier UAV.
		const bool bHasNoBarrierUAV = PreviousUniqueHandle == NextUniqueHandle && PreviousUniqueHandle.IsValid();

		// We require a UAV barrier unless we have a valid no-barrier UAV being used.
		return !bHasNoBarrierUAV;
	}
	else
	{
		return false;
	}
}

bool FRDGSubresourceState::IsMergeAllowed(ERDGParentResourceType ResourceType, const FRDGSubresourceState& Previous, const FRDGSubresourceState& Next)
{
	const ERHIAccess AccessUnion = Previous.Access | Next.Access;

	// If we have the same access between the two states, we don't need to check for invalid access combinations.
	if (Previous.Access != Next.Access)
	{
		// Not allowed to merge read-only and writable states.
		if (EnumHasAnyFlags(Previous.Access, ERHIAccess::ReadOnlyExclusiveMask) && EnumHasAnyFlags(Next.Access, ERHIAccess::WritableMask))
		{
			return false;
		}

		// Not allowed to merge write-only and readable states.
		if (EnumHasAnyFlags(Previous.Access, ERHIAccess::WriteOnlyExclusiveMask) && EnumHasAnyFlags(Next.Access, ERHIAccess::ReadableMask))
		{
			return false;
		}

		// Textures only allow certain read states to merge.
		if (ResourceType == ERDGParentResourceType::Texture && EnumHasAnyFlags(AccessUnion, ~GRHITextureReadAccessMask))
		{
			return false;
		}
	}

	// For merging purposes we are conservative and assume a UAV barrier.
	if (EnumHasAnyFlags(AccessUnion, ERHIAccess::UAVMask))
	{
		return false;
	}

	// We are not allowed to cross pipelines or change flags in a merge.
	if (Previous.Pipeline != Next.Pipeline || Previous.Flags != Next.Flags)
	{
		return false;
	}

	return true;
}

bool FRDGTextureDesc::IsValid() const
{
	if (Extent.X <= 0 || Extent.Y <= 0 || Depth == 0 || ArraySize == 0 || NumMips == 0 || NumSamples < 1 || NumSamples > 8)
	{
		return false;
	}

	if (NumSamples > 1 && !(Dimension == ETextureDimension::Texture2D || Dimension == ETextureDimension::Texture2DArray))
	{
		return false;
	}

	if (Dimension == ETextureDimension::Texture3D)
	{
		if (ArraySize > 1)
		{
			return false;
		}
	}
	else if (Depth > 1)
	{
		return false;
	}

	if (Format == PF_Unknown)
	{
		return false;
	}

	return true;
}

void FRDGPooledTexture::InitViews(const FUnorderedAccessViewRHIRef& FirstMipUAV)
{
	if (EnumHasAnyFlags(Desc.Flags, TexCreate_ShaderResource))
	{
		SRVs.Empty(Desc.NumMips);
	}

	if (EnumHasAnyFlags(Desc.Flags, TexCreate_UAV))
	{
		MipUAVs.Empty(Desc.NumMips);

		uint32 MipLevel = 0;

		if (FirstMipUAV)
		{
			MipUAVs.Add(FirstMipUAV);
			MipLevel++;
		}

		for (; MipLevel < Desc.NumMips; MipLevel++)
		{
			MipUAVs.Add(RHICreateUnorderedAccessView(Texture, MipLevel));
		}
	}
}

FRDGTextureSubresourceRange FRDGTexture::GetSubresourceRangeSRV() const
{
	FRDGTextureSubresourceRange Range = GetSubresourceRange();

	// When binding a whole texture for shader read (SRV), we only use the first plane.
	// Other planes like stencil require a separate view to access for read in the shader.
	Range.PlaneSlice = FRHITransitionInfo::kDepthPlaneSlice;
	Range.NumPlaneSlices = 1;

	return Range;
}

void FRDGTexture::SetRHI(FPooledRenderTarget* InPooledRenderTarget, FRDGTextureRef& OutPreviousOwner)
{
	check(InPooledRenderTarget);

	if (!InPooledRenderTarget->HasRDG())
	{
		InPooledRenderTarget->InitRDG();
	}
	PooledTexture = InPooledRenderTarget->GetRDG(RenderTargetTexture);
	check(PooledTexture);

	State = &PooledTexture->State;

	// Return the previous owner and assign this texture as the new one.
	OutPreviousOwner = PooledTexture->Owner;
	PooledTexture->Owner = this;

	// Link the previous alias to this one.
	if (OutPreviousOwner)
	{
		OutPreviousOwner->NextOwner = Handle;
		OutPreviousOwner->bLastOwner = false;
	}

	Allocation = InPooledRenderTarget;
	PooledRenderTarget = InPooledRenderTarget;
	ResourceRHI = PooledTexture->GetRHI();
	check(ResourceRHI);
}

void FRDGTexture::Finalize()
{
	checkf(NextOwner.IsNull() == !!bLastOwner, TEXT("NextOwner must match bLastOwner."));
	checkf(((bExternal || bExtracted) && !bLastOwner) == false, TEXT("Both external and extracted resources must be the last owner of a resource."));

	if (bLastOwner)
	{
		// External and extracted resources are user controlled, so we cannot assume the texture stays in its final state.
		if (bExternal || bExtracted)
		{
			PooledTexture->Reset();
		}
		else
		{
			PooledTexture->Finalize();
		}

		// Restore the reference to the last owner in the aliasing chain.
		Allocation = PooledRenderTarget;
	}
}

void FRDGBuffer::SetRHI(FRDGPooledBuffer* InPooledBuffer, FRDGBufferRef& OutPreviousOwner)
{
	check(InPooledBuffer);

	// Return the previous owner and assign this buffer as the new one.
	OutPreviousOwner = InPooledBuffer->Owner;
	InPooledBuffer->Owner = this;

	// Link the previous owner to this one.
	if (OutPreviousOwner)
	{
		OutPreviousOwner->NextOwner = Handle;
		OutPreviousOwner->bLastOwner = false;
	}

	PooledBuffer = InPooledBuffer;
	Allocation = InPooledBuffer;
	State = &PooledBuffer->State;
	switch (Desc.UnderlyingType)
	{
	case FRDGBufferDesc::EUnderlyingType::VertexBuffer:
		ResourceRHI = InPooledBuffer->VertexBuffer;
		break;
	case FRDGBufferDesc::EUnderlyingType::IndexBuffer:
		ResourceRHI = InPooledBuffer->IndexBuffer;
		break;
	case FRDGBufferDesc::EUnderlyingType::StructuredBuffer:
		ResourceRHI = InPooledBuffer->StructuredBuffer;
		break;
	}
	check(ResourceRHI);
}

void FRDGBuffer::Finalize()
{
	// If these fire, the graph is not tracking state properly.
	check(NextOwner.IsNull() == !!bLastOwner);
	check(!((bExternal || bExtracted) && !bLastOwner));

	if (bLastOwner)
	{
		if (bExternal || bExtracted)
		{
			PooledBuffer->Reset();
		}
		else
		{
			PooledBuffer->Finalize();
		}

		// Restore the reference to the last owner in the chain and sanitize all graph state.
		Allocation = PooledBuffer;
	}
}

FRDGTextureRef FRDGTexture::GetPassthrough(const TRefCountPtr<IPooledRenderTarget>& PooledRenderTargetBase)
{
	if (PooledRenderTargetBase)
	{
		check(PooledRenderTargetBase->IsCompatibleWithRDG());
		FRDGTextureRef Texture = &static_cast<FPooledRenderTarget&>(*PooledRenderTargetBase).PassthroughShaderResourceTexture;
		checkf(Texture->GetRHI(), TEXT("The render target pool didn't allocate a passthrough RHI texture for %s"), PooledRenderTargetBase->GetDesc().DebugName);
		return Texture;
	}
	return nullptr;
}

FRHIShaderResourceView* FRDGPooledBuffer::GetOrCreateSRV(FRDGBufferSRVDesc SRVDesc)
{
	if (const auto* FoundPtr = SRVs.Find(SRVDesc))
	{
		return FoundPtr->GetReference();
	}

	FShaderResourceViewRHIRef RHIShaderResourceView;

	if (Desc.UnderlyingType == FRDGBufferDesc::EUnderlyingType::VertexBuffer)
	{
		RHIShaderResourceView = RHICreateShaderResourceView(VertexBuffer, SRVDesc.BytesPerElement, SRVDesc.Format);
	}
	else if (Desc.UnderlyingType == FRDGBufferDesc::EUnderlyingType::StructuredBuffer)
	{
		RHIShaderResourceView = RHICreateShaderResourceView(StructuredBuffer);
	}
	else
	{
		checkNoEntry();
	}

	FRHIShaderResourceView* View = RHIShaderResourceView.GetReference();
	SRVs.Emplace(SRVDesc, MoveTemp(RHIShaderResourceView));
	return View;
}

FRHIUnorderedAccessView* FRDGPooledBuffer::GetOrCreateUAV(FRDGBufferUAVDesc UAVDesc)
{
	if (const auto* FoundPtr = UAVs.Find(UAVDesc))
	{
		return FoundPtr->GetReference();
	}

	FUnorderedAccessViewRHIRef RHIUnorderedAccessView;

	if (Desc.UnderlyingType == FRDGBufferDesc::EUnderlyingType::VertexBuffer)
	{
		RHIUnorderedAccessView = RHICreateUnorderedAccessView(VertexBuffer, UAVDesc.Format);
	}
	else if (Desc.UnderlyingType == FRDGBufferDesc::EUnderlyingType::StructuredBuffer)
	{
		RHIUnorderedAccessView = RHICreateUnorderedAccessView(StructuredBuffer, UAVDesc.bSupportsAtomicCounter, UAVDesc.bSupportsAppendBuffer);
	}
	else
	{
		checkNoEntry();
	}

	FRHIUnorderedAccessView* View = RHIUnorderedAccessView.GetReference();
	UAVs.Emplace(UAVDesc, MoveTemp(RHIUnorderedAccessView));
	return View;
}