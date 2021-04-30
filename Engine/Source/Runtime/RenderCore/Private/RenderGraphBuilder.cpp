// Copyright Epic Games, Inc. All Rights Reserved.

#include "RenderGraphBuilder.h"
#include "RenderGraphPrivate.h"
#include "RenderTargetPool.h"
#include "RenderGraphResourcePool.h"
#include "VisualizeTexture.h"
#include "ProfilingDebugging/CsvProfiler.h"

inline ERHIAccess MakeValidAccess(ERHIAccess Access)
{
	// If we find any write states in the access mask, remove all read-only states. This mainly exists
	// to allow RDG uniform buffers to contain read-only parameters which are also bound for write on the
	// pass. Often times these uniform buffers are created and only relevant things are accessed. If an
	// invalid access does occur, the RHI validation layer will catch it.
	return IsWritableAccess(Access) ? (Access & ~ERHIAccess::ReadOnlyExclusiveMask) : Access;
}

inline void GetPassAccess(ERDGPassFlags PassFlags, ERHIAccess& SRVAccess, ERHIAccess& UAVAccess)
{
	SRVAccess = ERHIAccess::Unknown;
	UAVAccess = ERHIAccess::Unknown;

	if (EnumHasAnyFlags(PassFlags, ERDGPassFlags::Raster))
	{
		SRVAccess |= ERHIAccess::SRVGraphics;
		UAVAccess |= ERHIAccess::UAVGraphics;
	}

	if (EnumHasAnyFlags(PassFlags, ERDGPassFlags::AsyncCompute | ERDGPassFlags::Compute))
	{
		SRVAccess |= ERHIAccess::SRVCompute;
		UAVAccess |= ERHIAccess::UAVCompute;
	}

	if (EnumHasAnyFlags(PassFlags, ERDGPassFlags::Copy))
	{
		SRVAccess |= ERHIAccess::CopySrc;
	}
}

/** Enumerates all texture accesses and provides the access and subresource range info. This results in
 *  multiple invocations of the same resource, but with different access / subresource range.
 */
template <typename TAccessFunction>
void EnumerateTextureAccess(FRDGParameterStruct PassParameters, ERDGPassFlags PassFlags, TAccessFunction AccessFunction)
{
	ERHIAccess SRVAccess, UAVAccess;
	GetPassAccess(PassFlags, SRVAccess, UAVAccess);

	PassParameters.EnumerateTextures([&](FRDGParameter Parameter)
	{
		switch (Parameter.GetType())
		{
		case UBMT_RDG_TEXTURE:
			if (FRDGTextureRef Texture = Parameter.GetAsTexture())
			{
				AccessFunction(nullptr, Texture, SRVAccess, Texture->GetSubresourceRangeSRV());
			}
		break;
		case UBMT_RDG_TEXTURE_ACCESS:
		{
			if (FRDGTextureAccess TextureAccess = Parameter.GetAsTextureAccess())
			{
				AccessFunction(nullptr, TextureAccess.GetTexture(), TextureAccess.GetAccess(), TextureAccess.GetTexture()->GetSubresourceRange());
			}
		}
		break;
		case UBMT_RDG_TEXTURE_SRV:
			if (FRDGTextureSRVRef SRV = Parameter.GetAsTextureSRV())
			{
				AccessFunction(SRV, SRV->GetParent(), SRVAccess, SRV->GetSubresourceRange());
			}
		break;
		case UBMT_RDG_TEXTURE_UAV:
			if (FRDGTextureUAVRef UAV = Parameter.GetAsTextureUAV())
			{
				AccessFunction(UAV, UAV->GetParent(), UAVAccess, UAV->GetSubresourceRange());
			}
		break;
		case UBMT_RENDER_TARGET_BINDING_SLOTS:
		{
			const ERHIAccess RTVAccess = ERHIAccess::RTV;

			const FRenderTargetBindingSlots& RenderTargets = Parameter.GetAsRenderTargetBindingSlots();

			RenderTargets.Enumerate([&](FRenderTargetBinding RenderTarget)
			{
				FRDGTextureRef Texture = RenderTarget.GetTexture();
				FRDGTextureRef ResolveTexture = RenderTarget.GetResolveTexture();

				FRDGTextureSubresourceRange Range(Texture->GetSubresourceRange());
				Range.MipIndex = RenderTarget.GetMipIndex();
				Range.NumMips = 1;

				if (RenderTarget.GetArraySlice() != -1)
				{
					Range.ArraySlice = RenderTarget.GetArraySlice();
					Range.NumArraySlices = 1;
				}

				AccessFunction(nullptr, Texture, RTVAccess, Range);

				if (ResolveTexture && ResolveTexture != Texture)
				{
					// Resolve targets must use the RTV|ResolveDst flag combination when the resolve is performed through the render
					// pass. The ResolveDst flag must be used alone only when the resolve is performed using RHICopyToResolveTarget.
					AccessFunction(nullptr, ResolveTexture, ERHIAccess::RTV | ERHIAccess::ResolveDst, Range);
				}
			});

			const FDepthStencilBinding& DepthStencil = RenderTargets.DepthStencil;

			if (FRDGTextureRef Texture = DepthStencil.GetTexture())
			{
				DepthStencil.GetDepthStencilAccess().EnumerateSubresources([&](ERHIAccess NewAccess, uint32 PlaneSlice)
				{
					FRDGTextureSubresourceRange Range = Texture->GetSubresourceRange();

					// Adjust the range to use a single plane slice if not using of them all.
					if (PlaneSlice != FRHITransitionInfo::kAllSubresources)
					{
						Range.PlaneSlice = PlaneSlice;
						Range.NumPlaneSlices = 1;
					}

					AccessFunction(nullptr, Texture, NewAccess, Range);
				});
			}

			if (FRDGTextureRef Texture = RenderTargets.ShadingRateTexture)
			{
				AccessFunction(nullptr, Texture, ERHIAccess::ShadingRateSource, Texture->GetSubresourceRangeSRV());
			}
		}
		break;
		}
	});
}

/** Enumerates all texture pass parameters and calls the provided function. The input function must accept an FRDGResource*,
 *  since either views or textures are provided.
 */
template <typename TParameterFunction>
void EnumerateTextureParameters(FRDGParameterStruct PassParameters, TParameterFunction ParameterFunction)
{
	PassParameters.EnumerateTextures([&](FRDGParameter Parameter)
	{
		switch (Parameter.GetType())
		{
		case UBMT_RDG_TEXTURE:
		case UBMT_RDG_TEXTURE_ACCESS:
			if (FRDGTextureRef Texture = Parameter.GetAsTexture())
			{
				ParameterFunction(Texture);
			}
		break;
		case UBMT_RDG_TEXTURE_SRV:
			if (FRDGTextureSRVRef SRV = Parameter.GetAsTextureSRV())
			{
				ParameterFunction(SRV);
			}
		break;
		case UBMT_RDG_TEXTURE_UAV:
			if (FRDGTextureUAVRef UAV = Parameter.GetAsTextureUAV())
			{
				ParameterFunction(UAV);
			}
		break;
		case UBMT_RENDER_TARGET_BINDING_SLOTS:
		{
			const FRenderTargetBindingSlots& RenderTargets = Parameter.GetAsRenderTargetBindingSlots();

			RenderTargets.Enumerate([&](FRenderTargetBinding RenderTarget)
			{
				ParameterFunction(RenderTarget.GetTexture());

				if (RenderTarget.GetResolveTexture())
				{
					ParameterFunction(RenderTarget.GetResolveTexture());
				}
			});

			if (FRDGTextureRef Texture = RenderTargets.DepthStencil.GetTexture())
			{
				ParameterFunction(Texture);
			}

			if (FRDGTextureRef Texture = RenderTargets.ShadingRateTexture)
			{
				ParameterFunction(Texture);
			}
		}
		break;
		}
	});
}

/** Enumerates all buffer accesses and provides the access info. */
template <typename TAccessFunction>
void EnumerateBufferAccess(FRDGParameterStruct PassParameters, ERDGPassFlags PassFlags, TAccessFunction AccessFunction)
{
	ERHIAccess SRVAccess, UAVAccess;
	GetPassAccess(PassFlags, SRVAccess, UAVAccess);

	PassParameters.EnumerateBuffers([&](FRDGParameter Parameter)
	{
		switch (Parameter.GetType())
		{
		case UBMT_RDG_BUFFER:
			if (FRDGBufferRef Buffer = Parameter.GetAsBuffer())
			{
				ERHIAccess BufferAccess = SRVAccess;

				if (EnumHasAnyFlags(Buffer->Desc.Usage, BUF_DrawIndirect))
				{
					BufferAccess |= ERHIAccess::IndirectArgs;
				}

				AccessFunction(nullptr, Buffer, BufferAccess);
			}
		break;
		case UBMT_RDG_BUFFER_ACCESS:
			if (FRDGBufferAccess BufferAccess = Parameter.GetAsBufferAccess())
			{
				AccessFunction(nullptr, BufferAccess.GetBuffer(), BufferAccess.GetAccess());
			}
		break;
		case UBMT_RDG_BUFFER_SRV:
			if (FRDGBufferSRVRef SRV = Parameter.GetAsBufferSRV())
			{
				AccessFunction(SRV, SRV->GetParent(), SRVAccess);
			}
		break;
		case UBMT_RDG_BUFFER_UAV:
			if (FRDGBufferUAVRef UAV = Parameter.GetAsBufferUAV())
			{
				AccessFunction(UAV, UAV->GetParent(), UAVAccess);
			}
		break;
		}
	});
}

/** Enumerates all buffer pass parameters and calls the provided function. The input function must accept an FRDGResource*,
 *  since either views or textures are provided.
 */
template <typename TParameterFunction>
void EnumerateBufferParameters(FRDGParameterStruct PassParameters, TParameterFunction ParameterFunction)
{
	PassParameters.EnumerateBuffers([&](FRDGParameter Parameter)
	{
		switch (Parameter.GetType())
		{
		case UBMT_RDG_BUFFER:
		case UBMT_RDG_BUFFER_ACCESS:
			if (FRDGBufferRef Buffer = Parameter.GetAsBuffer())
			{
				ParameterFunction(Buffer);
			}
		break;
		case UBMT_RDG_BUFFER_SRV:
			if (FRDGBufferSRVRef SRV = Parameter.GetAsBufferSRV())
			{
				ParameterFunction(SRV);
			}
		break;
		case UBMT_RDG_BUFFER_UAV:
			if (FRDGBufferUAVRef UAV = Parameter.GetAsBufferUAV())
			{
				ParameterFunction(UAV);
			}
		break;
		}
	});
}

inline FRDGViewHandle GetHandleIfNoUAVBarrier(FRDGViewRef Resource)
{
	if (Resource && (Resource->Type == ERDGViewType::BufferUAV || Resource->Type == ERDGViewType::TextureUAV))
	{
		if (EnumHasAnyFlags(static_cast<FRDGUnorderedAccessViewRef>(Resource)->Flags, ERDGUnorderedAccessViewFlags::SkipBarrier))
		{
			return Resource->GetHandle();
		}
	}
	return FRDGViewHandle::Null;
}

inline EResourceTransitionFlags GetTextureViewTransitionFlags(FRDGViewRef Resource, FRDGTextureRef Texture)
{
	if (Resource)
	{
		switch (Resource->Type)
		{
		case ERDGViewType::TextureUAV:
		{
			FRDGTextureUAVRef UAV = static_cast<FRDGTextureUAVRef>(Resource);
			if (UAV->Desc.MetaData != ERDGTextureMetaDataAccess::None)
			{
				return EResourceTransitionFlags::MaintainCompression;
			}
		}
		break;
		case ERDGViewType::TextureSRV:
		{
			FRDGTextureSRVRef SRV = static_cast<FRDGTextureSRVRef>(Resource);
			if (SRV->Desc.MetaData != ERDGTextureMetaDataAccess::None)
			{
				return EResourceTransitionFlags::MaintainCompression;
			}
		}
		break;
		}
	}
	else
	{
		if (EnumHasAnyFlags(Texture->Flags, ERDGTextureFlags::MaintainCompression))
		{
			return EResourceTransitionFlags::MaintainCompression;
		}
	}
	return EResourceTransitionFlags::None;
}

