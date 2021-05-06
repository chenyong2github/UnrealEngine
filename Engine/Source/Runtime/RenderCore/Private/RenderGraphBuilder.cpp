// Copyright Epic Games, Inc. All Rights Reserved.

#include "RenderGraphBuilder.h"
#include "RenderGraphPrivate.h"
#include "RenderGraphTrace.h"
#include "RenderTargetPool.h"
#include "RenderGraphResourcePool.h"
#include "VisualizeTexture.h"
#include "ProfilingDebugging/CsvProfiler.h"

#if ENABLE_RHI_VALIDATION

inline void GatherPassUAVsForOverlapValidation(const FRDGPass* Pass, TArray<FRHIUnorderedAccessView*, TInlineAllocator<MaxSimultaneousUAVs, FRDGArrayAllocator>>& OutUAVs)
{
	// RHI validation tracking of Begin/EndUAVOverlaps happens on the underlying resource, so we need to be careful about not
	// passing multiple UAVs that refer to the same resource, otherwise we get double-Begin and double-End validation errors.
	// Filter UAVs to only those with unique parent resources.
	TArray<FRDGParentResource*, TInlineAllocator<MaxSimultaneousUAVs, FRDGArrayAllocator>> UniqueParents;
	Pass->GetParameters().Enumerate([&](FRDGParameter Parameter)
	{
		if (Parameter.IsUAV())
		{
			if (FRDGUnorderedAccessViewRef UAV = Parameter.GetAsUAV())
			{
				FRDGParentResource* Parent = UAV->GetParent();

				// Check if we've already seen this parent.
				bool bFound = false;
				for (int32 Index = 0; !bFound && Index < UniqueParents.Num(); ++Index)
				{
					bFound = UniqueParents[Index] == Parent;
				}

				if (!bFound)
				{
					UniqueParents.Add(Parent);
					OutUAVs.Add(UAV->GetRHI());
				}
			}
		}
	});
}

#endif

inline void BeginUAVOverlap(const FRDGPass* Pass, FRHIComputeCommandList& RHICmdList)
{
#if ENABLE_RHI_VALIDATION
	TArray<FRHIUnorderedAccessView*, TInlineAllocator<MaxSimultaneousUAVs, FRDGArrayAllocator>> UAVs;
	GatherPassUAVsForOverlapValidation(Pass, UAVs);

	if (UAVs.Num())
	{
		RHICmdList.BeginUAVOverlap(UAVs);
	}
#endif
}

inline void EndUAVOverlap(const FRDGPass* Pass, FRHIComputeCommandList& RHICmdList)
{
#if ENABLE_RHI_VALIDATION
	TArray<FRHIUnorderedAccessView*, TInlineAllocator<MaxSimultaneousUAVs, FRDGArrayAllocator>> UAVs;
	GatherPassUAVsForOverlapValidation(Pass, UAVs);

	if (UAVs.Num())
	{
		RHICmdList.EndUAVOverlap(UAVs);
	}
#endif
}

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

enum class ERDGTextureAccessFlags
{
	None = 0,

	// Access is within the fixed-function render pass.
	RenderTarget = 1 << 0
};
ENUM_CLASS_FLAGS(ERDGTextureAccessFlags);

/** Enumerates all texture accesses and provides the access and subresource range info. This results in
 *  multiple invocations of the same resource, but with different access / subresource range.
 */
