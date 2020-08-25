// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraphResources.h"
#include "RenderGraphBuilder.h"
#include "Shader.h"
#include "ShaderParameterStruct.h"
#include "ShaderParameterMacros.h"

class FGlobalShaderMap;

/**
 * Clears all render graph tracked resources that are not bound by a shader.
 * Excludes any resources on the ExcludeList from being cleared regardless of whether the 
 * shader binds them or not. This is needed for resources that are used outside of shader
 * bindings such as indirect arguments buffers.
 */
extern RENDERCORE_API void ClearUnusedGraphResourcesImpl(
	const FShaderParameterBindings& ShaderBindings,
	const FShaderParametersMetadata* ParametersMetadata,
	void* InoutParameters,
	std::initializer_list<FRDGResourceRef> ExcludeList);

/** Similar to the function above, but takes a list of shader bindings and only clears if none of the shaders contain the resource. */
extern RENDERCORE_API void ClearUnusedGraphResourcesImpl(
	TArrayView<const FShaderParameterBindings*> ShaderBindingsList,
	const FShaderParametersMetadata* ParametersMetadata,
	void* InoutParameters,
	std::initializer_list<FRDGResourceRef> ExcludeList);

template<typename TShaderClass>
void ClearUnusedGraphResources(
	const TShaderRef<TShaderClass>& Shader,
	typename TShaderClass::FParameters* InoutParameters,
	std::initializer_list<FRDGResourceRef> ExcludeList = {})
{
	const FShaderParametersMetadata* ParametersMetadata = TShaderClass::FParameters::FTypeInfo::GetStructMetadata();

	// Verify the shader have all the parameters it needs. This is done before the
	// ClearUnusedGraphResourcesImpl() to not mislead user on why some resource are missing
	// when debugging a validation failure.
	ValidateShaderParameters(Shader, ParametersMetadata, InoutParameters);

	// Clear the resources the shader won't need.
	return ClearUnusedGraphResourcesImpl(Shader->Bindings, ParametersMetadata, InoutParameters, ExcludeList);
}

template <typename TShaderClassA, typename TShaderClassB, typename TPassParameterStruct>
void ClearUnusedGraphResources(
	const TShaderRef<TShaderClassA>& ShaderA,
	const TShaderRef<TShaderClassB>& ShaderB,
	TPassParameterStruct* InoutParameters,
	std::initializer_list<FRDGResourceRef> ExcludeList = {})
{
	static_assert(TIsSame<typename TShaderClassA::FParameters, TPassParameterStruct>::Value, "First shader FParameter type must match pass parameters.");
	static_assert(TIsSame<typename TShaderClassB::FParameters, TPassParameterStruct>::Value, "Second shader FParameter type must match pass parameters.");
	const FShaderParametersMetadata* ParametersMetadata = TPassParameterStruct::FTypeInfo::GetStructMetadata();

	// Verify the shader have all the parameters it needs. This is done before the
	// ClearUnusedGraphResourcesImpl() to not mislead user on why some resource are missing
	// when debugging a validation failure.
	ValidateShaderParameters(ShaderA, ParametersMetadata, InoutParameters);
	ValidateShaderParameters(ShaderB, ParametersMetadata, InoutParameters);

	// Clear the resources the shader won't need.
	const FShaderParameterBindings* ShaderBindings[] = { &ShaderA->Bindings, &ShaderB->Bindings };
	return ClearUnusedGraphResourcesImpl(ShaderBindings, ParametersMetadata, InoutParameters, ExcludeList);
}

/**
 * Register external texture with fallback if the resource is invalid.
 *
 * CAUTION: use this function very wisely. It may actually remove shader parameter validation
 * failure when a pass is actually trying to access a resource not yet or no longer available.
 */
RENDERCORE_API FRDGTextureRef RegisterExternalTextureWithFallback(
	FRDGBuilder& GraphBuilder,
	const TRefCountPtr<IPooledRenderTarget>& ExternalPooledTexture,
	const TRefCountPtr<IPooledRenderTarget>& FallbackPooledTexture,
	const TCHAR* ExternalPooledTextureName = TEXT("External"));

/** All utils for compute shaders.
 */
