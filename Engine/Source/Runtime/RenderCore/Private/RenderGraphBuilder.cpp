// Copyright Epic Games, Inc. All Rights Reserved.

#include "RenderGraphBuilder.h"
#include "RenderGraphPrivate.h"
#include "RenderTargetPool.h"
#include "RenderGraphResourcePool.h"
#include "VisualizeTexture.h"
#include "ProfilingDebugging/CsvProfiler.h"

inline bool IsSubresourceTrackingRequired(EResourceTransitionAccess Access, EResourceTransitionFlags Flags)
{
	// Aggressively merge into a whole union state if possible. Otherwise, revert to subresource tracking.
	return !IsValidAccess(Access) || Flags != EResourceTransitionFlags::None;
}

inline void GetPassAccess(ERDGPassFlags PassFlags, EResourceTransitionAccess& SRVAccess, EResourceTransitionAccess& UAVAccess)
{
	SRVAccess = EResourceTransitionAccess::Unknown;
	UAVAccess = EResourceTransitionAccess::Unknown;

	if (EnumHasAnyFlags(PassFlags, ERDGPassFlags::Raster))
	{
		SRVAccess |= EResourceTransitionAccess::SRVGraphics;
		UAVAccess |= EResourceTransitionAccess::UAVGraphics;
	}

	if (EnumHasAnyFlags(PassFlags, ERDGPassFlags::Compute | ERDGPassFlags::AsyncCompute))
	{
		SRVAccess |= EResourceTransitionAccess::SRVCompute;
		UAVAccess |= EResourceTransitionAccess::UAVCompute;
	}

	if (EnumHasAnyFlags(PassFlags, ERDGPassFlags::Copy))
	{
		SRVAccess |= EResourceTransitionAccess::CopySrc;
	}
}

/** Enumerates all texture accesses and provides the access and subresource range info. This results in
 *  multiple invocations of the same resource, but with different access / subresource range.
 */
template <typename TAccessFunction>
void EnumerateTextureAccess(FRDGPassParameterStruct PassParameters, ERDGPassFlags PassFlags, TAccessFunction AccessFunction)
{
	EResourceTransitionAccess SRVAccess, UAVAccess;
	GetPassAccess(PassFlags, SRVAccess, UAVAccess);

	for (uint32 Index = 0; Index < PassParameters.GetTextureParameterCount(); ++Index)
	{
		const FRDGPassParameter Parameter = PassParameters.GetTextureParameter(Index);
		switch (Parameter.GetType())
		{
		case UBMT_RDG_TEXTURE:
			if (FRDGTextureRef Texture = Parameter.GetAsTexture())
			{
				AccessFunction(nullptr, Texture, SRVAccess, Texture->GetSubresourceRange());
			}
		break;
		case UBMT_RDG_TEXTURE_COPY_DEST:
			if (FRDGTextureRef Texture = Parameter.GetAsTexture())
			{
				AccessFunction(nullptr, Texture, EResourceTransitionAccess::CopyDest, Texture->GetSubresourceRange());
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
			const EResourceTransitionAccess RTVAccess = EResourceTransitionAccess::RTV;

			const FRenderTargetBindingSlots& RenderTargets = Parameter.GetAsRenderTargetBindingSlots();

			RenderTargets.Enumerate([&](FRenderTargetBinding RenderTarget)
			{
				FRDGTextureRef Texture = RenderTarget.GetTexture();

				FRDGTextureSubresourceRange Range(Texture->GetSubresourceRange());
				Range.MipIndex = RenderTarget.GetMipIndex();
				Range.NumMips = 1;

				if (RenderTarget.GetArraySlice() != -1)
				{
					Range.ArraySlice = RenderTarget.GetArraySlice();
					Range.NumArraySlices = 1;
				}

				AccessFunction(nullptr, Texture, RTVAccess, Range);
			});

			const FDepthStencilBinding& DepthStencil = RenderTargets.DepthStencil;

			if (FRDGTextureRef Texture = DepthStencil.GetTexture())
			{
				DepthStencil.GetDepthStencilAccess().EnumerateSubresources([&](EResourceTransitionAccess NewAccess, uint32 PlaneSlice)
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
		}
		break;
		}
	}
}

/** Enumerates all texture pass parameters and calls the provided function. The input function must accept an FRDGResource*,
 *  since either views or textures are provided.
 */
template <typename TParameterFunction>
void EnumerateTextureParameters(FRDGPassParameterStruct PassParameters, TParameterFunction ParameterFunction)
{
	for (uint32 Index = 0; Index < PassParameters.GetTextureParameterCount(); ++Index)
	{
		const FRDGPassParameter Parameter = PassParameters.GetTextureParameter(Index);
		switch (Parameter.GetType())
		{
		case UBMT_RDG_TEXTURE:
		case UBMT_RDG_TEXTURE_COPY_DEST:
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
			});

			if (FRDGTextureRef Texture = RenderTargets.DepthStencil.GetTexture())
			{
				ParameterFunction(Texture);
			}
		}
		break;
		}
	}
}