template <typename TAccessFunction>
void EnumerateTextureAccess(FRDGParameterStruct PassParameters, ERDGPassFlags PassFlags, TAccessFunction AccessFunction)
{
	const ERDGTextureAccessFlags NoneFlags = ERDGTextureAccessFlags::None;

	ERHIAccess SRVAccess, UAVAccess;
	GetPassAccess(PassFlags, SRVAccess, UAVAccess);

	PassParameters.EnumerateTextures([&](FRDGParameter Parameter)
	{
		switch (Parameter.GetType())
		{
		case UBMT_RDG_TEXTURE:
			if (FRDGTextureRef Texture = Parameter.GetAsTexture())
			{
				AccessFunction(nullptr, Texture, SRVAccess, NoneFlags, Texture->GetSubresourceRangeSRV());
			}
		break;
		case UBMT_RDG_TEXTURE_ACCESS:
		{
			if (FRDGTextureAccess TextureAccess = Parameter.GetAsTextureAccess())
			{
				AccessFunction(nullptr, TextureAccess.GetTexture(), TextureAccess.GetAccess(), NoneFlags, TextureAccess->GetSubresourceRange());
			}
		}
		break;
		case UBMT_RDG_TEXTURE_ACCESS_ARRAY:
		{
			const FRDGTextureAccessArray& TextureAccessArray = Parameter.GetAsTextureAccessArray();

			for (FRDGTextureAccess TextureAccess : TextureAccessArray)
			{
				AccessFunction(nullptr, TextureAccess.GetTexture(), TextureAccess.GetAccess(), NoneFlags, TextureAccess->GetSubresourceRange());
			}
		}
		break;
		case UBMT_RDG_TEXTURE_SRV:
			if (FRDGTextureSRVRef SRV = Parameter.GetAsTextureSRV())
			{
				AccessFunction(SRV, SRV->GetParent(), SRVAccess, NoneFlags, SRV->GetSubresourceRange());
			}
		break;
		case UBMT_RDG_TEXTURE_UAV:
			if (FRDGTextureUAVRef UAV = Parameter.GetAsTextureUAV())
			{
				AccessFunction(UAV, UAV->GetParent(), UAVAccess, NoneFlags, UAV->GetSubresourceRange());
			}
		break;
		case UBMT_RENDER_TARGET_BINDING_SLOTS:
		{
			const ERDGTextureAccessFlags RenderTargetAccess = ERDGTextureAccessFlags::RenderTarget;

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

				AccessFunction(nullptr, Texture, RTVAccess, RenderTargetAccess, Range);

				if (ResolveTexture && ResolveTexture != Texture)
				{
					// Resolve targets must use the RTV|ResolveDst flag combination when the resolve is performed through the render
					// pass. The ResolveDst flag must be used alone only when the resolve is performed using RHICopyToResolveTarget.
					AccessFunction(nullptr, ResolveTexture, ERHIAccess::RTV | ERHIAccess::ResolveDst, RenderTargetAccess, Range);
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

					AccessFunction(nullptr, Texture, NewAccess, RenderTargetAccess, Range);
				});
			}

			if (FRDGTextureRef Texture = RenderTargets.ShadingRateTexture)
			{
				AccessFunction(nullptr, Texture, ERHIAccess::ShadingRateSource, RenderTargetAccess, Texture->GetSubresourceRangeSRV());
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
		case UBMT_RDG_BUFFER_ACCESS:
			if (FRDGBufferAccess BufferAccess = Parameter.GetAsBufferAccess())
			{
				AccessFunction(nullptr, BufferAccess.GetBuffer(), BufferAccess.GetAccess());
			}
		break;
		case UBMT_RDG_BUFFER_ACCESS_ARRAY:
		{
			const FRDGBufferAccessArray& BufferAccessArray = Parameter.GetAsBufferAccessArray();

			for (FRDGBufferAccess BufferAccess : BufferAccessArray)
			{
				AccessFunction(nullptr, BufferAccess.GetBuffer(), BufferAccess.GetAccess());
			}
		}
		break;
		case UBMT_RDG_BUFFER_SRV:
			if (FRDGBufferSRVRef SRV = Parameter.GetAsBufferSRV())
			{
				FRDGBufferRef Buffer = SRV->GetParent();
				ERHIAccess BufferAccess = SRVAccess;

				if (EnumHasAnyFlags(Buffer->Desc.Usage, BUF_AccelerationStructure))
				{
					BufferAccess = ERHIAccess::BVHRead;
				}

				AccessFunction(SRV, Buffer, BufferAccess);
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
	SET_DWORD_STAT(STAT_RDG_PassWithParameterCount, GRDGStatPassWithParameterCount);
	SET_DWORD_STAT(STAT_RDG_PassCullCount, GRDGStatPassCullCount);
	SET_DWORD_STAT(STAT_RDG_RenderPassMergeCount, GRDGStatRenderPassMergeCount);
	SET_DWORD_STAT(STAT_RDG_PassDependencyCount, GRDGStatPassDependencyCount);
	SET_DWORD_STAT(STAT_RDG_TextureCount, GRDGStatTextureCount);
	SET_DWORD_STAT(STAT_RDG_TextureReferenceCount, GRDGStatTextureReferenceCount);
	SET_FLOAT_STAT(STAT_RDG_TextureReferenceAverage, (float)(GRDGStatTextureReferenceCount / FMath::Max<float>(GRDGStatTextureCount, 1.0f)));
	SET_DWORD_STAT(STAT_RDG_BufferCount, GRDGStatBufferCount);
	SET_DWORD_STAT(STAT_RDG_BufferReferenceCount, GRDGStatBufferReferenceCount);
	SET_FLOAT_STAT(STAT_RDG_BufferReferenceAverage, (float)(GRDGStatBufferReferenceCount / FMath::Max<float>(GRDGStatBufferCount, 1.0f)));
	SET_DWORD_STAT(STAT_RDG_ViewCount, GRDGStatViewCount);
	SET_DWORD_STAT(STAT_RDG_TransientTextureCount, GRDGStatTransientTextureCount);
	SET_DWORD_STAT(STAT_RDG_TransientBufferCount, GRDGStatTransientBufferCount);
	SET_DWORD_STAT(STAT_RDG_TransitionCount, GRDGStatTransitionCount);
	SET_DWORD_STAT(STAT_RDG_AliasingCount, GRDGStatAliasingCount);
	SET_DWORD_STAT(STAT_RDG_TransitionBatchCount, GRDGStatTransitionBatchCount);
	SET_MEMORY_STAT(STAT_RDG_MemoryWatermark, int64(GRDGStatMemoryWatermark));
	GRDGStatPassCount = 0;
	GRDGStatPassWithParameterCount = 0;
	GRDGStatPassCullCount = 0;
	GRDGStatRenderPassMergeCount = 0;
	GRDGStatPassDependencyCount = 0;
	GRDGStatTextureCount = 0;
	GRDGStatTextureReferenceCount = 0;
	GRDGStatBufferCount = 0;
	GRDGStatBufferReferenceCount = 0;
	GRDGStatViewCount = 0;
	GRDGStatTransientTextureCount = 0;
	GRDGStatTransientBufferCount = 0;
	GRDGStatTransitionCount = 0;
	GRDGStatAliasingCount = 0;
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

	const bool bGlobalForceAsyncCompute = (GRDGAsyncCompute == RDG_ASYNC_COMPUTE_FORCE_ENABLED && !IsImmediateMode() && bDebugAllowedForPass);

	if (EnumHasAnyFlags(PassFlags, ERDGPassFlags::Compute) && (bGlobalForceAsyncCompute))
	{
		PassFlags &= ~ERDGPassFlags::Compute;
		PassFlags |= ERDGPassFlags::AsyncCompute;
	}

	if (EnumHasAnyFlags(PassFlags, ERDGPassFlags::AsyncCompute) && (GRDGAsyncCompute == RDG_ASYNC_COMPUTE_DISABLED || IsImmediateMode() || !bAsyncComputeSupported))
	{
		PassFlags &= ~ERDGPassFlags::AsyncCompute;
		PassFlags |= ERDGPassFlags::Compute;
	}

	return PassFlags;
}

bool FRDGBuilder::IsTransient(FRDGBufferRef Buffer) const
{
	if (!IsTransientInternal(Buffer))
	{
		return false;
	}

	return EnumHasAnyFlags(Buffer->Desc.Usage, BUF_UnorderedAccess);
}

bool FRDGBuilder::IsTransient(FRDGTextureRef Texture) const
{
	return IsTransientInternal(Texture);
}

bool FRDGBuilder::IsTransientInternal(FRDGParentResourceRef Resource) const
{
	// Immediate mode can't use the transient allocator because we don't know if the user will extract the resource.
	if (!GRDGTransientAllocator || IsImmediateMode())
	{
		return false;
	}

	// Transient resources must stay within the graph.
	if (Resource->bExternal || Resource->bExtracted || Resource->bUserSetNonTransient)
	{
		return false;
	}

#if RDG_ENABLE_DEBUG
	if (GRDGDebugDisableTransientResources != 0 && IsDebugAllowedForResource(Resource->Name))
	{
		return false;
	}
#endif

	// This resource cannot be extracted or made external later.
	check(bSetupComplete);

	return true;
}

FRDGBuilder::FTransientResourceAllocator::~FTransientResourceAllocator()
{
	if (Allocator)
	{
		Allocator->Release(RHICmdList);
	}
}

IRHITransientResourceAllocator* FRDGBuilder::FTransientResourceAllocator::GetOrCreate()
{
	check(!IsImmediateMode());
	if (!Allocator && !bCreateAttempted)
	{
		Allocator = RHICreateTransientResourceAllocator();
		bCreateAttempted = true;
	}
	return Allocator;
}

const char* const FRDGBuilder::kDefaultUnaccountedCSVStat = "RDG_Pass";

FRDGBuilder::FRDGBuilder(FRHICommandListImmediate& InRHICmdList, FRDGEventName InName)
	: RHICmdList(InRHICmdList)
	, Blackboard(Allocator)
	, RHICmdListAsyncCompute(FRHICommandListExecutor::GetImmediateAsyncComputeCommandList())
	, BuilderName(InName)
#if RDG_CPU_SCOPES
	, CPUScopeStacks(RHICmdList, Allocator, kDefaultUnaccountedCSVStat)
#endif
#if RDG_GPU_SCOPES
	, GPUScopeStacks(RHICmdList, RHICmdListAsyncCompute, Allocator)
#endif
#if RDG_ENABLE_DEBUG
	, UserValidation(Allocator)
	, BarrierValidation(&Passes, BuilderName)
#endif
{
	ProloguePass = Passes.Allocate<FRDGSentinelPass>(Allocator, RDG_EVENT_NAME("Graph Prologue"));
	SetupEmptyPass(ProloguePass);

#if RDG_EVENTS != RDG_EVENTS_NONE
	// This is polled once as a workaround for a race condition since the underlying global is not always changed on the render thread.
	GRDGEmitEvents = GetEmitDrawEvents();
#endif
}

const TRefCountPtr<FRDGPooledBuffer>& FRDGBuilder::ConvertToExternalBuffer(FRDGBufferRef Buffer)
{
	checkf(Buffer, TEXT("ConvertToExternalBuffer called with a null buffer."));
	if (!Buffer->bExternal)
	{
		Buffer->bExternal = 1;
		Buffer->AccessFinal = kDefaultAccessFinal;
		BeginResourceRHI(nullptr, GetProloguePassHandle(), Buffer);
		ExternalBuffers.Add(Buffer->PooledBuffer, Buffer);
	}
	return GetPooledBuffer(Buffer);
}

const TRefCountPtr<IPooledRenderTarget>& FRDGBuilder::ConvertToExternalTexture(FRDGTextureRef Texture)
{
	checkf(Texture, TEXT("ConvertToExternalTexture called with a null texture."));
	if (!Texture->bExternal)
	{
		Texture->bExternal = 1;
		Texture->AccessFinal = kDefaultAccessFinal;
		BeginResourceRHI(nullptr, GetProloguePassHandle(), Texture);
		ExternalTextures.Add(Texture->GetRHIUnchecked(), Texture);
	}
	return GetPooledTexture(Texture);
}

BEGIN_SHADER_PARAMETER_STRUCT(FFinalizePassParameters, )
	RDG_TEXTURE_ACCESS_ARRAY(Textures)
	RDG_BUFFER_ACCESS_ARRAY(Buffers)
END_SHADER_PARAMETER_STRUCT()

void FRDGBuilder::FinalizeResourceAccess(FRDGTextureAccessArray&& InTextures, FRDGBufferAccessArray&& InBuffers)
{
	auto* PassParameters = AllocParameters<FFinalizePassParameters>();
	PassParameters->Textures = Forward<FRDGTextureAccessArray&&>(InTextures);
	PassParameters->Buffers = Forward<FRDGBufferAccessArray&&>(InBuffers);

	// Take reference to pass parameters version since we've moved the memory.
	const auto& LocalTextures = PassParameters->Textures;
	const auto& LocalBuffers = PassParameters->Buffers;

#if RDG_ENABLE_DEBUG
	{
		const FRDGPassHandle FinalizePassHandle(Passes.Num());

		for (FRDGTextureAccess TextureAccess : LocalTextures)
		{
			UserValidation.ValidateFinalize(TextureAccess.GetTexture(), TextureAccess.GetAccess(), FinalizePassHandle);
		}

		for (FRDGBufferAccess BufferAccess : LocalBuffers)
		{
			UserValidation.ValidateFinalize(BufferAccess.GetBuffer(), BufferAccess.GetAccess(), FinalizePassHandle);
		}
	}
#endif

	AddPass(
		RDG_EVENT_NAME("FinalizeResourceAccess(Textures: %d, Buffers: %d)", LocalTextures.Num(), LocalBuffers.Num()),
		PassParameters,
		// Use all of the work flags so that any access is valid.
		ERDGPassFlags::Copy | ERDGPassFlags::Compute | ERDGPassFlags::Raster | ERDGPassFlags::SkipRenderPass |
		// We're not writing to anything, so we have to tell the pass not to cull.
		ERDGPassFlags::NeverCull,
		[](FRHICommandList&) {});

	// bFinalized must be set after adding the finalize pass, as future declarations of the resource will be ignored.

	for (FRDGTextureAccess TextureAccess : LocalTextures)
	{
		TextureAccess->bFinalizedAccess = 1;
	}

	for (FRDGBufferAccess BufferAccess : LocalBuffers)
	{
		BufferAccess->bFinalizedAccess = 1;
	}
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
	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateRegisterExternalTexture(ExternalPooledTexture, Name, RenderTargetTexture, Flags));
	FRHITexture* ExternalTextureRHI = ExternalPooledTexture->GetRenderTargetItem().GetRHI(RenderTargetTexture);
	IF_RDG_ENABLE_DEBUG(checkf(ExternalTextureRHI, TEXT("Attempted to register texture %s, but its RHI texture is null."), Name));

	if (FRDGTextureRef FoundTexture = FindExternalTexture(ExternalTextureRHI))
	{
		return FoundTexture;
	}

	FRDGTextureRef Texture = Textures.Allocate(Allocator, Name, Translate(ExternalPooledTexture->GetDesc(), RenderTargetTexture), Flags, RenderTargetTexture);
	Texture->SetRHI(static_cast<FPooledRenderTarget*>(ExternalPooledTexture.GetReference()));

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

	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateRegisterExternalTexture(Texture));
	IF_RDG_ENABLE_TRACE(Trace.AddResource(Texture));
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
	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateRegisterExternalBuffer(ExternalPooledBuffer, Name, Flags));

	if (FRDGBufferRef* FoundBufferPtr = ExternalBuffers.Find(ExternalPooledBuffer.GetReference()))
	{
		return *FoundBufferPtr;
	}

	FRDGBufferRef Buffer = Buffers.Allocate(Allocator, Name, ExternalPooledBuffer->Desc, Flags);
	Buffer->SetRHI(ExternalPooledBuffer);

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

	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateRegisterExternalBuffer(Buffer));
	IF_RDG_ENABLE_TRACE(Trace.AddResource(Buffer));
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

		const auto AddCullingDependency = [&](FRDGProducerStatesByPipeline& LastProducers, const FRDGProducerState& NextState, ERHIPipeline NextPipeline)
		{
			for (ERHIPipeline LastPipeline : GetRHIPipelines())
			{
				FRDGProducerState& LastProducer = LastProducers[LastPipeline];

				if (LastProducer.Access == ERHIAccess::Unknown)
				{
					continue;
				}

				if (FRDGProducerState::IsDependencyRequired(LastProducer, LastPipeline, NextState, NextPipeline))
				{
					AddPassDependency(LastProducer.PassHandle, NextState.PassHandle);
				}
			}

			if (IsWritableAccess(NextState.Access))
			{
				LastProducers[NextPipeline] = NextState;
			}
		};

		for (FRDGPassHandle PassHandle = Passes.Begin(); PassHandle != Passes.End(); ++PassHandle)
		{
			FRDGPass* Pass = Passes[PassHandle];
			const ERHIPipeline PassPipeline = Pass->GetPipeline();

			bool bUntrackedOutputs = Pass->bHasExternalOutputs;

			for (auto& PassState : Pass->TextureStates)
			{
				FRDGTextureRef Texture = PassState.Texture;
				auto& LastProducers = Texture->LastProducers;

				for (uint32 Index = 0, Count = LastProducers.Num(); Index < Count; ++Index)
				{
					const auto& SubresourceState = PassState.State[Index];

					if (SubresourceState.Access == ERHIAccess::Unknown)
					{
						continue;
					}

					FRDGProducerState ProducerState;
					ProducerState.Access = SubresourceState.Access;
					ProducerState.PassHandle = PassHandle;
					ProducerState.NoUAVBarrierHandle = SubresourceState.NoUAVBarrierFilter.GetUniqueHandle();

					AddCullingDependency(LastProducers[Index], ProducerState, PassPipeline);
				}

				bUntrackedOutputs |= Texture->bExternal;
			}

			for (auto& PassState : Pass->BufferStates)
			{
				FRDGBufferRef Buffer = PassState.Buffer;
				const auto& SubresourceState = PassState.State;

				FRDGProducerState ProducerState;
				ProducerState.Access = SubresourceState.Access;
				ProducerState.PassHandle = PassHandle;
				ProducerState.NoUAVBarrierHandle = SubresourceState.NoUAVBarrierFilter.GetUniqueHandle();

				AddCullingDependency(Buffer->LastProducer, ProducerState, PassPipeline);
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
			for (auto& LastProducer : Texture->LastProducers)
			{
				FRDGProducerState StateFinal;
				StateFinal.Access = Texture->AccessFinal;
				StateFinal.PassHandle = EpiloguePassHandle;

				AddCullingDependency(LastProducer, StateFinal, ERHIPipeline::Graphics);
			}
			Texture->ReferenceCount++;
		}

		for (const auto& Query : ExtractedBuffers)
		{
			FRDGBufferRef Buffer = Query.Key;

			FRDGProducerState StateFinal;
			StateFinal.Access = Buffer->AccessFinal;
			StateFinal.PassHandle = EpiloguePassHandle;

			AddCullingDependency(Buffer->LastProducer, StateFinal, ERHIPipeline::Graphics);
			Buffer->ReferenceCount++;
		}

		EnumerateExtendedLifetimeResources(Textures, [](FRDGTextureRef Texture)
		{
			Texture->ReferenceCount++;
		});

		EnumerateExtendedLifetimeResources(Buffers, [](FRDGBufferRef Buffer)
		{
			Buffer->ReferenceCount++;
		});

		// Prologue / Epilogue passes get a lot of transitions added for external / extracted resources.
		ProloguePass->GetEpilogueBarriersToBeginForGraphics(Allocator).Reserve(ExternalTextures.Num() + ExternalBuffers.Num());
		EpiloguePass->GetPrologueBarriersToEnd(Allocator).Reserve(32);
	}

	// All dependencies in the raw graph have been specified; if enabled, all passes are marked as culled and a
	// depth first search is employed to find reachable regions of the graph. Roots of the search are those passes
	// with outputs leaving the graph or those marked to never cull.

	if (GRDGCullPasses)
	{
		SCOPED_NAMED_EVENT(FRDGBuilder_Compile_Cull_Passes, FColor::Emerald);
		TArray<FRDGPassHandle, FRDGArrayAllocator> PassStack;
		PassesToCull.Init(true, Passes.Num());
		PassStack.Reserve(Passes.Num());

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

			const bool bAsyncComputePass = PassesOnAsyncCompute[PassHandle];

			FRDGPass* Pass = Passes[PassHandle];
			const ERHIPipeline PassPipeline = Pass->GetPipeline();

			const auto MergeSubresourceStates = [&](ERDGParentResourceType ResourceType, FRDGSubresourceState*& PassMergeState, FRDGSubresourceState*& ResourceMergeState, const FRDGSubresourceState& PassState)
			{
				if (!ResourceMergeState || !FRDGSubresourceState::IsMergeAllowed(ResourceType, *ResourceMergeState, PassState))
				{
					// Cross-pipeline, non-mergable state changes require a new pass dependency for fencing purposes.
					if (ResourceMergeState)
					{
						for (ERHIPipeline Pipeline : GetRHIPipelines())
						{
							if (Pipeline != PassPipeline && ResourceMergeState->LastPass[Pipeline].IsValid())
							{
								// Add a dependency from the other pipe to this pass to join back.
								AddPassDependency(ResourceMergeState->LastPass[Pipeline], PassHandle);
							}
						}
					}

					// Allocate a new pending merge state and assign it to the pass state.
					ResourceMergeState = AllocSubresource(PassState);
				}
				else
				{
					// Merge the pass state into the merged state.
					ResourceMergeState->Access |= PassState.Access;

					FRDGPassHandle& FirstPassHandle = ResourceMergeState->FirstPass[PassPipeline];

					if (FirstPassHandle.IsNull())
					{
						FirstPassHandle = PassHandle;
					}

					ResourceMergeState->LastPass[PassPipeline] = PassHandle;
				}

				PassMergeState = ResourceMergeState;
			};

			for (auto& PassState : Pass->TextureStates)
			{
				FRDGTextureRef Texture = PassState.Texture;
				Texture->ReferenceCount += PassState.ReferenceCount;
				Texture->bUsedByAsyncComputePass |= bAsyncComputePass;

			#if STATS
				GRDGStatTextureReferenceCount += PassState.ReferenceCount;
			#endif

				for (int32 Index = 0; Index < PassState.State.Num(); ++Index)
				{
					if (PassState.State[Index].Access == ERHIAccess::Unknown)
					{
						continue;
					}

					MergeSubresourceStates(ERDGParentResourceType::Texture, PassState.MergeState[Index], Texture->MergeState[Index], PassState.State[Index]);
				}
			}

			for (auto& PassState : Pass->BufferStates)
			{
				FRDGBufferRef Buffer = PassState.Buffer;
				Buffer->ReferenceCount += PassState.ReferenceCount;
				Buffer->bUsedByAsyncComputePass |= bAsyncComputePass;

			#if STATS
				GRDGStatBufferReferenceCount += PassState.ReferenceCount;
			#endif

				MergeSubresourceStates(ERDGParentResourceType::Buffer, PassState.MergeState, Buffer->MergeState, PassState.State);
			}
		}
	}

	// Traverses passes on the graphics pipe and merges raster passes with the same render targets into a single RHI render pass.
	if (GRDGMergeRenderPasses && RasterPassCount > 0)
	{
		SCOPED_NAMED_EVENT(FRDGBuilder_Compile_RenderPassMerge, FColor::Emerald);

		TArray<FRDGPassHandle, FRDGArrayAllocator> PassesToMerge;
		FRDGPass* PrevPass = nullptr;
		const FRenderTargetBindingSlots* PrevRenderTargets = nullptr;

		const auto CommitMerge = [&]
		{
			if (PassesToMerge.Num())
			{
				const auto SetEpilogueBarrierPass = [&](FRDGPass* Pass, FRDGPassHandle EpilogueBarrierPassHandle)
				{
					Pass->EpilogueBarrierPass = EpilogueBarrierPassHandle;
					Pass->ResourcesToEnd.Reset();
					Passes[EpilogueBarrierPassHandle]->ResourcesToEnd.Add(Pass);
				};

				const auto SetPrologueBarrierPass = [&](FRDGPass* Pass, FRDGPassHandle PrologueBarrierPassHandle)
				{
					Pass->PrologueBarrierPass = PrologueBarrierPassHandle;
					Pass->ResourcesToBegin.Reset();
					Passes[PrologueBarrierPassHandle]->ResourcesToBegin.Add(Pass);
				};

				const FRDGPassHandle FirstPassHandle = PassesToMerge[0];
				const FRDGPassHandle LastPassHandle = PassesToMerge.Last();
				Passes[FirstPassHandle]->ResourcesToBegin.Reserve(PassesToMerge.Num());
				Passes[LastPassHandle]->ResourcesToEnd.Reserve(PassesToMerge.Num());

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
					SetEpilogueBarrierPass(Pass, LastPassHandle);
				}

				// (X) Intermediate passes.
				for (int32 PassIndex = 1, PassCount = PassesToMerge.Num() - 1; PassIndex < PassCount; ++PassIndex)
				{
					const FRDGPassHandle PassHandle = PassesToMerge[PassIndex];
					FRDGPass* Pass = Passes[PassHandle];
					Pass->bSkipRenderPassBegin = 1;
					Pass->bSkipRenderPassEnd = 1;
					SetPrologueBarrierPass(Pass, FirstPassHandle);
					SetEpilogueBarrierPass(Pass, LastPassHandle);
				}

				// (E) Last pass in the merge sequence.
				{
					FRDGPass* Pass = Passes[LastPassHandle];
					Pass->bSkipRenderPassBegin = 1;
					SetPrologueBarrierPass(Pass, FirstPassHandle);
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
			if (PassesToCull[PassHandle] || PassesWithEmptyParameters[PassHandle])
			{
				continue;
			}

			if (PassesOnRaster[PassHandle])
			{
				FRDGPass* NextPass = Passes[PassHandle];

				// A pass where the user controls the render pass or it is forced to skip pass merging can't merge with other passes
				if (EnumHasAnyFlags(NextPass->GetFlags(), ERDGPassFlags::SkipRenderPass | ERDGPassFlags::NeverMerge))
				{
					CommitMerge();
					continue;
				}

				// A pass which writes to resources outside of the render pass introduces new dependencies which break merging.
				if (!NextPass->bRenderPassOnlyWrites)
				{
					CommitMerge();
					continue;
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
			InsertAsyncToGraphicsComputeJoin(PrevAsyncComputePass, PrevGraphicsJoinPass);
			PrevAsyncComputePass->bAsyncComputeEndExecute = 1;
		}
	}
}

void FRDGBuilder::Execute()
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(RDG);
	SCOPED_NAMED_EVENT(FRDGBuilder_Execute, FColor::Emerald);

	// Create the epilogue pass at the end of the graph just prior to compilation.
	EpiloguePass = Passes.Allocate<FRDGSentinelPass>(Allocator, RDG_EVENT_NAME("Graph Epilogue"));
	SetupEmptyPass(EpiloguePass);
	bSetupComplete = true;

	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateExecuteBegin());

	const FRDGPassHandle ProloguePassHandle = GetProloguePassHandle();
	const FRDGPassHandle EpiloguePassHandle = GetEpiloguePassHandle();

	FTransientResourceAllocator TransientResourceAllocator(RHICmdList);

	if (!IsImmediateMode())
	{
		RDG_ALLOW_RHI_ACCESS_SCOPE();
		Compile();

		IF_RDG_ENABLE_DEBUG(LogFile.Begin(BuilderName, &Passes, PassesToCull, ProloguePassHandle, EpiloguePassHandle));

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FRDGBuilder::CollectResources);
			SCOPE_CYCLE_COUNTER(STAT_RDG_CollectResourcesTime);
			CSV_SCOPED_TIMING_STAT_EXCLUSIVE(RDG_CollectResources);

			for (FRDGPassHandle PassHandle = Passes.Begin(); PassHandle != Passes.End(); ++PassHandle)
			{
				if (!PassesToCull[PassHandle])
				{
					CollectPassResources(TransientResourceAllocator, PassHandle);
				}
			}

			for (const auto& Query : ExtractedTextures)
			{
				EndResourceRHI(TransientResourceAllocator, EpiloguePassHandle, Query.Key, 1);
			}

			for (const auto& Query : ExtractedBuffers)
			{
				EndResourceRHI(TransientResourceAllocator, EpiloguePassHandle, Query.Key, 1);
			}

			EnumerateExtendedLifetimeResources(Textures, [&](FRDGTextureRef Texture)
			{
				EndResourceRHI(TransientResourceAllocator, EpiloguePassHandle, Texture, 1);
			});

			EnumerateExtendedLifetimeResources(Buffers, [&](FRDGBufferRef Buffer)
			{
				EndResourceRHI(TransientResourceAllocator, EpiloguePassHandle, Buffer, 1);
			});

			if (TransientResourceAllocator)
			{
				TransientResourceAllocator->Freeze(RHICmdList);
			}
		}

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FRDGBuilder::CreateUniformBuffers);

			for (TConstSetBitIterator It(UniformBuffersToCreate); It; ++It)
			{
				const FRDGUniformBufferHandle UniformBufferHandle(It.GetIndex());

				BeginResourceRHI(UniformBuffers[UniformBufferHandle]);
			}
		}

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FRDGBuilder::CollectBarriers);
			SCOPE_CYCLE_COUNTER(STAT_RDG_CollectBarriersTime);
			CSV_SCOPED_TIMING_STAT_EXCLUSIVE_CONDITIONAL(RDG_CollectBarriers, GRDGVerboseCSVStats != 0);

			for (FRDGPassHandle PassHandle = Passes.Begin(); PassHandle != Passes.End(); ++PassHandle)
			{
				if (!PassesToCull[PassHandle])
				{
					CollectPassBarriers(PassHandle);
				}
			}
		}
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FRDGBuilder::Finalize);

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
				AddEpilogueTransition(Texture);
				Texture->Finalize();

				IF_RDG_ENABLE_DEBUG(LogResource(Texture, Textures));
			}
		}

		for (FRDGBufferHandle BufferHandle = Buffers.Begin(); BufferHandle != Buffers.End(); ++BufferHandle)
		{
			FRDGBufferRef Buffer = Buffers[BufferHandle];

			if (Buffer->GetRHIUnchecked())
			{
				AddEpilogueTransition(Buffer);
				Buffer->Finalize();

				IF_RDG_ENABLE_DEBUG(LogResource(Buffer, Buffers));
			}
		}
	}

	IF_RDG_CPU_SCOPES(CPUScopeStacks.BeginExecute());
	IF_RDG_GPU_SCOPES(GPUScopeStacks.BeginExecute());
	IF_RDG_ENABLE_TRACE(Trace.OutputGraphBegin());

	if (!IsImmediateMode())
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

	RHICmdList.SetStaticUniformBuffers({});

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

	IF_RDG_ENABLE_TRACE(Trace.OutputGraphEnd(*this));
	IF_RDG_GPU_SCOPES(GPUScopeStacks.Graphics.EndExecute());
	IF_RDG_CPU_SCOPES(CPUScopeStacks.EndExecute());
	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateExecuteEnd());