void FRDGBuilder::TickPoolElements()
{
	GRenderGraphResourcePool.TickPoolElements();

#if RDG_ENABLE_DEBUG
	if (GRDGDumpGraph)
	{
		--GRDGDumpGraph;
	}
	if (GRDGTransitionLog > 0)
	{
		--GRDGTransitionLog;
	}
	GRDGDumpGraphUnknownCount = 0;
#endif

#if STATS
	SET_DWORD_STAT(STAT_RDG_PassCount, GRDGStatPassCount);
	SET_DWORD_STAT(STAT_RDG_PassCullCount, GRDGStatPassCullCount);
	SET_DWORD_STAT(STAT_RDG_RenderPassMergeCount, GRDGStatRenderPassMergeCount);
	SET_DWORD_STAT(STAT_RDG_PassDependencyCount, GRDGStatPassDependencyCount);
	SET_DWORD_STAT(STAT_RDG_TextureCount, GRDGStatTextureCount);
	SET_DWORD_STAT(STAT_RDG_BufferCount, GRDGStatBufferCount);
	SET_DWORD_STAT(STAT_RDG_TransitionCount, GRDGStatTransitionCount);
	SET_DWORD_STAT(STAT_RDG_TransitionBatchCount, GRDGStatTransitionBatchCount);
	SET_MEMORY_STAT(STAT_RDG_MemoryWatermark, int64(GRDGStatMemoryWatermark));
	GRDGStatPassCount = 0;
	GRDGStatPassCullCount = 0;
	GRDGStatRenderPassMergeCount = 0;
	GRDGStatPassDependencyCount = 0;
	GRDGStatTextureCount = 0;
	GRDGStatBufferCount = 0;
	GRDGStatTransitionCount = 0;
	GRDGStatTransitionBatchCount = 0;
	GRDGStatMemoryWatermark = 0;
#endif
}

ERDGPassFlags FRDGBuilder::OverridePassFlags(const TCHAR* PassName, ERDGPassFlags PassFlags, bool bAsyncComputeSupported)
{
	const bool bDebugAllowedForPass =
#if RDG_ENABLE_DEBUG
		IsDebugAllowedForPass(PassName);
#else
		true;
#endif

	const bool bGlobalForceAsyncCompute = (GRDGAsyncCompute == RDG_ASYNC_COMPUTE_FORCE_ENABLED && !GRDGImmediateMode && bDebugAllowedForPass);
	bAsyncComputeSupported &= !EnumHasAnyFlags(PassFlags, ERDGPassFlags::UntrackedAccess);

	if (EnumHasAnyFlags(PassFlags, ERDGPassFlags::Compute) && (bGlobalForceAsyncCompute))
	{
		PassFlags &= ~ERDGPassFlags::Compute;
		PassFlags |= ERDGPassFlags::AsyncCompute;
	}

	if (EnumHasAnyFlags(PassFlags, ERDGPassFlags::AsyncCompute) && (GRDGAsyncCompute == RDG_ASYNC_COMPUTE_DISABLED || GRDGImmediateMode || !bAsyncComputeSupported))
	{
		PassFlags &= ~ERDGPassFlags::AsyncCompute;
		PassFlags |= ERDGPassFlags::Compute;
	}

	return PassFlags;
}

const char* const FRDGBuilder::kDefaultUnaccountedCSVStat = "RDG_Pass";

FRDGBuilder::FRDGBuilder(FRHICommandListImmediate& InRHICmdList, FRDGEventName InName, const char* UnaccountedCSVStat)
	: RHICmdList(InRHICmdList)
	, RHICmdListAsyncCompute(FRHICommandListExecutor::GetImmediateAsyncComputeCommandList())
	, Blackboard(Allocator)
	, BuilderName(InName)
#if RDG_CPU_SCOPES
	, CPUScopeStacks(RHICmdList, UnaccountedCSVStat)
#endif
#if RDG_GPU_SCOPES
	, GPUScopeStacks(RHICmdList, RHICmdListAsyncCompute)
#endif
#if RDG_ENABLE_DEBUG
	, BarrierValidation(&Passes, BuilderName)
#endif
{
	ProloguePass = Passes.Allocate<FRDGSentinelPass>(Allocator, RDG_EVENT_NAME("Graph Prologue"));
	SetupEmptyPass(ProloguePass);
}

void FRDGBuilder::PreallocateBuffer(FRDGBufferRef Buffer)
{
	if (!Buffer->bExternal)
	{
		Buffer->bExternal = 1;
		Buffer->AccessFinal = kDefaultAccessFinal;
		BeginResourceRHI(GetProloguePassHandle(), Buffer);
		ExternalBuffers.Add(Buffer->PooledBuffer, Buffer);
	}
}

void FRDGBuilder::PreallocateTexture(FRDGTextureRef Texture)
{
	if (!Texture->bExternal)
	{
		Texture->bExternal = 1;
		Texture->AccessFinal = kDefaultAccessFinal;
		BeginResourceRHI(GetProloguePassHandle(), Texture);
		ExternalTextures.Add(Texture->GetRHIUnchecked(), Texture);
	}
}

FRDGTextureRef FRDGBuilder::CreateTexture(
	const FRDGTextureDesc& Desc,
	const TCHAR* Name,
	ERDGTextureFlags Flags)
{
#if RDG_ENABLE_DEBUG
	{
		checkf(Name, TEXT("Creating a texture requires a valid debug name."));
		UserValidation.ExecuteGuard(TEXT("CreateTexture"), Name);

		// Validate the pixel format.
		checkf(Desc.Format != PF_Unknown, TEXT("Illegal to create texture %s with an invalid pixel format."), Name);
		checkf(Desc.Format < PF_MAX, TEXT("Illegal to create texture %s with invalid FPooledRenderTargetDesc::Format."), Name);
		checkf(GPixelFormats[Desc.Format].Supported,
			TEXT("Failed to create texture %s with pixel format %s because it is not supported."), Name, GPixelFormats[Desc.Format].Name);
		checkf(Desc.IsValid(), TEXT("Texture %s was created with an invalid descriptor."), Name);

		const bool bCanHaveUAV = (Desc.Flags & TexCreate_UAV) > 0;
		const bool bIsMSAA = Desc.NumSamples > 1;

		// D3D11 doesn't allow creating a UAV on MSAA texture.
		const bool bIsUAVForMSAATexture = bIsMSAA && bCanHaveUAV;
		checkf(!bIsUAVForMSAATexture, TEXT("TexCreate_UAV is not allowed on MSAA texture %s."), Name);
	}
#endif

	FRDGTextureDesc TransientDesc = Desc;

	if (FRenderTargetPool::DoesTargetNeedTransienceOverride(Desc.Flags, ERenderTargetTransience::Transient))
	{
		TransientDesc.Flags |= TexCreate_Transient;
	}

	FRDGTextureRef Texture = Textures.Allocate(Allocator, Name, TransientDesc, Flags, ERenderTargetTexture::ShaderResource);
	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateCreateTexture(Texture));
	return Texture;
}

FRDGTextureRef FRDGBuilder::RegisterExternalTexture(
	const TRefCountPtr<IPooledRenderTarget>& ExternalPooledTexture,
	ERenderTargetTexture RenderTargetTexture,
	ERDGTextureFlags Flags)
{
#if RDG_ENABLE_DEBUG
	checkf(ExternalPooledTexture.IsValid(), TEXT("Attempted to register NULL external texture."));
#endif

	const TCHAR* Name = ExternalPooledTexture->GetDesc().DebugName;
	if (!Name)
	{
		Name = TEXT("External");
	}
	return RegisterExternalTexture(ExternalPooledTexture, Name, RenderTargetTexture, Flags);
}

FRDGTextureRef FRDGBuilder::RegisterExternalTexture(
	const TRefCountPtr<IPooledRenderTarget>& ExternalPooledTexture,
	const TCHAR* Name,
	ERenderTargetTexture RenderTargetTexture,
	ERDGTextureFlags Flags)
{
#if RDG_ENABLE_DEBUG
	checkf(Name, TEXT("Attempted to register external texture with NULL name."));
	checkf(ExternalPooledTexture.IsValid(), TEXT("Attempted to register NULL external texture."));
	checkf(ExternalPooledTexture->IsCompatibleWithRDG(), TEXT("Pooled render target %s is not a compatible type for RDG."), Name);
	UserValidation.ExecuteGuard(TEXT("RegisterExternalTexture"), Name);
#endif

	FRHITexture* ExternalTextureRHI = ExternalPooledTexture->GetRenderTargetItem().GetRHI(RenderTargetTexture);
	IF_RDG_ENABLE_DEBUG(checkf(ExternalTextureRHI, TEXT("Attempted to register texture %s, but its RHI texture is null."), Name));

	if (FRDGTextureRef FoundTexture = FindExternalTexture(ExternalTextureRHI))
	{
#if RDG_ENABLE_DEBUG
		checkf(FoundTexture->Flags == Flags, TEXT("External texture %s is already registered, but with different resource flags."), Name);
#endif
		return FoundTexture;
	}

	FRDGTextureRef Texture = Textures.Allocate(Allocator, Name, Translate(ExternalPooledTexture->GetDesc(), RenderTargetTexture), Flags, RenderTargetTexture);

	FRDGTextureRef PreviousOwner = nullptr;
	Texture->SetRHI(static_cast<FPooledRenderTarget*>(ExternalPooledTexture.GetReference()), PreviousOwner);
	checkf(!PreviousOwner,
		TEXT("Externally registered texture '%s' has a previous RDG texture owner '%s'. This can happen if two RDG builder instances register the same resource."),
		Name, PreviousOwner->Name);

	const ERHIAccess AccessInitial = kDefaultAccessInitial;

	Texture->bExternal = true;
	Texture->AccessInitial = AccessInitial;
	Texture->AccessFinal = EnumHasAnyFlags(ExternalTextureRHI->GetFlags(), TexCreate_Foveation)? ERHIAccess::ShadingRateSource : kDefaultAccessFinal;
	Texture->FirstPass = GetProloguePassHandle();

	FRDGTextureSubresourceState& TextureState = Texture->GetState();

	checkf(IsWholeResource(TextureState) && GetWholeResource(TextureState).Access == ERHIAccess::Unknown,
		TEXT("Externally registered texture '%s' has known RDG state. This means the graph did not sanitize it correctly, or ")
		TEXT("an IPooledRenderTarget reference was improperly held within a pass."), Texture->Name);

	{
		FRDGSubresourceState SubresourceState;
		SubresourceState.Access = AccessInitial;
		InitAsWholeResource(TextureState, SubresourceState);
	}

	ExternalTextures.Add(Texture->GetRHIUnchecked(), Texture);

	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateCreateExternalTexture(Texture));
	return Texture;
}

FRDGBufferRef FRDGBuilder::RegisterExternalBuffer(const TRefCountPtr<FRDGPooledBuffer>& ExternalPooledBuffer, ERDGBufferFlags Flags)
{
#if RDG_ENABLE_DEBUG
	checkf(ExternalPooledBuffer.IsValid(), TEXT("Attempted to register NULL external buffer."));
#endif

	const TCHAR* Name = ExternalPooledBuffer->Name;
	if (!Name)
	{
		Name = TEXT("External");
	}
	return RegisterExternalBuffer(ExternalPooledBuffer, Name, Flags);
}

FRDGBufferRef FRDGBuilder::RegisterExternalBuffer(
	const TRefCountPtr<FRDGPooledBuffer>& ExternalPooledBuffer,
	const TCHAR* Name,
	ERDGBufferFlags Flags)
{
#if RDG_ENABLE_DEBUG
	checkf(Name, TEXT("Attempted to register external buffer with NULL name."));
	checkf(ExternalPooledBuffer.IsValid(), TEXT("Attempted to register NULL external buffer."));
	UserValidation.ExecuteGuard(TEXT("RegisterExternalBuffer"), Name);
#endif

	if (FRDGBufferRef* FoundBufferPtr = ExternalBuffers.Find(ExternalPooledBuffer.GetReference()))
	{
		FRDGBufferRef FoundBuffer = *FoundBufferPtr;
#if RDG_ENABLE_DEBUG
		checkf(FoundBuffer->Flags == Flags, TEXT("External buffer %s is already registered, but with different resource flags."), Name);
#endif
		return FoundBuffer;
	}

	FRDGBufferRef Buffer = Buffers.Allocate(Allocator, Name, ExternalPooledBuffer->Desc, Flags);

	FRDGBufferRef PreviousOwner = nullptr;
	Buffer->SetRHI(ExternalPooledBuffer, PreviousOwner);
	checkf(!PreviousOwner,
		TEXT("Externally registered buffer '%s' has a previous RDG buffer owner '%s'. This can happen if two RDG builder instances register the same resource."),
		Name, PreviousOwner->Name);

	const ERHIAccess AccessInitial = kDefaultAccessInitial;

	Buffer->bExternal = true;
	Buffer->AccessInitial = AccessInitial;
	Buffer->AccessFinal = kDefaultAccessFinal;
	Buffer->FirstPass = GetProloguePassHandle();

	FRDGSubresourceState& BufferState = Buffer->GetState();
	checkf(BufferState.Access == ERHIAccess::Unknown,
		TEXT("Externally registered buffer '%s' has known RDG state. This means the graph did not sanitize it correctly, or ")
		TEXT("an FRDGPooledBuffer reference was improperly held within a pass."), Buffer->Name);
	BufferState.Access = AccessInitial;

	ExternalBuffers.Add(ExternalPooledBuffer, Buffer);

	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateCreateExternalBuffer(Buffer));
	return Buffer;
}

