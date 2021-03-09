// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraphResources.h"
#include "RenderGraphBuilder.h"
#include "Shader.h"
#include "ShaderParameterStruct.h"
#include "ShaderParameterMacros.h"
#include "RHIGPUReadback.h"

class FGlobalShaderMap;

/** Returns whether the resource was produced by a prior pass. */
inline bool HasBeenProduced(FRDGParentResourceRef Resource)
{
	return Resource && Resource->HasBeenProduced();
}

/** Returns the texture if it was produced by a prior pass, or null otherwise. */
inline FRDGTextureRef GetIfProduced(FRDGTextureRef Texture, FRDGTextureRef FallbackTexture = nullptr)
{
	return HasBeenProduced(Texture) ? Texture : FallbackTexture;
}

/** Returns the buffer if has been produced by a prior pass, or null otherwise. */
inline FRDGBufferRef GetIfProduced(FRDGBufferRef Buffer, FRDGBufferRef FallbackBuffer = nullptr)
{
	return HasBeenProduced(Buffer) ? Buffer : FallbackBuffer;
}

/** Returns 'Load' if the texture has already been produced by a prior pass, or the requested initial action. */
inline ERenderTargetLoadAction GetLoadActionIfProduced(FRDGTextureRef Texture, ERenderTargetLoadAction ActionIfNotProduced)
{
	return HasBeenProduced(Texture) ? ERenderTargetLoadAction::ELoad : ActionIfNotProduced;
}

/** Returns a binding with the requested initial action, or a load action if the resource has been produced by a prior pass. */
inline FRenderTargetBinding GetLoadBindingIfProduced(FRDGTextureRef Texture, ERenderTargetLoadAction ActionIfNotProduced)
{
	return FRenderTargetBinding(Texture, GetLoadActionIfProduced(Texture, ActionIfNotProduced));
}

/** Returns the RHI texture from an RDG texture if it exists, or null otherwise. */
inline FRHITexture* TryGetRHI(FRDGTextureRef Texture)
{
	return Texture ? Texture->GetRHI() : nullptr;
}

/** Returns the pooled render target from an RDG texture if it exists, or null otherwise. */
UE_DEPRECATED(5.0, "Accessing the underlying pooled render target has been deprecated. Use TryGetRHI() instead.")
inline IPooledRenderTarget* TryGetPooledRenderTarget(FRDGTextureRef Texture)
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	return Texture ? Texture->GetPooledRenderTarget() : nullptr;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

inline FRenderTargetBindingSlots GetRenderTargetBindings(ERenderTargetLoadAction ColorLoadAction, TArrayView<FRDGTextureRef> ColorTextures)
{
	check(ColorTextures.Num() <= MaxSimultaneousRenderTargets);

	FRenderTargetBindingSlots BindingSlots;
	for (int32 Index = 0, Count = ColorTextures.Num(); Index < Count; ++Index)
	{
		check(ColorTextures[Index]);
		BindingSlots[Index] = FRenderTargetBinding(ColorTextures[Index], ColorLoadAction);
	}
	return BindingSlots;
}

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

template <typename TShaderClass>
void ClearUnusedGraphResources(
	const TShaderRef<TShaderClass>& Shader,
	const FShaderParametersMetadata* ParametersMetadata,
	typename TShaderClass::FParameters* InoutParameters,
	std::initializer_list<FRDGResourceRef> ExcludeList = {})
{
	// Verify the shader have all the parameters it needs. This is done before the
	// ClearUnusedGraphResourcesImpl() to not mislead user on why some resource are missing
	// when debugging a validation failure.
	ValidateShaderParameters(Shader, ParametersMetadata, InoutParameters);

	// Clear the resources the shader won't need.
	return ClearUnusedGraphResourcesImpl(Shader->Bindings, ParametersMetadata, InoutParameters, ExcludeList);
}