#if STATS
	GRDGStatPassCount += Passes.Num();
	GRDGStatPassWithParameterCount = GRDGStatPassCount;
	for (FRDGPassHandle PassHandle = Passes.Begin(); PassHandle != Passes.End(); ++PassHandle)
	{
		GRDGStatPassWithParameterCount -= PassesWithEmptyParameters[PassHandle] ? 1 : 0;
	}
	GRDGStatBufferCount += Buffers.Num();
	GRDGStatTextureCount += Textures.Num();
	GRDGStatViewCount += Views.Num();
	GRDGStatMemoryWatermark = FMath::Max(GRDGStatMemoryWatermark, Allocator.GetByteCount());
#endif

	Clear();
}

void FRDGBuilder::Clear()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FRDGBuilder::Clear);
	ExternalTextures.Empty();
	ExternalBuffers.Empty();
	ExtractedTextures.Empty();
	ExtractedBuffers.Empty();
	Passes.Clear();
	Views.Clear();
	Textures.Clear();
	Buffers.Clear();
	UniformBuffers.Clear();
	Blackboard.Clear();
}

void FRDGBuilder::SetupPass(FRDGPass* Pass)
{
	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateAddPass(Pass, bInDebugPassScope));
	SCOPE_CYCLE_COUNTER(STAT_RDG_SetupTime);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(RDG_SetupPass);

	const FRDGParameterStruct PassParameters = Pass->GetParameters();
	const FRDGPassHandle PassHandle = Pass->GetHandle();
	const ERDGPassFlags PassFlags = Pass->GetFlags();
	const ERHIPipeline PassPipeline = Pass->GetPipeline();

	bool bRenderPassOnlyWrites = true;

	const auto TryAddView = [&](FRDGViewRef View)
	{
		if (View && View->LastPass != PassHandle)
		{
			View->LastPass = PassHandle;
			Pass->Views.Add(View->Handle);
		}
	};

	Pass->Views.Reserve(PassParameters.GetBufferParameterCount() + PassParameters.GetTextureParameterCount());
	Pass->TextureStates.Reserve(PassParameters.GetTextureParameterCount() + (PassParameters.HasRenderTargets() ? (MaxSimultaneousRenderTargets + 1) : 0));
	EnumerateTextureAccess(PassParameters, PassFlags, [&](FRDGViewRef TextureView, FRDGTextureRef Texture, ERHIAccess Access, ERDGTextureAccessFlags AccessFlags, FRDGTextureSubresourceRange Range)
	{
		TryAddView(TextureView);

		if (Texture->bFinalizedAccess)
		{
			// Finalized resources expected to remain in the same state, so are ignored by the graph.
			// As only External | Extracted resources can be finalized by the user, the graph doesn't
			// need to track them any more for culling / transition purposes. Validation checks that these
			// invariants are true.
			IF_RDG_ENABLE_DEBUG(UserValidation.ValidateFinalizedAccess(Texture, Access, Pass));
			return;
		}

		const FRDGViewHandle NoUAVBarrierHandle = GetHandleIfNoUAVBarrier(TextureView);
		const EResourceTransitionFlags TransitionFlags = GetTextureViewTransitionFlags(TextureView, Texture);

		FRDGPass::FTextureState* PassState;

		if (Texture->LastPass != PassHandle)
		{
			Texture->LastPass = PassHandle;
			Texture->PassStateIndex = Pass->TextureStates.Num();

			PassState = &Pass->TextureStates.Emplace_GetRef(Texture);
		}
		else
		{
			PassState = &Pass->TextureStates[Texture->PassStateIndex];
		}

		PassState->ReferenceCount++;

		const auto AddSubresourceAccess = [&](FRDGSubresourceState& State)
		{
			State.Access = MakeValidAccess(State.Access | Access);
			State.Flags |= TransitionFlags;
			State.NoUAVBarrierFilter.AddHandle(NoUAVBarrierHandle);
			State.SetPass(PassPipeline, PassHandle);
		};

		if (IsWholeResource(PassState->State))
		{
			AddSubresourceAccess(GetWholeResource(PassState->State));
		}
		else
		{
			EnumerateSubresourceRange(PassState->State, Texture->Layout, Range, AddSubresourceAccess);
		}

		const bool bWritableAccess = IsWritableAccess(Access);
		bRenderPassOnlyWrites &= (!bWritableAccess || EnumHasAnyFlags(AccessFlags, ERDGTextureAccessFlags::RenderTarget));
		Texture->bProduced |= bWritableAccess;
	});

	Pass->BufferStates.Reserve(PassParameters.GetBufferParameterCount());
	EnumerateBufferAccess(PassParameters, PassFlags, [&](FRDGViewRef BufferView, FRDGBufferRef Buffer, ERHIAccess Access)
	{
		TryAddView(BufferView);

		if (Buffer->bFinalizedAccess)
		{
			// Finalized resources expected to remain in the same state, so are ignored by the graph.
			// As only External | Extracted resources can be finalized by the user, the graph doesn't
			// need to track them any more for culling / transition purposes. Validation checks that these
			// invariants are true.
			IF_RDG_ENABLE_DEBUG(UserValidation.ValidateFinalizedAccess(Buffer, Access, Pass));
			return;
		}

		const FRDGViewHandle NoUAVBarrierHandle = GetHandleIfNoUAVBarrier(BufferView);

		FRDGPass::FBufferState* PassState;

		if (Buffer->LastPass != PassHandle)
		{
			Buffer->LastPass = PassHandle;
			Buffer->PassStateIndex = Pass->BufferStates.Num();

			PassState = &Pass->BufferStates.Emplace_GetRef(Buffer);
		}
		else
		{
			PassState = &Pass->BufferStates[Buffer->PassStateIndex];
		}

		PassState->ReferenceCount++;
		PassState->State.Access = MakeValidAccess(PassState->State.Access | Access);
		PassState->State.NoUAVBarrierFilter.AddHandle(NoUAVBarrierHandle);
		PassState->State.SetPass(PassPipeline, PassHandle);

		const bool bWritableAccess = IsWritableAccess(Access);
		bRenderPassOnlyWrites &= !bWritableAccess;
		Buffer->bProduced |= bWritableAccess;
	});

	PassParameters.EnumerateUniformBuffers([&](FRDGUniformBufferBinding UniformBuffer)
	{
		UniformBuffersToCreate[UniformBuffer->Handle] = true;
	});

	Pass->bRenderPassOnlyWrites = bRenderPassOnlyWrites;
	Pass->bHasExternalOutputs = PassParameters.HasExternalOutputs();

	const bool bEmptyParameters = !Pass->TextureStates.Num() && !Pass->BufferStates.Num();
	PassesWithEmptyParameters.Add(bEmptyParameters);

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

	if (!EnumHasAnyFlags(Pass->GetFlags(), ERDGPassFlags::AsyncCompute))
	{
		Pass->ResourcesToBegin = { Pass };
		Pass->ResourcesToEnd   = { Pass };
	}