/** Enumerates all buffer accesses and provides the access info. */
template <typename TAccessFunction>
void EnumerateBufferAccess(FRDGPassParameterStruct PassParameters, ERDGPassFlags PassFlags, TAccessFunction AccessFunction)
{
	EResourceTransitionAccess SRVAccess, UAVAccess;
	GetPassAccess(PassFlags, SRVAccess, UAVAccess);

	for (uint32 Index = 0; Index < PassParameters.GetBufferParameterCount(); ++Index)
	{
		const FRDGPassParameter Parameter = PassParameters.GetBufferParameter(Index);
		switch (Parameter.GetType())
		{
		case UBMT_RDG_BUFFER:
			if (FRDGBufferRef Buffer = Parameter.GetAsBuffer())
			{
				EResourceTransitionAccess BufferAccess = SRVAccess;

				if (EnumHasAnyFlags(Buffer->Desc.Usage, BUF_DrawIndirect))
				{
					BufferAccess |= EResourceTransitionAccess::IndirectArgs;
				}

				AccessFunction(nullptr, Buffer, BufferAccess);
			}
		break;
		case UBMT_RDG_BUFFER_COPY_DEST:
			if (FRDGBufferRef Buffer = Parameter.GetAsBuffer())
			{
				AccessFunction(nullptr, Buffer, EResourceTransitionAccess::CopyDest);
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
	}
}

/** Enumerates all buffer pass parameters and calls the provided function. The input function must accept an FRDGResource*,
 *  since either views or textures are provided.
 */
template <typename TParameterFunction>
void EnumerateBufferParameters(FRDGPassParameterStruct PassParameters, TParameterFunction ParameterFunction)
{
	for (uint32 Index = 0; Index < PassParameters.GetBufferParameterCount(); ++Index)
	{
		const FRDGPassParameter Parameter = PassParameters.GetBufferParameter(Index);
		switch (Parameter.GetType())
		{
		case UBMT_RDG_BUFFER:
			if (FRDGBufferRef Buffer = Parameter.GetAsBuffer())
			{
				ParameterFunction(Buffer);
			}
		break;
		case UBMT_RDG_BUFFER_COPY_DEST:
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
	}
}

inline FRDGResourceHandle GetHandleIfNoUAVBarrier(FRDGChildResourceRef Resource)
{
	if (Resource && EnumHasAnyFlags(Resource->Flags, ERDGChildResourceFlags::NoUAVBarrier))
	{
		return Resource->GetHandle();
	}
	else
	{
		return FRDGResourceHandle::Null;
	}
}

inline EResourceTransitionFlags GetTextureViewTransitionFlags(FRDGChildResourceRef Resource)
{
	if (Resource)
	{
		switch (Resource->Type)
		{
		case ERDGChildResourceType::TextureUAV:
		{
			FRDGTextureUAVRef UAV = static_cast<FRDGTextureUAVRef>(Resource);
			if (UAV->Desc.MetaData != ERDGTextureMetaDataAccess::None)
			{
				return EResourceTransitionFlags::MaintainCompression;
			}
		}
		break;
		case ERDGChildResourceType::TextureSRV:
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
	const bool bLocalForceAsyncCompute = EnumHasAnyFlags(BuilderFlags, ERDGBuilderFlags::ForceAsyncComputeEnable);
	bAsyncComputeSupported |= EnumHasAnyFlags(BuilderFlags, ERDGBuilderFlags::ForceAsyncComputeDisable);

	if (EnumHasAnyFlags(PassFlags, ERDGPassFlags::Compute) && (bGlobalForceAsyncCompute || bLocalForceAsyncCompute))
	{
		PassFlags &= ~ERDGPassFlags::Compute;
		PassFlags |= ERDGPassFlags::AsyncCompute;
	}

	if (EnumHasAnyFlags(PassFlags, ERDGPassFlags::AsyncCompute) && (GRDGAsyncCompute == RDG_ASYNC_COMPUTE_DISABLED || GRDGImmediateMode != 0 || !bAsyncComputeSupported))
	{
		PassFlags &= ~ERDGPassFlags::AsyncCompute;
		PassFlags |= ERDGPassFlags::Compute;
	}

	return PassFlags;
}

FRDGBuilder::FRDGBuilder(FRHICommandListImmediate& InRHICmdList, FRDGEventName InBuilderName, ERDGBuilderFlags InBuilderFlags)
	: RHICmdList(InRHICmdList)
	, MemStack(FMemStack::Get())
	, RHICmdListAsyncCompute(FRHICommandListExecutor::GetImmediateAsyncComputeCommandList())
	, BuilderName(InBuilderName)
	, BuilderFlags(InBuilderFlags)
#if RDG_SCOPES
	, ScopeStacks(RHICmdList, RHICmdListAsyncCompute)
#endif
#if RDG_ENABLE_DEBUG
	, BarrierValidation(&Passes, BuilderName)
#endif
{
	ProloguePass = Allocate<FRDGSentinelPass>(RDG_EVENT_NAME("Graph Prologue"));
	EpiloguePass = Allocate<FRDGSentinelPass>(RDG_EVENT_NAME("Graph Epilogue"));

	AddPassInternal(ProloguePass);
}

FRDGTextureRef FRDGBuilder::RegisterExternalTexture(
	const TRefCountPtr<IPooledRenderTarget>& ExternalPooledTexture,
	const TCHAR* Name,
	ERDGParentResourceFlags Flags,
	EResourceTransitionAccess AccessInitial,
	EResourceTransitionAccess AccessFinal)
{
#if RDG_ENABLE_DEBUG
	checkf(ExternalPooledTexture.IsValid(), TEXT("Attempted to register NULL external texture: %s"), Name);
	checkf(Name, TEXT("Externally allocated texture requires a debug name when registering them to render graph."));
	UserValidation.ExecuteGuard(TEXT("RegisterExternalTexture"), Name);
#endif

	if (FRDGTextureRef* TexturePtr = ExternalTextures.Find(ExternalPooledTexture.GetReference()))
	{
		FRDGTextureRef Texture = *TexturePtr;
#if RDG_ENABLE_DEBUG
		checkf(Texture->AccessInitial == AccessInitial,
			TEXT("External texture %s is already registered, but with different initial access values. Existing (%s), Requested (%s)"), Name,
			*GetResourceTransitionAccessName(Texture->AccessInitial), *GetResourceTransitionAccessName(AccessInitial));
		checkf(Texture->AccessInitial == AccessInitial && Texture->AccessFinal == AccessFinal && Texture->Flags == Flags,
			TEXT("External texture %s is already registered, but with different final access values. Existing (%s), Requested (%s)"), Name,
			*GetResourceTransitionAccessName(Texture->AccessFinal), *GetResourceTransitionAccessName(AccessFinal));
		checkf(Texture->Flags == Flags, TEXT("External texture %s is already registered, but with different resource flags."), Name);
#endif
		return Texture;
	}

	FRDGSubresourceState StateInitial;
	StateInitial.Access = AccessInitial;

	const bool bIsExternal = true;
	FRDGTexture* OutTexture = AllocateResource<FRDGTexture>(Name, ExternalPooledTexture->GetDesc(), Flags, bIsExternal);
	OutTexture->Init(ExternalPooledTexture);
	OutTexture->AccessInitial = AccessInitial;
	OutTexture->AccessFinal = AccessFinal;
	OutTexture->State.InitAsWholeResource(StateInitial);
	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateCreateExternalTexture(OutTexture));

	AllocatedTextures.Add(OutTexture);
	ExternalTextures.Add(ExternalPooledTexture.GetReference(), OutTexture);
	TextureCount++;

	// Perform a dummy extraction to get the resource into the final state.
	if (AccessFinal != EResourceTransitionAccess::Unknown)
	{
		FDeferredInternalTextureQuery Query;
		Query.Texture = OutTexture;
		DeferredInternalTextureQueries.Emplace(Query);
	}

	return OutTexture;
}

FRDGBufferRef FRDGBuilder::RegisterExternalBuffer(
	const TRefCountPtr<FPooledRDGBuffer>& ExternalPooledBuffer,
	const TCHAR* Name,
	ERDGParentResourceFlags Flags,
	EResourceTransitionAccess AccessInitial,
	EResourceTransitionAccess AccessFinal)
{
#if RDG_ENABLE_DEBUG
	checkf(ExternalPooledBuffer.IsValid(), TEXT("Attempted to register NULL external buffer: %s"), Name);
	UserValidation.ExecuteGuard(TEXT("RegisterExternalBuffer"), Name);
#endif

	if (FRDGBufferRef* BufferPtr = ExternalBuffers.Find(ExternalPooledBuffer.GetReference()))
	{
		FRDGBufferRef Buffer = *BufferPtr;
#if RDG_ENABLE_DEBUG
		checkf(Buffer->AccessInitial == AccessInitial,
			TEXT("External buffer %s is already registered, but with different initial access values. Existing (%s), Requested (%s)"), Name,
			*GetResourceTransitionAccessName(Buffer->AccessInitial), *GetResourceTransitionAccessName(AccessInitial));
		checkf(Buffer->AccessInitial == AccessInitial && Buffer->AccessFinal == AccessFinal && Buffer->Flags == Flags,
			TEXT("External buffer %s is already registered, but with different final access values. Existing (%s), Requested (%s)"), Name,
			*GetResourceTransitionAccessName(Buffer->AccessFinal), *GetResourceTransitionAccessName(AccessFinal));
		checkf(Buffer->Flags == Flags, TEXT("External buffer %s is already registered, but with different resource flags."), Name);
#endif
		return Buffer;
	}

	const bool bIsExternal = true;
	FRDGBuffer* OutBuffer = AllocateResource<FRDGBuffer>(Name, ExternalPooledBuffer->Desc, Flags, bIsExternal);
	OutBuffer->Init(ExternalPooledBuffer);
	OutBuffer->AccessInitial = AccessInitial;
	OutBuffer->AccessFinal = AccessFinal;
	OutBuffer->State.Access = AccessInitial;
	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateCreateExternalBuffer(OutBuffer));

	AllocatedBuffers.Add(OutBuffer);
	ExternalBuffers.Add(ExternalPooledBuffer.GetReference(), OutBuffer);
	BufferCount++;

	// Perform a dummy extraction to get the resource into the final state.
	if (AccessFinal != EResourceTransitionAccess::Unknown)
	{
		FDeferredInternalBufferQuery Query;
		Query.Buffer = OutBuffer;
		DeferredInternalBufferQueries.Emplace(Query);
	}

	return OutBuffer;
}

void FRDGBuilder::AddPassDependencyInternal(FRDGPassHandle ProducerHandle, FRDGPassHandle ConsumerHandle)
{
	checkf(ProducerHandle.IsValid(), TEXT("AddPassDependency called with null producer."));
	checkf(ConsumerHandle.IsValid(), TEXT("AddPassDependency called with null consumer."));

	FRDGPass* Producer = Passes[ProducerHandle];
	Producer->Consumers.AddUnique(ConsumerHandle);

	FRDGPass* Consumer = Passes[ConsumerHandle];
	if (Consumer->Producers.Find(ProducerHandle) == INDEX_NONE)
	{
		Consumer->Producers.Add(ProducerHandle);
	}
};

void FRDGBuilder::Compile()
{
	// Immediate mode skips all compilation and executes in place.
	if (GRDGImmediateMode)
	{
		PassesToCull.Init(false, Passes.Num());
		return;
	}

	SCOPED_NAMED_EVENT(FRDGBuilder_Compile, FColor::Emerald);

	FPassBitArray PassesOnAsyncCompute(false, Passes.Num());
	FPassBitArray PassesOnRaster(false, Passes.Num());
	FPassBitArray PassesWithExternalOutputs(false, Passes.Num());
	PassesToCull.Init(GRDGCullPasses ? true : false, Passes.Num());

	const auto IsRaster = [&](FRDGPassHandle A)
	{
		return PassesOnRaster[A.GetIndex()];
	};

	const auto IsAsyncCompute = [&](FRDGPassHandle A)
	{
		return PassesOnAsyncCompute[A.GetIndex()];
	};

	const auto IsCrossPipeline = [&](FRDGPassHandle A, FRDGPassHandle B)
	{
		return PassesOnAsyncCompute[A.GetIndex()] != PassesOnAsyncCompute[B.GetIndex()];
	};

	const auto HasExternalOutputs = [&](FRDGPassHandle A)
	{
		return PassesWithExternalOutputs[A.GetIndex()];
	};

	const auto IsSortedBefore = [&](FRDGPassHandle A, FRDGPassHandle B)
	{
		return A.GetIndex() < B.GetIndex();
	};

	const auto IsSortedAfter = [&](FRDGPassHandle A, FRDGPassHandle B)
	{
		return A.GetIndex() > B.GetIndex();
	};

	const auto AddPassDependenciesForResource = [&](FRDGPassHandle PassHandle, FRDGParentResource* Resource, EResourceTransitionAccess AccessUnion)
	{
		const FRDGPassHandle ProducerHandle = Resource->LastProducer;

		// Certain readable states are also writable, so this could be the first access.
		if (ProducerHandle.IsValid())
		{
			AddPassDependencyInternal(ProducerHandle, PassHandle);
		}

		// If the access is writable, we store the new producer.
		if (IsWritableAccess(AccessUnion))
		{
			for (FRDGPassHandle ConsumerHandle : Resource->LastConsumers)
			{
				AddPassDependencyInternal(ConsumerHandle, PassHandle);
			}
			Resource->LastConsumers.Empty();
			Resource->LastProducer = PassHandle;

			// External outputs make this a root candidate (e.g. we can't cull it).
			PassesWithExternalOutputs[PassHandle.GetIndex()] |= Resource->bIsExternal;
		}
		else
		{
			check(IsReadableAccess(AccessUnion));
			Resource->LastConsumers.Emplace(PassHandle);
		}
	};

	FPassBitArray PassesToNeverCull(false, Passes.Num());

	for (FRDGPassHandle PassHandle = Passes.Begin(); PassHandle != Passes.End(); ++PassHandle)
	{
		FRDGPass* Pass = Passes[PassHandle];
		const FRDGPassParameterStruct PassParameters = Pass->GetParameters();
		const ERDGPassFlags PassFlags = Pass->GetFlags();

		PassesOnRaster[PassHandle.GetIndex()] = EnumHasAnyFlags(PassFlags, ERDGPassFlags::Raster);
		PassesOnAsyncCompute[PassHandle.GetIndex()] = EnumHasAnyFlags(PassFlags, ERDGPassFlags::AsyncCompute);
		PassesToNeverCull[PassHandle.GetIndex()] = EnumHasAnyFlags(PassFlags, ERDGPassFlags::NeverCull);
		PassesWithExternalOutputs[PassHandle.GetIndex()] |= PassParameters.HasExternalOutputs();

		for (auto TextureAccessPair : Pass->TextureAccessMap)
		{
			AddPassDependenciesForResource(PassHandle, TextureAccessPair.Key, TextureAccessPair.Value.AccessUnion);
		}

		for (auto BufferAccessPair : Pass->BufferAccessMap)
		{
			AddPassDependenciesForResource(PassHandle, BufferAccessPair.Key, BufferAccessPair.Value.AccessUnion);
		}
	}
	
	const FRDGPassHandle ProloguePassHandle = GetProloguePassHandle();
	const FRDGPassHandle EpiloguePassHandle = GetEpiloguePassHandle();

	// The prologue / epilogue is responsible for external resource import / export, respectively.
	PassesWithExternalOutputs[ProloguePassHandle.GetIndex()] = true;
	PassesWithExternalOutputs[EpiloguePassHandle.GetIndex()] = true;

	// Add additional dependencies from deferred queries.
	for (const auto& Query : DeferredInternalTextureQueries)
	{
		AddPassDependenciesForResource(EpiloguePassHandle, Query.Texture, Query.Texture->AccessFinal);
		Query.Texture->ReferenceCount += Query.Texture->GetSubresourceRange().GetSubresourceCount();
	}
	for (const auto& Query : DeferredInternalBufferQueries)
	{
		AddPassDependenciesForResource(EpiloguePassHandle, Query.Buffer, Query.Buffer->AccessFinal);
		Query.Buffer->ReferenceCount++;
	}

	if (GRDGCullPasses)
	{
		TArray<FRDGPassHandle, TInlineAllocator<32, SceneRenderingAllocator>> PassStack;

		for (FRDGPassHandle PassHandle = Passes.Begin(); PassHandle != Passes.End(); ++PassHandle)
		{
			if (HasExternalOutputs(PassHandle) || PassesToNeverCull[PassHandle.GetIndex()])
			{
				PassStack.Add(PassHandle);
			}
		}

		while (PassStack.Num())
		{
			const FRDGPassHandle PassHandle = PassStack.Pop();

			if (PassesToCull[PassHandle.GetIndex()])
			{
				PassesToCull[PassHandle.GetIndex()] = false;
				PassStack.Append(Passes[PassHandle]->Producers);
			}
		}
	}

	FPassBitArray PassesWithCrossPipelineProducer(false, Passes.Num());
	FPassBitArray PassesWithCrossPipelineConsumer(false, Passes.Num());

	for (FRDGPassHandle PassHandle = Passes.Begin(); PassHandle != Passes.End(); ++PassHandle)
	{
		if (IsCulled(PassHandle))
		{
			continue;
		}

		FRDGPass* Pass = Passes[PassHandle];

		for (FRDGPassHandle ProducerHandle : Pass->GetProducers())
		{
			const FRDGPassHandle ConsumerHandle = PassHandle;

			if (IsCrossPipeline(ProducerHandle, ConsumerHandle))
			{
				FRDGPass* Consumer = Pass;
				FRDGPass* Producer = Passes[ProducerHandle];

				// Finds the earliest consumer on the other pipeline for the producer.
				if (Producer->CrossPipelineConsumer.IsNull() || IsSortedBefore(ConsumerHandle, Producer->CrossPipelineConsumer))
				{
					Producer->CrossPipelineConsumer = PassHandle;
					PassesWithCrossPipelineConsumer[ProducerHandle.GetIndex()] = true;
				}

				// Finds the latest producer on the other pipeline for the consumer.
				if (Consumer->CrossPipelineProducer.IsNull() || IsSortedAfter(ProducerHandle, Consumer->CrossPipelineProducer))
				{
					Consumer->CrossPipelineProducer = ProducerHandle;
					PassesWithCrossPipelineProducer[ConsumerHandle.GetIndex()] = true;
				}
			}
		}

		for (auto TextureAccessPair : Pass->TextureAccessMap)
		{
			TextureAccessPair.Key->ReferenceCount += TextureAccessPair.Value.ReferenceCount;
		}

		for (auto BufferAccessPair : Pass->BufferAccessMap)
		{
			BufferAccessPair.Key->ReferenceCount += BufferAccessPair.Value.ReferenceCount;
		}
	}

	const auto IsCrossPipelineProducer = [&](FRDGPassHandle A)
	{
		return PassesWithCrossPipelineConsumer[A.GetIndex()];
	};

	const auto IsCrossPipelineConsumer = [&](FRDGPassHandle A)
	{
		return PassesWithCrossPipelineProducer[A.GetIndex()];
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
			if (!IsCulled(ConsumerHandle) && !IsCrossPipeline(ConsumerHandle, PassHandle) && IsCrossPipelineConsumer(ConsumerHandle))
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
			if (!IsCulled(ProducerHandle) && !IsCrossPipeline(ProducerHandle, PassHandle) && IsCrossPipelineProducer(ProducerHandle))
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

	// Establishes Fork / Join regions for async compute. This is used for fencing as well as resource
	// allocation / deallocation. Async compute passes can't allocate / release their resource references until
	// the fork / join is complete, since the two pipes run in parallel.
	{
		FRDGPass* PrevGraphicsForkPass = nullptr;
		FRDGPass* PrevGraphicsJoinPass = nullptr;
		FRDGPass* PrevAsyncComputePass = nullptr;

		const auto InsertGraphicsToAsyncComputeFork = [&](FRDGPass* GraphicsPass, FRDGPass* AsyncComputePass)
		{
			GraphicsPass->bGraphicsFork = 1;
			GraphicsPass->EpilogueBarriersToBeginForAsyncCompute.SetUseCrossPipelineFence();

			AsyncComputePass->bAsyncComputeBegin = 1;
			AsyncComputePass->PrologueBarriersToEnd.AddDependency(&GraphicsPass->EpilogueBarriersToBeginForAsyncCompute);
		};

		const auto InsertAsyncToGraphicsComputeJoin = [&](FRDGPass* AsyncComputePass, FRDGPass* GraphicsPass)
		{
			AsyncComputePass->bAsyncComputeEnd = 1;
			AsyncComputePass->EpilogueBarriersToBeginForGraphics.SetUseCrossPipelineFence();

			GraphicsPass->bGraphicsJoin = 1;
			GraphicsPass->PrologueBarriersToEnd.AddDependency(&AsyncComputePass->EpilogueBarriersToBeginForGraphics);
		};

		for (FRDGPassHandle PassHandle = Passes.Begin(); PassHandle != Passes.End(); ++PassHandle)
		{
			if (!IsCulled(PassHandle) && IsAsyncCompute(PassHandle))
			{
				FRDGPass* AsyncComputePass = Passes[PassHandle];

				const FRDGPassParameterStruct PassParameters = AsyncComputePass->GetParameters();
				const ERDGPipeline Pipeline = AsyncComputePass->GetPipeline();

				const FRDGPassHandle GraphicsForkPassHandle = FindCrossPipelineProducer(PassHandle);
				const FRDGPassHandle GraphicsJoinPassHandle = FindCrossPipelineConsumer(PassHandle);

				AsyncComputePass->GraphicsForkPass = GraphicsForkPassHandle;
				AsyncComputePass->GraphicsJoinPass = GraphicsJoinPassHandle;

				FRDGPass* GraphicsForkPass = Passes[GraphicsForkPassHandle];
				FRDGPass* GraphicsJoinPass = Passes[GraphicsJoinPassHandle];

				GraphicsForkPass->PassesToBegin.Add(PassParameters);

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
		}

		if (PrevAsyncComputePass)
		{
			InsertAsyncToGraphicsComputeJoin(PrevAsyncComputePass, EpiloguePass);
			PrevAsyncComputePass->bAsyncComputeEndExecute = 1;
		}
	}

	// Release external textures that have ReferenceCount == 0 and yet are already allocated.
	for (auto TextureIt = AllocatedTextures.CreateIterator(); TextureIt; ++TextureIt)
	{
		FRDGTextureRef Texture = *TextureIt;
		if (Texture->ReferenceCount == 0)
		{
			Texture->PooledTexture = nullptr;
			TextureIt.RemoveCurrent();
		}
		else
		{
			IF_RDG_ENABLE_DEBUG(LogFile.AddFirstEdge(Texture, ProloguePassHandle));
		}
	}

	// Release external buffers that have ReferenceCount == 0 and yet are already allocated.
	for (auto BufferIt = AllocatedBuffers.CreateIterator(); BufferIt; ++BufferIt)
	{
		FRDGBufferRef Buffer = *BufferIt;
		if (Buffer->ReferenceCount == 0)
		{
			Buffer->PooledBuffer = nullptr;
			BufferIt.RemoveCurrent();
		}
		else
		{
			IF_RDG_ENABLE_DEBUG(LogFile.AddFirstEdge(Buffer, ProloguePassHandle));
		}
	}

	if (GRDGMergeRenderPasses)
	{
		FRDGPassHandle PrevPassHandle;
		const FRenderTargetBindingSlots* PrevRenderTargets = nullptr;

		const auto InvalidateMerge = [&]()
		{
			PrevPassHandle = FRDGPassHandle::Null;
			PrevRenderTargets = nullptr;
		};

		for (FRDGPassHandle PassHandle = Passes.Begin(); PassHandle != Passes.End(); ++PassHandle)
		{
			if (IsCulled(PassHandle))
			{
				continue;
			}

			if (IsRaster(PassHandle))
			{
				FRDGPass* NextPass = Passes[PassHandle];

				if (NextPass->IsGraphicsFork())
				{
					InvalidateMerge();
				}

				const FRenderTargetBindingSlots* RenderTargets = NextPass->GetParameters().GetRenderTargetBindingSlots();
				check(RenderTargets);

				if (PrevPassHandle.IsValid())
				{
					check(PrevRenderTargets);

					if (PrevRenderTargets->CanMergeBefore(*RenderTargets))
					{
						FRDGPass* PrevPass = Passes[PrevPassHandle];
						PrevPass->bSkipRenderPassEnd = 1;
						NextPass->bSkipRenderPassBegin = 1;
					}
				}

				PrevPassHandle = PassHandle;
				PrevRenderTargets = RenderTargets;
			}
			else if (!IsAsyncCompute(PassHandle))
			{
				// A compute pass on the graphics pipe will invalidate the render target merge.
				InvalidateMerge();
			}
		}
	}
}

void FRDGBuilder::Execute()
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(FRDGBuilder_Execute);
	SCOPED_NAMED_EVENT(FRDGBuilder_Execute, FColor::Emerald);

	AddPassInternal(EpiloguePass);

	const FRDGPassHandle ProloguePassHandle = GetProloguePassHandle();

	AllocatedTextures.Reserve(TextureCount);
	AllocatedBuffers.Reserve(BufferCount);

	Compile();

#if RDG_ENABLE_DEBUG
	UserValidation.ValidateExecuteBegin();
	LogFile.Begin(BuilderName, &Passes, GetProloguePassHandle(), GetEpiloguePassHandle());
#endif

	if (!GRDGImmediateMode)
	{
		for (FRDGPassHandle PassHandle = Passes.Begin(); PassHandle != Passes.End(); ++PassHandle)
		{
			if (!IsCulled(PassHandle))
			{
				CollectPassBarriers(PassHandle);
			}
		}
	}

	for (auto& Query : DeferredInternalTextureQueries)
	{
		FRDGTextureRef Texture = Query.Texture;

		// We only transition the resource if it's used in the graph or if the user explicitly requested that it be extracted.
		if (Texture->AccessFinal != EResourceTransitionAccess::Unknown && (Texture->StateFirst.PassHandle.IsValid() || Query.OutTexturePtr))
		{
			Texture->StatePending.InitAsWholeResource(FRDGSubresourceState(FRDGPassHandle::Null, ERDGPipeline::Graphics, Texture->AccessFinal));
			AddTransition(Texture, Texture->State, Texture->StatePending);
			ResolveState(EpiloguePass, Texture);
			Texture->AccessFinal = EResourceTransitionAccess::Unknown;
		}
	}

	for (auto& Query : DeferredInternalBufferQueries)
	{
		FRDGBufferRef Buffer = Query.Buffer;

		// We only transition the resource if it's used in the graph or if the user explicitly requested that it be extracted.
		if (Buffer->AccessFinal != EResourceTransitionAccess::Unknown && (Buffer->StateFirst.PassHandle.IsValid() || Query.OutBufferPtr))
		{
			Buffer->StatePending = FRDGSubresourceState(FRDGPassHandle::Null, ERDGPipeline::Graphics, Buffer->AccessFinal);
			AddTransition(Buffer, Buffer->State, Buffer->StatePending);
			ResolveState(EpiloguePass, Buffer);
			Buffer->AccessFinal = EResourceTransitionAccess::Unknown;
		}
	}

	IF_RDG_SCOPES(ScopeStacks.BeginExecute());

	if (!GRDGImmediateMode)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FRDGBuilder_Execute_Passes);

		for (FRDGPassHandle PassHandle = Passes.Begin(); PassHandle != Passes.End(); ++PassHandle)
		{
			if (!IsCulled(PassHandle))
			{
				ExecutePass(Passes[PassHandle]);
			}
		}
	}
	else
	{
		ExecutePass(EpiloguePass);
	}

#if WITH_MGPU
	if (NameForTemporalEffect != NAME_None)
	{
		TArray<FRHITexture*> BroadcastTexturesForTemporalEffect;
		for (const auto& Query : DeferredInternalTextureQueries)
		{
			if (EnumHasAnyFlags(Query.Texture->Flags, ERDGParentResourceFlags::MultiFrame))
			{
				BroadcastTexturesForTemporalEffect.Add(Query.Texture->GetRHIUnchecked());
			}
		}
		RHICmdList.BroadcastTemporalEffect(NameForTemporalEffect, BroadcastTexturesForTemporalEffect);
	}
#endif

	// Extract resources requested by the user.
	{
		for (const FDeferredInternalTextureQuery& Query : DeferredInternalTextureQueries)
		{
			if (Query.OutTexturePtr)
			{
				*Query.OutTexturePtr = Query.Texture->PooledTexture;
			}
			EndResourceRHI(nullptr, Query.Texture, Query.Texture->GetSubresourceRange());
		}
		DeferredInternalTextureQueries.Empty();

		for (const FDeferredInternalBufferQuery& Query : DeferredInternalBufferQueries)
		{
			if (Query.OutBufferPtr)
			{
				*Query.OutBufferPtr = Query.Buffer->PooledBuffer;
			}
			EndResourceRHI(nullptr, Query.Buffer);
		}
		DeferredInternalBufferQueries.Empty();
	}

	IF_RDG_SCOPES(ScopeStacks.Graphics.EndExecute());

#if RDG_ENABLE_DEBUG
	LogFile.End();
	UserValidation.ValidateExecuteEnd();
	BarrierValidation.ValidateExecuteEnd();
#endif

	Clear();
}

void FRDGBuilder::Clear()
{
	Passes.DestructAndClear();
	ExternalTextures.Empty();
	ExternalBuffers.Empty();
	if (GRDGImmediateMode != 0 || GRDGExtendResourceLifetimes != 0)
	{
		while (AllocatedTextures.Num())
		{
			ReleaseResourceRHI(*AllocatedTextures.CreateIterator());
		}
		while (AllocatedBuffers.Num())
		{
			ReleaseResourceRHI(*AllocatedBuffers.CreateIterator());
		}
	}
	check(!AllocatedTextures.Num());
	check(!AllocatedBuffers.Num());
	Resources.DestructAndClear();
}

FRDGPassHandle FRDGBuilder::AddPassInternal(FRDGPass* Pass)
{
	const FRDGPassHandle Handle = Passes.Insert(Pass);
	Pass->Handle = Handle;
	return Handle;
}

FRDGPassRef FRDGBuilder::AddPass(FRDGPass* Pass)
{
	IF_RDG_ENABLE_DEBUG(ClobberPassOutputs(Pass));
	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateAddPass(Pass, bInDebugPassScope));

	const FRDGPassParameterStruct PassParameters = Pass->GetParameters();
	const ERDGPassFlags PassFlags = Pass->GetFlags();
	const ERDGPipeline Pipeline = Pass->GetPipeline();

	// Reserve and generate the texture access map; track one reference for each subresource.
	Pass->TextureAccessMap.Reserve(PassParameters.GetTextureParameterCount() + (EnumHasAnyFlags(PassFlags, ERDGPassFlags::Raster) ? (MaxSimultaneousRenderTargets + 1) : 0));
	EnumerateTextureAccess(PassParameters, PassFlags, [&](FRDGChildResourceRef TextureView, FRDGTextureRef Texture, EResourceTransitionAccess Access, FRDGTextureSubresourceRange Range)
	{
		check(Access != EResourceTransitionAccess::Unknown);
		auto& AccessInfo = Pass->TextureAccessMap.FindOrAdd(Texture);
		AccessInfo.AccessUnion |= Access;
		AccessInfo.ReferenceCount += Range.GetSubresourceCount();
		AccessInfo.NoUAVBarrierFilter.AddHandle(GetHandleIfNoUAVBarrier(TextureView));
		AccessInfo.FlagsUnion |= GetTextureViewTransitionFlags(TextureView);
	});

	// Build the pass and texture subresource tracking policy.
	bool bPassSubresourceTrackingRequired = true;
	for (auto& TextureAccessPair : Pass->TextureAccessMap)
	{
		auto& AccessInfo = TextureAccessPair.Value;
		const bool bSubresourceTrackingRequired = IsSubresourceTrackingRequired(AccessInfo.AccessUnion, AccessInfo.FlagsUnion);
		AccessInfo.bSubresourceTrackingRequired |= bSubresourceTrackingRequired;
		bPassSubresourceTrackingRequired |= bSubresourceTrackingRequired;
	}
	Pass->bSubresourceTrackingRequired = bPassSubresourceTrackingRequired;

	// Reserve and generate the buffer access map.
	Pass->BufferAccessMap.Reserve(PassParameters.GetBufferParameterCount());
	EnumerateBufferAccess(PassParameters, PassFlags, [&](FRDGChildResourceRef BufferView, FRDGBufferRef Buffer, EResourceTransitionAccess Access)
	{
		check(Access != EResourceTransitionAccess::Unknown);
		auto& AccessInfo = Pass->BufferAccessMap.FindOrAdd(Buffer);
		AccessInfo.AccessUnion |= Access;
		AccessInfo.ReferenceCount++;
		AccessInfo.NoUAVBarrierFilter.AddHandle(GetHandleIfNoUAVBarrier(BufferView));
	});

	const FRDGPassHandle PassHandle = AddPassInternal(Pass);

	// The pass can begin / end its own resources on the graphics pipe; async compute is scheduled during compilation.
	if (Pipeline == ERDGPipeline::Graphics)
	{
		Pass->PassesToBegin.Add(PassParameters);
		Pass->GraphicsJoinPass = PassHandle;
		Pass->GraphicsForkPass = PassHandle;
	}
	IF_RDG_SCOPES(Pass->Scopes = ScopeStacks.GetCurrentScopes(Pipeline));

	if (GRDGImmediateMode != 0 || GRDGExtendResourceLifetimes != 0)
	{
		EnumerateTextureParameters(PassParameters, [&](auto Resource)
		{
			BeginResourceRHI(Pass, Resource);
		});

		EnumerateBufferParameters(PassParameters, [&](auto Resource)
		{
			BeginResourceRHI(Pass, Resource);
		});

		if (GRDGImmediateMode)
		{
			check(Pipeline != ERDGPipeline::AsyncCompute);
			CollectPassBarriers(PassHandle);
			ExecutePass(Pass);
		}
	}

	IF_RDG_ENABLE_DEBUG(VisualizePassOutputs(Pass));

	return Pass;
}

void FRDGBuilder::ExecutePassPrologue(FRHIComputeCommandList& RHICmdListPass, FRDGPass* Pass)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FRDGBuilder_ExecutePassPrologue);

	for (FRDGPassParameterStruct PassParameters : Pass->PassesToBegin)
	{
		EnumerateTextureParameters(PassParameters, [&](auto Resource)
		{
			BeginResourceRHI(Pass, Resource);
		});

		EnumerateBufferParameters(PassParameters, [&](auto Resource)
		{
			BeginResourceRHI(Pass, Resource);
		});
	}

	IF_RDG_ENABLE_DEBUG(BarrierValidation.ValidateBarrierBatchBegin(Pass->PrologueBarriersToBegin));
	IF_RDG_ENABLE_DEBUG(BarrierValidation.ValidateBarrierBatchEnd(Pass->PrologueBarriersToEnd));

	Pass->PrologueBarriersToBegin.Submit(RHICmdListPass);
	Pass->PrologueBarriersToEnd.Submit(RHICmdListPass);

	const ERDGPassFlags PassFlags = Pass->GetFlags();

	if (EnumHasAnyFlags(PassFlags, ERDGPassFlags::Raster))
	{
		if (!Pass->SkipRenderPassBegin())
		{
			static_cast<FRHICommandList&>(RHICmdListPass).BeginRenderPass(GetRenderPassInfo(Pass), Pass->GetName());
		}
	}
	else if (EnumHasAnyFlags(PassFlags, ERDGPassFlags::Compute))
	{
		check(!RHICmdListPass.IsInsideRenderPass());
		//TODO-Zach to double check if this should be somehow fixed.
		//UnbindRenderTargets(static_cast<FRHICommandList&>(RHICmdListPass));
	}
}

