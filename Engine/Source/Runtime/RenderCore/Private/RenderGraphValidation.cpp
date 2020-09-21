// Copyright Epic Games, Inc. All Rights Reserved.

#include "RenderGraphValidation.h"
#include "RenderGraphPrivate.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if RDG_ENABLE_DEBUG

namespace
{
template <typename TFunction>
void EnumerateSubresources(const FRHITransitionInfo& Transition, uint32 NumMips, uint32 NumArraySlices, uint32 NumPlaneSlices, TFunction Function)
{
	uint32 MinMipIndex = 0;
	uint32 MaxMipIndex = NumMips;
	uint32 MinArraySlice = 0;
	uint32 MaxArraySlice = NumArraySlices;
	uint32 MinPlaneSlice = 0;
	uint32 MaxPlaneSlice = NumPlaneSlices;

	if (!Transition.IsAllMips())
	{
		MinMipIndex = Transition.MipIndex;
		MaxMipIndex = MinMipIndex + 1;
	}

	if (!Transition.IsAllArraySlices())
	{
		MinArraySlice = Transition.ArraySlice;
		MaxArraySlice = MinArraySlice + 1;
	}

	if (!Transition.IsAllPlaneSlices())
	{
		MinPlaneSlice = Transition.PlaneSlice;
		MaxPlaneSlice = MinPlaneSlice + 1;
	}

	for (uint32 PlaneSlice = MinPlaneSlice; PlaneSlice < MaxPlaneSlice; ++PlaneSlice)
	{
		for (uint32 ArraySlice = MinArraySlice; ArraySlice < MaxArraySlice; ++ArraySlice)
		{
			for (uint32 MipIndex = MinMipIndex; MipIndex < MaxMipIndex; ++MipIndex)
			{
				Function(MipIndex, ArraySlice, PlaneSlice);
			}
		}
	}
}

/** Validates that we are only executing a single render graph instance in the callstack. Used to catch if a
 *  user creates a second FRDGBuilder instance inside of a pass that is executing.
 */
bool GRDGInExecutePassScope = false;
} //! namespace

FRDGUserValidation::~FRDGUserValidation()
{
	checkf(bHasExecuted, TEXT("Render graph execution is required to ensure consistency with immediate mode."));
}

void FRDGUserValidation::ExecuteGuard(const TCHAR* Operation, const TCHAR* ResourceName)
{
	checkf(!bHasExecuted, TEXT("Render graph operation '%s' with resource '%s' must be performed prior to graph execution."), Operation, ResourceName);
}

void FRDGUserValidation::ValidateCreateTexture(FRDGTextureRef Texture)
{
	check(Texture);
	if (GRDGDebug)
	{
		TrackedTextures.Add(Texture);
	}
}

void FRDGUserValidation::ValidateCreateBuffer(FRDGBufferRef Buffer)
{
	check(Buffer);
	if (GRDGDebug)
	{
		TrackedBuffers.Add(Buffer);
	}
}

void FRDGUserValidation::ValidateCreateExternalTexture(FRDGTextureRef Texture)
{
	ValidateCreateTexture(Texture);
	Texture->ParentDebugData.bHasBeenProduced = true;
}

void FRDGUserValidation::ValidateCreateExternalBuffer(FRDGBufferRef Buffer)
{
	ValidateCreateBuffer(Buffer);
	Buffer->ParentDebugData.bHasBeenProduced = true;
}

void FRDGUserValidation::ValidateExtractResource(FRDGParentResourceRef Resource)
{
	check(Resource);

	checkf(Resource->ParentDebugData.bHasBeenProduced,
		TEXT("Unable to queue the extraction of the resource %s because it has not been produced by any pass."),
		Resource->Name);

	/** Increment pass access counts for externally registered buffers and textures to avoid
	 *  emitting a 'produced but never used' warning. We don't have the history of registered
	 *  resources to be able to emit a proper warning.
	 */
	Resource->ParentDebugData.PassAccessCount++;
}

void FRDGUserValidation::RemoveUnusedWarning(FRDGParentResourceRef Resource)
{
	check(Resource);

	// Removes 'produced but not used' warning.
	Resource->ParentDebugData.PassAccessCount++;

	// Removes 'not used' warning.
	Resource->DebugData.bIsActuallyUsedByPass = true;
}

bool FRDGUserValidation::TryMarkForClobber(FRDGParentResourceRef Resource) const
{
	const bool bClobber = !Resource->ParentDebugData.bHasBeenClobbered && !Resource->bIsExternal && IsDebugAllowedForResource(Resource->Name);

	if (bClobber)
	{
		Resource->ParentDebugData.bHasBeenClobbered = true;
	}

	return bClobber;
}

void FRDGUserValidation::ValidateAllocPassParameters(const void* Parameters)
{
	check(Parameters);
	AllocatedUnusedPassParameters.Add(Parameters);
}

