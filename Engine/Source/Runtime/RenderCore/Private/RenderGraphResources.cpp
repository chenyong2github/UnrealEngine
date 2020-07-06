// Copyright Epic Games, Inc. All Rights Reserved.

#include "RenderGraphResources.h"
#include "RenderGraphPrivate.h"

FRDGParentResource::FRDGParentResource(const TCHAR* InName, const ERDGParentResourceType InType, const ERDGParentResourceFlags InFlags, bool bInIsExternal)
	: FRDGResource(InName)
	, Type(InType)
	, Flags(InFlags)
	, bIsExternal(bInIsExternal)
{}

bool FRDGSubresourceState::IsTransitionRequired(const FRDGSubresourceState& Previous, const FRDGSubresourceState& Next)
{
	check(Next.Access != EResourceTransitionAccess::Unknown);

	if (Previous.Access != Next.Access || Previous.Pipeline != Next.Pipeline)
	{
		return true;
	}
	else if (
		EnumHasAnyFlags(Previous.Access, EResourceTransitionAccess::UAVMask) &&
		EnumHasAnyFlags(Next.Access, EResourceTransitionAccess::UAVMask))
	{
		if (!GRDGOverlapUAVs)
		{
			return true;
		}

		const FRDGResourceHandle PreviousUniqueHandle = Previous.NoUAVBarrierFilter.GetUniqueHandle();
		const FRDGResourceHandle NextUniqueHandle = Next.NoUAVBarrierFilter.GetUniqueHandle();

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

void FRDGSubresourceState::SetPass(FRDGPassHandle InPassHandle, ERDGPipeline InPipeline)
{
	PassHandle = InPassHandle;
	Pipeline = InPipeline;
}

void FRDGSubresourceState::MergeSanitizedFrom(const FRDGSubresourceState& Other)
{
	//ensureMsgf(Other.Pipeline == ERDGPipeline::Graphics, TEXT("Resource should be on the graphics pipeline!"));

	// Preserve pipeline / access members.
	const ERDGPipeline LocalPipeline = Pipeline;
	const EResourceTransitionAccess LocalAccess = Access;
	*this = Other;
	Pipeline = LocalPipeline;
	Access = LocalAccess;
}

bool FRDGSubresourceState::MergeCrossPipelineFrom(const FRDGSubresourceState& Other)
{
	if (Other.Access != EResourceTransitionAccess::Unknown && Pipeline != Other.Pipeline)
	{
		const ERDGPipeline LocalPipeline = Pipeline;
		*this = Other;
		Pipeline = LocalPipeline;
		return true;
	}
	return false;
}

bool FRDGSubresourceState::MergeFrom(const FRDGSubresourceState& Other)
{
	if (Other.Access != EResourceTransitionAccess::Unknown)
	{
		*this = Other;
		return true;
	}
	return false;
}

FRDGTextureState::FRDGTextureState(const FRDGTextureDesc& Desc)
	: Layout(Desc.NumMips, Desc.ArraySize, IsStencilFormat(Desc.Format) ? 2 : 1)
{
	checkf(Layout.GetSubresourceCount(), TEXT("Texture '%s' doesn't have any subresources."), Desc.DebugName);
}

void FRDGTextureState::InitAsWholeResource(FRDGSubresourceState InState)
{
	check(Layout.GetSubresourceCount());

	WholeResourceState = InState;

	// Reset the subresource states. This keeps the array memory allocation in case the resource
	// is accessed on a subresource level again for the lifetime of this render graph instance.
	SubresourceStates.Reset();
}

void FRDGTextureState::InitAsSubresources(FRDGSubresourceState InState)
{
	check(Layout.GetSubresourceCount());

	WholeResourceState = {};
	SubresourceStates.SetNum(Layout.GetSubresourceCount());

	for (FRDGSubresourceState& State : SubresourceStates)
	{
		State = InState;
	}
}

void FRDGTextureState::MergeSanitizedFrom(const FRDGTextureState& Other)
{
	check(Layout == Other.Layout);

	if (Other.IsWholeResourceState())
	{
		InitAsWholeResource({});
		WholeResourceState.MergeSanitizedFrom(Other.WholeResourceState);
	}
	else
	{
		if (IsWholeResourceState())
		{
			InitAsSubresources(WholeResourceState);
		}

		for (int32 Index = 0; Index < SubresourceStates.Num(); ++Index)
		{
			SubresourceStates[Index].MergeSanitizedFrom(Other.SubresourceStates[Index]);
		}
	}
}

void FRDGTextureState::MergeCrossPipelineFrom(const FRDGTextureState& Other)
{
	check(Layout == Other.Layout);

	if (Other.IsWholeResourceState())
	{
		if (IsWholeResourceState())
		{
			WholeResourceState.MergeCrossPipelineFrom(Other.WholeResourceState);
		}
		else
		{
			int32 MergedSubresourceCount = 0;

			// We can't know whether to coalesce to a whole resource state until we compare each individual
			// subresource, since we're comparing against individual subresource pipelines.
			for (int32 Index = 0; Index < SubresourceStates.Num(); ++Index)
			{
				if (SubresourceStates[Index].MergeCrossPipelineFrom(Other.WholeResourceState))
				{
					MergedSubresourceCount++;
				}
			}

			// If all sub-resources were merged, just reset to a whole resource.
			if (MergedSubresourceCount == SubresourceStates.Num())
			{
				InitAsWholeResource(Other.WholeResourceState);
			}
		}
	}
	else
	{
		if (IsWholeResourceState())
		{
			InitAsSubresources(WholeResourceState);
		}

		for (int32 Index = 0; Index < SubresourceStates.Num(); ++Index)
		{
			SubresourceStates[Index].MergeCrossPipelineFrom(Other.SubresourceStates[Index]);
		}
	}
}

void FRDGTextureState::MergeFrom(const FRDGTextureState& Other)
{
	check(Layout == Other.Layout);

	if (Other.IsWholeResourceState())
	{
		if (Other.WholeResourceState.Access != EResourceTransitionAccess::Unknown)
		{
			InitAsWholeResource(Other.WholeResourceState);
		}
	}
	else
	{
		if (IsWholeResourceState())
		{
			InitAsSubresources(WholeResourceState);
		}

		// Only merge subresource states that are known.
		for (int32 Index = 0; Index < SubresourceStates.Num(); ++Index)
		{
			SubresourceStates[Index].MergeFrom(Other.SubresourceStates[Index]);
		}
	}
}

void FRDGTextureState::SetPass(FRDGPassHandle PassHandle, ERDGPipeline Pipeline)
{
	if (IsWholeResourceState())
	{
		WholeResourceState.SetPass(PassHandle, Pipeline);
	}
	else
	{
		for (FRDGSubresourceState& State : SubresourceStates)
		{
			State.SetPass(PassHandle, Pipeline);
		}
	}
}

FRDGTexture::FRDGTexture(const TCHAR* InName, const FPooledRenderTargetDesc& InDesc, ERDGParentResourceFlags InFlags, bool bIsExternal)
	: FRDGParentResource(InName, ERDGParentResourceType::Texture, InFlags, bIsExternal)
	, Desc(InDesc)
	, State(InDesc)
	, StatePending(InDesc)
{}

void FRDGTexture::Init(const TRefCountPtr<IPooledRenderTarget>& InPooledTexture)
{
	check(InPooledTexture);
	PooledTexture = InPooledTexture;
	ResourceRHI = InPooledTexture->GetRenderTargetItem().ShaderResourceTexture;
	check(ResourceRHI);
}

FRDGBuffer::FRDGBuffer(
	const TCHAR* InName,
	const FRDGBufferDesc& InDesc,
	ERDGParentResourceFlags InFlags,
	bool bIsExternal)
	: FRDGParentResource(InName, ERDGParentResourceType::Buffer, InFlags, bIsExternal)
	, Desc(InDesc)
{}

void FRDGBuffer::Init(const TRefCountPtr<FPooledRDGBuffer>& InPooledBuffer)
{
	check(InPooledBuffer);
	PooledBuffer = InPooledBuffer;
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