void FRDGBuilder::AddPassDependency(FRDGPassHandle ProducerHandle, FRDGPassHandle ConsumerHandle)
{
	checkf(ProducerHandle.IsValid(), TEXT("AddPassDependency called with null producer."));
	checkf(ConsumerHandle.IsValid(), TEXT("AddPassDependency called with null consumer."));
	FRDGPass* Consumer = Passes[ConsumerHandle];

	auto& Producers = Consumer->Producers;
	if (Producers.Find(ProducerHandle) == INDEX_NONE)
	{
		Producers.Add(ProducerHandle);

#if STATS
		GRDGStatPassDependencyCount++;
#endif
	}
};

void FRDGBuilder::Compile()
{
	SCOPE_CYCLE_COUNTER(STAT_RDG_CompileTime);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE_CONDITIONAL(RDG_Compile, GRDGVerboseCSVStats != 0);
	SCOPED_NAMED_EVENT(FRDGBuilder_Compile, FColor::Emerald);

	uint32 RasterPassCount = 0;
	uint32 AsyncComputePassCount = 0;

	FRDGPassBitArray PassesOnAsyncCompute(false, Passes.Num());
	FRDGPassBitArray PassesOnRaster(false, Passes.Num());
	FRDGPassBitArray PassesWithUntrackedOutputs(false, Passes.Num());
	FRDGPassBitArray PassesToNeverCull(false, Passes.Num());

	const FRDGPassHandle ProloguePassHandle = GetProloguePassHandle();
	const FRDGPassHandle EpiloguePassHandle = GetEpiloguePassHandle();

	const auto IsCrossPipeline = [&](FRDGPassHandle A, FRDGPassHandle B)
	{
		return PassesOnAsyncCompute[A] != PassesOnAsyncCompute[B];
	};

	const auto IsSortedBefore = [&](FRDGPassHandle A, FRDGPassHandle B)
	{
		return A < B;
	};

	const auto IsSortedAfter = [&](FRDGPassHandle A, FRDGPassHandle B)
	{
		return A > B;
	};

	// Build producer / consumer dependencies across the graph and construct packed bit-arrays of metadata
	// for better cache coherency when searching for passes meeting specific criteria. Search roots are also
	// identified for culling. Passes with untracked RHI output (e.g. SHADER_PARAMETER_{BUFFER, TEXTURE}_UAV)
	// cannot be culled, nor can any pass which writes to an external resource. Resource extractions extend the
	// lifetime to the epilogue pass which is always a root of the graph. The prologue and epilogue are helper
	// passes and therefore never culled.

	{
		SCOPED_NAMED_EVENT(FRDGBuilder_Compile_Culling_Dependencies, FColor::Emerald);

		const auto AddCullingDependency = [&](FRDGPassHandle& ProducerHandle, FRDGPassHandle PassHandle, ERHIAccess Access)
		{
			if (Access != ERHIAccess::Unknown)
			{
				if (ProducerHandle.IsValid())
				{
					AddPassDependency(ProducerHandle, PassHandle);
				}

				// If the access is writable, we store the new producer.
				if (IsWritableAccess(Access))
				{
					ProducerHandle = PassHandle;
				}
			}
		};

		for (FRDGPassHandle PassHandle = Passes.Begin(); PassHandle != Passes.End(); ++PassHandle)
		{
			FRDGPass* Pass = Passes[PassHandle];

			bool bUntrackedOutputs = Pass->GetParameters().HasExternalOutputs();

			for (auto& TexturePair : Pass->TextureStates)
			{
				FRDGTextureRef Texture = TexturePair.Key;
				auto& LastProducers = Texture->LastProducers;
				auto& PassState = TexturePair.Value.State;

				const bool bWholePassState = IsWholeResource(PassState);
				const bool bWholeProducers = IsWholeResource(LastProducers);

				// The producer array needs to be at least as large as the pass state array.
				if (bWholeProducers && !bWholePassState)
				{
					InitAsSubresources(LastProducers, Texture->Layout);
				}

				for (uint32 Index = 0, Count = LastProducers.Num(); Index < Count; ++Index)
				{
					AddCullingDependency(LastProducers[Index], PassHandle, PassState[bWholePassState ? 0 : Index].Access);
				}

				bUntrackedOutputs |= Texture->bExternal;
			}

			for (auto& BufferPair : Pass->BufferStates)
			{
				FRDGBufferRef Buffer = BufferPair.Key;
				AddCullingDependency(Buffer->LastProducer, PassHandle, BufferPair.Value.State.Access);
				bUntrackedOutputs |= Buffer->bExternal;
			}

			const ERDGPassFlags PassFlags = Pass->GetFlags();
			const bool bAsyncCompute = EnumHasAnyFlags(PassFlags, ERDGPassFlags::AsyncCompute);
			const bool bRaster = EnumHasAnyFlags(PassFlags, ERDGPassFlags::Raster);
			const bool bNeverCull = EnumHasAnyFlags(PassFlags, ERDGPassFlags::NeverCull);

			PassesOnRaster[PassHandle] = bRaster;
			PassesOnAsyncCompute[PassHandle] = bAsyncCompute;
			PassesToNeverCull[PassHandle] = bNeverCull;
			PassesWithUntrackedOutputs[PassHandle] = bUntrackedOutputs;
			AsyncComputePassCount += bAsyncCompute ? 1 : 0;
			RasterPassCount += bRaster ? 1 : 0;
		}

		// The prologue / epilogue is responsible for external resource import / export, respectively.
		PassesWithUntrackedOutputs[ProloguePassHandle] = true;
		PassesWithUntrackedOutputs[EpiloguePassHandle] = true;

		for (const auto& Query : ExtractedTextures)
		{
			FRDGTextureRef Texture = Query.Key;
			for (FRDGPassHandle& ProducerHandle : Texture->LastProducers)
			{
				AddCullingDependency(ProducerHandle, EpiloguePassHandle, Texture->AccessFinal);
			}
			Texture->ReferenceCount++;
		}

		for (const auto& Query : ExtractedBuffers)
		{
			FRDGBufferRef Buffer = Query.Key;
			AddCullingDependency(Buffer->LastProducer, EpiloguePassHandle, Buffer->AccessFinal);
			Buffer->ReferenceCount++;
		}
	}

	// All dependencies in the raw graph have been specified; if enabled, all passes are marked as culled and a
	// depth first search is employed to find reachable regions of the graph. Roots of the search are those passes
	// with outputs leaving the graph or those marked to never cull.

	if (GRDGCullPasses)
	{
		SCOPED_NAMED_EVENT(FRDGBuilder_Compile_Cull_Passes, FColor::Emerald);
		TArray<FRDGPassHandle, TInlineAllocator<32, SceneRenderingAllocator>> PassStack;
		PassesToCull.Init(true, Passes.Num());

	#if STATS
		GRDGStatPassCullCount += Passes.Num();
	#endif

		for (FRDGPassHandle PassHandle = Passes.Begin(); PassHandle != Passes.End(); ++PassHandle)
		{
			if (PassesWithUntrackedOutputs[PassHandle] || PassesToNeverCull[PassHandle])
			{
				PassStack.Add(PassHandle);
			}
		}

		while (PassStack.Num())
		{
			const FRDGPassHandle PassHandle = PassStack.Pop();

			if (PassesToCull[PassHandle])
			{
				PassesToCull[PassHandle] = false;
				PassStack.Append(Passes[PassHandle]->Producers);

			#if STATS
				--GRDGStatPassCullCount;
			#endif
			}
		}
	}
	else
	{
		PassesToCull.Init(false, Passes.Num());
	}

	// Walk the culled graph and compile barriers for each subresource. Certain transitions are redundant; read-to-read, for example.
	// We can avoid them by traversing and merging compatible states together. The merging states removes a transition, but the merging
	// heuristic is conservative and choosing not to merge doesn't necessarily mean a transition is performed. They are two distinct steps.
	// Merged states track the first and last pass interval. Pass references are also accumulated onto each resource. This must happen
	// after culling since culled passes can't contribute references.

	{
		SCOPED_NAMED_EVENT(FRDGBuilder_Compile_Barriers, FColor::Emerald);

		for (FRDGPassHandle PassHandle = Passes.Begin(); PassHandle != Passes.End(); ++PassHandle)
		{
			if (PassesToCull[PassHandle] || PassesWithEmptyParameters[PassHandle])
			{
				continue;
			}

			const auto MergeSubresourceStates = [&](ERDGParentResourceType ResourceType, FRDGSubresourceState*& PassMergeState, FRDGSubresourceState*& ResourceMergeState, const FRDGSubresourceState& PassState)
			{
				if (PassState.Access == ERHIAccess::Unknown)
				{
					return;
				}

				if (!ResourceMergeState || !FRDGSubresourceState::IsMergeAllowed(ResourceType, *ResourceMergeState, PassState))
				{
					// Cross-pipeline, non-mergable state changes require a new pass dependency for fencing purposes.
					if (ResourceMergeState && ResourceMergeState->Pipeline != PassState.Pipeline)
					{
						AddPassDependency(ResourceMergeState->LastPass, PassHandle);
					}

					// Allocate a new pending merge state and assign it to the pass state.
					ResourceMergeState = AllocSubresource(PassState);
					ResourceMergeState->SetPass(PassHandle);
				}
				else
				{
					// Merge the pass state into the merged state.
					ResourceMergeState->Access |= PassState.Access;
					ResourceMergeState->LastPass = PassHandle;
				}

				PassMergeState = ResourceMergeState;
			};

			const bool bAsyncComputePass = PassesOnAsyncCompute[PassHandle];

			FRDGPass* Pass = Passes[PassHandle];

			for (auto& TexturePair : Pass->TextureStates)
			{
				FRDGTextureRef Texture = TexturePair.Key;
				auto& PassState = TexturePair.Value;

				Texture->ReferenceCount += PassState.ReferenceCount;
				Texture->bUsedByAsyncComputePass |= bAsyncComputePass;

				const bool bWholePassState = IsWholeResource(PassState.State);
				const bool bWholeMergeState = IsWholeResource(Texture->MergeState);

				// For simplicity, the merge / pass state dimensionality should match.
				if (bWholeMergeState && !bWholePassState)
				{
					InitAsSubresources(Texture->MergeState, Texture->Layout);
				}
				else if (!bWholeMergeState && bWholePassState)
				{
					InitAsWholeResource(Texture->MergeState);
				}

				const uint32 SubresourceCount = PassState.State.Num();
				check(Texture->MergeState.Num() == SubresourceCount);
				PassState.MergeState.SetNum(SubresourceCount);

				for (uint32 Index = 0; Index < SubresourceCount; ++Index)
				{
					MergeSubresourceStates(ERDGParentResourceType::Texture, PassState.MergeState[Index], Texture->MergeState[Index], PassState.State[Index]);
				}
			}

			for (auto& BufferPair : Pass->BufferStates)
			{
				FRDGBufferRef Buffer = BufferPair.Key;
				auto& PassState = BufferPair.Value;

				Buffer->ReferenceCount += PassState.ReferenceCount;
				Buffer->bUsedByAsyncComputePass |= bAsyncComputePass;

				MergeSubresourceStates(ERDGParentResourceType::Buffer, PassState.MergeState, Buffer->MergeState, PassState.State);
			}
		}
	}

	if (AsyncComputePassCount > 0)
	{
		SCOPED_NAMED_EVENT(FRDGBuilder_Compile_AsyncCompute, FColor::Emerald);

		// Traverse the active passes in execution order to find latest cross-pipeline producer and the earliest
		// cross-pipeline consumer for each pass. This helps narrow the search space later when building async
		// compute overlap regions.

		FRDGPassBitArray PassesWithCrossPipelineProducer(false, Passes.Num());
		FRDGPassBitArray PassesWithCrossPipelineConsumer(false, Passes.Num());

		for (FRDGPassHandle PassHandle = Passes.Begin(); PassHandle != Passes.End(); ++PassHandle)
		{
			if (PassesToCull[PassHandle] || PassesWithEmptyParameters[PassHandle])
			{
				continue;
			}

			FRDGPass* Pass = Passes[PassHandle];

			for (FRDGPassHandle ProducerHandle : Pass->GetProducers())
			{
				const FRDGPassHandle ConsumerHandle = PassHandle;

				if (!IsCrossPipeline(ProducerHandle, ConsumerHandle))
				{
					continue;
				}

				FRDGPass* Consumer = Pass;
				FRDGPass* Producer = Passes[ProducerHandle];

				// Finds the earliest consumer on the other pipeline for the producer.
				if (Producer->CrossPipelineConsumer.IsNull() || IsSortedBefore(ConsumerHandle, Producer->CrossPipelineConsumer))
				{
					Producer->CrossPipelineConsumer = PassHandle;
					PassesWithCrossPipelineConsumer[ProducerHandle] = true;
				}

				// Finds the latest producer on the other pipeline for the consumer.
				if (Consumer->CrossPipelineProducer.IsNull() || IsSortedAfter(ProducerHandle, Consumer->CrossPipelineProducer))
				{
					Consumer->CrossPipelineProducer = ProducerHandle;
					PassesWithCrossPipelineProducer[ConsumerHandle] = true;
				}
			}
		}

		// Establishes fork / join overlap regions for async compute. This is used for fencing as well as resource
		// allocation / deallocation. Async compute passes can't allocate / release their resource references until
		// the fork / join is complete, since the two pipes run in parallel. Therefore, all resource lifetimes on
		// async compute are extended to cover the full async region.

		const auto IsCrossPipelineProducer = [&](FRDGPassHandle A)
		{
			return PassesWithCrossPipelineConsumer[A];
		};

		const auto IsCrossPipelineConsumer = [&](FRDGPassHandle A)
		{
			return PassesWithCrossPipelineProducer[A];
		};

		const auto FindCrossPipelineProducer = [&](FRDGPassHandle PassHandle)
		{
			check(PassHandle != ProloguePassHandle);

			FRDGPassHandle LatestProducerHandle = ProloguePassHandle;
			FRDGPassHandle ConsumerHandle = PassHandle;

			// We want to find the latest producer on the other pipeline in order to establish a fork point.
			// Since we could be consuming N resources with N producer passes, we only care about the last one.
			while (ConsumerHandle != Passes.Begin())
			{
				if (!PassesToCull[ConsumerHandle] && !IsCrossPipeline(ConsumerHandle, PassHandle) && IsCrossPipelineConsumer(ConsumerHandle))
				{
					const FRDGPass* Consumer = Passes[ConsumerHandle];

					if (IsSortedAfter(Consumer->CrossPipelineProducer, LatestProducerHandle))
					{
						LatestProducerHandle = Consumer->CrossPipelineProducer;
					}
				}
				--ConsumerHandle;
			}

			return LatestProducerHandle;
		};

		const auto FindCrossPipelineConsumer = [&](FRDGPassHandle PassHandle)
		{
			check(PassHandle != EpiloguePassHandle);

			FRDGPassHandle EarliestConsumerHandle = EpiloguePassHandle;
			FRDGPassHandle ProducerHandle = PassHandle;

			// We want to find the earliest consumer on the other pipeline, as this establishes a join point
			// between the pipes. Since we could be producing for N consumers on the other pipeline, we only
			// care about the first one to execute.
			while (ProducerHandle != Passes.End())
			{
				if (!PassesToCull[ProducerHandle] && !IsCrossPipeline(ProducerHandle, PassHandle) && IsCrossPipelineProducer(ProducerHandle))
				{
					const FRDGPass* Producer = Passes[ProducerHandle];

					if (IsSortedBefore(Producer->CrossPipelineConsumer, EarliestConsumerHandle))
					{
						EarliestConsumerHandle = Producer->CrossPipelineConsumer;
					}
				}
				++ProducerHandle;
			}

			return EarliestConsumerHandle;
		};

		const auto InsertGraphicsToAsyncComputeFork = [&](FRDGPass* GraphicsPass, FRDGPass* AsyncComputePass)
		{
			FRDGBarrierBatchBegin& EpilogueBarriersToBeginForAsyncCompute = GraphicsPass->GetEpilogueBarriersToBeginForAsyncCompute(Allocator);

			GraphicsPass->bGraphicsFork = 1;
			EpilogueBarriersToBeginForAsyncCompute.SetUseCrossPipelineFence();

			AsyncComputePass->bAsyncComputeBegin = 1;
			AsyncComputePass->GetPrologueBarriersToEnd(Allocator).AddDependency(&EpilogueBarriersToBeginForAsyncCompute);
		};

		const auto InsertAsyncToGraphicsComputeJoin = [&](FRDGPass* AsyncComputePass, FRDGPass* GraphicsPass)
		{
			FRDGBarrierBatchBegin& EpilogueBarriersToBeginForGraphics = AsyncComputePass->GetEpilogueBarriersToBeginForGraphics(Allocator);

			AsyncComputePass->bAsyncComputeEnd = 1;
			EpilogueBarriersToBeginForGraphics.SetUseCrossPipelineFence();

			GraphicsPass->bGraphicsJoin = 1;
			GraphicsPass->GetPrologueBarriersToEnd(Allocator).AddDependency(&EpilogueBarriersToBeginForGraphics);
		};

		FRDGPass* PrevGraphicsForkPass = nullptr;
		FRDGPass* PrevGraphicsJoinPass = nullptr;
		FRDGPass* PrevAsyncComputePass = nullptr;

		for (FRDGPassHandle PassHandle = Passes.Begin(); PassHandle != Passes.End(); ++PassHandle)
		{
			if (!PassesOnAsyncCompute[PassHandle] || PassesToCull[PassHandle])
			{
				continue;
			}

			FRDGPass* AsyncComputePass = Passes[PassHandle];

			const FRDGPassHandle GraphicsForkPassHandle = FindCrossPipelineProducer(PassHandle);
			const FRDGPassHandle GraphicsJoinPassHandle = FindCrossPipelineConsumer(PassHandle);

			AsyncComputePass->GraphicsForkPass = GraphicsForkPassHandle;
			AsyncComputePass->GraphicsJoinPass = GraphicsJoinPassHandle;

			FRDGPass* GraphicsForkPass = Passes[GraphicsForkPassHandle];
			FRDGPass* GraphicsJoinPass = Passes[GraphicsJoinPassHandle];

			// Extend the lifetime of resources used on async compute to the fork / join graphics passes.
			GraphicsForkPass->ResourcesToBegin.Add(AsyncComputePass);
			GraphicsJoinPass->ResourcesToEnd.Add(AsyncComputePass);

			if (PrevGraphicsForkPass != GraphicsForkPass)
			{
				InsertGraphicsToAsyncComputeFork(GraphicsForkPass, AsyncComputePass);
			}

			if (PrevGraphicsJoinPass != GraphicsJoinPass && PrevAsyncComputePass)
			{
				InsertAsyncToGraphicsComputeJoin(PrevAsyncComputePass, PrevGraphicsJoinPass);
			}

			PrevAsyncComputePass = AsyncComputePass;
			PrevGraphicsForkPass = GraphicsForkPass;
			PrevGraphicsJoinPass = GraphicsJoinPass;
		}

		// Last async compute pass in the graph needs to be manually joined back to the epilogue pass.
		if (PrevAsyncComputePass)
		{
			InsertAsyncToGraphicsComputeJoin(PrevAsyncComputePass, EpiloguePass);
			PrevAsyncComputePass->bAsyncComputeEndExecute = 1;
		}
	}

	// Traverses passes on the graphics pipe and merges raster passes with the same render targets into a single RHI render pass.
	if (GRDGMergeRenderPasses && RasterPassCount > 0)
	{
		SCOPED_NAMED_EVENT(FRDGBuilder_Compile_RenderPassMerge, FColor::Emerald);

		TArray<FRDGPassHandle, SceneRenderingAllocator> PassesToMerge;
		FRDGPass* PrevPass = nullptr;
		const FRenderTargetBindingSlots* PrevRenderTargets = nullptr;

		const auto CommitMerge = [&]
		{
			if (PassesToMerge.Num())
			{
				const FRDGPassHandle FirstPassHandle = PassesToMerge[0];
				const FRDGPassHandle LastPassHandle = PassesToMerge.Last();

				// Given an interval of passes to merge into a single render pass: [B, X, X, X, X, E]
				//
				// The begin pass (B) and end (E) passes will call {Begin, End}RenderPass, respectively. Also,
				// begin will handle all prologue barriers for the entire merged interval, and end will handle all
				// epilogue barriers. This avoids transitioning of resources within the render pass and batches the
				// transitions more efficiently. This assumes we have filtered out dependencies between passes from
				// the merge set, which is done during traversal.

				// (B) First pass in the merge sequence.
				{
					FRDGPass* Pass = Passes[FirstPassHandle];
					Pass->bSkipRenderPassEnd = 1;
					Pass->EpilogueBarrierPass = LastPassHandle;
				}

				// (X) Intermediate passes.
				for (int32 PassIndex = 1, PassCount = PassesToMerge.Num() - 1; PassIndex < PassCount; ++PassIndex)
				{
					const FRDGPassHandle PassHandle = PassesToMerge[PassIndex];
					FRDGPass* Pass = Passes[PassHandle];
					Pass->bSkipRenderPassBegin = 1;
					Pass->bSkipRenderPassEnd = 1;
					Pass->PrologueBarrierPass = FirstPassHandle;
					Pass->EpilogueBarrierPass = LastPassHandle;
				}

				// (E) Last pass in the merge sequence.
				{
					FRDGPass* Pass = Passes[LastPassHandle];
					Pass->bSkipRenderPassBegin = 1;
					Pass->PrologueBarrierPass = FirstPassHandle;
				}

			#if STATS
				GRDGStatRenderPassMergeCount += PassesToMerge.Num();
			#endif
			}
			PassesToMerge.Reset();
			PrevPass = nullptr;
			PrevRenderTargets = nullptr;
		};

		for (FRDGPassHandle PassHandle = Passes.Begin(); PassHandle != Passes.End(); ++PassHandle)
		{
			if (PassesToCull[PassHandle])
			{
				continue;
			}

			if (PassesOnRaster[PassHandle])
			{
				FRDGPass* NextPass = Passes[PassHandle];

				// A pass where the user controls the render pass can't merge with other passes, and raster UAV passes can't merge due to potential interdependencies.
				if (EnumHasAnyFlags(NextPass->GetFlags(), ERDGPassFlags::SkipRenderPass) || NextPass->bUAVAccess)
				{
					CommitMerge();
					continue;
				}

				// A graphics fork pass can't merge with a previous raster pass.
				if (NextPass->bGraphicsFork)
				{
					CommitMerge();
				}

				const FRenderTargetBindingSlots& RenderTargets = NextPass->GetParameters().GetRenderTargets();

				if (PrevPass)
				{
					check(PrevRenderTargets);

					if (PrevRenderTargets->CanMergeBefore(RenderTargets)
					#if WITH_MGPU
						&& PrevPass->GPUMask == NextPass->GPUMask
					#endif
						)
					{
						if (!PassesToMerge.Num())
						{
							PassesToMerge.Add(PrevPass->GetHandle());
						}
						PassesToMerge.Add(PassHandle);
					}
					else
					{
						CommitMerge();
					}
				}

				PrevPass = NextPass;
				PrevRenderTargets = &RenderTargets;
			}
			else if (!PassesOnAsyncCompute[PassHandle])
			{
				// A non-raster pass on the graphics pipe will invalidate the render target merge.
				CommitMerge();
			}
		}

		CommitMerge();
	}
}