void FRDGUserValidation::ValidateAddPass(const FRDGPass* Pass, bool bSkipPassAccessMarking)
{
	checkf(!bHasExecuted, TEXT("Render graph pass %s needs to be added before the builder execution."), Pass->GetName());

	{
		const void* ParameterStructData = Pass->GetParameters().GetContents();

		/** The lifetime of each pass parameter structure must extend until deferred pass execution; therefore, it needs to be
		 *  allocated with FRDGBuilder::AllocParameters(). Also, all references held by the parameter structure are released
		 *  immediately after pass execution, so a pass parameter struct instance must be 1-to-1 with a pass instance (i.e. one
		 *  per AddPass() call).
		 */
		checkf(
			AllocatedUnusedPassParameters.Contains(ParameterStructData),
			TEXT("The pass parameter structure has not been allocated for correct life time FRDGBuilder::AllocParameters() or has already ")
			TEXT("been used by another previous FRDGBuilder::AddPass()."));

		AllocatedUnusedPassParameters.Remove(ParameterStructData);
	}

	const FRenderTargetBindingSlots* RenderTargetBindingSlots = nullptr;

	// Pass flags are validated as early as possible by the builder in AddPass.
	const ERDGPassFlags PassFlags = Pass->GetFlags();

	const TCHAR* PassName = Pass->GetName();
	const bool bIsRaster = EnumHasAnyFlags(PassFlags, ERDGPassFlags::Raster);
	const bool bIsCopy = EnumHasAnyFlags(PassFlags, ERDGPassFlags::Copy);

	const auto MarkAsProduced = [&] (FRDGParentResourceRef Resource)
	{
		if (!bSkipPassAccessMarking)
		{
			auto& Debug = Resource->ParentDebugData;
			if (!Debug.bHasBeenProduced)
			{
				Debug.bHasBeenProduced = true;
				Debug.FirstProducer = Pass;
			}
			Debug.PassAccessCount++;
		}
	};

	const auto MarkAsConsumed = [&] (FRDGParentResourceRef Resource)
	{
		auto& Debug = Resource->ParentDebugData;

		ensureMsgf(Debug.bHasBeenProduced,
			TEXT("Pass %s has a read dependency on %s, but it was never written to."),
			PassName, Resource->Name);

		if (!bSkipPassAccessMarking)
		{
			Debug.PassAccessCount++;
		}
	};

	const auto CheckNotCopy = [&](FRDGResourceRef Resource)
	{
		ensureMsgf(!bIsCopy, TEXT("Pass %s, parameter %s is valid for Raster or (Async)Compute, but the pass is a Copy pass."), PassName, Resource->Name);
	};

	const FRDGPassParameterStruct PassParameters = Pass->GetParameters();

	for (uint32 Index = 0; Index < PassParameters.GetParameterCount(); ++Index)
	{
		const FRDGPassParameter Parameter = PassParameters.GetParameter(Index);

		switch (Parameter.GetType())
		{
		case UBMT_RDG_TEXTURE:
		{
			if (FRDGTextureRef Texture = Parameter.GetAsTexture())
			{
				MarkAsConsumed(Texture);
			}
		}
		break;
		case UBMT_RDG_TEXTURE_SRV:
		{
			if (FRDGTextureSRVRef SRV = Parameter.GetAsTextureSRV())
			{
				FRDGTextureRef Texture = SRV->GetParent();

				CheckNotCopy(Texture);
				MarkAsConsumed(Texture);
			}
		}
		break;
		case UBMT_RDG_TEXTURE_UAV:
		{
			if (FRDGTextureUAVRef UAV = Parameter.GetAsTextureUAV())
			{
				FRDGTextureRef Texture = UAV->GetParent();
				CheckNotCopy(Texture);
				MarkAsProduced(Texture);
			}
		}
		break;
		case UBMT_RDG_BUFFER:
		{
			if (FRDGBufferRef Buffer = Parameter.GetAsBuffer())
			{
				MarkAsConsumed(Buffer);
			}
		}
		break;
		case UBMT_RDG_BUFFER_SRV:
		{
			if (FRDGBufferSRVRef SRV = Parameter.GetAsBufferSRV())
			{
				FRDGBufferRef Buffer = SRV->GetParent();
				CheckNotCopy(Buffer);
				MarkAsConsumed(Buffer);
			}
		}
		break;
		case UBMT_RDG_BUFFER_UAV:
		{
			if (FRDGBufferUAVRef UAV = Parameter.GetAsBufferUAV())
			{
				FRDGBufferRef Buffer = UAV->GetParent();
				CheckNotCopy(Buffer);
				MarkAsProduced(Buffer);
			}
		}
		break;
		case UBMT_RDG_TEXTURE_COPY_DEST:
		{
			if (FRDGTextureRef Texture = Parameter.GetAsTexture())
			{
				MarkAsProduced(Texture);
			}
		}
		break;
		case UBMT_RDG_BUFFER_COPY_DEST:
		{
			if (FRDGBufferRef Buffer = Parameter.GetAsBuffer())
			{
				MarkAsProduced(Buffer);
			}
		}
		break;
		case UBMT_RENDER_TARGET_BINDING_SLOTS:
		{
			RenderTargetBindingSlots = &Parameter.GetAsRenderTargetBindingSlots();
		}
		break;
		}
	}

	/** Validate that raster passes have render target binding slots and compute passes don't. */
	if (RenderTargetBindingSlots)
	{
		checkf(bIsRaster, TEXT("Pass '%s' has render target binding slots but is not set to 'Raster'."), PassName);
	}
	else
	{
		checkf(!bIsRaster, TEXT("Pass '%s' is set to 'Raster' but is missing render target binding slots."), PassName);
	}

	/** Validate render target / depth stencil binding usage. */
	if (RenderTargetBindingSlots)
	{
		const auto& RenderTargets = RenderTargetBindingSlots->Output;

		{
			const auto& DepthStencil = RenderTargetBindingSlots->DepthStencil;

			if (FRDGTextureRef Texture = DepthStencil.GetTexture())
			{
				checkf(
					Texture->Desc.TargetableFlags & TexCreate_DepthStencilTargetable,
					TEXT("Pass '%s' attempted to bind texture '%s' as a depth stencil render target, but the texture has not been created with TexCreate_DepthStencilTargetable."),
					PassName, Texture->Name);

				// Depth stencil only supports one mip, since there isn't actually a way to select the mip level.
				check(Texture->Desc.NumMips == 1);

				if (DepthStencil.GetDepthStencilAccess().IsAnyWrite())
				{
					MarkAsProduced(Texture);
				}
				else
				{
					MarkAsConsumed(Texture);
				}
			}
		}

		const uint32 RenderTargetCount = RenderTargets.Num();

		{
			/** Tracks the number of contiguous, non-null textures in the render target output array. */
			uint32 ValidRenderTargetCount = 0;

			for (uint32 RenderTargetIndex = 0; RenderTargetIndex < RenderTargetCount; ++RenderTargetIndex)
			{
				const FRenderTargetBinding& RenderTarget = RenderTargets[RenderTargetIndex];

				if (FRDGTextureRef Texture = RenderTarget.GetTexture())
				{
					const bool bIsLoadAction = RenderTarget.GetLoadAction() == ERenderTargetLoadAction::ELoad;

					ensureMsgf(
						Texture->Desc.TargetableFlags & TexCreate_RenderTargetable,
						TEXT("Pass '%s' attempted to bind texture '%s' as a render target, but the texture has not been created with TexCreate_RenderTargetable."),
						PassName, Texture->Name);

					/** Validate that load action is correct. We can only load contents if a pass previously produced something. */
					{
						const bool bIsLoadActionInvalid = bIsLoadAction && !Texture->ParentDebugData.bHasBeenProduced;
						checkf(
							!bIsLoadActionInvalid,
							TEXT("Pass '%s' attempted to bind texture '%s' as a render target with the 'Load' action specified, but the texture has not been produced yet. The render target must use either 'Clear' or 'NoAction' action instead."),
							PassName,
							Texture->Name);
					}

					/** Validate that any previously produced texture contents are loaded. This occurs if the user failed to specify a load action
					 *  on a texture that was produced by a previous pass, effectively losing that data. This can also happen if the user 're-uses'
					 *  a texture for some other purpose. The latter is considered bad practice, since it increases memory pressure on the render
					 *  target pool. Instead, the user should create a new texture instance. An exception to this rule are untracked render targets,
					 *  which are not actually managed by the render target pool and likely represent the frame buffer.
					 */
					{
						// Ignore external textures which are always marked as produced. We don't need to enforce this warning on them.
						const bool bHasBeenProduced = Texture->ParentDebugData.bHasBeenProduced && !Texture->bIsExternal;

						// We only validate single-mip textures since we don't track production at the subresource level.
						const bool bFailedToLoadProducedContent = !bIsLoadAction && bHasBeenProduced && Texture->Desc.NumMips == 1;

						// Untracked render targets aren't actually managed by the render target pool.
						const bool bIsUntrackedRenderTarget = Texture->PooledTexture && !Texture->PooledTexture->IsTracked();

						ensureMsgf(!bFailedToLoadProducedContent || bIsUntrackedRenderTarget,
							TEXT("Pass '%s' attempted to bind texture '%s' as a render target without the 'Load' action specified, despite a prior pass having produced it. It's invalid to completely clobber the contents of a resource. Create a new texture instance instead."),
							PassName,
							Texture->Name);
					}

					/** Mark the pass as a producer for render targets with a store action. */
					MarkAsProduced(Texture);
				}
				else
				{
					/** Found end of contiguous interval of valid render targets. */
					ValidRenderTargetCount = RenderTargetIndex;
					break;
				}
			}

			/** Validate that no holes exist in the render target output array. Render targets must be bound contiguously. */
			for (uint32 RenderTargetIndex = ValidRenderTargetCount; RenderTargetIndex < RenderTargetCount; ++RenderTargetIndex)
			{
				const FRenderTargetBinding& RenderTarget = RenderTargets[RenderTargetIndex];
				checkf(RenderTarget.GetTexture() == nullptr, TEXT("Render targets must be packed. No empty spaces in the array."));
			}
		}
	}
}

