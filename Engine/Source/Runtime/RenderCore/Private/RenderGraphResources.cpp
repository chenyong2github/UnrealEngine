// Copyright Epic Games, Inc. All Rights Reserved.

#include "RenderGraphResources.h"
#include "RenderGraphPass.h"
#include "RenderGraphPrivate.h"

inline bool NeedsUAVBarrier(FRDGViewHandle PreviousHandle, FRDGViewHandle NextHandle)
{
	// Barrier if previous / next don't have a matching valid skip-barrier UAV handle.
	if (GRDGOverlapUAVs != 0 && NextHandle.IsValid() && PreviousHandle == NextHandle)
	{
		return false;
	}

	return true;
}

inline bool NeedsUAVBarrier(const FRDGSubresourceState& Previous, const FRDGSubresourceState& Next)
{
	return NeedsUAVBarrier(Previous.NoUAVBarrierFilter.GetUniqueHandle(), Next.NoUAVBarrierFilter.GetUniqueHandle());
}

FRDGParentResource::FRDGParentResource(const TCHAR* InName, const ERDGParentResourceType InType)
	: FRDGResource(InName)
	, Type(InType)
	, bExternal(0)
	, bExtracted(0)
	, bProduced(0)
	, bTransient(0)
	, bUserSetNonTransient(0)
	, bFinalizedAccess(0)
	, bLastOwner(1)
	// Culling logic runs only when immediate mode is off.
	, bCulled(1)
	, bUsedByAsyncComputePass(0)
{}

FRDGParentResource::~FRDGParentResource()
{
	check(IsImmediateMode() || ReferenceCount == 0);
}

bool FRDGProducerState::IsDependencyRequired(FRDGProducerState LastProducer, ERHIPipeline LastPipeline, FRDGProducerState NextState, ERHIPipeline NextPipeline)
{
	/** This function determines whether a producer-consumer relationship exists in the graph, which is used for culling and
	 *  async-compute fence derivation. Producers are tracked per-pipeline, so it's safe to elide a cross-pipeline producer
	 *  for the purposes of overlapping producers, as long as a dependency exists on the same pipeline. Eliding both will
	 *  split the producer / consumer graph into two and break culling. The only current use case this is allowing multiple
	 *  pipes to write UAVs.
	 *
	 *  Producer / consumer dependencies take place independent of resource state merging / transitions, so the logic must
	 *  be carefully aligned so that cross-pipe dependencies align with transitions.
	 */

	// The first needs to be known producers.
	check(IsWritableAccess(LastProducer.Access));

	// A dependency is always applied on the same pipe to ensure that connectivity is preserved for culling purposes.
	if (LastPipeline == NextPipeline)
	{
		return true;
	}

	// Only certain platforms allow multi-pipe UAV access.
	const ERHIAccess MultiPipelineUAVMask = ERHIAccess::UAVMask & GRHIMultiPipelineMergeableAccessMask;

	// Skip the dependency if the states are used as UAV on different pipes and a UAV barrier can be skipped. This elides the async fence.
	if (EnumHasAnyFlags(NextState.Access, MultiPipelineUAVMask) && !NeedsUAVBarrier(LastProducer.NoUAVBarrierHandle, NextState.NoUAVBarrierHandle))
	{
		return false;
	}

	// Everything else requires a dependency.
	return true;
}

bool FRDGSubresourceState::IsMergeAllowed(ERDGParentResourceType ResourceType, const FRDGSubresourceState& Previous, const FRDGSubresourceState& Next)
{
	/** State merging occurs during compilation and before resource transitions are collected. It serves to remove the bulk
	 *  of unnecessary transitions by looking ahead in the resource usage chain. A resource transition cannot occur within
	 *  a merged state, so a merge is not allowed to proceed if a barrier might be required. Merging is also where multi-pipe
	 *  transitions are determined, if supported by the platform.
	 */

	const ERHIAccess AccessUnion = Previous.Access | Next.Access;
	const ERHIAccess DSVMask = ERHIAccess::DSVRead | ERHIAccess::DSVWrite;

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

		// UAVs will filter through the above checks because they are both read and write. UAV can only merge it itself.
		if (EnumHasAnyFlags(AccessUnion, ERHIAccess::UAVMask) && EnumHasAnyFlags(AccessUnion, ~ERHIAccess::UAVMask))
		{
			return false;
		}

		// Depth Read / Write should never merge with anything other than itself.
		if (EnumHasAllFlags(AccessUnion, DSVMask) && EnumHasAnyFlags(AccessUnion, ~DSVMask))
		{
			return false;
		}

		// Filter out platform-specific unsupported mergeable states.
		if (EnumHasAnyFlags(AccessUnion, ~GRHIMergeableAccessMask))
		{
			return false;
		}
	}

	// Not allowed if the resource is being used as a UAV and needs a barrier.
	if (EnumHasAnyFlags(Next.Access, ERHIAccess::UAVMask) && NeedsUAVBarrier(Previous, Next))
	{
		return false;
	}

	// Filter out unsupported platform-specific multi-pipeline merged accesses.
	if (EnumHasAnyFlags(AccessUnion, ~GRHIMultiPipelineMergeableAccessMask) && Previous.GetPipelines() != Next.GetPipelines())
	{
		return false;
	}

	// Not allowed to merge differing flags.
	if (Previous.Flags != Next.Flags)
	{
		return false;
	}

	return true;
}