struct RENDERCORE_API FComputeShaderUtils
{
	/** Ideal size of group size 8x8 to occupy at least an entire wave on GCN, two warp on Nvidia. */
	static constexpr int32 kGolden2DGroupSize = 8;

	/** Compute the number of group to dispatch. */
	static FIntVector GetGroupCount(const int32 ThreadCount, const int32 GroupSize)
	{
		return FIntVector(
			FMath::DivideAndRoundUp(ThreadCount, GroupSize),
			1,
			1);
	}
	static FIntVector GetGroupCount(const FIntPoint& ThreadCount, const FIntPoint& GroupSize)
	{
		return FIntVector(
			FMath::DivideAndRoundUp(ThreadCount.X, GroupSize.X),
			FMath::DivideAndRoundUp(ThreadCount.Y, GroupSize.Y),
			1);
	}
	static FIntVector GetGroupCount(const FIntPoint& ThreadCount, const int32 GroupSize)
	{
		return FIntVector(
			FMath::DivideAndRoundUp(ThreadCount.X, GroupSize),
			FMath::DivideAndRoundUp(ThreadCount.Y, GroupSize),
			1);
	}
	static FIntVector GetGroupCount(const FIntVector& ThreadCount, const FIntVector& GroupSize)
	{
		return FIntVector(
			FMath::DivideAndRoundUp(ThreadCount.X, GroupSize.X),
			FMath::DivideAndRoundUp(ThreadCount.Y, GroupSize.Y),
			FMath::DivideAndRoundUp(ThreadCount.Z, GroupSize.Z));
	}
	static FIntVector GetGroupCount(const FIntVector& ThreadCount, const int32 GroupSize)
	{
		return FIntVector(
			FMath::DivideAndRoundUp(ThreadCount.X, GroupSize),
			FMath::DivideAndRoundUp(ThreadCount.Y, GroupSize),
			FMath::DivideAndRoundUp(ThreadCount.Z, GroupSize));
	}


	/** Dispatch a compute shader to rhi command list with its parameters. */
	template<typename TShaderClass>
	static void Dispatch(FRHIComputeCommandList& RHICmdList, const TShaderRef<TShaderClass>& ComputeShader, const typename TShaderClass::FParameters& Parameters, FIntVector GroupCount)
	{
		ValidateGroupCount(GroupCount);
		FRHIComputeShader* ShaderRHI = ComputeShader.GetComputeShader();
		RHICmdList.SetComputeShader(ShaderRHI);
		SetShaderParameters(RHICmdList, ComputeShader, ShaderRHI, Parameters);
		RHICmdList.DispatchComputeShader(GroupCount.X, GroupCount.Y, GroupCount.Z);
		UnsetShaderUAVs(RHICmdList, ComputeShader, ShaderRHI);
	}
	
	/** Indirect dispatch a compute shader to rhi command list with its parameters. */
	template<typename TShaderClass>
	static void DispatchIndirect(
		FRHIComputeCommandList& RHICmdList,
		const TShaderRef<TShaderClass>& ComputeShader,
		const typename TShaderClass::FParameters& Parameters,
		FRHIVertexBuffer* IndirectArgsBuffer,
		uint32 IndirectArgOffset)
	{
		ValidateIndirectArgsBuffer(IndirectArgsBuffer->GetSize(), IndirectArgOffset);
		FRHIComputeShader* ShaderRHI = ComputeShader.GetComputeShader();
		RHICmdList.SetComputeShader(ShaderRHI);
		SetShaderParameters(RHICmdList, ComputeShader, ShaderRHI, Parameters);
		RHICmdList.DispatchIndirectComputeShader(IndirectArgsBuffer, IndirectArgOffset);
		UnsetShaderUAVs(RHICmdList, ComputeShader, ShaderRHI);
	}