void FRDGUserValidation::ValidateExecuteBegin()
{
	checkf(!bHasExecuted, TEXT("Render graph execution should only happen once to ensure consistency with immediate mode."));

	/** FRDGBuilder::AllocParameters() allocates shader parameter structure for the lifetime until pass execution.
	 *  But they are allocated on a FMemStack for CPU performance, and are destructed immediately after pass execution.
	 *  Therefore allocating pass parameter unused by a FRDGBuilder::AddPass() can lead on a memory leak of RHI resources
	 *  that have been reference in the parameter structure.
	 */
	checkf(
		AllocatedUnusedPassParameters.Num() == 0,
		TEXT("%i pass parameter structure has been allocated with FRDGBuilder::AllocParameters(), but has not be used by a ")
		TEXT("FRDGBuilder::AddPass() that can cause RHI resource leak."), AllocatedUnusedPassParameters.Num());
}

void FRDGUserValidation::ValidateExecuteEnd()
{
	bHasExecuted = true;

	if (GRDGDebug)
	{
		auto ValidateResourceAtExecuteEnd = [](const FRDGParentResourceRef Resource)
		{
			check(Resource->ReferenceCount == 0);

			const auto& ParentDebugData = Resource->ParentDebugData;
			const bool bProducedButNeverUsed = ParentDebugData.PassAccessCount == 1 && ParentDebugData.FirstProducer;

			if (bProducedButNeverUsed)
			{
				check(ParentDebugData.bHasBeenProduced);

				EmitRDGWarningf(
					TEXT("Resource %s has been produced by the pass %s, but never used by another pass."),
					Resource->Name, ParentDebugData.FirstProducer->GetName());
			}
		};

		for (const FRDGTextureRef Texture : TrackedTextures)
		{
			ValidateResourceAtExecuteEnd(Texture);

			const bool bHasBeenProducedByGraph = !Texture->bIsExternal && Texture->ParentDebugData.PassAccessCount > 0;

			if (bHasBeenProducedByGraph && !Texture->TextureDebugData.bHasNeededUAV && (Texture->Desc.TargetableFlags & TexCreate_UAV))
			{
				EmitRDGWarningf(
					TEXT("Resource %s first produced by the pass %s had the TexCreate_UAV flag, but no UAV has been used."),
					Texture->Name, Texture->ParentDebugData.FirstProducer->GetName());
			}

			if (bHasBeenProducedByGraph && !Texture->TextureDebugData.bHasBeenBoundAsRenderTarget && (Texture->Desc.TargetableFlags & TexCreate_RenderTargetable))
			{
				EmitRDGWarningf(
					TEXT("Resource %s first produced by the pass %s had the TexCreate_RenderTargetable flag, but has never been bound as a render target of a pass."),
					Texture->Name, Texture->ParentDebugData.FirstProducer->GetName());
			}
		}

		for (const FRDGBufferRef Buffer : TrackedBuffers)
		{
			ValidateResourceAtExecuteEnd(Buffer);
		}
	}

	TrackedTextures.Empty();
	TrackedBuffers.Empty();
}