void FRDGBuilder::ExecutePassEpilogue(FRHIComputeCommandList& RHICmdListPass, FRDGPass* Pass)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FRDGBuilder_ExecutePassEpilogue);

	const ERDGPassFlags PassFlags = Pass->GetFlags();

	if (EnumHasAnyFlags(PassFlags, ERDGPassFlags::Raster) && !Pass->SkipRenderPassEnd())
	{
		static_cast<FRHICommandList&>(RHICmdListPass).EndRenderPass();
	}

	const FRDGPassParameterStruct PassParameters = Pass->GetParameters();

	EnumerateTextureAccess(PassParameters, PassFlags, [&](FRDGChildResourceRef, FRDGTextureRef Texture, EResourceTransitionAccess, FRDGTextureSubresourceRange Range)
	{
		EndResourceRHI(Pass, Texture, Range);
	});

	EnumerateBufferAccess(PassParameters, PassFlags, [&](FRDGChildResourceRef, FRDGBufferRef Buffer, EResourceTransitionAccess)
	{
		EndResourceRHI(Pass, Buffer);
	});

	for (FRDGTexture* Texture : Pass->TexturesToRelease)
	{
		ReleaseResourceRHI(Texture);
	}
	Pass->TexturesToRelease.Empty();

	for (FRDGBuffer* Buffer : Pass->BuffersToRelease)
	{
		ReleaseResourceRHI(Buffer);
	}
	Pass->BuffersToRelease.Empty();

	// Submit epilogue barriers after executing the pass.
	IF_RDG_ENABLE_DEBUG(BarrierValidation.ValidateBarrierBatchBegin(Pass->EpilogueBarriersToBeginForGraphics));
	IF_RDG_ENABLE_DEBUG(BarrierValidation.ValidateBarrierBatchBegin(Pass->EpilogueBarriersToBeginForAsyncCompute));
	Pass->EpilogueBarriersToBeginForGraphics.Submit(RHICmdListPass);
	Pass->EpilogueBarriersToBeginForAsyncCompute.Submit(RHICmdListPass);
}