	/** Dispatch a compute shader to rhi command list with its parameters and indirect args. */
	template<typename TShaderClass>
	static FORCEINLINE_DEBUGGABLE void DispatchIndirect(
		FRHICommandList& RHICmdList,
		const TShaderRef<TShaderClass>& ComputeShader,
		const typename TShaderClass::FParameters& Parameters,
		FRDGBufferRef IndirectArgsBuffer,
		uint32 IndirectArgOffset)
	{
		ValidateIndirectArgsBuffer(IndirectArgsBuffer, IndirectArgOffset);
		FRHIComputeShader* ShaderRHI = ComputeShader.GetComputeShader();
		RHICmdList.SetComputeShader(ShaderRHI);
		SetShaderParameters(RHICmdList, ComputeShader, ShaderRHI, Parameters);
		RHICmdList.DispatchIndirectComputeShader(IndirectArgsBuffer->GetIndirectRHICallBuffer(), IndirectArgOffset);
		UnsetShaderUAVs(RHICmdList, ComputeShader, ShaderRHI);
	}

	/** Dispatch a compute shader to render graph builder with its parameters. */
	template<typename TShaderClass>
	static void AddPass(
		FRDGBuilder& GraphBuilder,
		FRDGEventName&& PassName,
		ERDGPassFlags PassFlags,
		const TShaderRef<TShaderClass>& ComputeShader,
		typename TShaderClass::FParameters* Parameters,
		FIntVector GroupCount)
	{
		checkf(PassFlags == ERDGPassFlags::Compute || PassFlags == ERDGPassFlags::AsyncCompute, TEXT("AddPass only supports 'Compute' or 'AsyncCompute'."));

		ValidateGroupCount(GroupCount);
		ClearUnusedGraphResources(ComputeShader, Parameters);

		GraphBuilder.AddPass(
			Forward<FRDGEventName>(PassName),
			Parameters,
			PassFlags,
			[Parameters, ComputeShader, GroupCount](FRHIComputeCommandList& RHICmdList)
		{
			FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, *Parameters, GroupCount);
		});
	}

	template <typename TShaderClass>
	static FORCEINLINE void AddPass(
		FRDGBuilder& GraphBuilder,
		FRDGEventName&& PassName,
		const TShaderRef<TShaderClass>& ComputeShader,
		typename TShaderClass::FParameters* Parameters,
		FIntVector GroupCount)
	{
		AddPass(GraphBuilder, Forward<FRDGEventName>(PassName), ERDGPassFlags::Compute, ComputeShader, Parameters, GroupCount);
	}

	/** Dispatch a compute shader to render graph builder with its parameters. */
	template<typename TShaderClass>
	static void AddPass(
		FRDGBuilder& GraphBuilder,
		FRDGEventName&& PassName,
		ERDGPassFlags PassFlags,
		const TShaderRef<TShaderClass>& ComputeShader,
		typename TShaderClass::FParameters* Parameters,
		FRDGBufferRef IndirectArgsBuffer,
		uint32 IndirectArgsOffset)
	{
		checkf(PassFlags == ERDGPassFlags::Compute || PassFlags == ERDGPassFlags::AsyncCompute, TEXT("AddPass only supports 'Compute' or 'AsyncCompute'."));
		ValidateIndirectArgsBuffer(IndirectArgsBuffer, IndirectArgsOffset);
		ClearUnusedGraphResources(ComputeShader, Parameters, { IndirectArgsBuffer });

		GraphBuilder.AddPass(
			Forward<FRDGEventName>(PassName),
			Parameters,
			PassFlags,
			[Parameters, ComputeShader, IndirectArgsBuffer, IndirectArgsOffset](FRHIComputeCommandList& RHICmdList)
		{			
			// Marks the indirect draw parameter as used by the pass manually, given it can't be bound directly by any of the shader,
			// meaning SetShaderParameters() won't be able to do it.
			IndirectArgsBuffer->MarkResourceAsUsed();

			FComputeShaderUtils::DispatchIndirect(RHICmdList, ComputeShader, *Parameters, IndirectArgsBuffer->GetIndirectRHICallBuffer(), IndirectArgsOffset);
		});
	}

	template<typename TShaderClass>
	static FORCEINLINE void AddPass(
		FRDGBuilder& GraphBuilder,
		FRDGEventName&& PassName,
		const TShaderRef<TShaderClass>& ComputeShader,
		typename TShaderClass::FParameters* Parameters,
		FRDGBufferRef IndirectArgsBuffer,
		uint32 IndirectArgsOffset)
	{
		AddPass(GraphBuilder, Forward<FRDGEventName>(PassName), ERDGPassFlags::Compute, ComputeShader, Parameters, IndirectArgsBuffer, IndirectArgsOffset);
	}

	static void ClearUAV(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap, FRDGBufferUAVRef UAV, uint32 ClearValue);
	static void ClearUAV(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap, FRDGBufferUAVRef UAV, FVector4 ClearValue);

	static inline void ValidateGroupCount(const FIntVector& GroupCount)
	{
		check(GroupCount.X <= GRHIMaxDispatchThreadGroupsPerDimension.X);
		check(GroupCount.Y <= GRHIMaxDispatchThreadGroupsPerDimension.Y);
		check(GroupCount.Z <= GRHIMaxDispatchThreadGroupsPerDimension.Z);
	}

	static inline void ValidateIndirectArgsBuffer(uint32 IndirectArgsBufferSize, uint32 IndirectArgOffset)
	{
		checkf((IndirectArgOffset % 4) == 0, TEXT("IndirectArgOffset for compute shader indirect dispatch needs to be a multiple of 4."));
		checkf(
			(IndirectArgOffset + sizeof(FRHIDispatchIndirectParameters)) <= IndirectArgsBufferSize,
			TEXT("Indirect parameters buffer for compute shader indirect dispatch at byte offset %d doesn't have anought room for FRHIDispatchIndirectParameters."),
			IndirectArgOffset);
	}

	static inline void ValidateIndirectArgsBuffer(FRDGBufferRef IndirectArgsBuffer, uint32 IndirectArgOffset)
	{
		checkf(IndirectArgsBuffer->Desc.UnderlyingType == FRDGBufferDesc::EUnderlyingType::VertexBuffer, TEXT("The buffer %s needs to be a vertex buffer to be used as an indirect dispatch parameters"), IndirectArgsBuffer->Name);
		checkf(IndirectArgsBuffer->Desc.Usage & BUF_DrawIndirect, TEXT("The buffer %s for indirect dispatch parameters was not flagged with BUF_DrawIndirect"), IndirectArgsBuffer->Name);
		ValidateIndirectArgsBuffer(IndirectArgsBuffer->Desc.GetTotalNumBytes(), IndirectArgOffset);
	}
};