void FRDGUserValidation::ValidateExecutePassBegin(const FRDGPass* Pass)
{
	check(Pass);
	checkf(!GRDGInExecutePassScope, TEXT("Render graph is being executed recursively. This usually means a separate FRDGBuilder instance was created inside of an executing pass."));

	GRDGInExecutePassScope = true;

	SetAllowRHIAccess(Pass, true);

	if (GRDGDebug)
	{
		const FRDGPassParameterStruct PassParameters = Pass->GetParameters();

		for (uint32 Index = 0; Index < PassParameters.GetParameterCount(); ++Index)
		{
			const FRDGPassParameter Parameter = PassParameters.GetParameter(Index);

			if (Parameter.GetType() == UBMT_RDG_TEXTURE_UAV)
			{
				if (FRDGTextureUAVRef UAV = Parameter.GetAsTextureUAV())
				{
					FRDGTextureRef Texture = UAV->Desc.Texture;
					Texture->TextureDebugData.bHasNeededUAV = true;
				}
			}
			else if (Parameter.GetType() == UBMT_RENDER_TARGET_BINDING_SLOTS)
			{
				const FRenderTargetBindingSlots& RenderTargets = Parameter.GetAsRenderTargetBindingSlots();

				RenderTargets.Enumerate([&](FRenderTargetBinding RenderTarget)
				{
					RenderTarget.GetTexture()->TextureDebugData.bHasBeenBoundAsRenderTarget = true;
				});

				if (FRDGTextureRef Texture = RenderTargets.DepthStencil.GetTexture())
				{
					Texture->TextureDebugData.bHasBeenBoundAsRenderTarget = true;
				}
			}
		}
	}
}

void FRDGUserValidation::ValidateExecutePassEnd(const FRDGPass* Pass)
{
	SetAllowRHIAccess(Pass, false);

	const FRDGPassParameterStruct PassParameters = Pass->GetParameters();

	if (GRDGDebug)
	{
		uint32 TrackedResourceCount = 0;
		uint32 UsedResourceCount = 0;

		for (uint32 Index = 0; Index < PassParameters.GetParameterCount(); ++Index)
		{
			const FRDGPassParameter Parameter = PassParameters.GetParameter(Index);
			if (Parameter.IsResource())
			{
				if (FRDGResourceRef Resource = Parameter.GetAsResource())
				{
					TrackedResourceCount++;
					UsedResourceCount += Resource->DebugData.bIsActuallyUsedByPass ? 1 : 0;
				}
			}
		}

		if (TrackedResourceCount != UsedResourceCount)
		{
			FString WarningMessage = FString::Printf(
				TEXT("'%d' of the '%d' resources of the pass '%s' were not actually used."),
				TrackedResourceCount - UsedResourceCount, TrackedResourceCount, Pass->GetName());

			for (uint32 Index = 0; Index < PassParameters.GetParameterCount(); ++Index)
			{
				const FRDGPassParameter Parameter = PassParameters.GetParameter(Index);
				if (Parameter.IsResource())
				{
					if (const FRDGResourceRef Resource = Parameter.GetAsResource())
					{
						if (!Resource->DebugData.bIsActuallyUsedByPass)
						{
							WarningMessage += FString::Printf(TEXT("\n    %s"), Resource->Name);
						}
					}
				}
			}

			EmitRDGWarning(WarningMessage);
		}
	}

	for (uint32 Index = 0; Index < PassParameters.GetParameterCount(); ++Index)
	{
		const FRDGPassParameter Parameter = PassParameters.GetParameter(Index);
		if (Parameter.IsResource())
		{
			if (FRDGResourceRef Resource = Parameter.GetAsResource())
			{
				Resource->DebugData.bIsActuallyUsedByPass = false;
			}
		}
	}

	GRDGInExecutePassScope = false;
}

void FRDGUserValidation::SetAllowRHIAccess(const FRDGPass* Pass, bool bAllowAccess)
{
	const FRDGPassParameterStruct PassParameters = Pass->GetParameters();

	for (uint32 Index = 0; Index < PassParameters.GetParameterCount(); ++Index)
	{
		const FRDGPassParameter Parameter = PassParameters.GetParameter(Index);

		if (Parameter.IsResource())
		{
			if (FRDGResourceRef Resource = Parameter.GetAsResource())
			{
				Resource->DebugData.bAllowRHIAccess = bAllowAccess;
			}
		}
		else if (Parameter.IsRenderTargetBindingSlots())
		{
			const FRenderTargetBindingSlots& RenderTargets = Parameter.GetAsRenderTargetBindingSlots();

			RenderTargets.Enumerate([&](FRenderTargetBinding RenderTarget)
			{
				RenderTarget.GetTexture()->DebugData.bAllowRHIAccess = bAllowAccess;
			});

			if (FRDGTextureRef Texture = RenderTargets.DepthStencil.GetTexture())
			{
				Texture->DebugData.bAllowRHIAccess = bAllowAccess;
			}
		}
	}
}

FRDGBarrierValidation::FRDGBarrierValidation(const FRDGPassRegistry* InPasses, const FRDGEventName& InGraphName)
	: Passes(InPasses)
	, GraphName(InGraphName.GetTCHAR())
{
	check(Passes);
}

void FRDGBarrierValidation::ValidateState(const FRDGPass* RequestingPass, FRDGTextureRef Texture, const FRDGTextureState& State)
{
	check(Texture);

	if (State.IsWholeResourceState())
	{
		const FRDGSubresourceState SubresourceState = State.GetWholeResourceState();

		if (!IsValidAccess(SubresourceState.Access))
		{
			const FRDGPass* Pass = Passes->Get(SubresourceState.PassHandle);

			UE_LOG(LogRDG, Error,
				TEXT("RDG Texture '%s' in Pass '%s' using Access (%s). Simultaneous read and write access and is not allowed. Visual corruption may occur."),
				Pass->GetName(), Texture->Name, *GetResourceTransitionAccessName(SubresourceState.Access));
		}
	}
	else
	{
		const FRDGTextureSubresourceRange WholeRange = Texture->GetSubresourceRange();

		WholeRange.EnumerateSubresources([&](uint32 MipIndex, uint32 ArraySlice, uint32 PlaneSlice)
		{
			const FRDGSubresourceState SubresourceState = State.GetSubresourceState(MipIndex, ArraySlice, PlaneSlice);

			if (!IsValidAccess(SubresourceState.Access))
			{
				const FRDGPass* Pass = Passes->Get(SubresourceState.PassHandle);

				UE_LOG(LogRDG, Error,
					TEXT("RDG Texture '%s' in Pass '%s' with subresource Mip(%u), Array(%u), Plane(%u) using Access (%s). Simultaneous read and write access and is not allowed. Visual corruption may occur."),
					Pass->GetName(), Texture->Name, MipIndex, ArraySlice, PlaneSlice, *GetResourceTransitionAccessName(SubresourceState.Access));
			}
		});
	}

	Texture->TextureDebugData.States.Emplace(RequestingPass, State);
}