template <typename TShaderClass>
void ClearUnusedGraphResources(
	const TShaderRef<TShaderClass>& Shader,
	typename TShaderClass::FParameters* InoutParameters,
	std::initializer_list<FRDGResourceRef> ExcludeList = {})
{
	const FShaderParametersMetadata* ParametersMetadata = TShaderClass::FParameters::FTypeInfo::GetStructMetadata();
	return ClearUnusedGraphResources(Shader, ParametersMetadata, InoutParameters, MoveTemp(ExcludeList));
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
	ERenderTargetTexture ExternalTexture = ERenderTargetTexture::ShaderResource,
	ERenderTargetTexture FallbackTexture = ERenderTargetTexture::ShaderResource);

UE_DEPRECATED(4.26, "RegisterExternalTextureWithFallback no longer takes a Name. It uses name of the external texture instead.")
inline FRDGTextureRef RegisterExternalTextureWithFallback(
	FRDGBuilder& GraphBuilder,
	const TRefCountPtr<IPooledRenderTarget>& ExternalPooledTexture,
	const TRefCountPtr<IPooledRenderTarget>& FallbackPooledTexture,
	const TCHAR* ExternalPooledTextureName)
{
	return RegisterExternalTextureWithFallback(GraphBuilder, ExternalPooledTexture, FallbackPooledTexture);
}

/** Variants of RegisterExternalTexture which will returns null (rather than assert) if the external texture is null. */
inline FRDGTextureRef TryRegisterExternalTexture(
	FRDGBuilder& GraphBuilder,
	const TRefCountPtr<IPooledRenderTarget>& ExternalPooledTexture,
	ERenderTargetTexture RenderTargetTexture = ERenderTargetTexture::ShaderResource,
	ERDGTextureFlags Flags = ERDGTextureFlags::None)
{
	return ExternalPooledTexture ? GraphBuilder.RegisterExternalTexture(ExternalPooledTexture, RenderTargetTexture, Flags) : nullptr;
}

/** Variants of RegisterExternalBuffer which will return null (rather than assert) if the external buffer is null. */
inline FRDGBufferRef TryRegisterExternalBuffer(
	FRDGBuilder& GraphBuilder,
	const TRefCountPtr<FRDGPooledBuffer>& ExternalPooledBuffer,
	ERDGBufferFlags Flags = ERDGBufferFlags::None)
{
	return ExternalPooledBuffer ? GraphBuilder.RegisterExternalBuffer(ExternalPooledBuffer, Flags) : nullptr;
}

inline FRDGTextureRef RegisterExternalTexture(FRDGBuilder& GraphBuilder, FRHITexture* Texture, const TCHAR* NameIfUnregistered)
{
	if (FRDGTextureRef FoundTexture = GraphBuilder.FindExternalTexture(Texture))
	{
		return FoundTexture;
	}

	return GraphBuilder.RegisterExternalTexture(CreateRenderTarget(Texture, NameIfUnregistered));
}

/** Simple pair of RDG textures used for MSAA. */
struct FRDGTextureMSAA
{
	FRDGTextureMSAA() = default;

	FRDGTextureMSAA(FRDGTextureRef InTarget, FRDGTextureRef InResolve)
		: Target(InTarget)
		, Resolve(InResolve)
	{}

	FRDGTextureMSAA(FRDGTextureRef InTexture)
		: Target(InTexture)
		, Resolve(InTexture)
	{}

	bool IsValid() const
	{
		return Target != nullptr && Resolve != nullptr;
	}

	bool IsSeparate() const
	{
		return Target != Resolve;
	}

	bool operator==(FRDGTextureMSAA Other) const
	{
		return Target == Other.Target && Resolve == Other.Resolve;
	}

	bool operator!=(FRDGTextureMSAA Other) const
	{
		return !(*this == Other);
	}

	FRDGTextureRef Target = nullptr;
	FRDGTextureRef Resolve = nullptr;
};

RENDERCORE_API FRDGTextureMSAA CreateTextureMSAA(
	FRDGBuilder& GraphBuilder,
	FRDGTextureDesc Desc,
	const TCHAR* Name,
	ETextureCreateFlags ResolveFlagsToAdd = TexCreate_None);