#if WITH_MGPU
	Pass->GPUMask = RHICmdList.GetGPUMask();
#endif

#if STATS
	Pass->CommandListStat = CommandListStat;
#endif

	IF_RDG_CPU_SCOPES(Pass->CPUScopes = CPUScopeStacks.GetCurrentScopes());
	IF_RDG_GPU_SCOPES(Pass->GPUScopes = GPUScopeStacks.GetCurrentScopes(PassPipeline));

#if RDG_GPU_SCOPES && RDG_ENABLE_TRACE
	Pass->TraceEventScope = GPUScopeStacks.GetCurrentScopes(ERHIPipeline::Graphics).Event;
#endif

#if RDG_GPU_SCOPES && RDG_ENABLE_DEBUG
	if (const FRDGEventScope* Scope = Pass->GPUScopes.Event)
	{
		Pass->FullPathIfDebug = Scope->GetPath(Pass->Name);
	}
#endif

	if (IsImmediateMode() && Pass != EpiloguePass)
	{
		RDG_ALLOW_RHI_ACCESS_SCOPE();

		// Trivially redirect the merge states to the pass states, since we won't be compiling the graph.
		for (auto& PassState : Pass->TextureStates)
		{
			const uint32 SubresourceCount = PassState.State.Num();
			PassState.MergeState.SetNum(SubresourceCount);
			for (uint32 Index = 0; Index < SubresourceCount; ++Index)
			{
				if (PassState.State[Index].Access != ERHIAccess::Unknown)
				{
					PassState.MergeState[Index] = &PassState.State[Index];
				}
			}
		}

		for (auto& PassState : Pass->BufferStates)
		{
			PassState.MergeState = &PassState.State;
		}

		check(!EnumHasAnyFlags(PassPipeline, ERHIPipeline::AsyncCompute));

		// Transient allocator should not be initialized in immediate mode.
		{
			FTransientResourceAllocator TransientResourceAllocator(RHICmdList);
			CollectPassResources(TransientResourceAllocator, PassHandle);
			check(!TransientResourceAllocator);
		}

		Pass->GetParameters().EnumerateUniformBuffers([&](FRDGUniformBufferBinding UniformBuffer)
		{
			BeginResourceRHI(UniformBuffer.GetUniformBuffer());
		});

		CollectPassBarriers(PassHandle);
		ExecutePass(Pass);
	}

	IF_RDG_ENABLE_DEBUG(VisualizePassOutputs(Pass));
}