void FRDGBarrierValidation::ValidateState(const FRDGPass* RequestingPass, FRDGBufferRef Buffer, FRDGSubresourceState State)
{
	check(Buffer);

	if (!IsValidAccess(State.Access))
	{
		const FRDGPass* Pass = Passes->Get(State.PassHandle);

		UE_LOG(LogRDG, Error,
			TEXT("RDG Buffer '%s' in Pass '%s' using Access (%s). Simultaneous read and write access and is not allowed. Visual corruption may occur."),
			Pass->GetName(), Buffer->Name, *GetResourceTransitionAccessName(State.Access));
	}

	Buffer->BufferDebugData.States.Emplace(RequestingPass, State);
}

void FRDGBarrierValidation::ValidateBarrierBatchBegin(const FRDGBarrierBatchBegin& Batch)
{
	checkf(!Batch.IsTransitionValid() && !BatchMap.Contains(&Batch), TEXT("Begin barrier batch '%s' has already been submitted."), *Batch.GetName());

	if (!GRDGTransitionLog)
	{
		return;
	}

	FResourceMap& ResourceMap = BatchMap.Emplace(&Batch);

	for (int32 Index = 0; Index < Batch.Transitions.Num(); ++Index)
	{
		FRDGParentResourceRef Resource = Batch.Resources[Index];
		const FRHITransitionInfo& Transition = Batch.Transitions[Index];

		if (Resource->Type == ERDGParentResourceType::Texture)
		{
			ResourceMap.Textures.FindOrAdd(static_cast<FRDGTextureRef>(Resource)).Add(Transition);
		}
		else
		{
			check(Resource->Type == ERDGParentResourceType::Buffer);
			FRDGBufferRef Buffer = static_cast<FRDGBufferRef>(Resource);
			checkf(!ResourceMap.Buffers.Contains(Buffer), TEXT("Buffer %s was added multiple times to batch %s."), Buffer->Name, *Batch.GetName());
			ResourceMap.Buffers.Emplace(Buffer, Transition);
		}
	}

	if (!IsDebugAllowedForGraph(GraphName) || !IsDebugAllowedForPass(Batch.Pass->GetName()))
	{
		return;
	}

	bool bFoundFirst = false;

	const auto LogHeader = [&]()
	{
		if (!bFoundFirst)
		{
			bFoundFirst = true;
			UE_LOG(LogRDG, Display, TEXT("%s (Begin):"), *Batch.GetName());
		}
	};

	for (const auto& Pair : ResourceMap.Textures)
	{
		FRDGTextureRef Texture = Pair.Key;
		
		if (!IsDebugAllowedForResource(Texture->Name))
		{
			continue;
		}

		const auto& Transitions = Pair.Value;
		if (Transitions.Num())
		{
			LogHeader();
			UE_LOG(LogRDG, Display, TEXT("\t%s:"), Texture->Name);
		}

		const FRDGTextureSubresourceLayout SubresourceLayout = Texture->GetSubresourceLayout();

		TBitArray<SceneRenderingBitArrayAllocator> SubresourceBits;
		SubresourceBits.Init(false, SubresourceLayout.GetSubresourceCount());

		for (const FRHITransitionInfo& Transition : Transitions)
		{
			check(SubresourceLayout.GetSubresourceCount() > 0);

			EnumerateSubresources(Transition, SubresourceLayout.NumMips, SubresourceLayout.NumArraySlices, SubresourceLayout.NumPlaneSlices,
				[&](uint32 MipIndex, uint32 ArraySlice, uint32 PlaneSlice)
			{
				const int32 SubresourceIndex = SubresourceLayout.GetSubresourceIndex(MipIndex, ArraySlice, PlaneSlice);

				UE_LOG(LogRDG, Display, TEXT("\t\tMip(%d), Array(%d), Slice(%d): %s -> %s"),
					MipIndex, ArraySlice, PlaneSlice,
					*GetResourceTransitionAccessName(Transition.AccessBefore),
					*GetResourceTransitionAccessName(Transition.AccessAfter));

				if (SubresourceBits[SubresourceIndex])
				{
					UE_LOG(LogRDG, Error, TEXT("\t\t\tDetected duplicate subresource transitions! Resource(%s), Mip(%d), Array(%d), Plane(%d)"),
						Texture->Name, MipIndex, ArraySlice, PlaneSlice);
				}

				SubresourceBits[SubresourceIndex] = true;
			});
		}
	}

	for (const auto Pair : ResourceMap.Buffers)
	{
		FRDGBufferRef Buffer = Pair.Key;
		const FRHITransitionInfo& Transition = Pair.Value;

		if (!IsDebugAllowedForResource(Buffer->Name))
		{
			continue;
		}

		LogHeader();

		UE_LOG(LogRDG, Display, TEXT("\t%s: %s -> %s"),
			Buffer->Name,
			*GetResourceTransitionAccessName(Transition.AccessBefore),
			*GetResourceTransitionAccessName(Transition.AccessAfter));
	}
}