/** Adds a render graph pass to copy a region from one texture to another. Uses RHICopyTexture under the hood.
 *  Formats of the two textures must match. The output and output texture regions be within the respective extents.
 */
RENDERCORE_API void AddCopyTexturePass(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef InputTexture,
	FRDGTextureRef OutputTexture,
	const FRHICopyTextureInfo& CopyInfo);

/** Simpler variant of the above function for 2D textures.
 *  @param InputPosition The pixel position within the input texture of the top-left corner of the box.
 *  @param OutputPosition The pixel position within the output texture of the top-left corner of the box.
 *  @param Size The size in pixels of the region to copy from input to output. If zero, the full extent of
 *         the input texture is copied.
 */
inline void AddCopyTexturePass(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef InputTexture,
	FRDGTextureRef OutputTexture,
	FIntPoint InputPosition = FIntPoint::ZeroValue,
	FIntPoint OutputPosition = FIntPoint::ZeroValue,
	FIntPoint Size = FIntPoint::ZeroValue)
{
	FRHICopyTextureInfo CopyInfo;
	CopyInfo.SourcePosition.X = InputPosition.X;
	CopyInfo.SourcePosition.Y = InputPosition.Y;
	CopyInfo.DestPosition.X = OutputPosition.X;
	CopyInfo.DestPosition.Y = OutputPosition.Y;
	if (Size != FIntPoint::ZeroValue)
	{
		CopyInfo.Size = FIntVector(Size.X, Size.Y, 1);
	}
	AddCopyTexturePass(GraphBuilder, InputTexture, OutputTexture, CopyInfo);
}

/** Adds a render graph pass to resolve from one texture to another. Uses RHICopyToResolveTarget under the hood.
 *  The formats of the two textures don't need to match.
 */
RENDERCORE_API void AddCopyToResolveTargetPass(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef InputTexture,
	FRDGTextureRef OutputTexture,
	const FResolveParams& ResolveParams);