void FRDGBuilder::Execute()
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(RDG);
	SCOPED_NAMED_EVENT(FRDGBuilder_Execute, FColor::Emerald);

	// Create the epilogue pass at the end of the graph just prior to compilation.
	EpiloguePass = Passes.Allocate<FRDGSentinelPass>(Allocator, RDG_EVENT_NAME("Graph Epilogue"));
	SetupEmptyPass(EpiloguePass);

	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateExecuteBegin());

	const FRDGPassHandle ProloguePassHandle = GetProloguePassHandle();
	const FRDGPassHandle EpiloguePassHandle = GetEpiloguePassHandle();
	FRDGPassHandle LastUntrackedPassHandle = ProloguePassHandle;

	if (!GRDGImmediateMode)
	{
		Compile();

		IF_RDG_ENABLE_DEBUG(LogFile.Begin(BuilderName, &Passes, PassesToCull, GetProloguePassHandle(), GetEpiloguePassHandle()));

		{
			SCOPED_NAMED_EVENT_TEXT("CollectPassResources", FColor::Magenta);
			SCOPE_CYCLE_COUNTER(STAT_RDG_CollectResourcesTime);
			CSV_SCOPED_TIMING_STAT_EXCLUSIVE(RDG_CollectResources);

			for (FRDGPassHandle PassHandle = Passes.Begin(); PassHandle != Passes.End(); ++PassHandle)
			{
				if (!PassesToCull[PassHandle])
				{
					CollectPassResources(PassHandle);
				}
			}

			for (const auto& Query : ExtractedTextures)
			{
				EndResourceRHI(EpiloguePassHandle, Query.Key, 1);
			}

			for (const auto& Query : ExtractedBuffers)
			{
				EndResourceRHI(EpiloguePassHandle, Query.Key, 1);
			}
		}

		{
			SCOPED_NAMED_EVENT_TEXT("CollectPassBarriers", FColor::Magenta);
			SCOPE_CYCLE_COUNTER(STAT_RDG_CollectBarriersTime);
			CSV_SCOPED_TIMING_STAT_EXCLUSIVE_CONDITIONAL(RDG_CollectBarriers, GRDGVerboseCSVStats != 0);

			for (FRDGPassHandle PassHandle = Passes.Begin(); PassHandle != Passes.End(); ++PassHandle)
			{
				if (!PassesToCull[PassHandle])
				{
					CollectPassBarriers(PassHandle, LastUntrackedPassHandle);
				}
			}
		}
	}