void FRDGBuilder::ExecutePassPrologue(FRHIComputeCommandList& RHICmdListPass, FRDGPass* Pass)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FRDGBuilder_ExecutePassPrologue);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE_CONDITIONAL(RDGBuilder_ExecutePassPrologue, GRDGVerboseCSVStats != 0);

	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateExecutePassBegin(Pass));
	IF_RDG_CMDLIST_STATS(RHICmdList.SetCurrentStat(Pass->CommandListStat));

	const ERDGPassFlags PassFlags = Pass->GetFlags();
	const ERHIPipeline PassPipeline = Pass->GetPipeline();

	if (Pass->PrologueBarriersToBegin)
	{
		IF_RDG_ENABLE_DEBUG(BarrierValidation.ValidateBarrierBatchBegin(Pass, *Pass->PrologueBarriersToBegin));
		Pass->PrologueBarriersToBegin->Submit(RHICmdListPass, PassPipeline);
	}

	if (Pass->PrologueBarriersToEnd)
	{
		IF_RDG_ENABLE_DEBUG(BarrierValidation.ValidateBarrierBatchEnd(Pass, *Pass->PrologueBarriersToEnd));
		Pass->PrologueBarriersToEnd->Submit(RHICmdListPass, PassPipeline);
	}

	if (PassPipeline == ERHIPipeline::AsyncCompute)
	{
		RHICmdListPass.SetAsyncComputeBudget(Pass->AsyncComputeBudget);
	}

	if (EnumHasAnyFlags(PassFlags, ERDGPassFlags::Raster))
	{
		if (!EnumHasAnyFlags(PassFlags, ERDGPassFlags::SkipRenderPass) && !Pass->SkipRenderPassBegin())
		{
			static_cast<FRHICommandList&>(RHICmdListPass).BeginRenderPass(Pass->GetParameters().GetRenderPassInfo(), Pass->GetName());
		}
	}

	BeginUAVOverlap(Pass, RHICmdListPass);
}