void FRDGBarrierValidation::ValidateBarrierBatchEnd(const FRDGBarrierBatchEnd& Batch)
{
	if (!GRDGTransitionLog || !IsDebugAllowedForGraph(GraphName) || !IsDebugAllowedForPass(Batch.Pass->GetName()))
	{
		return;
	}

	bool bFoundFirstBatch = false;

	for (const FRDGBarrierBatchBegin* Dependent : Batch.Dependencies)
	{
		// Transitions can be queued into multiple end batches. The first to get flushed nulls out the transition.
		if (!Dependent->IsTransitionValid())
		{
			continue;
		}

		const FResourceMap& ResourceMap = BatchMap.FindChecked(Dependent);

		TArray<FRDGTextureRef> Textures;
		if (ResourceMap.Textures.Num())
		{
			ResourceMap.Textures.GetKeys(Textures);
		}

		TArray<FRDGBufferRef> Buffers;
		if (ResourceMap.Buffers.Num())
		{
			ResourceMap.Buffers.GetKeys(Buffers);
		}

		if (Textures.Num() || Buffers.Num())
		{
			if (!bFoundFirstBatch)
			{
				UE_LOG(LogRDG, Display, TEXT("%s (End):"), *Batch.GetName());
				bFoundFirstBatch = true;
			}
		}

		for (FRDGTextureRef Texture : Textures)
		{
			UE_LOG(LogRDG, Display, TEXT("\t%s"), Texture->Name);
		}

		for (FRDGBufferRef Buffer : Buffers)
		{
			UE_LOG(LogRDG, Display, TEXT("\t%s"), Buffer->Name);
		}
	}
}

void FRDGBarrierValidation::ValidateExecuteEnd()
{
	for (auto Pair : BatchMap)
	{
		checkf(!Pair.Key->IsTransitionValid(), TEXT("A batch was begun but never ended."));
	}
	BatchMap.Empty();
}

namespace
{
	const TCHAR* RasterColorName = TEXT("#ff7070");
	const TCHAR* ComputeColorName = TEXT("#70b8ff");
	const TCHAR* AsyncComputeColorName = TEXT("#70ff99");
	const TCHAR* CopyColorName = TEXT("#ffdb70");
	const TCHAR* TextureColorAttributes = TEXT("color=\"#5800a1\", fontcolor=\"#5800a1\"");
	const TCHAR* BufferColorAttributes = TEXT("color=\"#007309\", fontcolor=\"#007309\"");

	const TCHAR* GetPassColorName(ERDGPassFlags Flags)
	{
		if (EnumHasAnyFlags(Flags, ERDGPassFlags::Raster))
		{
			return RasterColorName;
		}
		if (EnumHasAnyFlags(Flags, ERDGPassFlags::Compute))
		{
			return ComputeColorName;
		}
		if (EnumHasAnyFlags(Flags, ERDGPassFlags::AsyncCompute))
		{
			return AsyncComputeColorName;
		}
		if (EnumHasAnyFlags(Flags, ERDGPassFlags::Copy))
		{
			return CopyColorName;
		}
		checkNoEntry();
		return nullptr;
	}

	FString GetSubresourceStateLabel(FRDGSubresourceState State)
	{
		check(State.Pipeline == ERDGPipeline::Graphics || State.Pipeline == ERDGPipeline::AsyncCompute);
		const TCHAR* FontColor = State.Pipeline == ERDGPipeline::AsyncCompute ? AsyncComputeColorName : RasterColorName;
		return FString::Printf(TEXT("<font color=\"%s\">%s</font>"), FontColor, *GetResourceTransitionAccessName(State.Access));
	}
}

FString FRDGLogFile::GetProducerName(FRDGPassHandle PassHandle)
{
	if (PassHandle.IsValid())
	{
		return GetNodeName(PassHandle);
	}
	else
	{
		return GetNodeName(ProloguePassHandle);
	}
}

FString FRDGLogFile::GetConsumerName(FRDGPassHandle PassHandle)
{
	if (PassHandle.IsValid())
	{
		return GetNodeName(PassHandle);
	}
	else
	{
		return GetNodeName(EpiloguePassHandle);
	}
}

FString FRDGLogFile::GetNodeName(FRDGPassHandle PassHandle)
{
	PassesReferenced.Add(PassHandle);
	return FString::Printf(TEXT("P%d"), PassHandle.GetIndex());
}

FString FRDGLogFile::GetNodeName(const FRDGTexture* Texture)
{
	return FString::Printf(TEXT("T%d"), Textures.AddUnique(Texture));
}

FString FRDGLogFile::GetNodeName(const FRDGBuffer* Buffer)
{
	return FString::Printf(TEXT("B%d"), Buffers.AddUnique(Buffer));
}

void FRDGLogFile::AddLine(const FString& Line)
{
	File += Indentation + Line + TEXT("\n");
}

void FRDGLogFile::AddBraceBegin()
{
	AddLine(TEXT("{"));
	Indentation += TEXT("\t");
}

void FRDGLogFile::AddBraceEnd()
{
	const bool bSuccess = Indentation.RemoveFromEnd(TEXT("\t"));
	check(bSuccess);

	AddLine(TEXT("}"));
}

void FRDGLogFile::Begin(
	const FRDGEventName& InGraphName,
	const FRDGPassRegistry* InPasses,
	FRDGPassHandle InProloguePassHandle,
	FRDGPassHandle InEpiloguePassHandle)
{
	if (GRDGDumpGraph)
	{
		if (GRDGImmediateMode)
		{
			UE_LOG(LogRDG, Warning, TEXT("Dump graph (%d) requested, but immediate mode is enabled. Skipping."), GRDGDumpGraph);
			return;
		}

		check(File.IsEmpty());
		check(InPasses && InEpiloguePassHandle.IsValid());

		Passes = InPasses;
		ProloguePassHandle = InProloguePassHandle;
		EpiloguePassHandle = InEpiloguePassHandle;
		GraphName = InGraphName.GetTCHAR();

		if (GraphName.IsEmpty())
		{
			const int32 UnknownGraphIndex = GRDGDumpGraphUnknownCount++;
			GraphName = FString::Printf(TEXT("Unknown%d"), UnknownGraphIndex);
		}

		AddLine(TEXT("digraph RDG"));
		AddBraceBegin();
		AddLine(TEXT("rankdir=LR; labelloc=\"t\""));

		bOpen = true;
	}
}