inline FRDGTextureMSAA RegisterExternalTextureMSAA(
	FRDGBuilder& GraphBuilder,
	const TRefCountPtr<IPooledRenderTarget>& ExternalPooledTexture)
{
	return FRDGTextureMSAA(
		GraphBuilder.RegisterExternalTexture(ExternalPooledTexture, ERenderTargetTexture::Targetable),
		GraphBuilder.RegisterExternalTexture(ExternalPooledTexture, ERenderTargetTexture::ShaderResource));
}

inline FRDGTextureMSAA TryRegisterExternalTextureMSAA(
	FRDGBuilder& GraphBuilder,
	const TRefCountPtr<IPooledRenderTarget>& ExternalPooledTexture)
{
	return FRDGTextureMSAA(
		TryRegisterExternalTexture(GraphBuilder, ExternalPooledTexture, ERenderTargetTexture::Targetable),
		TryRegisterExternalTexture(GraphBuilder, ExternalPooledTexture, ERenderTargetTexture::ShaderResource));
}

RENDERCORE_API FRDGTextureMSAA RegisterExternalTextureMSAAWithFallback(
	FRDGBuilder& GraphBuilder,
	const TRefCountPtr<IPooledRenderTarget>& ExternalPooledTexture,
	const TRefCountPtr<IPooledRenderTarget>& FallbackPooledTexture);

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
	static void Dispatch(
		FRHIComputeCommandList& RHICmdList, 
		const TShaderRef<TShaderClass>& ComputeShader, 
		const FShaderParametersMetadata* ParametersMetadata,
		const typename TShaderClass::FParameters& Parameters,
		FIntVector GroupCount)
	{
		ValidateGroupCount(GroupCount);
		FRHIComputeShader* ShaderRHI = ComputeShader.GetComputeShader();
		RHICmdList.SetComputeShader(ShaderRHI);
		SetShaderParameters(RHICmdList, ComputeShader, ShaderRHI, ParametersMetadata, Parameters);
		RHICmdList.DispatchComputeShader(GroupCount.X, GroupCount.Y, GroupCount.Z);
		UnsetShaderUAVs(RHICmdList, ComputeShader, ShaderRHI);
	}

	template<typename TShaderClass>
	static void Dispatch(
		FRHIComputeCommandList& RHICmdList,
		const TShaderRef<TShaderClass>& ComputeShader,
		const typename TShaderClass::FParameters& Parameters,
		FIntVector GroupCount)
	{
		const FShaderParametersMetadata* ParametersMetadata = TShaderClass::FParameters::FTypeInfo::GetStructMetadata();
		Dispatch(RHICmdList, ComputeShader, ParametersMetadata, Parameters, GroupCount);
	}
	
	/** Indirect dispatch a compute shader to rhi command list with its parameters. */
	template<typename TShaderClass>
	static void DispatchIndirect(
		FRHIComputeCommandList& RHICmdList,
		const TShaderRef<TShaderClass>& ComputeShader,
		const typename TShaderClass::FParameters& Parameters,
		FRHIBuffer* IndirectArgsBuffer,
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
		FRHIComputeCommandList& RHICmdList,
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
		const FShaderParametersMetadata* ParametersMetadata,
		typename TShaderClass::FParameters* Parameters,
		FIntVector GroupCount)
	{
		checkf(
			 EnumHasAnyFlags(PassFlags, ERDGPassFlags::Compute | ERDGPassFlags::AsyncCompute) &&
			!EnumHasAnyFlags(PassFlags, ERDGPassFlags::Copy | ERDGPassFlags::Raster), TEXT("AddPass only supports 'Compute' or 'AsyncCompute'."));

		ValidateGroupCount(GroupCount);
		ClearUnusedGraphResources(ComputeShader, ParametersMetadata, Parameters);

		GraphBuilder.AddPass(
			Forward<FRDGEventName>(PassName),
			ParametersMetadata,
			Parameters,
			PassFlags,
			[ParametersMetadata, Parameters, ComputeShader, GroupCount](FRHIComputeCommandList& RHICmdList)
		{
			FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, ParametersMetadata, *Parameters, GroupCount);
		});
	}

	template<typename TShaderClass>
	static void AddPass(
		FRDGBuilder& GraphBuilder,
		FRDGEventName&& PassName,
		ERDGPassFlags PassFlags,
		const TShaderRef<TShaderClass>& ComputeShader,
		typename TShaderClass::FParameters* Parameters,
		FIntVector GroupCount)
	{
		const FShaderParametersMetadata* ParametersMetadata = TShaderClass::FParameters::FTypeInfo::GetStructMetadata();
		AddPass(GraphBuilder, Forward<FRDGEventName>(PassName), PassFlags, ComputeShader, ParametersMetadata, Parameters, GroupCount);
	}

	template <typename TShaderClass>
	static FORCEINLINE void AddPass(
		FRDGBuilder& GraphBuilder,
		FRDGEventName&& PassName,
		const TShaderRef<TShaderClass>& ComputeShader,
		typename TShaderClass::FParameters* Parameters,
		FIntVector GroupCount)
	{
		const FShaderParametersMetadata* ParametersMetadata = TShaderClass::FParameters::FTypeInfo::GetStructMetadata();
		AddPass(GraphBuilder, Forward<FRDGEventName>(PassName), ERDGPassFlags::Compute, ComputeShader, ParametersMetadata, Parameters, GroupCount);
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
		checkf(IndirectArgsBuffer->Desc.Usage & BUF_DrawIndirect, TEXT("The buffer %s was not flagged for indirect draw parameters"), IndirectArgsBuffer->Name);

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
		ensure(GroupCount.X <= GRHIMaxDispatchThreadGroupsPerDimension.X);
		ensure(GroupCount.Y <= GRHIMaxDispatchThreadGroupsPerDimension.Y);
		ensure(GroupCount.Z <= GRHIMaxDispatchThreadGroupsPerDimension.Z);
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
RENDERCORE_API void AddClearUAVFloatPass(FRDGBuilder& GraphBuilder, FRDGBufferUAVRef BufferUAV, float Value);

RENDERCORE_API void AddClearUAVPass(FRDGBuilder& GraphBuilder, FRDGTextureUAVRef TextureUAV, const FUintVector4& ClearValues);

RENDERCORE_API void AddClearUAVPass(FRDGBuilder& GraphBuilder, FRDGTextureUAVRef TextureUAV, const float(&ClearValues)[4]);

RENDERCORE_API void AddClearUAVPass(FRDGBuilder& GraphBuilder, FRDGTextureUAVRef TextureUAV, const uint32(&ClearValues)[4]);

RENDERCORE_API void AddClearUAVPass(FRDGBuilder& GraphBuilder, FRDGTextureUAVRef TextureUAV, const FLinearColor& ClearColor);

/** Clears parts of UAV specified by an array of screen rects. If no rects are specific, then it falls back to a standard UAV clear. */
RENDERCORE_API void AddClearUAVPass(FRDGBuilder& GraphBuilder, FRDGTextureUAVRef TextureUAV, const uint32(&ClearValues)[4], FRDGBufferSRVRef RectMinMaxBufferSRV, uint32 NumRects);

/** Adds a render graph pass to clear a render target to its clear value. */
RENDERCORE_API void AddClearRenderTargetPass(FRDGBuilder& GraphBuilder, FRDGTextureRef Texture);

/** Adds a render graph pass to clear a render target. Uses render pass clear actions if the clear color matches the fast clear color. */
RENDERCORE_API void AddClearRenderTargetPass(FRDGBuilder& GraphBuilder, FRDGTextureRef Texture, const FLinearColor& ClearColor);

/** Adds a render graph pass to clear a render target. Draws a quad to the requested viewport. */
RENDERCORE_API void AddClearRenderTargetPass(FRDGBuilder& GraphBuilder, FRDGTextureRef Texture, const FLinearColor& ClearColor, FIntRect Viewport);

/** Adds a render graph pass to clear a depth stencil target. Prefer to use clear actions if possible. */
RENDERCORE_API void AddClearDepthStencilPass(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef Texture,
	bool bClearDepth,
	float Depth,
	bool bClearStencil,
	uint8 Stencil);

/** Adds a render graph pass to clear a depth stencil target to its optimized clear value using a raster pass. */
RENDERCORE_API void AddClearDepthStencilPass(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef Texture,
	ERenderTargetLoadAction DepthLoadAction = ERenderTargetLoadAction::EClear,
	ERenderTargetLoadAction StencilLoadAction = ERenderTargetLoadAction::EClear);

/** Adds a render graph pass to clear the stencil portion of a depth / stencil target to its fast clear value. */
RENDERCORE_API void AddClearStencilPass(FRDGBuilder& GraphBuilder, FRDGTextureRef Texture);

/** Adds a render graph pass to resummarize the htile plane. */
RENDERCORE_API void AddResummarizeHTilePass(FRDGBuilder& GraphBuilder, FRDGTextureRef DepthTexture);

/** Adds a pass to readback contents of an RDG texture. */
RENDERCORE_API void AddEnqueueCopyPass(FRDGBuilder& GraphBuilder, FRHIGPUTextureReadback* Readback, FRDGTextureRef SourceTexture, FResolveRect Rect = FResolveRect());

/** Adds a pass to readback contents of an RDG buffer. */
RENDERCORE_API void AddEnqueueCopyPass(FRDGBuilder& GraphBuilder, FRHIGPUBufferReadback* Readback, FRDGBufferRef SourceBuffer, uint32 NumBytes);

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

/** Adds a buffer initial data upload pass. */
RENDERCORE_API void AddBufferUploadPass(
	FRDGBuilder& GraphBuilder,
	FRDGBufferRef Buffer,
	const void* InitialData,
	uint64 InitialDataSize,
	ERDGInitialDataFlags InitialDataFlags = ERDGInitialDataFlags::None);


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
	static const ElementType DummyElement = ElementType();
	if (InitialData.Num() == 0)
	{
		return CreateStructuredBuffer(GraphBuilder, Name, InitialData.GetTypeSize(), 1, &DummyElement, InitialData.GetTypeSize());
	}
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

/** Helper functions to add parameterless passes to the graph. */
template <typename ExecuteLambdaType>
FORCEINLINE void AddPass(FRDGBuilder& GraphBuilder, FRDGEventName&& Name, ExecuteLambdaType&& ExecuteLambda)
{
	GraphBuilder.AddPass(MoveTemp(Name), ERDGPassFlags::None, MoveTemp(ExecuteLambda));
}

template <typename ExecuteLambdaType>
FORCEINLINE void AddPass(FRDGBuilder& GraphBuilder, ExecuteLambdaType&& ExecuteLambda)
{
	AddPass(GraphBuilder, {}, MoveTemp(ExecuteLambda));
}

template <typename ExecuteLambdaType>
FORCEINLINE void AddPassIfDebug(FRDGBuilder& GraphBuilder, FRDGEventName&& Name, ExecuteLambdaType&& ExecuteLambda)
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	AddPass(GraphBuilder, MoveTemp(Name), MoveTemp(ExecuteLambda));
#endif
}