#if RDG_ENABLE_DEBUG
	const auto LogResource = [&](auto* Resource, auto& Registry)
	{
		if (!Resource->bCulled)
		{
			if (!Resource->bLastOwner)
			{
				auto* NextOwner = Registry[Resource->NextOwner];
				LogFile.AddAliasEdge(Resource, Resource->LastPass, NextOwner, NextOwner->FirstPass);
			}
			LogFile.AddFirstEdge(Resource, Resource->FirstPass);
		}
	};
#endif

	for (FRDGTextureHandle TextureHandle = Textures.Begin(); TextureHandle != Textures.End(); ++TextureHandle)
	{
		FRDGTextureRef Texture = Textures[TextureHandle];

		if (Texture->GetRHIUnchecked())
		{
			AddEpilogueTransition(Texture, LastUntrackedPassHandle);
			Texture->Finalize();

			IF_RDG_ENABLE_DEBUG(LogResource(Texture, Textures));
		}
	}

	for (FRDGBufferHandle BufferHandle = Buffers.Begin(); BufferHandle != Buffers.End(); ++BufferHandle)
	{
		FRDGBufferRef Buffer = Buffers[BufferHandle];

		if (Buffer->GetRHIUnchecked())
		{
			AddEpilogueTransition(Buffer, LastUntrackedPassHandle);
			Buffer->Finalize();

			IF_RDG_ENABLE_DEBUG(LogResource(Buffer, Buffers));
		}
	}

	IF_RDG_CPU_SCOPES(CPUScopeStacks.BeginExecute());
	IF_RDG_GPU_SCOPES(GPUScopeStacks.BeginExecute());

	if (!GRDGImmediateMode)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FRDGBuilder_Execute_Passes);

		for (FRDGPassHandle PassHandle = Passes.Begin(); PassHandle != Passes.End(); ++PassHandle)
		{
			if (!PassesToCull[PassHandle])
			{
				ExecutePass(Passes[PassHandle]);
			}
		}

		IF_RDG_ENABLE_DEBUG(LogFile.End());
	}
	else
	{
		ExecutePass(EpiloguePass);
	}

	RHICmdList.SetGlobalUniformBuffers({});

#if WITH_MGPU
	if (NameForTemporalEffect != NAME_None)
	{
		TArray<FRHITexture*> BroadcastTexturesForTemporalEffect;
		for (const auto& Query : ExtractedTextures)
		{
			if (EnumHasAnyFlags(Query.Key->Flags, ERDGTextureFlags::MultiFrame))
			{
				BroadcastTexturesForTemporalEffect.Add(Query.Key->GetRHIUnchecked());
			}
		}
		RHICmdList.BroadcastTemporalEffect(NameForTemporalEffect, BroadcastTexturesForTemporalEffect);
	}
#endif

	for (const auto& Query : ExtractedTextures)
	{
		*Query.Value = Query.Key->PooledRenderTarget;
	}

	for (const auto& Query : ExtractedBuffers)
	{
		*Query.Value = Query.Key->PooledBuffer;
	}

	IF_RDG_GPU_SCOPES(GPUScopeStacks.Graphics.EndExecute());
	IF_RDG_CPU_SCOPES(CPUScopeStacks.EndExecute());

	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateExecuteEnd());
	IF_RDG_ENABLE_DEBUG(BarrierValidation.ValidateExecuteEnd());

#if STATS
	GRDGStatPassCount += Passes.Num();
	GRDGStatBufferCount += Buffers.Num();
	GRDGStatTextureCount += Textures.Num();
	GRDGStatMemoryWatermark = FMath::Max(GRDGStatMemoryWatermark, Allocator.GetByteCount());
#endif

	Clear();
}

void FRDGBuilder::Clear()
{
	SCOPED_NAMED_EVENT_TEXT("Clear", FColor::Magenta);
	SCOPE_CYCLE_COUNTER(STAT_RDG_ClearTime);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE_CONDITIONAL(RDGBuilder_Clear, GRDGVerboseCSVStats != 0);
	ExternalTextures.Empty();
	ExternalBuffers.Empty();
	ExtractedTextures.Empty();
	ExtractedBuffers.Empty();
	Passes.Clear();
	Views.Clear();
	Textures.Clear();
	Buffers.Clear();
	UniformBuffers.Clear();
	Allocator.ReleaseAll();
}

void FRDGBuilder::SetupPass(FRDGPass* Pass)
{
	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateAddPass(Pass, bInDebugPassScope));

	const FRDGParameterStruct PassParameters = Pass->GetParameters();
	const FRDGPassHandle PassHandle = Pass->GetHandle();
	const ERDGPassFlags PassFlags = Pass->GetFlags();
	const ERHIPipeline PassPipeline = Pass->GetPipeline();

	bool bPassUAVAccess = false;

	Pass->TextureStates.Reserve(PassParameters.GetTextureParameterCount() + (PassParameters.HasRenderTargets() ? (MaxSimultaneousRenderTargets + 1) : 0));
	EnumerateTextureAccess(PassParameters, PassFlags, [&](FRDGViewRef TextureView, FRDGTextureRef Texture, ERHIAccess Access, FRDGTextureSubresourceRange Range)
	{
		check(Access != ERHIAccess::Unknown);
		const FRDGViewHandle NoUAVBarrierHandle = GetHandleIfNoUAVBarrier(TextureView);
		const EResourceTransitionFlags TransitionFlags = GetTextureViewTransitionFlags(TextureView, Texture);

		auto& PassState = Pass->TextureStates.FindOrAdd(Texture);
		PassState.ReferenceCount++;

		const bool bWholeTextureRange = Range.IsWholeResource(Texture->GetSubresourceLayout());
		bool bWholePassState = IsWholeResource(PassState.State);

		// Convert the pass state to subresource dimensionality if we've found a subresource range.
		if (!bWholeTextureRange && bWholePassState)
		{
			InitAsSubresources(PassState.State, Texture->Layout);
			bWholePassState = false;
		}

		const auto AddSubresourceAccess = [&](FRDGSubresourceState& State)
		{
			State.Access = MakeValidAccess(State.Access | Access);
			State.Flags |= TransitionFlags;
			State.NoUAVBarrierFilter.AddHandle(NoUAVBarrierHandle);
			State.Pipeline = PassPipeline;
		};

		if (bWholePassState)
		{
			AddSubresourceAccess(GetWholeResource(PassState.State));
		}
		else
		{
			EnumerateSubresourceRange(PassState.State, Texture->Layout, Range, AddSubresourceAccess);
		}

		bPassUAVAccess |= EnumHasAnyFlags(Access, ERHIAccess::UAVMask);
	});

	Pass->BufferStates.Reserve(PassParameters.GetBufferParameterCount());
	EnumerateBufferAccess(PassParameters, PassFlags, [&](FRDGViewRef BufferView, FRDGBufferRef Buffer, ERHIAccess Access)
	{
		check(Access != ERHIAccess::Unknown);

		const FRDGViewHandle NoUAVBarrierHandle = GetHandleIfNoUAVBarrier(BufferView);

		auto& PassState = Pass->BufferStates.FindOrAdd(Buffer);
		PassState.ReferenceCount++;
		PassState.State.Access = MakeValidAccess(PassState.State.Access | Access);
		PassState.State.NoUAVBarrierFilter.AddHandle(NoUAVBarrierHandle);
		PassState.State.Pipeline = PassPipeline;

		bPassUAVAccess |= EnumHasAnyFlags(Access, ERHIAccess::UAVMask);
	});

	Pass->bUAVAccess = bPassUAVAccess;

	const bool bEmptyParameters = !Pass->TextureStates.Num() && !Pass->BufferStates.Num();
	PassesWithEmptyParameters.Add(bEmptyParameters);

	// The pass can begin / end its own resources on the graphics pipe; async compute is scheduled during compilation.
	if (PassPipeline == ERHIPipeline::Graphics && !bEmptyParameters)
	{
		Pass->ResourcesToBegin.Add(Pass);
		Pass->ResourcesToEnd.Add(Pass);
	}

	SetupPassInternal(Pass, PassHandle, PassPipeline);
}

void FRDGBuilder::SetupEmptyPass(FRDGPass* Pass)
{
	PassesWithEmptyParameters.Add(true);
	SetupPassInternal(Pass, Pass->GetHandle(), ERHIPipeline::Graphics);
}

void FRDGBuilder::SetupPassInternal(FRDGPass* Pass, FRDGPassHandle PassHandle, ERHIPipeline PassPipeline)
{
	check(Pass->GetHandle() == PassHandle);
	check(Pass->GetPipeline() == PassPipeline);

	Pass->GraphicsJoinPass = PassHandle;
	Pass->GraphicsForkPass = PassHandle;
	Pass->PrologueBarrierPass = PassHandle;
	Pass->EpilogueBarrierPass = PassHandle;

#if WITH_MGPU
	Pass->GPUMask = RHICmdList.GetGPUMask();
#endif

	IF_RDG_CPU_SCOPES(Pass->CPUScopes = CPUScopeStacks.GetCurrentScopes());
	IF_RDG_GPU_SCOPES(Pass->GPUScopes = GPUScopeStacks.GetCurrentScopes(PassPipeline));

#if RDG_GPU_SCOPES && RDG_ENABLE_DEBUG
	if (const FRDGEventScope* Scope = Pass->GPUScopes.Event)
	{
		Pass->FullPathIfDebug = Scope->GetPath(Pass->Name);
	}
#endif

	if (GRDGImmediateMode && Pass != EpiloguePass)
	{
		// Trivially redirect the merge states to the pass states, since we won't be compiling the graph.
		for (auto& TexturePair : Pass->TextureStates)
		{
			auto& PassState = TexturePair.Value;
			const uint32 SubresourceCount = PassState.State.Num();
			PassState.MergeState.SetNum(SubresourceCount);
			for (uint32 Index = 0; Index < SubresourceCount; ++Index)
			{
				if (PassState.State[Index].Access != ERHIAccess::Unknown)
				{
					PassState.MergeState[Index] = &PassState.State[Index];
					PassState.MergeState[Index]->SetPass(PassHandle);
				}
			}
		}

		for (auto& BufferPair : Pass->BufferStates)
		{
			auto& PassState = BufferPair.Value;
			PassState.MergeState = &PassState.State;
			PassState.MergeState->SetPass(PassHandle);
		}

		check(!EnumHasAnyFlags(PassPipeline, ERHIPipeline::AsyncCompute));
		FRDGPassHandle LastUntrackedPassHandle = GetProloguePassHandle();
		CollectPassResources(PassHandle);
		CollectPassBarriers(PassHandle, LastUntrackedPassHandle);
		ExecutePass(Pass);
	}

	IF_RDG_ENABLE_DEBUG(VisualizePassOutputs(Pass));
}