void FRDGLogFile::End()
{
	if (!GRDGDumpGraph || !bOpen)
	{
		return;
	}

	TSet<FRDGPassHandle> PassesCulled;
	TArray<FRDGPassHandle> PassesGraphics;
	TArray<FRDGPassHandle> PassesAsyncCompute;

	for (FRDGPassHandle PassHandle = Passes->Begin(); PassHandle != Passes->End(); ++PassHandle)
	{
		PassesCulled.Add(PassHandle);
	}

	for (FRDGPassHandle PassHandle = Passes->Begin(); PassHandle != Passes->End(); ++PassHandle)
	{
		const FRDGPass* Pass = Passes->Get(PassHandle);
		const ERDGPipeline Pipeline = Pass->GetPipeline();

		switch (Pipeline)
		{
		case ERDGPipeline::Graphics:
			PassesGraphics.Add(PassHandle);
			break;
		case ERDGPipeline::AsyncCompute:
			PassesAsyncCompute.Add(PassHandle);
			break;
		default:
			checkNoEntry();
		}

		PassesCulled.Remove(PassHandle);
	}

	if (GRDGDumpGraph == RDG_DUMP_GRAPH_TRACKS)
	{
		FRDGPassHandle PrevPassesByPipeline[uint32(ERDGPipeline::MAX)];

		for (FRDGPassHandle PassHandle = Passes->Begin(); PassHandle != Passes->End(); ++PassHandle)
		{
			const FRDGPass* Pass = Passes->Get(PassHandle);

			FRDGPassHandle& PrevPassInPipelineHandle = PrevPassesByPipeline[uint32(Pass->GetPipeline())];

			if (PrevPassInPipelineHandle.IsValid())
			{
				AddLine(FString::Printf(TEXT("\"%s\" -> \"%s\" [style=\"filled\", penwidth=2, color=\"%s\"]"),
					*GetNodeName(PrevPassInPipelineHandle), *GetNodeName(PassHandle), GetPassColorName(Pass->GetFlags())));
			}

			if (Pass->GetPipeline() == ERDGPipeline::AsyncCompute)
			{
				const auto AddCrossPipelineEdge = [&](FRDGPassHandle PassBefore, FRDGPassHandle PassAfter)
				{
					AddLine(FString::Printf(TEXT("\"%s\" -> \"%s\" [penwidth=5, style=\"dashed\" color=\"#f003fc\"]"),
						*GetNodeName(PassBefore), *GetNodeName(PassAfter)));
				};

				if (Pass->IsAsyncComputeBegin())
				{
					AddCrossPipelineEdge(Pass->GetGraphicsForkPass(), PassHandle);
				}

				if (Pass->IsAsyncComputeEnd())
				{
					AddCrossPipelineEdge(PassHandle, Pass->GetGraphicsJoinPass());
				}
			}

			PrevPassInPipelineHandle = PassHandle;
		}
	}
	else
	{
		for (FRDGPassHandle PassHandle = Passes->Begin(); PassHandle != Passes->End(); ++PassHandle)
		{
			if (PassHandle == EpiloguePassHandle)
			{
				break;
			}

			const FRDGPass* Pass = Passes->Get(PassHandle);

			for (const FRDGPassHandle ProducerHandle : Pass->GetProducers())
			{
				if (ProducerHandle != ProloguePassHandle)
				{
					const FRDGPass* Producer = Passes->Get(ProducerHandle);

					File += FString::Printf(TEXT("\t\"%s\" -> \"%s\" [penwidth=2, color=\"%s:%s\"]\n"),
						*GetNodeName(ProducerHandle), *GetNodeName(PassHandle), GetPassColorName(Pass->GetFlags()), GetPassColorName(Producer->GetFlags()));
				}
			}
		}
	}

	AddLine(TEXT("subgraph Passes"));
	AddBraceBegin();

	const auto AddPass = [&](FRDGPassHandle PassHandle)
	{
		if (!PassesReferenced.Contains(PassHandle))
		{
			return;
		}

		const FRDGPass* Pass = Passes->Get(PassHandle);
		const TCHAR* Style = PassesCulled.Contains(PassHandle) ? TEXT("dashed") : TEXT("filled");
		FString PassName = FString::Printf(TEXT("[%d]: %s"), PassHandle.GetIndex(), Pass->GetName());

		if (Pass->GetParameters().HasExternalOutputs())
		{
			PassName += TEXT("\n(Has External UAVs)");
		}

		AddLine(FString::Printf(TEXT("\"%s\" [shape=box, style=%s, label=\"%s\", color=\"%s\"]"), *GetNodeName(PassHandle), Style, *PassName, GetPassColorName(Pass->GetFlags())));
	};

	{
		uint32 RenderTargetClusterCount = 0;

		for (FRDGPassHandle PassHandle : PassesGraphics)
		{
			const FRDGPass* Pass = Passes->Get(PassHandle);

			if (Pass->IsMergedRenderPassBegin())
			{
				const uint32 RenderTargetClusterIndex = RenderTargetClusterCount++;

				AddLine(FString::Printf(TEXT("subgraph cluster_%d"), RenderTargetClusterIndex));
				AddBraceBegin();
				AddLine(TEXT("style=filled;color=\"#ffe0e0\";fontcolor=\"#aa0000\";label=\"Render Pass Merge\";fontsize=10"));
			}

			AddPass(PassHandle);

			if (Pass->IsMergedRenderPassEnd())
			{
				AddBraceEnd();
			}
		}
	}

	for (FRDGPassHandle PassHandle : PassesAsyncCompute)
	{
		AddPass(PassHandle);
	}

	AddBraceEnd();

	AddLine(TEXT("subgraph Textures"));
	AddBraceBegin();
	for (const FRDGTexture* Texture : Textures)
	{
		FString Line = FString::Printf(TEXT("\"%s\" [shape=oval, %s, label=\"%s"), *GetNodeName(Texture), TextureColorAttributes, Texture->Name);
		if (Texture->bIsExternal)
		{
			Line += TEXT("\n(External)");
		}
		Line += TEXT("\"]");
		AddLine(Line);
	}
	AddBraceEnd();

	AddLine(TEXT("subgraph Buffers"));
	AddBraceBegin();
	for (const FRDGBuffer* Buffer : Buffers)
	{
		FString Line = FString::Printf(TEXT("\"%s\" [shape=oval, %s, label=\"%s"), *GetNodeName(Buffer), BufferColorAttributes, Buffer->Name);
		if (Buffer->bIsExternal)
		{
			Line += TEXT("\n(External)");
		}
		Line += TEXT("\"]");
		AddLine(Line);
	}
	AddBraceEnd();

	AddLine(FString::Printf(TEXT("label=\"%s [Active Passes: %d, Culled Passes: %d, Textures: %d, Buffers: %d]\""), *GraphName, Passes->Num() - PassesCulled.Num(), PassesCulled.Num(), Textures.Num(), Buffers.Num()));

	AddBraceEnd();
	check(Indentation.IsEmpty());

	const TCHAR* DumpType = TEXT("");

	switch (GRDGDumpGraph)
	{
	case RDG_DUMP_GRAPH_VERBOSITY_HIGH:
		DumpType = TEXT("");
		break;
	case RDG_DUMP_GRAPH_VERBOSITY_LOW:
		DumpType = TEXT("_simple");
		break;
	case RDG_DUMP_GRAPH_TRACKS:
		DumpType = TEXT("_tracks");
		break;
	}

	FFileHelper::SaveStringToFile(File, *(FPaths::ProjectLogDir() / FString::Printf(TEXT("RDG_%s%s.gv"), *GraphName, DumpType)));

	bOpen = false;
}