FRHIRenderPassInfo FRDGBuilder::GetRenderPassInfo(const FRDGPass* Pass) const
{
	const FRDGPassParameterStruct PassParameters = Pass->GetParameters();
	const FRenderTargetBindingSlots* RenderTargets = PassParameters.GetRenderTargetBindingSlots();
	check(EnumHasAnyFlags(Pass->GetFlags(), ERDGPassFlags::Raster) && RenderTargets);

	uint32 SampleCount = 0;
	uint32 RenderTargetIndex = 0;

	FRHIRenderPassInfo RenderPassInfo;

	RenderTargets->Enumerate([&](FRenderTargetBinding RenderTarget)
	{
		FRDGTextureRef Texture = RenderTarget.GetTexture();

		// TODO(RDG): Clean up this legacy hack of the FPooledRenderTarget that can have TargetableTexture != ShaderResourceTexture
		// for MSAA texture. Instead the two texture should be independent FRDGTexture explicitly handled by the user code.
		const FSceneRenderTargetItem& RenderTargetItem = Texture->PooledTexture->GetRenderTargetItem();
		FRHITexture* TargetableTexture = RenderTargetItem.TargetableTexture;
		FRHITexture* ShaderResourceTexture = RenderTargetItem.ShaderResourceTexture;

		// TODO(RDG): The load store action could actually be optimised by render graph for tile hardware when there is multiple
		// consecutive rasterizer passes that have RDG resource as render target, a bit like resource transitions.
		ERenderTargetStoreAction StoreAction = ERenderTargetStoreAction::EStore;

		// Automatically switch the store action to MSAA resolve when there is MSAA texture.
		if (TargetableTexture != ShaderResourceTexture && Texture->Desc.NumSamples > 1 && StoreAction == ERenderTargetStoreAction::EStore)
		{
			StoreAction = ERenderTargetStoreAction::EMultisampleResolve;
		}

		// TODO(RDG): should force TargetableTexture == ShaderResourceTexture with MSAA, and instead have an explicit MSAA resolve pass.
		auto& OutRenderTarget = RenderPassInfo.ColorRenderTargets[RenderTargetIndex];
		OutRenderTarget.RenderTarget = TargetableTexture;
		OutRenderTarget.ResolveTarget = ShaderResourceTexture != TargetableTexture ? ShaderResourceTexture : nullptr;
		OutRenderTarget.ArraySlice = RenderTarget.GetArraySlice();
		OutRenderTarget.MipIndex = RenderTarget.GetMipIndex();
		OutRenderTarget.Action = MakeRenderTargetActions(RenderTarget.GetLoadAction(), StoreAction);

		SampleCount |= OutRenderTarget.RenderTarget->GetNumSamples();
		++RenderTargetIndex;
	});

	const FDepthStencilBinding& DepthStencil = RenderTargets->DepthStencil;

	if (FRDGTextureRef Texture = DepthStencil.GetTexture())
	{
		const FExclusiveDepthStencil ExclusiveDepthStencil = DepthStencil.GetDepthStencilAccess();
		const ERenderTargetStoreAction DepthStoreAction = ExclusiveDepthStencil.IsDepthWrite() ? ERenderTargetStoreAction::EStore : ERenderTargetStoreAction::ENoAction;
		const ERenderTargetStoreAction StencilStoreAction = ExclusiveDepthStencil.IsStencilWrite() ? ERenderTargetStoreAction::EStore : ERenderTargetStoreAction::ENoAction;

		auto& RenderTargetItem = Texture->PooledTexture->GetRenderTargetItem();

		auto& OutDepthStencil = RenderPassInfo.DepthStencilRenderTarget;
		OutDepthStencil.DepthStencilTarget = DepthStencil.GetMsaaPlane() == ERenderTargetMsaaPlane::Unresolved ? RenderTargetItem.TargetableTexture : RenderTargetItem.ShaderResourceTexture;
		OutDepthStencil.ResolveTarget = nullptr;
		OutDepthStencil.Action = MakeDepthStencilTargetActions(
			MakeRenderTargetActions(DepthStencil.GetDepthLoadAction(), DepthStoreAction),
			MakeRenderTargetActions(DepthStencil.GetStencilLoadAction(), StencilStoreAction));
		OutDepthStencil.ExclusiveDepthStencil = ExclusiveDepthStencil;

		SampleCount |= OutDepthStencil.DepthStencilTarget->GetNumSamples();
	}

	RenderPassInfo.bIsMSAA = SampleCount > 1;
	return RenderPassInfo;
}