void FRDGBuilder::ExecutePassPrologue(FRHIComputeCommandList& RHICmdListPass, FRDGPass* Pass)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FRDGBuilder_ExecutePassPrologue);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE_CONDITIONAL(RDGBuilder_ExecutePassPrologue, GRDGVerboseCSVStats != 0);

	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateExecutePassBegin(Pass));

	if (Pass->PrologueBarriersToBegin)
	{
		IF_RDG_ENABLE_DEBUG(BarrierValidation.ValidateBarrierBatchBegin(Pass, *Pass->PrologueBarriersToBegin));
		Pass->PrologueBarriersToBegin->Submit(RHICmdListPass);
	}

	if (Pass->PrologueBarriersToEnd)
	{
		IF_RDG_ENABLE_DEBUG(BarrierValidation.ValidateBarrierBatchEnd(Pass, *Pass->PrologueBarriersToEnd));
		Pass->PrologueBarriersToEnd->Submit(RHICmdListPass);
	}

	// Uniform buffers are initialized during first-use execution, since the access checks will allow calling GetRHI on RDG resources.
	Pass->GetParameters().EnumerateUniformBuffers([&](FRDGUniformBufferRef UniformBuffer)
	{
		BeginResourceRHI(UniformBuffer);
	});

	if (Pass->GetPipeline() == ERHIPipeline::AsyncCompute)
	{
		RHICmdListPass.SetAsyncComputeBudget(Pass->AsyncComputeBudget);
	}

	const ERDGPassFlags PassFlags = Pass->GetFlags();

	if (EnumHasAnyFlags(PassFlags, ERDGPassFlags::Raster))
	{
		if (!EnumHasAnyFlags(PassFlags, ERDGPassFlags::SkipRenderPass) && !Pass->SkipRenderPassBegin())
		{
			static_cast<FRHICommandList&>(RHICmdListPass).BeginRenderPass(Pass->GetParameters().GetRenderPassInfo(), Pass->GetName());
		}
	}
}

void FRDGBuilder::ExecutePassEpilogue(FRHIComputeCommandList& RHICmdListPass, FRDGPass* Pass)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FRDGBuilder_ExecutePassEpilogue);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE_CONDITIONAL(RDGBuilder_ExecutePassEpilogue, GRDGVerboseCSVStats != 0);

	const ERDGPassFlags PassFlags = Pass->GetFlags();

	if (EnumHasAnyFlags(PassFlags, ERDGPassFlags::Raster) && !EnumHasAnyFlags(PassFlags, ERDGPassFlags::SkipRenderPass) && !Pass->SkipRenderPassEnd())
	{
		static_cast<FRHICommandList&>(RHICmdListPass).EndRenderPass();
	}

	const FRDGParameterStruct PassParameters = Pass->GetParameters();

	if (Pass->EpilogueBarriersToBeginForGraphics)
	{
		IF_RDG_ENABLE_DEBUG(BarrierValidation.ValidateBarrierBatchBegin(Pass, *Pass->EpilogueBarriersToBeginForGraphics));
		Pass->EpilogueBarriersToBeginForGraphics->Submit(RHICmdListPass);
	}

	if (Pass->EpilogueBarriersToBeginForAsyncCompute)
	{
		IF_RDG_ENABLE_DEBUG(BarrierValidation.ValidateBarrierBatchBegin(Pass, *Pass->EpilogueBarriersToBeginForAsyncCompute));
		Pass->EpilogueBarriersToBeginForAsyncCompute->Submit(RHICmdListPass);
	}

	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateExecutePassEnd(Pass));
}

void FRDGBuilder::ExecutePass(FRDGPass* Pass)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FRDGBuilder_ExecutePass);

	for (FRHITexture* Texture : Pass->TexturesToAcquire)
	{
		RHIAcquireTransientResource(Texture);
	}

	// Note that we must do this before doing anything with RHICmdList for the pass.
	// For example, if this pass only executes on GPU 1 we want to avoid adding a
	// 0-duration event for this pass on GPU 0's time line.
	SCOPED_GPU_MASK(RHICmdList, Pass->GPUMask);

	IF_RDG_CPU_SCOPES(CPUScopeStacks.BeginExecutePass(Pass));

	IF_RDG_ENABLE_DEBUG(ConditionalDebugBreak(RDG_BREAKPOINT_PASS_EXECUTE, BuilderName.GetTCHAR(), Pass->GetName()));

#if WITH_MGPU
	if (!bWaitedForTemporalEffect && NameForTemporalEffect != NAME_None)
	{
		RHICmdList.WaitForTemporalEffect(NameForTemporalEffect);
		bWaitedForTemporalEffect = true;
	}
#endif

	// Execute the pass by invoking the prologue, then the pass body, then the epilogue.
	// The entire pass is executed using the command list on the specified pipeline.
	FRHIComputeCommandList& RHICmdListPass = (Pass->GetPipeline() == ERHIPipeline::AsyncCompute)
		? static_cast<FRHIComputeCommandList&>(RHICmdListAsyncCompute)
		: RHICmdList;

	ExecutePassPrologue(RHICmdListPass, Pass);

#if RDG_GPU_SCOPES
	const bool bUsePassEventScope = Pass != EpiloguePass && Pass != ProloguePass;
	if (bUsePassEventScope)
	{
		GPUScopeStacks.BeginExecutePass(Pass);
	}
#endif

	Pass->Execute(RHICmdListPass);

#if RDG_GPU_SCOPES
	if (bUsePassEventScope)
	{
		GPUScopeStacks.EndExecutePass(Pass);
	}
#endif

	ExecutePassEpilogue(RHICmdListPass, Pass);

	for (FRHITexture* Texture : Pass->TexturesToDiscard)
	{
		RHIDiscardTransientResource(Texture);
	}

	if (Pass->bAsyncComputeEnd)
	{
		if (Pass->bAsyncComputeEndExecute)
		{
			IF_RDG_GPU_SCOPES(GPUScopeStacks.AsyncCompute.EndExecute());
		}
		FRHIAsyncComputeCommandListImmediate::ImmediateDispatch(RHICmdListAsyncCompute);
	}

	if (GRDGDebugFlushGPU && !GRDGAsyncCompute)
	{
		RHICmdList.SubmitCommandsAndFlushGPU();
		RHICmdList.BlockUntilGPUIdle();
	}
}

void FRDGBuilder::CollectPassResources(FRDGPassHandle PassHandle)
{
	FRDGPass* Pass = Passes[PassHandle];

	for (FRDGPass* PassToBegin : Pass->ResourcesToBegin)
	{
		const FRDGParameterStruct PassParameters = PassToBegin->GetParameters();

		EnumerateTextureParameters(PassParameters, [&](auto Resource)
		{
			BeginResourceRHI(PassHandle, Resource);
		});

		EnumerateBufferParameters(PassParameters, [&](auto Resource)
		{
			BeginResourceRHI(PassHandle, Resource);
		});
	}

	for (FRDGPass* PassToEnd : Pass->ResourcesToEnd)
	{
		for (const auto& TexturePair : PassToEnd->TextureStates)
		{
			EndResourceRHI(PassHandle, TexturePair.Key, TexturePair.Value.ReferenceCount);
		}

		for (const auto& BufferPair : PassToEnd->BufferStates)
		{
			EndResourceRHI(PassHandle, BufferPair.Key, BufferPair.Value.ReferenceCount);
		}
	}
}

void FRDGBuilder::CollectPassBarriers(FRDGPassHandle PassHandle, FRDGPassHandle& LastUntrackedPassHandle)
{
	FRDGPass* Pass = Passes[PassHandle];

	IF_RDG_ENABLE_DEBUG(ConditionalDebugBreak(RDG_BREAKPOINT_PASS_COMPILE, BuilderName.GetTCHAR(), Pass->GetName()));

	const ERDGPassFlags PassFlags = Pass->GetFlags();
	if (EnumHasAnyFlags(PassFlags, ERDGPassFlags::UntrackedAccess))
	{
		LastUntrackedPassHandle = PassHandle;
	}

	if (PassesWithEmptyParameters[PassHandle])
	{
		return;
	}

	for (const auto& TexturePair : Pass->TextureStates)
	{
		FRDGTextureRef Texture = TexturePair.Key;
		AddTransition(PassHandle, Texture, TexturePair.Value.MergeState, LastUntrackedPassHandle);
		Texture->bCulled = false;
	}

	for (const auto& BufferPair : Pass->BufferStates)
	{
		FRDGBufferRef Buffer = BufferPair.Key;
		AddTransition(PassHandle, Buffer, *BufferPair.Value.MergeState, LastUntrackedPassHandle);
		Buffer->bCulled = false;
	}
}

void FRDGBuilder::AddEpilogueTransition(FRDGTextureRef Texture, FRDGPassHandle LastUntrackedPassHandle)
{
	if (!Texture->bLastOwner || Texture->bCulled)
	{
		return;
	}

	const FRDGPassHandle EpiloguePassHandle = GetEpiloguePassHandle();

	FRDGSubresourceState ScratchSubresourceState;

	// A known final state means extraction from the graph (or an external texture).
	if (Texture->AccessFinal != ERHIAccess::Unknown)
	{
		ScratchSubresourceState.SetPass(EpiloguePassHandle);
		ScratchSubresourceState.Access = Texture->AccessFinal;
		InitAsWholeResource(ScratchTextureState, &ScratchSubresourceState);
	}
	// Lifetime is within the graph, but a pass may have left the resource in an async compute state. We cannot
	// release the pooled texture back to the pool until we transition back to the graphics pipe.
	else if (Texture->bUsedByAsyncComputePass)
	{
		FRDGTextureSubresourceState& TextureState = Texture->GetState();
		ScratchTextureState.SetNumUninitialized(TextureState.Num(), false);

		for (uint32 Index = 0, Count = ScratchTextureState.Num(); Index < Count; ++Index)
		{
			FRDGSubresourceState SubresourceState = TextureState[Index];

			// Transition async compute back to the graphics pipe.
			if (SubresourceState.Pipeline == ERHIPipeline::AsyncCompute)
			{
				SubresourceState.SetPass(EpiloguePassHandle);
				SubresourceState.Pipeline = ERHIPipeline::Graphics;

				ScratchTextureState[Index] = AllocSubresource(SubresourceState);
			}
			else
			{
				ScratchTextureState[Index] = nullptr;
			}
		}
	}
	// No need to transition; texture stayed on the graphics pipe and its lifetime stayed within the graph.
	else
	{
		return;
	}

	AddTransition(EpiloguePassHandle, Texture, ScratchTextureState, LastUntrackedPassHandle);
	ScratchTextureState.Reset();
}

void FRDGBuilder::AddEpilogueTransition(FRDGBufferRef Buffer, FRDGPassHandle LastUntrackedPassHandle)
{
	if (!Buffer->bLastOwner || Buffer->bCulled)
	{
		return;
	}

	const FRDGPassHandle EpiloguePassHandle = GetEpiloguePassHandle();

	ERHIAccess AccessFinal = Buffer->AccessFinal;

	// Transition async compute back to the graphics pipe.
	if (AccessFinal == ERHIAccess::Unknown)
	{
		const FRDGSubresourceState State = Buffer->GetState();

		if (State.Pipeline == ERHIPipeline::AsyncCompute)
		{
			AccessFinal = State.Access;
		}
	}

	if (AccessFinal != ERHIAccess::Unknown)
	{
		FRDGSubresourceState StateFinal;
		StateFinal.SetPass(EpiloguePassHandle);
		StateFinal.Access = AccessFinal;
		AddTransition(EpiloguePassHandle, Buffer, StateFinal, LastUntrackedPassHandle);
	}
}

