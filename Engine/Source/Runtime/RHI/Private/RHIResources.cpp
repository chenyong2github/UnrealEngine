// Copyright Epic Games, Inc. All Rights Reserved.

#include "RHI.h"
#include "RHIUniformBufferLayoutInitializer.h"

template <>
struct TIsBitwiseConstructible<FRHIUniformBufferResource, FRHIUniformBufferResourceInitializer>
{
	enum { Value = true };
};

FRHIUniformBufferLayout::FRHIUniformBufferLayout(const FRHIUniformBufferLayoutInitializer& Initializer)
	: FRHIResource(RRT_UniformBufferLayout)
	, Name(Initializer.GetDebugName())
	, Resources(TConstArrayView<FRHIUniformBufferResourceInitializer>(Initializer.Resources))
	, GraphResources(TConstArrayView<FRHIUniformBufferResourceInitializer>(Initializer.GraphResources))
	, GraphTextures(TConstArrayView<FRHIUniformBufferResourceInitializer>(Initializer.GraphTextures))
	, GraphBuffers(TConstArrayView<FRHIUniformBufferResourceInitializer>(Initializer.GraphBuffers))
	, GraphUniformBuffers(TConstArrayView<FRHIUniformBufferResourceInitializer>(Initializer.GraphUniformBuffers))
	, UniformBuffers(TConstArrayView<FRHIUniformBufferResourceInitializer>(Initializer.UniformBuffers))
	, Hash(Initializer.GetHash())
	, ConstantBufferSize(Initializer.ConstantBufferSize)
	, RenderTargetsOffset(Initializer.RenderTargetsOffset)
	, StaticSlot(Initializer.StaticSlot)
	, BindingFlags(Initializer.BindingFlags)
	, bHasNonGraphOutputs(Initializer.bHasNonGraphOutputs)
	, bNoEmulatedUniformBuffer(Initializer.bNoEmulatedUniformBuffer)
{
}