void FRDGBuilder::ExecutePass(FRDGPass* Pass)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FRDGBuilder_ExecutePass);
	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateExecutePassBegin(Pass));

#if RDG_SCOPES
	const bool bUsePassEventScope = Pass != EpiloguePass && Pass != ProloguePass;
	if (bUsePassEventScope)
	{
		ScopeStacks.BeginExecutePass(Pass);
	}
#endif

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
	FRHIComputeCommandList& RHICmdListPass = (Pass->GetPipeline() == ERDGPipeline::AsyncCompute)
		? static_cast<FRHIComputeCommandList&>(RHICmdListAsyncCompute)
		: RHICmdList;

	ExecutePassPrologue(RHICmdListPass, Pass);

	Pass->Execute(RHICmdListPass);

	ExecutePassEpilogue(RHICmdListPass, Pass);

#if RDG_SCOPES
	if (bUsePassEventScope)
	{
		ScopeStacks.EndExecutePass(Pass);
	}
#endif
	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateExecutePassEnd(Pass));

	if (Pass->bAsyncComputeEnd)
	{
		if (Pass->bAsyncComputeEndExecute)
		{
			IF_RDG_SCOPES(ScopeStacks.AsyncCompute.EndExecute());
		}
		FRHIAsyncComputeCommandListImmediate::ImmediateDispatch(RHICmdListAsyncCompute);
	}
}