void FRDGBuilder::AddTransition(FRDGPassHandle PassHandle, FRDGTextureRef Texture, const FRDGTextureTransientSubresourceStateIndirect& StateAfter, FRDGPassHandle LastUntrackedPassHandle)
{
	const FRDGTextureSubresourceRange WholeRange = Texture->GetSubresourceRange();
	const FRDGTextureSubresourceLayout Layout = Texture->Layout;
	FRDGTextureSubresourceState& StateBefore = Texture->GetState();

	const auto AddSubresourceTransition = [&] (
		const FRDGSubresourceState& SubresourceStateBefore,
		const FRDGSubresourceState& SubresourceStateAfter,
		FRDGTextureSubresource* Subresource)
	{
		check(SubresourceStateAfter.Access != ERHIAccess::Unknown);
		check(SubresourceStateAfter.FirstPass.IsValid() && SubresourceStateAfter.LastPass.IsValid());

		if (FRDGSubresourceState::IsTransitionRequired(SubresourceStateBefore, SubresourceStateAfter))
		{
			FRHITransitionInfo Info;
			Info.Texture = Texture->GetRHIUnchecked();
			Info.Type = FRHITransitionInfo::EType::Texture;
			Info.Flags = SubresourceStateAfter.Flags;
			Info.AccessBefore = SubresourceStateBefore.Access;
			Info.AccessAfter = SubresourceStateAfter.Access;

			if (Subresource)
			{
				Info.MipIndex = Subresource->MipIndex;
				Info.ArraySlice = Subresource->ArraySlice;
				Info.PlaneSlice = Subresource->PlaneSlice;
			}

			AddTransitionInternal(Texture, SubresourceStateBefore, SubresourceStateAfter, LastUntrackedPassHandle, Info);
		}

		if (Subresource)
		{
			IF_RDG_ENABLE_DEBUG(LogFile.AddTransitionEdge(PassHandle, SubresourceStateBefore, SubresourceStateAfter, Texture, *Subresource));
		}
		else
		{
			IF_RDG_ENABLE_DEBUG(LogFile.AddTransitionEdge(PassHandle, SubresourceStateBefore, SubresourceStateAfter, Texture));
		}
	};

	const auto MergeSubresourceState = [&] (FRDGSubresourceState& SubresourceStateBefore, const FRDGSubresourceState& SubresourceStateAfter)
	{
		SubresourceStateBefore = SubresourceStateAfter;
		SubresourceStateBefore.FirstPass = PassHandle;
	};

	if (IsWholeResource(StateBefore))
	{
		// 1 -> 1
		if (IsWholeResource(StateAfter))
		{
			if (const FRDGSubresourceState* SubresourceStateAfter = GetWholeResource(StateAfter))
			{
				FRDGSubresourceState& SubresourceStateBefore = GetWholeResource(StateBefore);
				AddSubresourceTransition(SubresourceStateBefore, *SubresourceStateAfter, nullptr);
				SubresourceStateBefore = *SubresourceStateAfter;
				SubresourceStateBefore.FirstPass = PassHandle;
			}
		}
		// 1 -> N
		else
		{
			const FRDGSubresourceState SubresourceStateBeforeWhole = GetWholeResource(StateBefore);
			InitAsSubresources(StateBefore, Layout, SubresourceStateBeforeWhole);
			WholeRange.EnumerateSubresources([&](FRDGTextureSubresource Subresource)
			{
				if (FRDGSubresourceState* SubresourceStateAfter = GetSubresource(StateAfter, Layout, Subresource))
				{
					AddSubresourceTransition(SubresourceStateBeforeWhole, *SubresourceStateAfter, &Subresource);
					FRDGSubresourceState& SubresourceStateBefore = GetSubresource(StateBefore, Layout, Subresource);
					MergeSubresourceState(SubresourceStateBefore, *SubresourceStateAfter);
				}
			});
		}
	}
	else
	{
		// N -> 1
		if (IsWholeResource(StateAfter))
		{
			if (const FRDGSubresourceState* SubresourceStateAfter = GetWholeResource(StateAfter))
			{
				WholeRange.EnumerateSubresources([&](FRDGTextureSubresource Subresource)
				{
					AddSubresourceTransition(GetSubresource(StateBefore, Layout, Subresource), *SubresourceStateAfter, &Subresource);
				});
				InitAsWholeResource(StateBefore);
				FRDGSubresourceState& SubresourceStateBefore = GetWholeResource(StateBefore);
				MergeSubresourceState(SubresourceStateBefore, *SubresourceStateAfter);
			}
		}
		// N -> N
		else
		{
			WholeRange.EnumerateSubresources([&](FRDGTextureSubresource Subresource)
			{
				if (FRDGSubresourceState* SubresourceStateAfter = GetSubresource(StateAfter, Layout, Subresource))
				{
					FRDGSubresourceState& SubresourceStateBefore = GetSubresource(StateBefore, Layout, Subresource);
					AddSubresourceTransition(SubresourceStateBefore, *SubresourceStateAfter, &Subresource);
					MergeSubresourceState(SubresourceStateBefore, *SubresourceStateAfter);
				}
			});
		}
	}
}

void FRDGBuilder::AddTransition(FRDGPassHandle PassHandle, FRDGBufferRef Buffer, FRDGSubresourceState StateAfter, FRDGPassHandle LastUntrackedPassHandle)
{
	check(StateAfter.Access != ERHIAccess::Unknown);
	check(StateAfter.FirstPass.IsValid() && StateAfter.LastPass.IsValid());

	FRDGSubresourceState& StateBefore = Buffer->GetState();

	if (FRDGSubresourceState::IsTransitionRequired(StateBefore, StateAfter))
	{
		FRHITransitionInfo Info;
		Info.Resource = Buffer->GetRHIUnchecked();
		Info.Type = FRDGBufferDesc::GetTransitionResourceType(Buffer->Desc.UnderlyingType);
		Info.Flags = StateAfter.Flags;
		Info.AccessBefore = StateBefore.Access;
		Info.AccessAfter = StateAfter.Access;

		AddTransitionInternal(Buffer, StateBefore, StateAfter, LastUntrackedPassHandle, Info);
	}

	IF_RDG_ENABLE_DEBUG(LogFile.AddTransitionEdge(PassHandle, StateBefore, StateAfter, Buffer));
	StateBefore = StateAfter;
}

void FRDGBuilder::AddTransitionInternal(
	FRDGParentResource* Resource,
	FRDGSubresourceState StateBefore,
	FRDGSubresourceState StateAfter,
	FRDGPassHandle LastUntrackedPassHandle,
	const FRHITransitionInfo& TransitionInfo)
{
	check(StateAfter.FirstPass.IsValid());

	if (StateBefore.LastPass.IsNull())
	{
		check(StateBefore.Pipeline == ERHIPipeline::Graphics);
		StateBefore.LastPass = GetProloguePassHandle();
	}

	check(StateBefore.LastPass < StateAfter.FirstPass);

	// Before states may come from previous aliases of the texture.
	if (Resource->bTransient && StateBefore.LastPass < Resource->FirstPass)
	{
		// If this got left in an async compute state, we need to end the transition in the graphics
		// fork pass, since that's the lifetime of the previous owner was extended to that point. The
		// cost should be better absorbed by the fence as well.
		if (StateBefore.Pipeline == ERHIPipeline::AsyncCompute)
		{
			StateAfter.FirstPass = Passes[StateBefore.LastPass]->GraphicsJoinPass;
		}
		// Otherwise, can push the start of the transition forward until our alias is acquired.
		else
		{
			StateBefore.LastPass = Resource->FirstPass;
		}
	}

	check(StateBefore.LastPass <= StateAfter.FirstPass);

	// Avoids splitting across a pass that is doing untracked RHI access.
	if (StateBefore.LastPass < LastUntrackedPassHandle)
	{
		// Transitions exclusively on the graphics queue can be pushed forward.
		if (!EnumHasAnyFlags(StateBefore.Pipeline | StateAfter.Pipeline, ERHIPipeline::AsyncCompute))
		{
			StateBefore.LastPass = LastUntrackedPassHandle;
		}
		// Async compute transitions have to be split. Emit a warning to the user to avoid touching the resource.
		else
		{
			EmitRDGWarningf(
				TEXT("Resource '%s' is being split-transitioned across untracked pass '%s'. It was not possible to avoid the ")
				TEXT("split barrier due to async compute. Accessing this resource in the pass will cause an RHI validation failure. ")
				TEXT("Otherwise, it's safe to ignore this warning."),
				Resource->Name, Passes[LastUntrackedPassHandle]->GetName());
		}
	}

	FRDGPass* PrevPass = Passes[StateBefore.LastPass];
	FRDGPass* NextPass = Passes[StateAfter.FirstPass];

	// When in immediate mode or if the previous and next passes are the same, the transition
	// occurs immediately before the pass executes in the prologue (no splitting). Otherwise,
	// the transition is split to occur after the previous pass executes.

	FRDGPass* PrologueBarrierPass = Passes[NextPass->PrologueBarrierPass];

	FRDGBarrierBatchBegin* BarriersToBegin = nullptr;
	if (PrevPass == NextPass || GRDGImmediateMode)
	{
		BarriersToBegin = &PrologueBarrierPass->GetPrologueBarriersToBegin(Allocator);
	}
	else
	{
		FRDGPass* EpilogueBarrierPass = Passes[PrevPass->EpilogueBarrierPass];
		BarriersToBegin = &EpilogueBarrierPass->GetEpilogueBarriersToBeginFor(Allocator, StateAfter.Pipeline);
	}
	BarriersToBegin->AddTransition(Resource, TransitionInfo);

	PrologueBarrierPass->GetPrologueBarriersToEnd(Allocator).AddDependency(BarriersToBegin);
}

void FRDGBuilder::BeginResourceRHI(FRDGUniformBuffer* UniformBuffer)
{
	check(UniformBuffer);

	if (UniformBuffer->UniformBufferRHI)
	{
		return;
	}

	const FRDGParameterStruct PassParameters = UniformBuffer->GetParameters();

	const EUniformBufferValidation Validation =
#if RDG_ENABLE_DEBUG
		EUniformBufferValidation::ValidateResources;
#else
		EUniformBufferValidation::None;
#endif

	UniformBuffer->UniformBufferRHI = RHICreateUniformBuffer(PassParameters.GetContents(), PassParameters.GetLayout(), UniformBuffer_SingleFrame, Validation);
	UniformBuffer->ResourceRHI = UniformBuffer->UniformBufferRHI;
}

void FRDGBuilder::BeginResourceRHI(FRDGPassHandle PassHandle, FRDGTextureRef Texture)
{
	check(Texture);

	if (!Texture->FirstPass.IsValid())
	{
		Texture->FirstPass = PassHandle;
	}

	if (Texture->PooledTexture)
	{
		return;
	}

	check(Texture->ReferenceCount > 0 || Texture->bExternal || IsResourceLifetimeExtended());

#if RDG_ENABLE_DEBUG
	{
		FRDGPass* Pass = Passes[PassHandle];
		if (!Pass->bFirstTextureAllocated)
		{
			GRenderTargetPool.AddPhaseEvent(Pass->GetName());
			Pass->bFirstTextureAllocated = 1;
		}
	}
#endif

	TRefCountPtr<FPooledRenderTarget> PooledRenderTarget = GRenderTargetPool.FindFreeElementForRDG(RHICmdList, Texture->Desc, Texture->Name);

	const bool bTransient = PooledRenderTarget->IsTransient();
	FRDGTextureRef PreviousOwner = nullptr;
	Texture->SetRHI(PooledRenderTarget, PreviousOwner);
	Texture->FirstPass = PassHandle;

	if (bTransient)
	{
		if (Texture->bExternal)
		{
			RHIAcquireTransientResource(Texture->GetRHIUnchecked());
		}
		else if (!GRDGImmediateMode)
		{
			Texture->bTransient = bTransient;
			Passes[PassHandle]->TexturesToAcquire.Emplace(Texture->GetRHIUnchecked());
		}
	}
}