void FRDGBuilder::ExecutePassEpilogue(FRHIComputeCommandList& RHICmdListPass, FRDGPass* Pass)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FRDGBuilder_ExecutePassEpilogue);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE_CONDITIONAL(RDGBuilder_ExecutePassEpilogue, GRDGVerboseCSVStats != 0);

	EndUAVOverlap(Pass, RHICmdListPass);

	const ERDGPassFlags PassFlags = Pass->GetFlags();
	const ERHIPipeline PassPipeline = Pass->GetPipeline();
	const FRDGParameterStruct PassParameters = Pass->GetParameters();

	if (EnumHasAnyFlags(PassFlags, ERDGPassFlags::Raster) && !EnumHasAnyFlags(PassFlags, ERDGPassFlags::SkipRenderPass) && !Pass->SkipRenderPassEnd())
	{
		static_cast<FRHICommandList&>(RHICmdListPass).EndRenderPass();
	}

	FRDGTransitionQueue Transitions;

	if (Pass->EpilogueBarriersToBeginForGraphics)
	{
		IF_RDG_ENABLE_DEBUG(BarrierValidation.ValidateBarrierBatchBegin(Pass, *Pass->EpilogueBarriersToBeginForGraphics));
		Pass->EpilogueBarriersToBeginForGraphics->Submit(RHICmdListPass, PassPipeline, Transitions);
	}

	if (Pass->EpilogueBarriersToBeginForAsyncCompute)
	{
		IF_RDG_ENABLE_DEBUG(BarrierValidation.ValidateBarrierBatchBegin(Pass, *Pass->EpilogueBarriersToBeginForAsyncCompute));
		Pass->EpilogueBarriersToBeginForAsyncCompute->Submit(RHICmdListPass, PassPipeline, Transitions);
	}

	if (Pass->EpilogueBarriersToBeginForAll)
	{
		IF_RDG_ENABLE_DEBUG(BarrierValidation.ValidateBarrierBatchBegin(Pass, *Pass->EpilogueBarriersToBeginForAll));
		Pass->EpilogueBarriersToBeginForAll->Submit(RHICmdListPass, PassPipeline, Transitions);
	}

	for (FRDGBarrierBatchBegin* BarriersToBegin : Pass->SharedEpilogueBarriersToBegin)
	{
		IF_RDG_ENABLE_DEBUG(BarrierValidation.ValidateBarrierBatchBegin(Pass, *BarriersToBegin));
		BarriersToBegin->Submit(RHICmdListPass, PassPipeline, Transitions);
	}

	Transitions.Begin(RHICmdListPass);

	if (Pass->EpilogueBarriersToEnd)
	{
		IF_RDG_ENABLE_DEBUG(BarrierValidation.ValidateBarrierBatchEnd(Pass, *Pass->EpilogueBarriersToEnd));
		Pass->EpilogueBarriersToEnd->Submit(RHICmdListPass, PassPipeline);
	}

	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateExecutePassEnd(Pass));
}

void FRDGBuilder::ExecutePass(FRDGPass* Pass)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FRDGBuilder_ExecutePass);

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

void FRDGBuilder::CollectPassResources(FTransientResourceAllocator& TransientResourceAllocator, FRDGPassHandle PassHandle)
{
	FRDGPass* Pass = Passes[PassHandle];

	for (FRDGPass* PassToBegin : Pass->ResourcesToBegin)
	{
		for (const auto& PassState : PassToBegin->TextureStates)
		{
			BeginResourceRHI(&TransientResourceAllocator, PassHandle, PassState.Texture);
		}

		for (const auto& PassState : PassToBegin->BufferStates)
		{
			BeginResourceRHI(&TransientResourceAllocator, PassHandle, PassState.Buffer);
		}

		for (FRDGViewHandle ViewHandle : PassToBegin->Views)
		{
			BeginResourceRHI(PassHandle, Views[ViewHandle]);
		}
	}

	for (FRDGPass* PassToEnd : Pass->ResourcesToEnd)
	{
		for (const auto& PassState : PassToEnd->TextureStates)
		{
			EndResourceRHI(TransientResourceAllocator, PassHandle, PassState.Texture, PassState.ReferenceCount);
		}

		for (const auto& PassState : PassToEnd->BufferStates)
		{
			EndResourceRHI(TransientResourceAllocator, PassHandle, PassState.Buffer, PassState.ReferenceCount);
		}
	}
}

void FRDGBuilder::CollectPassBarriers(FRDGPassHandle PassHandle)
{
	FRDGPass* Pass = Passes[PassHandle];

	IF_RDG_ENABLE_DEBUG(ConditionalDebugBreak(RDG_BREAKPOINT_PASS_COMPILE, BuilderName.GetTCHAR(), Pass->GetName()));
	if (PassesWithEmptyParameters[PassHandle])
	{
		return;
	}

	for (const auto& PassState : Pass->TextureStates)
	{
		FRDGTextureRef Texture = PassState.Texture;
		AddTransition(PassHandle, Texture, PassState.MergeState);
		Texture->bCulled = false;

		IF_RDG_ENABLE_TRACE(Trace.AddTexturePassDependency(Texture, Pass));
	}

	for (const auto& PassState : Pass->BufferStates)
	{
		FRDGBufferRef Buffer = PassState.Buffer;
		AddTransition(PassHandle, Buffer, *PassState.MergeState);
		Buffer->bCulled = false;

		IF_RDG_ENABLE_TRACE(Trace.AddBufferPassDependency(Buffer, Pass));
	}
}