bool FRDGSubresourceState::IsTransitionRequired(const FRDGSubresourceState& Previous, const FRDGSubresourceState& Next)
{
	// This function only needs to filter out identical states and handle UAV barriers.
	check(Next.Access != ERHIAccess::Unknown);

	if (Previous.Access != Next.Access || Previous.GetPipelines() != Next.GetPipelines() || Previous.Flags != Next.Flags)
	{
		return true;
	}

	// UAV is a special case as a barrier may still be required even if the states match.
	if (EnumHasAnyFlags(Next.Access, ERHIAccess::UAVMask) && NeedsUAVBarrier(Previous, Next))
	{
		return true;
	}

	return false;
}

void FRDGPooledTexture::Finalize()
{
	for (FRDGSubresourceState& SubresourceState : State)
	{
		SubresourceState.Finalize();
	}
	Owner = nullptr;
}

void FRDGPooledTexture::Reset()
{
	InitAsWholeResource(State);
	Owner = nullptr;
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

void FRDGTexture::SetRHI(FPooledRenderTarget* InPooledRenderTarget)
{
	Allocation = InPooledRenderTarget;
	PooledRenderTarget = InPooledRenderTarget;

	if (!InPooledRenderTarget->HasRDG())
	{
		InPooledRenderTarget->InitRDG();
	}

	SetRHI(InPooledRenderTarget->GetRDG(RenderTargetTexture));
}

void FRDGTexture::SetRHI(FRDGPooledTexture* InPooledTexture)
{
	PooledTexture = InPooledTexture;
	State = &PooledTexture->State;
	ViewCache = &PooledTexture->ViewCache;

	// Return the previous owner and assign this texture as the new one.
	FRDGTextureRef PreviousOwner = PooledTexture->Owner;
	PooledTexture->Owner = this;

	// Link the previous alias to this one.
	if (PreviousOwner)
	{
		PreviousOwner->NextOwner = Handle;
		PreviousOwner->bLastOwner = false;
	}

	ResourceRHI = PooledTexture->GetRHI();
}

void FRDGTexture::SetRHI(FRHITransientTexture* InTransientTexture, FRDGAllocator& Allocator)
{
	TransientTexture = InTransientTexture;
	State = Allocator.AllocNoDestruct<FRDGTextureSubresourceState>();
	ViewCache = &InTransientTexture->ViewCache;
	ResourceRHI = InTransientTexture->GetRHI();

	bTransient = true;
}

void FRDGTexture::Finalize()
{
	checkf(NextOwner.IsNull() == !!bLastOwner, TEXT("NextOwner must match bLastOwner."));
	checkf(!bExtracted || bLastOwner, TEXT("Extracted resources must be the last owner of a resource."));

	if (bLastOwner)
	{
		if (bTransient)
		{
			// Manually deconstruct the allocated state so as not to invoke overhead from the allocators destructor tracking.
			State->~FRDGTextureSubresourceState();
			State = nullptr;
		}
		else
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

			// Restore the reference to the last owner in the aliasing chain; not necessary for the transient resource allocator.
			if (PooledRenderTarget)
			{
				Allocation = PooledRenderTarget;
			}
		}
	}
}

void FRDGBuffer::SetRHI(FRDGPooledBuffer* InPooledBuffer)
{
	// Return the previous owner and assign this buffer as the new one.
	FRDGBuffer* PreviousOwner = InPooledBuffer->Owner;
	InPooledBuffer->Owner = this;

	// Link the previous owner to this one.
	if (PreviousOwner)
	{
		PreviousOwner->NextOwner = Handle;
		PreviousOwner->bLastOwner = false;
	}

	PooledBuffer = InPooledBuffer;
	Allocation = InPooledBuffer;
	State = &PooledBuffer->State;
	ViewCache = &PooledBuffer->ViewCache;
	ResourceRHI = InPooledBuffer->GetRHI();
}

void FRDGBuffer::SetRHI(FRHITransientBuffer* InTransientBuffer, FRDGAllocator& Allocator)
{
	TransientBuffer = InTransientBuffer;
	State = Allocator.AllocNoDestruct<FRDGSubresourceState>();
	ViewCache = &InTransientBuffer->ViewCache;
	ResourceRHI = InTransientBuffer->GetRHI();

	bTransient = true;;
}

void FRDGBuffer::Finalize()
{
	// If these fire, the graph is not tracking state properly.
	checkf(NextOwner.IsNull() == !!bLastOwner, TEXT("NextOwner must match bLastOwner."));
	checkf(!bExtracted || bLastOwner, TEXT("Extracted resources must be the last owner of a resource."));

	if (bLastOwner)
	{
		if (bTransient)
		{
			State = nullptr;
		}
		else
		{
			if (bExternal || bExtracted)
			{
				PooledBuffer->Reset();
			}
			else
			{
				PooledBuffer->Finalize();
			}

			// Restore the reference to the last owner in the aliasing chain.
			Allocation = PooledBuffer;
		}
	}
}