void FRDGLogFile::AddFirstEdge(const FRDGTextureRef Texture, FRDGPassHandle FirstPass)
{
	if (GRDGDumpGraph == RDG_DUMP_GRAPH_VERBOSITY_HIGH && bOpen)
	{
		AddLine(FString::Printf(TEXT("\"%s\" -> \"%s\" [%s]"),
			*GetNodeName(Texture),
			*GetNodeName(FirstPass),
			TextureColorAttributes));
	}
}

void FRDGLogFile::AddFirstEdge(const FRDGBufferRef Buffer, FRDGPassHandle FirstPass)
{
	if (GRDGDumpGraph == RDG_DUMP_GRAPH_VERBOSITY_HIGH && bOpen)
	{
		AddLine(FString::Printf(TEXT("\"%s\" -> \"%s\" [%s]"),
			*GetNodeName(Buffer),
			*GetNodeName(FirstPass),
			BufferColorAttributes));
	}
}

void FRDGLogFile::AddTransitionEdge(FRDGSubresourceState StateBefore, FRDGSubresourceState StateAfter, const FRDGTextureRef Texture)
{
	if (GRDGDumpGraph == RDG_DUMP_GRAPH_VERBOSITY_HIGH && bOpen)
	{
		if (FRDGSubresourceState::IsTransitionRequired(StateBefore, StateAfter))
		{
			AddLine(FString::Printf(TEXT("\"%s\" -> \"%s\" [%s, label=<%s: <b>%s -&gt; %s</b>>]"),
				*GetProducerName(StateBefore.PassHandle),
				*GetConsumerName(StateAfter.PassHandle),
				TextureColorAttributes,
				Texture->Name,
				*GetSubresourceStateLabel(StateBefore),
				*GetSubresourceStateLabel(StateAfter)));
		}
		else
		{
			AddLine(FString::Printf(TEXT("\"%s\" -> \"%s\" [%s, label=<%s: <b>%s</b>>]"),
				*GetProducerName(StateBefore.PassHandle),
				*GetConsumerName(StateAfter.PassHandle),
				TextureColorAttributes,
				Texture->Name,
				*GetSubresourceStateLabel(StateBefore)));
		}
	}
}

void FRDGLogFile::AddTransitionEdge(FRDGSubresourceState StateBefore, FRDGSubresourceState StateAfter, const FRDGTextureRef Texture, uint32 MipIndex, uint32 ArraySlice, uint32 PlaneSlice)
{
	if (GRDGDumpGraph == RDG_DUMP_GRAPH_VERBOSITY_HIGH && bOpen)
	{
		if (FRDGSubresourceState::IsTransitionRequired(StateBefore, StateAfter))
		{
			AddLine(FString::Printf(TEXT("\"%s\" -> \"%s\" [%s, label=<%s[%d][%d][%d]: <b>%s -&gt; %s</b>>]"),
				*GetProducerName(StateBefore.PassHandle),
				*GetConsumerName(StateAfter.PassHandle),
				TextureColorAttributes,
				Texture->Name,
				MipIndex, ArraySlice, PlaneSlice,
				*GetSubresourceStateLabel(StateBefore),
				*GetSubresourceStateLabel(StateAfter)));
		}
		else
		{
			AddLine(FString::Printf(TEXT("\"%s\" -> \"%s\" [%s, label=<%s[%d][%d][%d]: <b>%s</b>>]"),
				*GetProducerName(StateBefore.PassHandle),
				*GetConsumerName(StateAfter.PassHandle),
				TextureColorAttributes,
				Texture->Name,
				MipIndex, ArraySlice, PlaneSlice,
				*GetSubresourceStateLabel(StateBefore)));
		}
	}
}

void FRDGLogFile::AddTransitionEdge(FRDGSubresourceState StateBefore, FRDGSubresourceState StateAfter, const FRDGBufferRef Buffer)
{
	if (GRDGDumpGraph == RDG_DUMP_GRAPH_VERBOSITY_HIGH && bOpen)
	{
		if (FRDGSubresourceState::IsTransitionRequired(StateBefore, StateAfter))
		{
			AddLine(FString::Printf(TEXT("\"%s\" -> \"%s\" [%s, label=<%s: <b>%s -&gt; %s</b>>]"),
				*GetProducerName(StateBefore.PassHandle),
				*GetConsumerName(StateAfter.PassHandle),
				BufferColorAttributes,
				Buffer->Name,
				*GetSubresourceStateLabel(StateBefore),
				*GetSubresourceStateLabel(StateAfter)));
		}
		else
		{
			AddLine(FString::Printf(TEXT("\"%s\" -> \"%s\" [%s, label=<%s: <b>%s</b>>]"),
				*GetProducerName(StateBefore.PassHandle),
				*GetConsumerName(StateAfter.PassHandle),
				BufferColorAttributes,
				Buffer->Name,
				*GetSubresourceStateLabel(StateBefore)));
		}
	}
}

#endif