void FRDGBuilder::AddEpilogueTransition(FRDGTextureRef Texture)
{
	if (!Texture->bLastOwner || Texture->bCulled || Texture->bFinalizedAccess)
	{
		return;
	}

	const FRDGPassHandle EpiloguePassHandle = GetEpiloguePassHandle();

	FRDGSubresourceState ScratchSubresourceState;
	EBarrierLocation BarrierLocation = EBarrierLocation::Prologue;

	// Texture is using the RHI transient allocator. Transition it back to Discard in the final pass it is used.
	if (Texture->bTransient)
	{
		ScratchSubresourceState.SetPass(ERHIPipeline::Graphics, Texture->LastPass);
		ScratchSubresourceState.Access = ERHIAccess::Discard;
		InitAsWholeResource(ScratchTextureState, &ScratchSubresourceState);

		BarrierLocation = EBarrierLocation::Epilogue;
	}
	// A known final state means extraction from the graph (or an external texture).
	else if(Texture->AccessFinal != ERHIAccess::Unknown)
	{
		ScratchSubresourceState.SetPass(ERHIPipeline::Graphics, EpiloguePassHandle);
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
			if (SubresourceState.IsUsedBy(ERHIPipeline::AsyncCompute))
			{
				SubresourceState.SetPass(ERHIPipeline::Graphics, EpiloguePassHandle);

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

	AddTransition(EpiloguePassHandle, Texture, ScratchTextureState, BarrierLocation);
	ScratchTextureState.Reset();
}

void FRDGBuilder::AddEpilogueTransition(FRDGBufferRef Buffer)
{
	if (!Buffer->bLastOwner || Buffer->bCulled || Buffer->bFinalizedAccess)
	{
		return;
	}

	const FRDGPassHandle EpiloguePassHandle = GetEpiloguePassHandle();

	if (Buffer->bTransient)
	{
		FRDGSubresourceState StateFinal;
		StateFinal.SetPass(ERHIPipeline::Graphics, Buffer->LastPass);
		StateFinal.Access = ERHIAccess::Discard;
		AddTransition(Buffer->LastPass, Buffer, StateFinal, EBarrierLocation::Epilogue);
	}
	else
	{
		ERHIAccess AccessFinal = Buffer->AccessFinal;

		// Transition async compute back to the graphics pipe.
		if (AccessFinal == ERHIAccess::Unknown)
		{
			const FRDGSubresourceState State = Buffer->GetState();

			if (State.IsUsedBy(ERHIPipeline::AsyncCompute))
			{
				AccessFinal = State.Access;
			}
		}

		if (AccessFinal != ERHIAccess::Unknown)
		{
			FRDGSubresourceState StateFinal;
			StateFinal.SetPass(ERHIPipeline::Graphics, EpiloguePassHandle);
			StateFinal.Access = AccessFinal;
			AddTransition(EpiloguePassHandle, Buffer, StateFinal);
		}
	}
}

void FRDGBuilder::AddTransition(FRDGPassHandle PassHandle, FRDGTextureRef Texture, const FRDGTextureTransientSubresourceStateIndirect& StateAfter, EBarrierLocation BarrierLocation)
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

		if (FRDGSubresourceState::IsTransitionRequired(SubresourceStateBefore, SubresourceStateAfter))
		{
			FRHITransitionInfo Info;
			Info.Texture = Texture->GetRHIUnchecked();
			Info.Type = FRHITransitionInfo::EType::Texture;
			Info.Flags = SubresourceStateAfter.Flags;
			Info.AccessBefore = SubresourceStateBefore.Access;
			Info.AccessAfter = SubresourceStateAfter.Access;

			if (Info.AccessBefore == ERHIAccess::Discard)
			{
				Info.Flags |= EResourceTransitionFlags::Discard;
			}

			if (Subresource)
			{
				Info.MipIndex = Subresource->MipIndex;
				Info.ArraySlice = Subresource->ArraySlice;
				Info.PlaneSlice = Subresource->PlaneSlice;
			}

			AddTransition(Texture, SubresourceStateBefore, SubresourceStateAfter, BarrierLocation, Info);
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

		for (ERHIPipeline Pipeline : GetRHIPipelines())
		{
			if (SubresourceStateBefore.IsUsedBy(Pipeline))
			{
				SubresourceStateBefore.FirstPass[Pipeline] = PassHandle;
			}
		}
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
				MergeSubresourceState(SubresourceStateBefore, *SubresourceStateAfter);
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

void FRDGBuilder::AddTransition(FRDGPassHandle PassHandle, FRDGBufferRef Buffer, FRDGSubresourceState StateAfter, EBarrierLocation BarrierLocation)
{
	check(StateAfter.Access != ERHIAccess::Unknown);

	FRDGSubresourceState& StateBefore = Buffer->GetState();

	if (FRDGSubresourceState::IsTransitionRequired(StateBefore, StateAfter))
	{
		FRHITransitionInfo Info;
		Info.Resource = Buffer->GetRHIUnchecked();
		Info.Type = FRHITransitionInfo::EType::Buffer;
		Info.Flags = StateAfter.Flags;
		Info.AccessBefore = StateBefore.Access;
		Info.AccessAfter = StateAfter.Access;

		AddTransition(Buffer, StateBefore, StateAfter, BarrierLocation, Info);
	}

	IF_RDG_ENABLE_DEBUG(LogFile.AddTransitionEdge(PassHandle, StateBefore, StateAfter, Buffer));
	StateBefore = StateAfter;
}

void FRDGBuilder::AddTransition(
	FRDGParentResource* Resource,
	FRDGSubresourceState StateBefore,
	FRDGSubresourceState StateAfter,
	EBarrierLocation BarrierLocation,
	const FRHITransitionInfo& TransitionInfo)
{
	const ERHIPipeline Graphics = ERHIPipeline::Graphics;
	const ERHIPipeline AsyncCompute = ERHIPipeline::AsyncCompute;

#if RDG_ENABLE_DEBUG
	StateBefore.Validate();
	StateAfter.Validate();
#endif

	if (IsImmediateMode())
	{
		// Immediate mode simply enqueues the barrier into the 'after' pass. Everything is on the graphics pipe.
		AddToBarriers(StateAfter.FirstPass[Graphics], BarrierLocation, [&](FRDGBarrierBatchBegin& Barriers)
		{
			Barriers.AddTransition(Resource, TransitionInfo);
		});
		return;
	}

	ERHIPipeline PipelinesBefore = StateBefore.GetPipelines();
	ERHIPipeline PipelinesAfter = StateAfter.GetPipelines();

	// This may be the first use of the resource in the graph, so we assign the prologue as the previous pass.
	if (PipelinesBefore == ERHIPipeline::None)
	{
		StateBefore.SetPass(Graphics, GetProloguePassHandle());
		PipelinesBefore = Graphics;
	}

	check(PipelinesBefore != ERHIPipeline::None && PipelinesAfter != ERHIPipeline::None);
	checkf(StateBefore.GetLastPass() <= StateAfter.GetFirstPass(), TEXT("Submitted a state for '%s' that begins before our previous state has ended."), Resource->Name);
	check(!Resource->bTransient || StateBefore.GetLastPass() >= Resource->FirstPass);

	FRDGPassHandlesByPipeline& PassesBefore = StateBefore.LastPass;
	FRDGPassHandlesByPipeline& PassesAfter  = StateAfter.FirstPass;

	const uint32 PipelinesBeforeCount = FMath::CountBits(uint64(PipelinesBefore));
	const uint32 PipelinesAfterCount = FMath::CountBits(uint64(PipelinesAfter));

	// 1-to-1 or 1-to-N pipe transition.
	if (PipelinesBeforeCount == 1)
	{
		const FRDGPassHandle BeginPassHandle = StateBefore.GetLastPass();
		const FRDGPassHandle FirstEndPassHandle = StateAfter.GetFirstPass();

		FRDGPass* BeginPass = nullptr;
		FRDGBarrierBatchBegin* BarriersToBegin = nullptr;

		// Issue the begin in the epilogue of the begin pass if the barrier is being split across multiple passes or the barrier end is in the epilogue.
		if (BeginPassHandle < FirstEndPassHandle || BarrierLocation == EBarrierLocation::Epilogue)
		{
			BeginPass = GetEpilogueBarrierPass(BeginPassHandle);
			BarriersToBegin = &BeginPass->GetEpilogueBarriersToBeginFor(Allocator, PipelinesAfter);
		}
		// This is an immediate prologue transition in the same pass. Issue the begin in the prologue.
		else
		{
			checkf(PipelinesAfter == ERHIPipeline::Graphics,
				TEXT("Attempted to queue an immediate async pipe transition for %s. Pipelines: %s. Async transitions must be split."),
				Resource->Name, *GetRHIPipelineName(PipelinesAfter));

			BeginPass = GetPrologueBarrierPass(BeginPassHandle);
			BarriersToBegin = &BeginPass->GetPrologueBarriersToBegin(Allocator);
		}

		BarriersToBegin->AddTransition(Resource, TransitionInfo);

		for (ERHIPipeline Pipeline : GetRHIPipelines())
		{
			/** If doing a 1-to-N transition and this is the same pipe as the begin, we end it immediately afterwards in the epilogue
			 *  of the begin pass. This is because we can't guarantee that the other pipeline won't join back before the end. This can
			 *  happen if the forking async compute pass joins back to graphics (via another independent transition) before the current
			 *  graphics transition is ended.
			 *
			 *  Async Compute Pipe:               EndA  BeginB
			 *                                   /            \
			 *  Graphics Pipe:            BeginA               EndB   EndA
			 *
			 *  A is our 1-to-N transition and B is a future transition of the same resource that we haven't evaluated yet. Instead, the
			 *  same pipe End is performed in the epilogue of the begin pass, which removes the spit barrier but simplifies the tracking:
			 *
			 *  Async Compute Pipe:               EndA  BeginB
			 *                                   /            \
			 *  Graphics Pipe:            BeginA  EndA         EndB
			 */
			if ((PipelinesBefore == Pipeline && PipelinesAfterCount > 1))
			{
				AddToEpilogueBarriersToEnd(BeginPassHandle, *BarriersToBegin);
			}
			else if (EnumHasAnyFlags(PipelinesAfter, Pipeline))
			{
				if (BarrierLocation == EBarrierLocation::Prologue)
				{
					AddToPrologueBarriersToEnd(PassesAfter[Pipeline], *BarriersToBegin);
				}
				else
				{
					AddToEpilogueBarriersToEnd(PassesAfter[Pipeline], *BarriersToBegin);
				}
			}
		}
	}
	// N-to-1 or N-to-N transition.
	else
	{
		checkf(StateBefore.GetLastPass() != StateAfter.GetFirstPass() || BarrierLocation == EBarrierLocation::Epilogue,
			TEXT("Attempted to queue a transition for resource '%s' from '%s' to '%s', but previous and next passes are the same on one pipe."),
			Resource->Name, *GetRHIPipelineName(PipelinesBefore), *GetRHIPipelineName(PipelinesAfter));

		FRDGBarrierBatchBeginId Id;
		Id.PipelinesAfter = PipelinesAfter;
		for (ERHIPipeline Pipeline : GetRHIPipelines())
		{
			Id.Passes[Pipeline] = GetEpilogueBarrierPassHandle(PassesBefore[Pipeline]);
		}

		FRDGBarrierBatchBegin*& BarriersToBegin = BarrierBatchMap.FindOrAdd(Id);

		if (!BarriersToBegin)
		{
			BarriersToBegin = Allocator.AllocNoDestruct<FRDGBarrierBatchBegin>(PipelinesBefore, PipelinesAfter, GetEpilogueBarriersToBeginDebugName(PipelinesAfter), Id.Passes);

			for (FRDGPassHandle PassHandle : Id.Passes)
			{
				Passes[PassHandle]->SharedEpilogueBarriersToBegin.Add(BarriersToBegin);
			}
		}

		BarriersToBegin->AddTransition(Resource, TransitionInfo);

		for (ERHIPipeline Pipeline : GetRHIPipelines())
		{
			if (EnumHasAnyFlags(PipelinesAfter, Pipeline))
			{
				if (BarrierLocation == EBarrierLocation::Prologue)
				{
					AddToPrologueBarriersToEnd(PassesAfter[Pipeline], *BarriersToBegin);
				}
				else
				{
					AddToEpilogueBarriersToEnd(PassesAfter[Pipeline], *BarriersToBegin);
				}
			}
		}
	}
}

void FRDGBuilder::BeginResourceRHI(FRDGUniformBuffer* UniformBuffer)
{
	check(UniformBuffer);

	if (UniformBuffer->GetRHIUnchecked())
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

void FRDGBuilder::BeginResourceRHI(FTransientResourceAllocator* TransientResourceAllocator, FRDGPassHandle PassHandle, FRDGTextureRef Texture)
{
	check(Texture);

	if (Texture->GetRHIUnchecked())
	{
		return;
	}

	check(TransientResourceAllocator  || Texture->bExternal);
	check(Texture->ReferenceCount > 0 || Texture->bExternal || IsImmediateMode());

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

	if (IsTransient(Texture))
	{
		check(TransientResourceAllocator);

		if (IRHITransientResourceAllocator* AllocatorRHI = TransientResourceAllocator->GetOrCreate())
		{
			if (FRHITransientTexture* TransientTexture = AllocatorRHI->CreateTexture(Texture->Desc, Texture->Name))
			{
				Texture->SetRHI(TransientTexture, Allocator);

				check(GetPrologueBarrierPassHandle(PassHandle) == PassHandle);

				FRDGSubresourceState InitialState;
				InitialState.SetPass(ERHIPipeline::Graphics, PassHandle);
				InitialState.Access = ERHIAccess::Discard;
				InitAsWholeResource(Texture->GetState(), InitialState);

				AddToPrologueBarriers(PassHandle, [&](FRDGBarrierBatchBegin& Barriers)
				{
					Barriers.AddAlias(Texture, FRHITransientAliasingInfo::Acquire(Texture->GetRHIUnchecked(), TransientTexture->GetAliasingOverlaps()));
				});

			#if STATS
				GRDGStatTransientTextureCount++;
			#endif
			}
		}
	}

	if (!Texture->bTransient)
	{
		Texture->SetRHI(GRenderTargetPool.FindFreeElementForRDG(RHICmdList, Texture->Desc, Texture->Name));
	}

	Texture->FirstPass = PassHandle;

	check(Texture->GetRHIUnchecked());
}

void FRDGBuilder::BeginResourceRHI(FRDGPassHandle PassHandle, FRDGTextureSRVRef SRV)
{
	check(SRV);

	if (SRV->GetRHIUnchecked())
	{
		return;
	}

	FRDGTextureRef Texture = SRV->Desc.Texture;
	FRHITexture* TextureRHI = Texture->GetRHIUnchecked();
	check(TextureRHI);

	SRV->ResourceRHI = Texture->ViewCache->GetOrCreateSRV(TextureRHI, SRV->Desc);
}

void FRDGBuilder::BeginResourceRHI(FRDGPassHandle PassHandle, FRDGTextureUAVRef UAV)
{
	check(UAV);

	if (UAV->GetRHIUnchecked())
	{
		return;
	}

	FRDGTextureRef Texture = UAV->Desc.Texture;
	FRHITexture* TextureRHI = Texture->GetRHIUnchecked();
	check(TextureRHI);

	UAV->ResourceRHI = Texture->ViewCache->GetOrCreateUAV(TextureRHI, UAV->Desc);
}

void FRDGBuilder::BeginResourceRHI(FTransientResourceAllocator* TransientResourceAllocator, FRDGPassHandle PassHandle, FRDGBufferRef Buffer)
{
	check(Buffer);

	if (Buffer->GetRHIUnchecked())
	{
		return;
	}

	check(TransientResourceAllocator || Buffer->bExternal);
	check(Buffer->ReferenceCount > 0 || Buffer->bExternal || IsImmediateMode());

	// If transient then create the resource on the transient allocator. External or extracted resource can't be transient because of lifetime tracking issues.
	if (IsTransient(Buffer))
	{
		check(TransientResourceAllocator);

		if (IRHITransientResourceAllocator* AllocatorRHI = TransientResourceAllocator->GetOrCreate())
		{
			if (FRHITransientBuffer* TransientBuffer = AllocatorRHI->CreateBuffer(Translate(Buffer->Desc), Buffer->Name))
			{
				Buffer->SetRHI(TransientBuffer, Allocator);

				check(GetPrologueBarrierPassHandle(PassHandle) == PassHandle);

				FRDGSubresourceState& InitialState = Buffer->GetState();
				InitialState.SetPass(ERHIPipeline::Graphics, PassHandle);
				InitialState.Access = ERHIAccess::Discard;

				AddToPrologueBarriers(PassHandle, [&](FRDGBarrierBatchBegin& Barriers)
				{
					Barriers.AddAlias(Buffer, FRHITransientAliasingInfo::Acquire(Buffer->GetRHIUnchecked(), TransientBuffer->GetAliasingOverlaps()));
				});

			#if STATS
				GRDGStatTransientBufferCount++;
			#endif
			}
		}
	}

	if (!Buffer->bTransient)
	{
		Buffer->SetRHI(GRenderGraphResourcePool.FindFreeBufferInternal(RHICmdList, Buffer->Desc, Buffer->Name));
	}

	Buffer->FirstPass = PassHandle;

	check(Buffer->GetRHIUnchecked());
}

void FRDGBuilder::BeginResourceRHI(FRDGPassHandle PassHandle, FRDGBufferSRVRef SRV)
{
	check(SRV);

	if (SRV->GetRHIUnchecked())
	{
		return;
	}

	FRDGBufferRef Buffer = SRV->Desc.Buffer;
	FRHIBuffer* BufferRHI = Buffer->GetRHIUnchecked();
	check(BufferRHI);

	FRHIBufferSRVCreateInfo SRVCreateInfo = SRV->Desc;

	if (Buffer->Desc.UnderlyingType == FRDGBufferDesc::EUnderlyingType::StructuredBuffer)
	{
		// RDG allows structured buffer views to be typed, but the view creation logic requires that it
		// be unknown (as do platform APIs -- structured buffers are not typed). This could be validated
		// at the high level but the current API makes it confusing. For now, it's considered a no-op.
		SRVCreateInfo.Format = PF_Unknown;
	}

	SRV->ResourceRHI = Buffer->ViewCache->GetOrCreateSRV(BufferRHI, SRVCreateInfo);
}

void FRDGBuilder::BeginResourceRHI(FRDGPassHandle PassHandle, FRDGBufferUAV* UAV)
{
	check(UAV);

	if (UAV->GetRHIUnchecked())
	{
		return;
	}

	FRDGBufferRef Buffer = UAV->Desc.Buffer;
	check(Buffer);

	FRHIBufferUAVCreateInfo UAVCreateInfo = UAV->Desc;

	if (Buffer->Desc.UnderlyingType == FRDGBufferDesc::EUnderlyingType::StructuredBuffer)
	{
		// RDG allows structured buffer views to be typed, but the view creation logic requires that it
		// be unknown (as do platform APIs -- structured buffers are not typed). This could be validated
		// at the high level but the current API makes it confusing. For now, it's considered a no-op.
		UAVCreateInfo.Format = PF_Unknown;
	}

	UAV->ResourceRHI = Buffer->ViewCache->GetOrCreateUAV(Buffer->GetRHIUnchecked(), UAVCreateInfo);
}

void FRDGBuilder::BeginResourceRHI(FRDGPassHandle PassHandle, FRDGView* View)
{
	if (View->GetRHIUnchecked())
	{
		return;
	}

	switch (View->Type)
	{
	case ERDGViewType::TextureUAV:
		BeginResourceRHI(PassHandle, static_cast<FRDGTextureUAV*>(View));
		break;
	case ERDGViewType::TextureSRV:
		BeginResourceRHI(PassHandle, static_cast<FRDGTextureSRV*>(View));
		break;
	case ERDGViewType::BufferUAV:
		BeginResourceRHI(PassHandle, static_cast<FRDGBufferUAV*>(View));
		break;
	case ERDGViewType::BufferSRV:
		BeginResourceRHI(PassHandle, static_cast<FRDGBufferSRV*>(View));
		break;
	}
}

void FRDGBuilder::EndResourceRHI(FTransientResourceAllocator& TransientResourceAllocator, FRDGPassHandle PassHandle, FRDGTextureRef Texture, uint32 ReferenceCount)
{
	check(Texture);
	check(Texture->ReferenceCount >= ReferenceCount || IsImmediateMode());
	Texture->ReferenceCount -= ReferenceCount;

	if (Texture->ReferenceCount == 0)
	{
		if (Texture->bTransient)
		{
			TransientResourceAllocator->DeallocateMemory(Texture->TransientTexture);

			AddToEpilogueBarriers(PassHandle, [&](FRDGBarrierBatchBegin& Barriers)
			{
				Barriers.AddAlias(Texture, FRHITransientAliasingInfo::Discard(Texture->GetRHIUnchecked()));
			});
		}
		else if (Texture->PooledRenderTarget && Texture->PooledRenderTarget->IsTracked())
		{
			Texture->Allocation = nullptr;
		}

		Texture->LastPass = PassHandle;
	}
}

void FRDGBuilder::EndResourceRHI(FTransientResourceAllocator& TransientResourceAllocator, FRDGPassHandle PassHandle, FRDGBufferRef Buffer, uint32 ReferenceCount)
{
	check(Buffer);
	check(Buffer->ReferenceCount >= ReferenceCount || IsImmediateMode());
	Buffer->ReferenceCount -= ReferenceCount;

	if (Buffer->ReferenceCount == 0)
	{
		if (Buffer->bTransient)
		{
			TransientResourceAllocator->DeallocateMemory(Buffer->TransientBuffer);

			AddToEpilogueBarriers(PassHandle, [&](FRDGBarrierBatchBegin& Barriers)
			{
				Barriers.AddAlias(Buffer, FRHITransientAliasingInfo::Discard(Buffer->GetRHIUnchecked()));
			});
		}
		else
		{
			Buffer->Allocation = nullptr;
		}

		Buffer->LastPass = PassHandle;
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
							AddClearUAVPass(*this, CreateUAV(FRDGTextureUAVDesc(Texture, MipLevel)), ClobberColor);
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
							AddClearUAVPass(*this, CreateUAV(FRDGTextureUAVDesc(Texture, MipLevel)), ClobberColor);
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