/** Adds a render graph pass to clear a texture or buffer UAV with a single typed value. */
RENDERCORE_API void AddClearUAVPass(FRDGBuilder& GraphBuilder, FRDGBufferUAVRef BufferUAV, uint32 Value);

RENDERCORE_API void AddClearUAVPass(FRDGBuilder& GraphBuilder, FRDGTextureUAVRef TextureUAV, const float(&ClearValues)[4]);

RENDERCORE_API void AddClearUAVPass(FRDGBuilder& GraphBuilder, FRDGTextureUAVRef TextureUAV, const uint32(&ClearValues)[4]);

RENDERCORE_API void AddClearUAVPass(FRDGBuilder& GraphBuilder, FRDGTextureUAVRef TextureUAV, const FLinearColor& ClearColor);

/** Clears parts of UAV specified by an array of screen rects. If no rects are specific, then it falls back to a standard UAV clear. */
RENDERCORE_API void AddClearUAVPass(FRDGBuilder& GraphBuilder, FRDGTextureUAVRef TextureUAV, const uint32(&ClearValues)[4], FRDGBufferSRVRef RectMinMaxBufferSRV, uint32 NumRects);

/** Adds a render graph pass to clear a render target. Prefer to use clear actions if possible. */
RENDERCORE_API void AddClearRenderTargetPass(FRDGBuilder& GraphBuilder, FRDGTextureRef Texture, const FLinearColor& ClearColor);

/** Adds a render graph pass to clear a depth stencil target. Prefer to use clear actions if possible. */
RENDERCORE_API void AddClearDepthStencilPass(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef Texture,
	bool bClearDepth,
	float Depth,
	bool bClearStencil,
	uint8 Stencil);

enum class ERDGInitialDataFlags : uint8
{
	/** Specifies the default behavior, which is to make a copy of the initial data for replay when
	 *  the graph is executed. The user does not need to preserve lifetime of the data pointer.
	 */
	None = 0,

	/** Specifies that the user will maintain ownership of the data until the graph is executed. The
	 *  upload pass will only use a reference to store the data. Use caution with this flag since graph
	 *  execution is deferred! Useful to avoid the copy if the initial data lifetime is guaranteed to
	 *  outlive the graph.
	 */
	NoCopy = 0x1
};
ENUM_CLASS_FLAGS(ERDGInitialDataFlags)

/** Creates a structured buffer with initial data by creating an upload pass. */
RENDERCORE_API FRDGBufferRef CreateStructuredBuffer(
	FRDGBuilder& GraphBuilder,
	const TCHAR* Name,
	uint32 BytesPerElement,
	uint32 NumElements,
	const void* InitialData,
	uint64 InitialDataSize,
	ERDGInitialDataFlags InitialDataFlags = ERDGInitialDataFlags::None);


/**
 * Helper to create a structured buffer with initial data from a TArray.
 */
template <typename ElementType, typename AllocatorType>
FORCEINLINE FRDGBufferRef CreateStructuredBuffer(
	FRDGBuilder& GraphBuilder,
	const TCHAR* Name,
	const TArray<ElementType, AllocatorType>& InitialData,
	ERDGInitialDataFlags InitialDataFlags = ERDGInitialDataFlags::None)
{
	return CreateStructuredBuffer(GraphBuilder, Name, InitialData.GetTypeSize(), InitialData.Num(), InitialData.GetData(), InitialData.Num() * InitialData.GetTypeSize(), InitialDataFlags);
}


/** Creates a vertex buffer with initial data by creating an upload pass. */
RENDERCORE_API FRDGBufferRef CreateVertexBuffer(
	FRDGBuilder& GraphBuilder,
	const TCHAR* Name,
	const FRDGBufferDesc& Desc,
	const void* InitialData,
	uint64 InitialDataSize,
	ERDGInitialDataFlags InitialDataFlags = ERDGInitialDataFlags::None);

RENDERCORE_API void GetPooledFreeBuffer(
	FRHICommandList& RHICmdList,
	const FRDGBufferDesc& Desc,
	TRefCountPtr<FPooledRDGBuffer>& Out,
	const TCHAR* InDebugName);