template <typename ExecuteLambdaType>
FORCEINLINE void AddPassIfDebug(FRDGBuilder& GraphBuilder, ExecuteLambdaType&& ExecuteLambda)
{
	AddPassIfDebug(GraphBuilder, {}, MoveTemp(ExecuteLambda));
}

UE_DEPRECATED(5.0, "AddSetCurrentStatPass is deprecated. Use GraphBuilder.SetCommandListStat instead.")
FORCEINLINE void AddSetCurrentStatPass(FRDGBuilder& GraphBuilder, TStatId StatId)
{
	GraphBuilder.SetCommandListStat(StatId);
}

FORCEINLINE void AddDispatchToRHIThreadPass(FRDGBuilder& GraphBuilder)
{
	AddPass(GraphBuilder, [](FRHICommandListImmediate& RHICmdList)
	{
		RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
	});
}

FORCEINLINE void AddBeginUAVOverlapPass(FRDGBuilder& GraphBuilder)
{
	AddPass(GraphBuilder, [](FRHICommandListImmediate& RHICmdList)
	{
		RHICmdList.BeginUAVOverlap();
	});
}

FORCEINLINE void AddEndUAVOverlapPass(FRDGBuilder& GraphBuilder)
{
	AddPass(GraphBuilder, [](FRHICommandListImmediate& RHICmdList)
	{
		RHICmdList.EndUAVOverlap();
	});
}