void FRDGBuilder::CollectPassBarriers(FRDGPassHandle PassHandle)
{
	FRDGPass* Pass = Passes[PassHandle];
	const FRDGPassParameterStruct PassParameters = Pass->GetParameters();
	const ERDGPassFlags PassFlags = Pass->GetFlags();
	const ERDGPipeline PassPipeline = Pass->GetPipeline();

	IF_RDG_ENABLE_DEBUG(ConditionalDebugBreak(RDG_BREAKPOINT_PASS_COMPILE, BuilderName.GetTCHAR(), Pass->GetName()));

	const auto IsTransitionAllowed = [&](FRDGParentResourceRef Resource, const FRDGPass::FAccessInfo& AccessInfo) -> bool
	{
		if (Resource->StateFirst.PassHandle.IsNull())
		{
			// The union state contains the combination of states used by the resource in the current pass.
			Resource->StateFirst = FRDGSubresourceState(PassHandle, PassPipeline, AccessInfo.AccessUnion, AccessInfo.FlagsUnion);

			// The first transition of unallocated resources have to be deferred.
			if (!Resource->GetRHIUnchecked())
			{
				// First access should always be a single write state for non-external resources. This is checked by the validator.
				check(IsWritableAccess(AccessInfo.AccessUnion) && (!AccessInfo.bSubresourceTrackingRequired || AccessInfo.FlagsUnion != EResourceTransitionFlags::None));
				return false;
			}
		}
		return true;
	};

	// Fast path: all states in this pass are whole-resource states. We don't need to iterate and collect.
	if (!Pass->bSubresourceTrackingRequired)
	{
		for (auto TextureAccessPair : Pass->TextureAccessMap)
		{
			FRDGTextureRef Texture = TextureAccessPair.Key;
			const auto& AccessInfo = TextureAccessPair.Value;

			Texture->StatePending.InitAsWholeResource(FRDGSubresourceState(
				PassHandle,
				PassPipeline,
				AccessInfo.AccessUnion,
				AccessInfo.FlagsUnion,
				AccessInfo.NoUAVBarrierFilter.GetUniqueHandle()));

			if (IsTransitionAllowed(Texture, AccessInfo))
			{
				AddTransition(Texture, Texture->State, Texture->StatePending);
			}
			ResolveState(Pass, Texture);
		}
	}
	// Slow path: we need to iterate and collect subresource states.
	else
	{
		for (auto TextureAccessPair : Pass->TextureAccessMap)
		{
			FRDGTextureRef Texture = TextureAccessPair.Key;
			const auto& AccessInfo = TextureAccessPair.Value;

			if (AccessInfo.bSubresourceTrackingRequired)
			{
				Texture->StatePending.InitAsSubresources({});
			}
		}

		EnumerateTextureAccess(PassParameters, PassFlags, [&](FRDGChildResourceRef View, FRDGTextureRef Texture, EResourceTransitionAccess Access, FRDGTextureSubresourceRange Range)
		{
			FRDGTextureState& StatePending = Texture->StatePending;

			const auto& AccessInfo = Pass->TextureAccessMap.FindChecked(Texture);
			if (!AccessInfo.bSubresourceTrackingRequired)
			{
				StatePending.InitAsWholeResource(FRDGSubresourceState(
					PassHandle,
					PassPipeline,
					AccessInfo.AccessUnion,
					AccessInfo.FlagsUnion,
					AccessInfo.NoUAVBarrierFilter.GetUniqueHandle()));
			}
			else
			{
				const FRDGResourceHandle NoUAVBarrierHandle = GetHandleIfNoUAVBarrier(View);
				const EResourceTransitionFlags TransitionFlags = GetTextureViewTransitionFlags(View);

				Range.EnumerateSubresources([&](uint32 MipIndex, uint32 ArraySlice, uint32 PlaneSlice)
				{
					FRDGSubresourceState& State = StatePending.GetSubresourceState(MipIndex, ArraySlice, PlaneSlice);
					State.PassHandle = PassHandle;
					State.Pipeline = PassPipeline;
					State.Access |= Access;
					State.Flags |= TransitionFlags;
					State.NoUAVBarrierFilter.AddHandle(NoUAVBarrierHandle);
				});
			}
		});

		for (auto TextureAccessPair : Pass->TextureAccessMap)
		{
			FRDGTextureRef Texture = TextureAccessPair.Key;
			const auto& AccessInfo = TextureAccessPair.Value;

			if (IsTransitionAllowed(Texture, AccessInfo))
			{
				AddTransition(Texture, Texture->State, Texture->StatePending);
			}
			ResolveState(Pass, Texture);
		}
	}

	for (auto BufferAccessPair : Pass->BufferAccessMap)
	{
		FRDGBufferRef Buffer = BufferAccessPair.Key;
		const auto& AccessInfo = BufferAccessPair.Value;

		Buffer->StatePending = FRDGSubresourceState(
			PassHandle,
			PassPipeline,
			AccessInfo.AccessUnion,
			AccessInfo.FlagsUnion,
			AccessInfo.NoUAVBarrierFilter.GetUniqueHandle());

		if (IsTransitionAllowed(Buffer, AccessInfo))
		{
			AddTransition(Buffer, Buffer->State, Buffer->StatePending);
		}
		ResolveState(Pass, Buffer);
	}
}

void FRDGBuilder::ResolveState(const FRDGPass* Pass, FRDGTextureRef Texture)
{
	FRDGTextureState& State = Texture->State;
	FRDGTextureState& StatePending = Texture->StatePending;
	IF_RDG_ENABLE_DEBUG(BarrierValidation.ValidateState(Pass, Texture, StatePending));
	State.MergeFrom(StatePending);
	if (StatePending.IsWholeResourceState())
	{
		StatePending.InitAsWholeResource({});
	}
	else
	{
		StatePending.InitAsSubresources({});
	}
}

void FRDGBuilder::ResolveState(const FRDGPass* Pass, FRDGBufferRef Buffer)
{
	FRDGSubresourceState& State = Buffer->State;
	FRDGSubresourceState& StatePending = Buffer->StatePending;
	IF_RDG_ENABLE_DEBUG(BarrierValidation.ValidateState(Pass, Buffer, StatePending));
	State.MergeFrom(StatePending);
}

void FRDGBuilder::AddTransition(FRDGTextureRef Texture, const FRDGTextureState& StateBefore, const FRDGTextureState& StateAfter)
{
	const auto TryAddSubresourceTransition = [this, Texture](
		const FRDGSubresourceState StateBefore,
		const FRDGSubresourceState StateAfter,
		uint32 MipIndex, uint32 ArraySlice, uint32 PlaneSlice)
	{
		if (StateAfter.Access != EResourceTransitionAccess::Unknown)
		{
			if (FRDGSubresourceState::IsTransitionRequired(StateBefore, StateAfter))
			{
				FRHITransitionInfo Info;
				Info.Type = FRHITransitionInfo::EType::Texture;
				Info.Flags = StateAfter.Flags;
				Info.AccessBefore = StateBefore.Access;
				Info.AccessAfter = StateAfter.Access;
				Info.MipIndex = MipIndex;
				Info.ArraySlice = ArraySlice;
				Info.PlaneSlice = PlaneSlice;

				AddTransitionInternal(Texture, StateBefore, StateAfter, Info);
			}

			IF_RDG_ENABLE_DEBUG(LogFile.AddTransitionEdge(StateBefore, StateAfter, Texture, MipIndex, ArraySlice, PlaneSlice));
		}
	};

	const FRDGTextureSubresourceRange WholeRange = Texture->GetSubresourceRange();

	if (StateBefore.IsWholeResourceState())
	{
		if (StateAfter.IsWholeResourceState())
		{
			const FRDGSubresourceState SubresourceStateBefore = StateBefore.GetWholeResourceState();
			const FRDGSubresourceState SubresourceStateAfter = StateAfter.GetWholeResourceState();

			if (SubresourceStateAfter.Access != EResourceTransitionAccess::Unknown)
			{
				if (FRDGSubresourceState::IsTransitionRequired(SubresourceStateBefore, SubresourceStateAfter))
				{
					FRHITransitionInfo Info;
					Info.Type = FRHITransitionInfo::EType::Texture;
					Info.Flags = SubresourceStateAfter.Flags;
					Info.AccessBefore = SubresourceStateBefore.Access;
					Info.AccessAfter = SubresourceStateAfter.Access;

					AddTransitionInternal(Texture, SubresourceStateBefore, SubresourceStateAfter, Info);
				}

				IF_RDG_ENABLE_DEBUG(LogFile.AddTransitionEdge(SubresourceStateBefore, SubresourceStateAfter, Texture));
			}
		}
		else
		{
			WholeRange.EnumerateSubresources([&](uint32 MipIndex, uint32 ArraySlice, uint32 PlaneSlice)
			{
				TryAddSubresourceTransition(
					StateBefore.GetWholeResourceState(),
					StateAfter.GetSubresourceState(MipIndex, ArraySlice, PlaneSlice),
					MipIndex, ArraySlice, PlaneSlice);
			});
		}
	}
	else
	{
		if (StateAfter.IsWholeResourceState())
		{
			const FRDGSubresourceState& SubresourceStateAfter = StateAfter.GetWholeResourceState();

			WholeRange.EnumerateSubresources([&](uint32 MipIndex, uint32 ArraySlice, uint32 PlaneSlice)
			{
				TryAddSubresourceTransition(
					StateBefore.GetSubresourceState(MipIndex, ArraySlice, PlaneSlice),
					SubresourceStateAfter,
					MipIndex, ArraySlice, PlaneSlice);
			});
		}
		else
		{
			WholeRange.EnumerateSubresources([&](uint32 MipIndex, uint32 ArraySlice, uint32 PlaneSlice)
			{
				TryAddSubresourceTransition(
					StateBefore.GetSubresourceState(MipIndex, ArraySlice, PlaneSlice),
					StateAfter.GetSubresourceState(MipIndex, ArraySlice, PlaneSlice),
					MipIndex, ArraySlice, PlaneSlice);
			});
		}
	}
}