void FRDGBuilder::BeginResourceRHI(FRDGPassHandle PassHandle, FRDGTextureSRVRef SRV)
{
	check(SRV);

	if (SRV->ResourceRHI)
	{
		return;
	}

	FRDGTextureRef Texture = SRV->Desc.Texture;
	FRDGPooledTexture* PooledTexture = Texture->PooledTexture;
	check(PooledTexture);

	if (SRV->Desc.MetaData == ERDGTextureMetaDataAccess::HTile)
	{
		check(GRHISupportsExplicitHTile);
		if (!PooledTexture->HTileSRV)
		{
			PooledTexture->HTileSRV = RHICreateShaderResourceViewHTile((FRHITexture2D*)PooledTexture->Texture);
		}
		SRV->ResourceRHI = PooledTexture->HTileSRV;
		check(SRV->ResourceRHI);
		return;
	}

	if (SRV->Desc.MetaData == ERDGTextureMetaDataAccess::FMask)
	{
		if (!PooledTexture->FMaskSRV)
		{
			PooledTexture->FMaskSRV = RHICreateShaderResourceViewFMask((FRHITexture2D*)PooledTexture->Texture);
		}
		SRV->ResourceRHI = PooledTexture->FMaskSRV;
		check(SRV->ResourceRHI);
		return;
	}

	if (SRV->Desc.MetaData == ERDGTextureMetaDataAccess::CMask)
	{
		if (!PooledTexture->CMaskSRV)
		{
			PooledTexture->CMaskSRV = RHICreateShaderResourceViewWriteMask((FRHITexture2D*)PooledTexture->Texture);
		}
		SRV->ResourceRHI = PooledTexture->CMaskSRV;
		check(SRV->ResourceRHI);
		return;
	}

	for (const auto& SRVPair : PooledTexture->SRVs)
	{
		if (SRVPair.Key == SRV->Desc)
		{
			SRV->ResourceRHI = SRVPair.Value;
			return;
		}
	}

	FShaderResourceViewRHIRef RHIShaderResourceView = RHICreateShaderResourceView(PooledTexture->Texture, SRV->Desc);

	SRV->ResourceRHI = RHIShaderResourceView;
	PooledTexture->SRVs.Emplace(SRV->Desc, MoveTemp(RHIShaderResourceView));
}

void FRDGBuilder::BeginResourceRHI(FRDGPassHandle PassHandle, FRDGTextureUAVRef UAV)
{
	check(UAV);

	if (UAV->ResourceRHI)
	{
		return;
	}

	BeginResourceRHI(PassHandle, UAV->Desc.Texture);

	FRDGTextureRef Texture = UAV->Desc.Texture;
	FRDGPooledTexture* PooledTexture = Texture->PooledTexture;
	check(PooledTexture);

	if (UAV->Desc.MetaData == ERDGTextureMetaDataAccess::HTile)
	{
		check(GRHISupportsExplicitHTile);
		if (!PooledTexture->HTileUAV)
		{
			PooledTexture->HTileUAV = RHICreateUnorderedAccessViewHTile((FRHITexture2D*)PooledTexture->Texture);
		}
		UAV->ResourceRHI = PooledTexture->HTileUAV;
		check(UAV->ResourceRHI);
		return;
	}

	if (UAV->Desc.MetaData == ERDGTextureMetaDataAccess::Stencil)
	{
		if (!PooledTexture->StencilUAV)
		{
			PooledTexture->StencilUAV = RHICreateUnorderedAccessViewStencil((FRHITexture2D*)PooledTexture->Texture, 0);
		}
		UAV->ResourceRHI = PooledTexture->StencilUAV;
		check(UAV->ResourceRHI);
		return;
	}

	UAV->ResourceRHI = PooledTexture->MipUAVs[UAV->Desc.MipLevel];
}

void FRDGBuilder::BeginResourceRHI(FRDGPassHandle PassHandle, FRDGBufferRef Buffer)
{
	check(Buffer);

	if (!Buffer->FirstPass.IsValid())
	{
		Buffer->FirstPass = PassHandle;
	}

	if (Buffer->PooledBuffer)
	{
		return;
	}

	check(Buffer->ReferenceCount > 0 || Buffer->bExternal || IsResourceLifetimeExtended());

	TRefCountPtr<FRDGPooledBuffer> PooledBuffer = GRenderGraphResourcePool.FindFreeBuffer(RHICmdList, Buffer->Desc, Buffer->Name);

	FRDGBufferRef PreviousOwner = nullptr;
	Buffer->SetRHI(PooledBuffer, PreviousOwner);
	Buffer->FirstPass = PassHandle;

}

void FRDGBuilder::BeginResourceRHI(FRDGPassHandle PassHandle, FRDGBufferSRVRef SRV)
{
	check(SRV);

	if (SRV->ResourceRHI)
	{
		return;
	}

	FRDGBufferRef Buffer = SRV->Desc.Buffer;
	check(Buffer->PooledBuffer);

	if (!Buffer->FirstPass.IsValid())
	{
		Buffer->FirstPass = PassHandle;
	}

	SRV->ResourceRHI = Buffer->PooledBuffer->GetOrCreateSRV(SRV->Desc);
}

void FRDGBuilder::BeginResourceRHI(FRDGPassHandle PassHandle, FRDGBufferUAV* UAV)
{
	check(UAV);

	if (UAV->ResourceRHI)
	{
		return;
	}

	FRDGBufferRef Buffer = UAV->Desc.Buffer;
	BeginResourceRHI(PassHandle, Buffer);
	UAV->ResourceRHI = Buffer->PooledBuffer->GetOrCreateUAV(UAV->Desc);
}

void FRDGBuilder::EndResourceRHI(FRDGPassHandle PassHandle, FRDGTextureRef Texture, uint32 ReferenceCount)
{
	check(Texture);

	if (!IsResourceLifetimeExtended())
	{
		check(Texture->ReferenceCount >= ReferenceCount);
		Texture->ReferenceCount -= ReferenceCount;

		if (Texture->ReferenceCount == 0)
		{
			// External textures should never release the reference.
			if (!Texture->bExternal && !Texture->bTransient)
			{
				Texture->Allocation = nullptr;
			}

			// Extracted textures will be discarded on release of the reference.
			if (Texture->bTransient && !Texture->bExtracted)
			{
				Passes[PassHandle]->TexturesToDiscard.Emplace(Texture->GetRHIUnchecked());

				static_cast<FPooledRenderTarget*>(Texture->PooledRenderTarget)->bAutoDiscard = false;
			}

			Texture->LastPass = PassHandle;
		}
	}
}

void FRDGBuilder::EndResourceRHI(FRDGPassHandle PassHandle, FRDGBufferRef Buffer, uint32 ReferenceCount)
{
	check(Buffer);

	if (!IsResourceLifetimeExtended())
	{
		check(Buffer->ReferenceCount >= ReferenceCount);
		Buffer->ReferenceCount -= ReferenceCount;

		if (Buffer->ReferenceCount == 0)
		{
			// External buffers should never release the reference.
			if (!Buffer->bExternal)
			{
				Buffer->Allocation = nullptr;
			}
			Buffer->LastPass = PassHandle;
		}
	}
}

#if RDG_ENABLE_DEBUG

void FRDGBuilder::VisualizePassOutputs(const FRDGPass* Pass)
{
#if SUPPORTS_VISUALIZE_TEXTURE
	if (bInDebugPassScope)
	{
		return;
	}

	Pass->GetParameters().EnumerateTextures([&](FRDGParameter Parameter)
	{
		switch (Parameter.GetType())
		{
		case UBMT_RDG_TEXTURE_ACCESS:
		{
			if (FRDGTextureAccess TextureAccess = Parameter.GetAsTextureAccess())
			{
				if (TextureAccess.GetAccess() == ERHIAccess::UAVCompute ||
					TextureAccess.GetAccess() == ERHIAccess::UAVGraphics ||
					TextureAccess.GetAccess() == ERHIAccess::RTV)
				{
					if (TOptional<uint32> CaptureId = GVisualizeTexture.ShouldCapture(TextureAccess->Name, /* MipIndex = */ 0))
					{
						GVisualizeTexture.CreateContentCapturePass(*this, TextureAccess.GetTexture(), *CaptureId);
					}
				}
			}
		}
		break;
		case UBMT_RDG_TEXTURE_UAV:
		{
			if (FRDGTextureUAVRef UAV = Parameter.GetAsTextureUAV())
			{
				FRDGTextureRef Texture = UAV->Desc.Texture;
				if (TOptional<uint32> CaptureId = GVisualizeTexture.ShouldCapture(Texture->Name, UAV->Desc.MipLevel))
				{
					GVisualizeTexture.CreateContentCapturePass(*this, Texture, *CaptureId);
				}
			}
		}
		break;
		case UBMT_RENDER_TARGET_BINDING_SLOTS:
		{
			const FRenderTargetBindingSlots& RenderTargets = Parameter.GetAsRenderTargetBindingSlots();

			RenderTargets.Enumerate([&](FRenderTargetBinding RenderTarget)
			{
				FRDGTextureRef Texture = RenderTarget.GetTexture();
				if (TOptional<uint32> CaptureId = GVisualizeTexture.ShouldCapture(Texture->Name, RenderTarget.GetMipIndex()))
				{
					GVisualizeTexture.CreateContentCapturePass(*this, Texture, *CaptureId);
				}
			});

			const FDepthStencilBinding& DepthStencil = RenderTargets.DepthStencil;

			if (FRDGTextureRef Texture = DepthStencil.GetTexture())
			{
				const bool bHasStoreAction = DepthStencil.GetDepthStencilAccess().IsAnyWrite();

				if (bHasStoreAction)
				{
					const uint32 MipIndex = 0;
					if (TOptional<uint32> CaptureId = GVisualizeTexture.ShouldCapture(Texture->Name, MipIndex))
					{
						GVisualizeTexture.CreateContentCapturePass(*this, Texture, *CaptureId);
					}
				}
			}
		}
		break;
		}
	});
#endif
}

void FRDGBuilder::ClobberPassOutputs(const FRDGPass* Pass)
{
	if (!GRDGClobberResources)
	{
		return;
	}

	if (bInDebugPassScope)
	{
		return;
	}
	bInDebugPassScope = true;

	RDG_EVENT_SCOPE(*this, "RDG ClobberResources");

	const FLinearColor ClobberColor = GetClobberColor();

	Pass->GetParameters().Enumerate([&](FRDGParameter Parameter)
	{
		switch (Parameter.GetType())
		{
		case UBMT_RDG_BUFFER_UAV:
		{
			if (FRDGBufferUAVRef UAV = Parameter.GetAsBufferUAV())
			{
				FRDGBufferRef Buffer = UAV->GetParent();

				if (UserValidation.TryMarkForClobber(Buffer))
				{
					AddClearUAVPass(*this, UAV, GetClobberBufferValue());
				}
			}
		}
		break;
		case UBMT_RDG_TEXTURE_ACCESS:
		{
			if (FRDGTextureAccess TextureAccess = Parameter.GetAsTextureAccess())
			{
				FRDGTextureRef Texture = TextureAccess.GetTexture();

				if (UserValidation.TryMarkForClobber(Texture))
				{
					if (EnumHasAnyFlags(TextureAccess.GetAccess(), ERHIAccess::UAVMask))
					{
						for (int32 MipLevel = 0; MipLevel < Texture->Desc.NumMips; MipLevel++)
						{
							AddClearUAVPass(*this, this->CreateUAV(FRDGTextureUAVDesc(Texture, MipLevel)), ClobberColor);
						}
					}
					else if (EnumHasAnyFlags(TextureAccess.GetAccess(), ERHIAccess::RTV))
					{
						AddClearRenderTargetPass(*this, Texture, ClobberColor);
					}
				}
			}
		}
		break;
		case UBMT_RDG_TEXTURE_UAV:
		{
			if (FRDGTextureUAVRef UAV = Parameter.GetAsTextureUAV())
			{
				FRDGTextureRef Texture = UAV->GetParent();

				if (UserValidation.TryMarkForClobber(Texture))
				{
					if (Texture->Desc.NumMips == 1)
					{
						AddClearUAVPass(*this, UAV, ClobberColor);
					}
					else
					{
						for (int32 MipLevel = 0; MipLevel < Texture->Desc.NumMips; MipLevel++)
						{
							AddClearUAVPass(*this, this->CreateUAV(FRDGTextureUAVDesc(Texture, MipLevel)), ClobberColor);
						}
					}
				}
			}
		}
		break;
		case UBMT_RENDER_TARGET_BINDING_SLOTS:
		{
			const FRenderTargetBindingSlots& RenderTargets = Parameter.GetAsRenderTargetBindingSlots();

			RenderTargets.Enumerate([&](FRenderTargetBinding RenderTarget)
			{
				FRDGTextureRef Texture = RenderTarget.GetTexture();

				if (UserValidation.TryMarkForClobber(Texture))
				{
					AddClearRenderTargetPass(*this, Texture, ClobberColor);
				}
			});

			if (FRDGTextureRef Texture = RenderTargets.DepthStencil.GetTexture())
			{
				if (UserValidation.TryMarkForClobber(Texture))
				{
					AddClearDepthStencilPass(*this, Texture, true, GetClobberDepth(), true, GetClobberStencil());
				}
			}
		}
		break;
		}
	});

	bInDebugPassScope = false;
}

#endif //! RDG_ENABLE_DEBUG