FORCEINLINE void AddBeginUAVOverlapPass(FRDGBuilder& GraphBuilder, FRHIUnorderedAccessView* UAV)
{
	AddPass(GraphBuilder, [UAV](FRHICommandListImmediate& RHICmdList)
	{
		RHICmdList.BeginUAVOverlap(UAV);
	});
}

FORCEINLINE void AddEndUAVOverlapPass(FRDGBuilder& GraphBuilder, FRHIUnorderedAccessView* UAV)
{
	AddPass(GraphBuilder, [UAV](FRHICommandListImmediate& RHICmdList)
	{
		RHICmdList.EndUAVOverlap(UAV);
	});
}

FORCEINLINE void AddBeginUAVOverlapPass(FRDGBuilder& GraphBuilder, TArrayView<FRHIUnorderedAccessView*> UAVs)
{
	uint32 AllocSize = UAVs.Num() * sizeof(FRHIUnorderedAccessView*);
	FRHIUnorderedAccessView** LocalUAVs = (FRHIUnorderedAccessView**)GraphBuilder.Alloc(AllocSize, alignof(FRHIUnorderedAccessView*));
	FMemory::Memcpy(LocalUAVs, UAVs.GetData(), AllocSize);
	TArrayView<FRHIUnorderedAccessView*> LocalView(LocalUAVs, UAVs.Num());
	AddPass(GraphBuilder, [LocalView](FRHICommandListImmediate& RHICmdList)
	{
		RHICmdList.BeginUAVOverlap(LocalView);
	});
}