void FRDGBuilder::AddTransition(FRDGBufferRef Buffer, FRDGSubresourceState StateBefore, FRDGSubresourceState StateAfter)
{
	if (StateAfter.Access != EResourceTransitionAccess::Unknown)
	{
		if (FRDGSubresourceState::IsTransitionRequired(StateBefore, StateAfter))
		{
			FRHITransitionInfo Info;
			Info.Type = FRDGBufferDesc::GetTransitionResourceType(Buffer->Desc.UnderlyingType);
			Info.Flags = StateAfter.Flags;
			Info.AccessBefore = StateBefore.Access;
			Info.AccessAfter = StateAfter.Access;

			AddTransitionInternal(Buffer, StateBefore, StateAfter, Info);
		}

		IF_RDG_ENABLE_DEBUG(LogFile.AddTransitionEdge(StateBefore, StateAfter, Buffer));
	}
}

void FRDGBuilder::AddTransitionInternal(
	FRDGParentResource* Resource,
	FRDGSubresourceState StateBefore,
	FRDGSubresourceState StateAfter,
	const FRHITransitionInfo& TransitionInfo)
{
	if (StateBefore.PassHandle.IsNull())
	{
		StateBefore.PassHandle = (Resource->bIsExternal || GRDGExtendResourceLifetimes) ? GetProloguePassHandle() : Resource->StateFirst.PassHandle;
	}

	if (StateAfter.PassHandle.IsNull())
	{
		StateAfter.PassHandle = GetEpiloguePassHandle();
	}

	FRDGPass* PrevPass = Passes[StateBefore.PassHandle];
	FRDGPass* NextPass = Passes[StateAfter.PassHandle];

	// When in immediate mode or if the previous and next passes are the same, the transition
	// occurs immediately before the pass executes in the prologue (no splitting). Otherwise,
	// the transition is split to occur after the previous pass executes.

	FRDGBarrierBatchBegin* BarriersToBegin = nullptr;
	if (PrevPass == NextPass || GRDGImmediateMode != 0)
	{
		BarriersToBegin = &NextPass->PrologueBarriersToBegin;
	}
	else
	{
		BarriersToBegin = PrevPass->GetEpilogueBarriersToBeginFor(StateAfter.Pipeline);
	}
	BarriersToBegin->AddTransition(Resource, TransitionInfo);

	NextPass->PrologueBarriersToEnd.AddDependency(BarriersToBegin);
}

void FRDGBuilder::BeginResourceRHI(FRDGPass* Pass, FRDGTextureRef Texture)
{
	check(Texture);
	check(GRDGExtendResourceLifetimes || !Pass->IsAsyncCompute());

	if (Texture->PooledTexture)
	{
		return;
	}

	IF_RDG_ENABLE_DEBUG(ConditionalDebugBreak(RDG_BREAKPOINT_RESOURCE_LIFETIME, BuilderName.GetTCHAR(), Pass->GetName(), Texture->Name));

	check(Texture->ReferenceCount > 0 || GRDGImmediateMode != 0 || GRDGExtendResourceLifetimes != 0);

	Texture->Init(GRenderTargetPool.AllocateElementForRDG(RHICmdList, Texture->Desc, Texture->Name));

	AllocatedTextures.Add(Texture);

	const FRDGPassHandle PassHandle = Pass->GetHandle();

	// Transition the resource into the correct state for first use.
	if (Texture->StateFirst.Access != EResourceTransitionAccess::Unknown)
	{
		FPooledRenderTarget* Target = static_cast<FPooledRenderTarget*>(Texture->PooledTexture.GetReference());

		// Re-use the target state to avoid an allocation. Initial state is the pass we
		// are allocating. For async compute, this will perform a split-transition; for
		// graphics, it will perform an immediate transition in the prologue.
		Target->State.SetPass(PassHandle, ERDGPipeline::Graphics);

		FRDGTextureState StateAfter(Texture->Desc);
		StateAfter.InitAsWholeResource(Texture->StateFirst);
		AddTransition(Texture, Target->State, StateAfter);
	}

	IF_RDG_ENABLE_DEBUG(LogFile.AddFirstEdge(Texture, PassHandle));
}

void FRDGBuilder::BeginResourceRHI(FRDGPass* Pass, FRDGTextureSRVRef SRV)
{
	check(SRV);
	check(GRDGExtendResourceLifetimes || !Pass->IsAsyncCompute());

	if (SRV->ResourceRHI)
	{
		return;
	}

	FRDGTextureRef Texture = SRV->Desc.Texture;
	check(Texture->PooledTexture);
	FSceneRenderTargetItem& RenderTarget = Texture->PooledTexture->GetRenderTargetItem();

	if (SRV->Desc.MetaData == ERDGTextureMetaDataAccess::HTile)
	{
		check(GRHISupportsExplicitHTile);
		if (!RenderTarget.HTileSRV)
		{
			RenderTarget.HTileSRV = RHICreateShaderResourceViewHTile((FTexture2DRHIRef&)RenderTarget.TargetableTexture);
		}
		SRV->ResourceRHI = RenderTarget.HTileSRV;
		check(SRV->ResourceRHI);
		return;
	}

	for (int32 Idx = 0; Idx < RenderTarget.SRVs.Num(); ++Idx)
	{
		if (RenderTarget.SRVs[Idx].Key == SRV->Desc)
		{
			SRV->ResourceRHI = RenderTarget.SRVs[Idx].Value;
			return;
		}
	}

	FShaderResourceViewRHIRef RHIShaderResourceView = RHICreateShaderResourceView(RenderTarget.ShaderResourceTexture, SRV->Desc);

	SRV->ResourceRHI = RHIShaderResourceView;
	RenderTarget.SRVs.Emplace(SRV->Desc, MoveTemp(RHIShaderResourceView));
}

void FRDGBuilder::BeginResourceRHI(FRDGPass* Pass, FRDGTextureUAVRef UAV)
{
	check(UAV);
	check(GRDGExtendResourceLifetimes || !Pass->IsAsyncCompute());

	if (UAV->ResourceRHI)
	{
		return;
	}

	BeginResourceRHI(Pass, UAV->Desc.Texture);

	FRDGTextureRef Texture = UAV->Desc.Texture;
	check(Texture->PooledTexture);
	FSceneRenderTargetItem& RenderTarget = Texture->PooledTexture->GetRenderTargetItem();

	if (UAV->Desc.MetaData == ERDGTextureMetaDataAccess::HTile)
	{
		check(GRHISupportsExplicitHTile);
		if (!RenderTarget.HTileUAV)
		{
			RenderTarget.HTileUAV = RHICreateUnorderedAccessViewHTile((FTexture2DRHIRef&)RenderTarget.TargetableTexture);
		}
		UAV->ResourceRHI = RenderTarget.HTileUAV;
		check(UAV->ResourceRHI);
		return;
	}

	if (UAV->Desc.MetaData == ERDGTextureMetaDataAccess::Stencil)
	{
		if (!RenderTarget.StencilUAV)
		{
			RenderTarget.StencilUAV = RHICreateUnorderedAccessViewStencil((FTexture2DRHIRef&)RenderTarget.TargetableTexture, 0);
		}
		UAV->ResourceRHI = RenderTarget.StencilUAV;
		check(UAV->ResourceRHI);
		return;
	}

	UAV->ResourceRHI = RenderTarget.MipUAVs[UAV->Desc.MipLevel];
}

void FRDGBuilder::BeginResourceRHI(FRDGPass* Pass, FRDGBufferRef Buffer)
{
	check(Buffer);
	check(GRDGExtendResourceLifetimes || !Pass->IsAsyncCompute());

	if (Buffer->PooledBuffer)
	{
		return;
	}

	IF_RDG_ENABLE_DEBUG(ConditionalDebugBreak(RDG_BREAKPOINT_RESOURCE_LIFETIME, BuilderName.GetTCHAR(), Pass->GetName(), Buffer->Name));

	check(Buffer->ReferenceCount > 0 || GRDGImmediateMode != 0 || GRDGExtendResourceLifetimes != 0);

	TRefCountPtr<FPooledRDGBuffer> PooledBuffer;
	GRenderGraphResourcePool.FindFreeBuffer(RHICmdList, Buffer->Desc, PooledBuffer, Buffer->Name);
	Buffer->Init(PooledBuffer);

	AllocatedBuffers.Add(Buffer);

	const FRDGPassHandle PassHandle = Pass->GetHandle();

	if (Buffer->StateFirst.Access != EResourceTransitionAccess::Unknown)
	{
		PooledBuffer->State.SetPass(PassHandle, ERDGPipeline::Graphics);
		AddTransition(Buffer, PooledBuffer->State, Buffer->StateFirst);
	}

	IF_RDG_ENABLE_DEBUG(LogFile.AddFirstEdge(Buffer, PassHandle));
}

void FRDGBuilder::BeginResourceRHI(FRDGPass* Pass, FRDGBufferSRVRef SRV)
{
	check(SRV);
	check(GRDGExtendResourceLifetimes || !Pass->IsAsyncCompute());

	if (SRV->ResourceRHI)
	{
		return;
	}

	FRDGBufferRef Buffer = SRV->Desc.Buffer;

	check(Buffer->PooledBuffer);

	if (Buffer->PooledBuffer->SRVs.Contains(SRV->Desc))
	{
		SRV->ResourceRHI = Buffer->PooledBuffer->SRVs[SRV->Desc];
		return;
	}

	FShaderResourceViewRHIRef RHIShaderResourceView;

	if (Buffer->Desc.UnderlyingType == FRDGBufferDesc::EUnderlyingType::VertexBuffer)
	{
		RHIShaderResourceView = RHICreateShaderResourceView(Buffer->PooledBuffer->VertexBuffer, SRV->Desc.BytesPerElement, SRV->Desc.Format);
	}
	else if (Buffer->Desc.UnderlyingType == FRDGBufferDesc::EUnderlyingType::StructuredBuffer)
	{
		RHIShaderResourceView = RHICreateShaderResourceView(Buffer->PooledBuffer->StructuredBuffer);
	}
	else
	{
		check(0);
	}

	SRV->ResourceRHI = RHIShaderResourceView;
	Buffer->PooledBuffer->SRVs.Add(SRV->Desc, RHIShaderResourceView);
}