FORCEINLINE void AddEndUAVOverlapPass(FRDGBuilder& GraphBuilder, TArrayView<FRHIUnorderedAccessView*> UAVs)
{
	uint32 AllocSize = UAVs.Num() * sizeof(FRHIUnorderedAccessView*);
	FRHIUnorderedAccessView** LocalUAVs = (FRHIUnorderedAccessView**)GraphBuilder.Alloc(AllocSize, alignof(FRHIUnorderedAccessView*));
	FMemory::Memcpy(LocalUAVs, UAVs.GetData(), AllocSize);
	TArrayView<FRHIUnorderedAccessView*> LocalView(LocalUAVs, UAVs.Num());
	AddPass(GraphBuilder, [LocalView](FRHICommandListImmediate& RHICmdList)
	{
		RHICmdList.EndUAVOverlap(LocalView);
	});
}

BEGIN_SHADER_PARAMETER_STRUCT(FReadbackTextureParameters, )
	RDG_TEXTURE_ACCESS(Texture, ERHIAccess::CopySrc)
END_SHADER_PARAMETER_STRUCT()

template <typename ExecuteLambdaType>
void AddReadbackTexturePass(FRDGBuilder& GraphBuilder, FRDGEventName&& Name, FRDGTextureRef Texture, ExecuteLambdaType&& ExecuteLambda)
{
	auto* PassParameters = GraphBuilder.AllocParameters<FReadbackTextureParameters>();
	PassParameters->Texture = Texture;
	GraphBuilder.AddPass(MoveTemp(Name), PassParameters, ERDGPassFlags::Readback, MoveTemp(ExecuteLambda));
}

BEGIN_SHADER_PARAMETER_STRUCT(FReadbackBufferParameters, )
	RDG_BUFFER_ACCESS(Buffer, ERHIAccess::CopySrc)
END_SHADER_PARAMETER_STRUCT()

template <typename ExecuteLambdaType>
void AddReadbackBufferPass(FRDGBuilder& GraphBuilder, FRDGEventName&& Name, FRDGBufferRef Buffer, ExecuteLambdaType&& ExecuteLambda)
{
	auto* PassParameters = GraphBuilder.AllocParameters<FReadbackBufferParameters>();
	PassParameters->Buffer = Buffer;
	GraphBuilder.AddPass(MoveTemp(Name), PassParameters, ERDGPassFlags::Readback, MoveTemp(ExecuteLambda));
}

// Forces the graph to make the resource immediately available as if it were registered. Returns the pooled resource.

inline void ConvertToExternalBuffer(FRDGBuilder& GraphBuilder, FRDGBufferRef Buffer, TRefCountPtr<FRDGPooledBuffer>& OutPooledBuffer)
{
	check(Buffer);
	GraphBuilder.PreallocateBuffer(Buffer);
	OutPooledBuffer = GraphBuilder.GetPooledBuffer(Buffer);
}

inline void ConvertToExternalTexture(FRDGBuilder& GraphBuilder, FRDGTextureRef Texture, TRefCountPtr<IPooledRenderTarget>& OutPooledRenderTarget)
{
	check(Texture);
	GraphBuilder.PreallocateTexture(Texture);
	OutPooledRenderTarget = GraphBuilder.GetPooledTexture(Texture);
}

// Forces a resource into its final state early in the graph. Useful if an external texture is produced by the graph but consumed
// later in the graph without being registered. For example, if the resource is read-only embedded in a non-RDG uniform buffer and
// or registration with RDG is too expensive or difficult.
RENDERCORE_API void ConvertToUntrackedTexture(FRDGBuilder& GraphBuilder, FRDGTextureRef Texture, ERHIAccess AccessFinal);
RENDERCORE_API void ConvertToUntrackedBuffer(FRDGBuilder& GraphBuilder, FRDGBufferRef Buffer, ERHIAccess AccessFinal);

inline void ConvertToUntrackedExternalTexture(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef Texture,
	TRefCountPtr<IPooledRenderTarget>& OutPooledRenderTarget,
	ERHIAccess AccessFinal)
{
	ConvertToExternalTexture(GraphBuilder, Texture, OutPooledRenderTarget);
	ConvertToUntrackedTexture(GraphBuilder, Texture, AccessFinal);
}

inline void ConvertToUntrackedExternalBuffer(
	FRDGBuilder& GraphBuilder,
	FRDGBufferRef Buffer,
	TRefCountPtr<FRDGPooledBuffer>& OutPooledBuffer,
	ERHIAccess AccessFinal)
{
	ConvertToExternalBuffer(GraphBuilder, Buffer, OutPooledBuffer);
	ConvertToUntrackedBuffer(GraphBuilder, Buffer, AccessFinal);
}

/** Scope used to wait for outstanding tasks when the scope destructor is called. Used for command list recording tasks. */
class FRDGWaitForTasksScope
{
public:
	FRDGWaitForTasksScope(FRDGBuilder& InGraphBuilder, bool InbCondition = true)
		: GraphBuilder(InGraphBuilder)
		, bCondition(InbCondition)
	{}

	RENDERCORE_API ~FRDGWaitForTasksScope();

private:
	FRDGBuilder& GraphBuilder;
	bool bCondition;
};

#define RDG_WAIT_FOR_TASKS_CONDITIONAL(GraphBuilder, bCondition) FRDGWaitForTasksScope PREPROCESSOR_JOIN(RDGWaitForTasksScope, __LINE__){ GraphBuilder, bCondition }
#define RDG_WAIT_FOR_TASKS(GraphBuilder) RDG_WAIT_FOR_TASKS_CONDITIONAL(GraphBuilder, true)

// Allocates an RDG pooled buffer instance. Attempts to reuse allocation if Out has a value. Returns true a new instance was allocated, or false if the existing allocation was reused.
RENDERCORE_API bool GetPooledFreeBuffer(
	FRHICommandList& RHICmdList,
	const FRDGBufferDesc& Desc,
	TRefCountPtr<FRDGPooledBuffer>& Out,
	const TCHAR* InDebugName);