void FRDGBuilder::BeginResourceRHI(FRDGPass* Pass, FRDGBufferUAV* UAV)
{
	check(UAV);
	check(GRDGExtendResourceLifetimes || !Pass->IsAsyncCompute());

	if (UAV->ResourceRHI)
	{
		return;
	}

	FRDGBufferRef Buffer = UAV->Desc.Buffer;
	BeginResourceRHI(Pass, Buffer);

	if (Buffer->PooledBuffer->UAVs.Contains(UAV->Desc))
	{
		UAV->ResourceRHI = Buffer->PooledBuffer->UAVs[UAV->Desc];
		return;
	}

	// Hack to make sure only one UAVs is around.
	Buffer->PooledBuffer->UAVs.Empty();

	FUnorderedAccessViewRHIRef RHIUnorderedAccessView;

	if (Buffer->Desc.UnderlyingType == FRDGBufferDesc::EUnderlyingType::VertexBuffer)
	{
		RHIUnorderedAccessView = RHICreateUnorderedAccessView(Buffer->PooledBuffer->VertexBuffer, UAV->Desc.Format);
	}
	else if (Buffer->Desc.UnderlyingType == FRDGBufferDesc::EUnderlyingType::StructuredBuffer)
	{
		RHIUnorderedAccessView = RHICreateUnorderedAccessView(Buffer->PooledBuffer->StructuredBuffer, UAV->Desc.bSupportsAtomicCounter, UAV->Desc.bSupportsAppendBuffer);
	}
	else
	{
		check(0);
	}

	UAV->ResourceRHI = RHIUnorderedAccessView;
	Buffer->PooledBuffer->UAVs.Add(UAV->Desc, RHIUnorderedAccessView);
}

void FRDGBuilder::EndResourceRHI(FRDGPass* Pass, FRDGTextureRef Texture, const FRDGTextureSubresourceRange Range)
{
	if (!GRDGImmediateMode)
	{
		const int32 ReferenceCount = Range.GetSubresourceCount();
		check(Texture->ReferenceCount >= ReferenceCount);
		Texture->ReferenceCount -= ReferenceCount;

		if (Texture->ReferenceCount == 0 && !GRDGExtendResourceLifetimes)
		{
			IF_RDG_ENABLE_DEBUG(ConditionalDebugBreak(RDG_BREAKPOINT_RESOURCE_LIFETIME, BuilderName.GetTCHAR(), Pass ? Pass->GetName() : TEXT("Epilogue"), Texture->Name));

			if (Pass && GRDGAsyncCompute)
			{
				const FRDGPassHandle GraphicsJoinPassHandle = Pass->GetGraphicsJoinPass();

				// Transition back to the graphics pipe prior to release.
				Texture->StatePending.InitAsSubresources({});
				Texture->StatePending.MergeCrossPipelineFrom(Texture->State);
				Texture->StatePending.SetPass(GraphicsJoinPassHandle, ERDGPipeline::Graphics);
				//AddTransition(Texture, Texture->State, Texture->StatePending);
				ResolveState(Pass, Texture);

				// Defer release of the resource until we've joined back to graphics.
				Passes[GraphicsJoinPassHandle]->TexturesToRelease.Add(Texture);
			}
			else
			{
				ReleaseResourceRHI(Texture);
			}
		}
	}
}

void FRDGBuilder::EndResourceRHI(FRDGPass* Pass, FRDGBufferRef Buffer)
{
	if (!GRDGImmediateMode)
	{
		check(Buffer->ReferenceCount > 0);
		Buffer->ReferenceCount--;

		if (Buffer->ReferenceCount == 0 && !GRDGExtendResourceLifetimes)
		{
			IF_RDG_ENABLE_DEBUG(ConditionalDebugBreak(RDG_BREAKPOINT_RESOURCE_LIFETIME, BuilderName.GetTCHAR(), Pass ? Pass->GetName() : TEXT("Epilogue"), Buffer->Name));

			if (Pass && GRDGAsyncCompute)
			{
				const FRDGPassHandle GraphicsJoinPassHandle = Pass->GetGraphicsJoinPass();

				// Transition back to the graphics pipe prior to release.
				FRDGSubresourceState StatePending;
				StatePending.MergeCrossPipelineFrom(Buffer->State);
				StatePending.SetPass(GraphicsJoinPassHandle, ERDGPipeline::Graphics);
				//AddTransition(Buffer, Buffer->State, StatePending);
				ResolveState(Pass, Buffer);

				// Defer release of the resource until we've joined back to graphics.
				Passes[GraphicsJoinPassHandle]->BuffersToRelease.Add(Buffer);
			}
			else
			{
				ReleaseResourceRHI(Buffer);
			}
		}
	}
}

void FRDGBuilder::ReleaseResourceRHI(FRDGTextureRef Texture)
{
	check(Texture->ReferenceCount == 0);
	{
		FPooledRenderTarget* Target = static_cast<FPooledRenderTarget*>(Texture->PooledTexture.GetReference());
		check(Target);
		Target->State.MergeSanitizedFrom(Texture->State);
	}
	Texture->ResourceRHI = nullptr;
	Texture->PooledTexture = nullptr;
	AllocatedTextures.Remove(Texture);
}

void FRDGBuilder::ReleaseResourceRHI(FRDGBufferRef Buffer)
{
	check(Buffer->ReferenceCount == 0);

	// Assign state back to the target, but clear out any graph-related information.
	Buffer->PooledBuffer->State.MergeSanitizedFrom(Buffer->State);
	Buffer->PooledBuffer = nullptr;
	Buffer->ResourceRHI = nullptr;
	AllocatedBuffers.Remove(Buffer);
}

void FRDGBuilder::BeginEventScope(FRDGEventName&& ScopeName)
{
	IF_RDG_SCOPES(ScopeStacks.BeginEventScope(Forward<FRDGEventName&&>(ScopeName)));
}

void FRDGBuilder::EndEventScope()
{
	IF_RDG_SCOPES(ScopeStacks.EndEventScope());
}

void FRDGBuilder::BeginStatScope(const FName& Name, const FName& StatName)
{
	IF_RDG_SCOPES(ScopeStacks.BeginStatScope(Name, StatName));
}

void FRDGBuilder::EndStatScope()
{
	IF_RDG_SCOPES(ScopeStacks.EndStatScope());
}

#if RDG_ENABLE_DEBUG

void FRDGBuilder::VisualizePassOutputs(const FRDGPass* Pass)
{
#if SUPPORTS_VISUALIZE_TEXTURE
	if (!GVisualizeTexture.bEnabled || bInDebugPassScope)
	{
		return;
	}

	const FRDGPassParameterStruct PassParameters = Pass->GetParameters();

	for (uint32 Index = 0; Index < PassParameters.GetParameterCount(); ++Index)
	{
		const FRDGPassParameter Parameter = PassParameters.GetParameter(Index);
		switch (Parameter.GetType())
		{
		case UBMT_RDG_TEXTURE_UAV:
		{
			if (FRDGTextureUAVRef UAV = Parameter.GetAsTextureUAV())
			{
				FRDGTextureRef Texture = UAV->Desc.Texture;
				check(Texture);

				int32 CaptureId = GVisualizeTexture.ShouldCapture(Texture->Name);
				if (CaptureId != FVisualizeTexture::kInvalidCaptureId && UAV->Desc.MipLevel == GVisualizeTexture.CustomMip)
				{
					GVisualizeTexture.CreateContentCapturePass(*this, Texture, CaptureId);
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
				int32 CaptureId = GVisualizeTexture.ShouldCapture(Texture->Name);

				if (CaptureId != FVisualizeTexture::kInvalidCaptureId && RenderTarget.GetMipIndex() == GVisualizeTexture.CustomMip)
				{
					GVisualizeTexture.CreateContentCapturePass(*this, Texture, CaptureId);
				}
			});

			const FDepthStencilBinding& DepthStencil = RenderTargets.DepthStencil;

			if (FRDGTextureRef Texture = DepthStencil.GetTexture())
			{
				const bool bHasStoreAction = DepthStencil.GetDepthStencilAccess().IsAnyWrite();

				if (bHasStoreAction)
				{
					// Depth render target binding can only be done on mip level 0.
					const int32 MipLevel = 0;

					int32 CaptureId = GVisualizeTexture.ShouldCapture(Texture->Name);
					if (CaptureId != FVisualizeTexture::kInvalidCaptureId && MipLevel == GVisualizeTexture.CustomMip)
					{
						GVisualizeTexture.CreateContentCapturePass(*this, Texture, CaptureId);
					}
				}
			}
		}
		break;
		}
	}
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

	const FRDGPassParameterStruct PassParameters = Pass->GetParameters();

	for (uint32 Index = 0; Index < PassParameters.GetParameterCount(); ++Index)
	{
		const FRDGPassParameter Parameter = PassParameters.GetParameter(Index);
		switch (Parameter.GetType())
		{
		case UBMT_RDG_BUFFER_UAV:
		{
			if (FRDGBufferUAVRef UAV = Parameter.GetAsBufferUAV())
			{
				FRDGBufferRef Buffer = UAV->GetParent();

				if (UserValidation.TryMarkForClobber(Buffer))
				{
					if (UAV->Desc.Buffer->Desc.UnderlyingType == FRDGBufferDesc::EUnderlyingType::StructuredBuffer)
					{
						// TODO(RDG): Fix me.
						//AddClearStructuredBufferUAVPass(*this, UAV, GetClobberBufferValue());
					}
					else
					{
						AddClearUAVPass(*this, UAV, GetClobberBufferValue());
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
	}

	bInDebugPassScope = false;
}

#endif //! RDG_ENABLE_DEBUG
