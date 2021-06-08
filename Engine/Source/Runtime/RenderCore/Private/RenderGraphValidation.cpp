// Copyright Epic Games, Inc. All Rights Reserved.

#include "RenderGraphValidation.h"
#include "RenderGraphPrivate.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "MultiGPU.h"

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
				Function(FRDGTextureSubresource(MipIndex, ArraySlice, PlaneSlice));
			}
		}
	}
}

const ERHIAccess AccessMaskCopy    = ERHIAccess::CopySrc | ERHIAccess::CopyDest | ERHIAccess::CPURead;
const ERHIAccess AccessMaskCompute = ERHIAccess::SRVCompute | ERHIAccess::UAVCompute | ERHIAccess::IndirectArgs;
const ERHIAccess AccessMaskRaster  = ERHIAccess::ResolveSrc | ERHIAccess::ResolveDst | ERHIAccess::DSVRead | ERHIAccess::DSVWrite | ERHIAccess::RTV | ERHIAccess::SRVGraphics | ERHIAccess::UAVGraphics | ERHIAccess::Present | ERHIAccess::IndirectArgs | ERHIAccess::VertexOrIndexBuffer;

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
	const bool bClobber = !Resource->ParentDebugData.bHasBeenClobbered && !Resource->bExternal && IsDebugAllowedForResource(Resource->Name);

	if (bClobber)
	{
		Resource->ParentDebugData.bHasBeenClobbered = true;
	}

	return bClobber;
}

void FRDGUserValidation::ValidateAddPass(const FRDGPass* Pass, bool bSkipPassAccessMarking)
{
	const FRenderTargetBindingSlots* RenderTargetBindingSlots = nullptr;

	// Pass flags are validated as early as possible by the builder in AddPass.
	const ERDGPassFlags PassFlags = Pass->GetFlags();
	const FRDGParameterStruct PassParameters = Pass->GetParameters();


	const TCHAR* PassName = Pass->GetName();
	const bool bIsRaster = EnumHasAnyFlags(PassFlags, ERDGPassFlags::Raster);
	const bool bIsCopy = EnumHasAnyFlags(PassFlags, ERDGPassFlags::Copy);
	const bool bIsAnyCompute = EnumHasAnyFlags(PassFlags, ERDGPassFlags::Compute | ERDGPassFlags::AsyncCompute);
	const bool bSkipRenderPass = EnumHasAnyFlags(PassFlags, ERDGPassFlags::SkipRenderPass);

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

	const auto CheckNotPassthrough = [&](FRDGResourceRef Resource)
	{
		checkf(!Resource->IsPassthrough(), TEXT("Resource '%s' was created as a passthrough resource but is attached to pass '%s'."), Resource->Name, Pass->GetName());
	};

	const auto CheckNotCopy = [&](FRDGResourceRef Resource)
	{
		ensureMsgf(!bIsCopy, TEXT("Pass %s, parameter %s is valid for Raster or (Async)Compute, but the pass is a Copy pass."), PassName, Resource->Name);
	};

	bool bCanProduce = false;

	PassParameters.Enumerate([&](FRDGParameter Parameter)
	{
		if (Parameter.IsParentResource())
		{
			if (FRDGParentResourceRef Resource = Parameter.GetAsParentResource())
			{
				CheckNotPassthrough(Resource);
			}
		}
		else if (Parameter.IsView())
		{
			if (FRDGViewRef View = Parameter.GetAsView())
			{
				CheckNotPassthrough(View);
				CheckNotPassthrough(View->GetParent());
			}
		}

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
			bCanProduce = true;
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
			bCanProduce = true;
			if (FRDGBufferUAVRef UAV = Parameter.GetAsBufferUAV())
			{
				FRDGBufferRef Buffer = UAV->GetParent();
				CheckNotCopy(Buffer);
				MarkAsProduced(Buffer);
			}
		}
		break;
		case UBMT_RDG_TEXTURE_ACCESS:
		{
			const FRDGTextureAccess TextureAccess = Parameter.GetAsTextureAccess();
			const ERHIAccess Access = TextureAccess.GetAccess();
			const bool bIsWriteAccess = IsWritableAccess(Access);
			bCanProduce |= bIsWriteAccess;

			if (FRDGTextureRef Texture = TextureAccess.GetTexture())
			{
				if (!IsLegacyAccess(Access))
				{
					checkf(bIsCopy       || !EnumHasAnyFlags(Access, AccessMaskCopy),    TEXT("Pass '%s' uses texture '%s' with access '%s' containing states which require the 'ERDGPass::Copy' flag."), Pass->GetName(), Texture->Name, *GetRHIAccessName(Access));
					checkf(bIsAnyCompute || !EnumHasAnyFlags(Access, AccessMaskCompute), TEXT("Pass '%s' uses texture '%s' with access '%s' containing states which require the 'ERDGPass::Compute' or 'ERDGPassFlags::AsyncCompute' flag."), Pass->GetName(), Texture->Name, *GetRHIAccessName(Access));
					checkf(bIsRaster     || !EnumHasAnyFlags(Access, AccessMaskRaster),  TEXT("Pass '%s' uses texture '%s' with access '%s' containing states which require the 'ERDGPass::Raster' flag."), Pass->GetName(), Texture->Name, *GetRHIAccessName(Access));
				}

				if (bIsWriteAccess)
				{
					MarkAsProduced(Texture);
				}
			}
		}
		break;
		case UBMT_RDG_BUFFER_ACCESS:
		{
			const FRDGBufferAccess BufferAccess = Parameter.GetAsBufferAccess();
			const ERHIAccess Access = BufferAccess.GetAccess();
			const bool bIsWriteAccess = IsWritableAccess(Access);
			bCanProduce |= bIsWriteAccess;

			if (FRDGBufferRef Buffer = BufferAccess.GetBuffer())
			{
				if (!IsLegacyAccess(Access))
				{
					checkf(bIsCopy       || !EnumHasAnyFlags(Access, AccessMaskCopy),    TEXT("Pass '%s' uses buffer '%s' with access '%s' containing states which require the 'ERDGPass::Copy' flag."), Pass->GetName(), Buffer->Name, *GetRHIAccessName(Access));
					checkf(bIsAnyCompute || !EnumHasAnyFlags(Access, AccessMaskCompute), TEXT("Pass '%s' uses buffer '%s' with access '%s' containing states which require the 'ERDGPass::Compute' or 'ERDGPassFlags::AsyncCompute' flag."), Pass->GetName(), Buffer->Name, *GetRHIAccessName(Access));
					checkf(bIsRaster     || !EnumHasAnyFlags(Access, AccessMaskRaster),  TEXT("Pass '%s' uses buffer '%s' with access '%s' containing states which require the 'ERDGPass::Raster' flag."), Pass->GetName(), Buffer->Name, *GetRHIAccessName(Access));
				}

				if (IsWritableAccess(BufferAccess.GetAccess()))
				{
					MarkAsProduced(Buffer);
				}
			}
		}
		break;
		case UBMT_RENDER_TARGET_BINDING_SLOTS:
		{
			RenderTargetBindingSlots = &Parameter.GetAsRenderTargetBindingSlots();
			bCanProduce = true;
		}
		break;
		}
	});

	checkf(bCanProduce || EnumHasAnyFlags(PassFlags, ERDGPassFlags::NeverCull) || PassParameters.HasExternalOutputs(),
		TEXT("Pass '%s' has no graph parameters defined on its parameter struct and did not specify 'NeverCull'. The pass will always be culled."), PassName);

	/** Validate that raster passes have render target binding slots and compute passes don't. */
	if (RenderTargetBindingSlots)
	{
		checkf(bIsRaster, TEXT("Pass '%s' has render target binding slots but is not set to 'Raster'."), PassName);
	}
	else
	{
		checkf(!bIsRaster || bSkipRenderPass, TEXT("Pass '%s' is set to 'Raster' but is missing render target binding slots. Use 'SkipRenderPass' if this is desired."), PassName);
	}

	/** Validate render target / depth stencil binding usage. */
	if (RenderTargetBindingSlots)
	{
		const auto& RenderTargets = RenderTargetBindingSlots->Output;

		{
			if (FRDGTextureRef Texture = RenderTargetBindingSlots->ShadingRateTexture)
			{
				MarkAsConsumed(Texture);
			}

			const auto& DepthStencil = RenderTargetBindingSlots->DepthStencil;

			const auto CheckDepthStencil = [&](FRDGTextureRef Texture)
			{
				// Depth stencil only supports one mip, since there isn't actually a way to select the mip level.
				check(Texture->Desc.NumMips == 1);
				CheckNotPassthrough(Texture);
				if (DepthStencil.GetDepthStencilAccess().IsAnyWrite())
				{
					MarkAsProduced(Texture);
				}
				else
				{
					MarkAsConsumed(Texture);
				}
			};

			FRDGTextureRef Texture = DepthStencil.GetTexture();

			if (Texture)
			{
				checkf(
					EnumHasAnyFlags(Texture->Desc.Flags, TexCreate_DepthStencilTargetable | TexCreate_DepthStencilResolveTarget),
					TEXT("Pass '%s' attempted to bind texture '%s' as a depth stencil render target, but the texture has not been created with TexCreate_DepthStencilTargetable."),
					PassName, Texture->Name);

				CheckDepthStencil(Texture);
			}
		}

		const uint32 RenderTargetCount = RenderTargets.Num();

		{
			/** Tracks the number of contiguous, non-null textures in the render target output array. */
			uint32 ValidRenderTargetCount = RenderTargetCount;

			for (uint32 RenderTargetIndex = 0; RenderTargetIndex < RenderTargetCount; ++RenderTargetIndex)
			{
				const FRenderTargetBinding& RenderTarget = RenderTargets[RenderTargetIndex];

				FRDGTextureRef Texture = RenderTarget.GetTexture();
				FRDGTextureRef ResolveTexture = RenderTarget.GetResolveTexture();

				if (ResolveTexture && ResolveTexture != Texture)
				{
					checkf(RenderTarget.GetTexture(), TEXT("Pass %s specified resolve target '%s' with a null render target."), PassName, ResolveTexture->Name);

					ensureMsgf(
						EnumHasAnyFlags(ResolveTexture->Desc.Flags, TexCreate_ResolveTargetable),
						TEXT("Pass '%s' attempted to bind texture '%s' as a render target, but the texture has not been created with TexCreate_ResolveTargetable."),
						PassName, ResolveTexture->Name);

					CheckNotPassthrough(ResolveTexture);
					MarkAsProduced(ResolveTexture);
				}

				if (Texture)
				{
					ensureMsgf(
						EnumHasAnyFlags(Texture->Desc.Flags, TexCreate_RenderTargetable | TexCreate_ResolveTargetable),
						TEXT("Pass '%s' attempted to bind texture '%s' as a render target, but the texture has not been created with TexCreate_RenderTargetable."),
						PassName, Texture->Name);

					CheckNotPassthrough(Texture);
					const bool bIsLoadAction = RenderTarget.GetLoadAction() == ERenderTargetLoadAction::ELoad;

					/** Validate that load action is correct. We can only load contents if a pass previously produced something. */
					{
						const bool bIsLoadActionInvalid = bIsLoadAction && !Texture->ParentDebugData.bHasBeenProduced;
						checkf(
							!bIsLoadActionInvalid,
							TEXT("Pass '%s' attempted to bind texture '%s' as a render target with the 'Load' action specified, but the texture has not been produced yet. The render target must use either 'Clear' or 'NoAction' action instead."),
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
				checkf(RenderTarget.GetTexture() == nullptr && RenderTarget.GetResolveTexture() == nullptr, TEXT("Render targets must be packed. No empty spaces in the array."));
			}
		}
	}
}

void FRDGUserValidation::ValidateExecuteBegin()
{
	checkf(!bHasExecuted, TEXT("Render graph execution should only happen once to ensure consistency with immediate mode."));
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

			const bool bHasBeenProducedByGraph = !Texture->bExternal && Texture->ParentDebugData.PassAccessCount > 0;

			if (bHasBeenProducedByGraph && !Texture->TextureDebugData.bHasNeededUAV && EnumHasAnyFlags(Texture->Desc.Flags, TexCreate_UAV))
			{
				EmitRDGWarningf(
					TEXT("Resource %s first produced by the pass %s had the TexCreate_UAV flag, but no UAV has been used."),
					Texture->Name, Texture->ParentDebugData.FirstProducer->GetName());
			}

			if (bHasBeenProducedByGraph && !Texture->TextureDebugData.bHasBeenBoundAsRenderTarget && EnumHasAnyFlags(Texture->Desc.Flags, TexCreate_RenderTargetable))
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
		Pass->GetParameters().EnumerateUniformBuffers([&](FRDGUniformBufferRef UniformBuffer)
		{
			// Global uniform buffers are always marked as used, because FShader traversal doesn't know about them.
			if (UniformBuffer->IsGlobal())
			{
				UniformBuffer->MarkResourceAsUsed();
			}
		});

		Pass->GetParameters().Enumerate([&](FRDGParameter Parameter)
		{
			switch (Parameter.GetType())
			{
			case UBMT_RDG_TEXTURE_UAV:
				if (FRDGTextureUAVRef UAV = Parameter.GetAsTextureUAV())
				{
					FRDGTextureRef Texture = UAV->Desc.Texture;
					Texture->TextureDebugData.bHasNeededUAV = true;
				}
			break;
			case UBMT_RDG_TEXTURE_ACCESS:
			{
				const FRDGTextureAccess TextureAccess = Parameter.GetAsTextureAccess();
				if (FRDGTextureRef Texture = TextureAccess.GetTexture())
				{
					const ERHIAccess Access = TextureAccess.GetAccess();
					if (EnumHasAnyFlags(Access, ERHIAccess::UAVMask))
					{
						Texture->TextureDebugData.bHasNeededUAV = true;
					}
					if (EnumHasAnyFlags(Access, ERHIAccess::RTV | ERHIAccess::DSVRead | ERHIAccess::DSVWrite))
					{
						Texture->TextureDebugData.bHasBeenBoundAsRenderTarget = true;
					}
					Texture->MarkResourceAsUsed();
				}
			}
			break;
			case UBMT_RDG_BUFFER_ACCESS:
				if (FRDGBufferRef Buffer = Parameter.GetAsBuffer())
				{
					Buffer->MarkResourceAsUsed();
				}
			break;
			case UBMT_RENDER_TARGET_BINDING_SLOTS:
			{
				const FRenderTargetBindingSlots& RenderTargets = Parameter.GetAsRenderTargetBindingSlots();

				RenderTargets.Enumerate([&](FRenderTargetBinding RenderTarget)
				{
					FRDGTextureRef Texture = RenderTarget.GetTexture();
					Texture->TextureDebugData.bHasBeenBoundAsRenderTarget = true;
					Texture->MarkResourceAsUsed();
				});

				if (FRDGTextureRef Texture = RenderTargets.DepthStencil.GetTexture())
				{
					Texture->TextureDebugData.bHasBeenBoundAsRenderTarget = true;
					Texture->MarkResourceAsUsed();
				}

				if (FRDGTextureRef Texture = RenderTargets.ShadingRateTexture)
				{
					Texture->MarkResourceAsUsed();
				}
			}
			break;
			}
		});
	}
}

void FRDGUserValidation::ValidateExecutePassEnd(const FRDGPass* Pass)
{
	SetAllowRHIAccess(Pass, false);

	const FRDGParameterStruct PassParameters = Pass->GetParameters();

	if (GRDGDebug)
	{
		uint32 TrackedResourceCount = 0;
		uint32 UsedResourceCount = 0;

		PassParameters.Enumerate([&](FRDGParameter Parameter)
		{
			if (Parameter.IsResource())
			{
				if (FRDGResourceRef Resource = Parameter.GetAsResource())
				{
					TrackedResourceCount++;
					UsedResourceCount += Resource->DebugData.bIsActuallyUsedByPass ? 1 : 0;
				}
			}
		});

		if (TrackedResourceCount != UsedResourceCount)
		{
			FString WarningMessage = FString::Printf(
				TEXT("'%d' of the '%d' resources of the pass '%s' were not actually used."),
				TrackedResourceCount - UsedResourceCount, TrackedResourceCount, Pass->GetName());

			PassParameters.Enumerate([&](FRDGParameter Parameter)
			{
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
			});

			EmitRDGWarning(WarningMessage);
		}
	}

	PassParameters.Enumerate([&](FRDGParameter Parameter)
	{
		if (Parameter.IsResource())
		{
			if (FRDGResourceRef Resource = Parameter.GetAsResource())
			{
				Resource->DebugData.bIsActuallyUsedByPass = false;
			}
		}
	});

	GRDGInExecutePassScope = false;
}

void FRDGUserValidation::SetAllowRHIAccess(const FRDGPass* Pass, bool bAllowAccess)
{
	Pass->GetParameters().Enumerate([&](FRDGParameter Parameter)
	{
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

				if (FRDGTextureRef ResolveTexture = RenderTarget.GetResolveTexture())
				{
					ResolveTexture->DebugData.bAllowRHIAccess = bAllowAccess;
				}
			});

			if (FRDGTextureRef Texture = RenderTargets.DepthStencil.GetTexture())
			{
				Texture->DebugData.bAllowRHIAccess = bAllowAccess;
			}

			if (FRDGTexture* Texture = RenderTargets.ShadingRateTexture)
			{
				Texture->DebugData.bAllowRHIAccess = bAllowAccess;
			}
		}
	});
}

FRDGBarrierValidation::FRDGBarrierValidation(const FRDGPassRegistry* InPasses, const FRDGEventName& InGraphName)
	: Passes(InPasses)
	, GraphName(InGraphName.GetTCHAR())
{
	check(Passes);
}

void FRDGBarrierValidation::ValidateBarrierBatchBegin(const FRDGPass* Pass, const FRDGBarrierBatchBegin& Batch)
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

	const bool bAllowedForPass = IsDebugAllowedForGraph(GraphName) && IsDebugAllowedForPass(Pass->GetName());

	// Debug mode will report errors regardless of logging filter.
	if (!bAllowedForPass && !GRDGDebug)
	{
		return;
	}

	bool bFoundFirst = false;

	const auto LogHeader = [&]()
	{
		if (!bFoundFirst)
		{
			bFoundFirst = true;
			UE_CLOG(bAllowedForPass, LogRDG, Display, TEXT("%s (Begin):"), *Batch.GetName());
		}
	};

	for (const auto& Pair : ResourceMap.Textures)
	{
		FRDGTextureRef Texture = Pair.Key;
		
		const bool bAllowedForResource = bAllowedForPass && IsDebugAllowedForResource(Texture->Name);

		const auto& Transitions = Pair.Value;
		if (bAllowedForResource && Transitions.Num())
		{
			LogHeader();
			UE_LOG(LogRDG, Display, TEXT("\t(%p) %s:"), Texture, Texture->Name);
		}

		const FRDGTextureSubresourceLayout SubresourceLayout = Texture->GetSubresourceLayout();

		for (const FRHITransitionInfo& Transition : Transitions)
		{
			check(SubresourceLayout.GetSubresourceCount() > 0);

			EnumerateSubresources(Transition, SubresourceLayout.NumMips, SubresourceLayout.NumArraySlices, SubresourceLayout.NumPlaneSlices,
				[&](FRDGTextureSubresource Subresource)
			{
				const int32 SubresourceIndex = SubresourceLayout.GetSubresourceIndex(Subresource);

				UE_CLOG(bAllowedForResource, LogRDG, Display, TEXT("\t\tMip(%d), Array(%d), Slice(%d): %s -> %s"),
					Subresource.MipIndex, Subresource.ArraySlice, Subresource.PlaneSlice,
					*GetRHIAccessName(Transition.AccessBefore),
					*GetRHIAccessName(Transition.AccessAfter));
			});
		}
	}

	if (bAllowedForPass)
	{
		for (auto Pair : ResourceMap.Buffers)
		{
			FRDGBufferRef Buffer = Pair.Key;
			const FRHITransitionInfo& Transition = Pair.Value;

			if (!IsDebugAllowedForResource(Buffer->Name))
			{
				continue;
			}

			LogHeader();

			UE_LOG(LogRDG, Display, TEXT("\t(%p) %s: %s -> %s"),
				Buffer,
				Buffer->Name,
				*GetRHIAccessName(Transition.AccessBefore),
				*GetRHIAccessName(Transition.AccessAfter));
		}
	}
}

void FRDGBarrierValidation::ValidateBarrierBatchEnd(const FRDGPass* Pass, const FRDGBarrierBatchEnd& Batch)
{
	if (!GRDGTransitionLog || !IsDebugAllowedForGraph(GraphName) || !IsDebugAllowedForPass(Pass->GetName()))
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
			UE_LOG(LogRDG, Display, TEXT("\t(%p) %s"), Texture, Texture->Name);
		}

		for (FRDGBufferRef Buffer : Buffers)
		{
			UE_LOG(LogRDG, Display, TEXT("\t(%p) %s"), Buffer, Buffer->Name);
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
	const TCHAR* AliasColorAttributes = TEXT("color=\"#00ff00\", fontcolor=\"#00ff00\"");

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
		return TEXT("#ffffff");
	}

	FString GetSubresourceStateLabel(FRDGSubresourceState State)
	{
		check(State.Pipeline == ERHIPipeline::Graphics || State.Pipeline == ERHIPipeline::AsyncCompute);
		const TCHAR* FontColor = State.Pipeline == ERHIPipeline::AsyncCompute ? AsyncComputeColorName : RasterColorName;
		return FString::Printf(TEXT("<font color=\"%s\">%s</font>"), FontColor, *GetRHIAccessName(State.Access));
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
	FRDGPassBitArray InPassesCulled,
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
		PassesCulled = InPassesCulled;
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

	TArray<FRDGPassHandle> PassesGraphics;
	TArray<FRDGPassHandle> PassesAsyncCompute;

	for (FRDGPassHandle PassHandle = Passes->Begin(); PassHandle != Passes->End(); ++PassHandle)
	{
		const FRDGPass* Pass = Passes->Get(PassHandle);
		const ERHIPipeline Pipeline = Pass->GetPipeline();

		switch (Pipeline)
		{
		case ERHIPipeline::Graphics:
			PassesGraphics.Add(PassHandle);
			break;
		case ERHIPipeline::AsyncCompute:
			PassesAsyncCompute.Add(PassHandle);
			break;
		default:
			checkNoEntry();
		}
	}

	if (GRDGDumpGraph == RDG_DUMP_GRAPH_TRACKS)
	{
		FRDGPassHandle PrevPassesByPipeline[uint32(ERHIPipeline::Num)];

		for (FRDGPassHandle PassHandle = Passes->Begin(); PassHandle != Passes->End(); ++PassHandle)
		{
			const FRDGPass* Pass = Passes->Get(PassHandle);

			if (!EnumHasAnyFlags(Pass->GetFlags(), ERDGPassFlags::Copy | ERDGPassFlags::Raster | ERDGPassFlags::Compute | ERDGPassFlags::AsyncCompute))
			{
				continue;
			}

			ERHIPipeline PassPipeline = Pass->GetPipeline();
			checkf(FMath::IsPowerOfTwo(uint32(PassPipeline)), TEXT("This logic doesn't handle multi-pipe passes."));
			uint32 PipeIndex = FMath::FloorLog2(uint32(PassPipeline));

			FRDGPassHandle& PrevPassInPipelineHandle = PrevPassesByPipeline[PipeIndex];

			if (PrevPassInPipelineHandle.IsValid())
			{
				AddLine(FString::Printf(TEXT("\"%s\" -> \"%s\" [style=\"filled\", penwidth=2, color=\"%s\"]"),
					*GetNodeName(PrevPassInPipelineHandle), *GetNodeName(PassHandle), GetPassColorName(Pass->GetFlags())));
			}

			if (Pass->GetPipeline() == ERHIPipeline::AsyncCompute)
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
	else if (GRDGDumpGraph == RDG_DUMP_GRAPH_PRODUCERS)
	{
		for (FRDGPassHandle PassHandle = Passes->Begin(); PassHandle != Passes->End(); ++PassHandle)
		{
			if (PassHandle == EpiloguePassHandle)
			{
				break;
			}

			const FRDGPass* Pass = Passes->Get(PassHandle);

			for (FRDGPassHandle ProducerHandle : Pass->GetProducers())
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
		const TCHAR* Style = PassesCulled[PassHandle] ? TEXT("dashed") : TEXT("filled");
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
		if (Texture->IsExternal())
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
		if (Buffer->IsExternal())
		{
			Line += TEXT("\n(External)");
		}
		Line += TEXT("\"]");
		AddLine(Line);
	}
	AddBraceEnd();

	uint32 NumPassesActive = 0;
	uint32 NumPassesCulled = 0;
	for (FRDGPassHandle PassHandle = Passes->Begin(); PassHandle != Passes->End(); ++PassHandle)
	{
		if (PassesCulled[PassHandle])
		{
			NumPassesCulled++;
		}
		else
		{
			NumPassesActive++;
		}
	}

	AddLine(FString::Printf(TEXT("label=\"%s [Active Passes: %d, Culled Passes: %d, Textures: %d, Buffers: %d]\""), *GraphName, NumPassesActive, NumPassesCulled, Textures.Num(), Buffers.Num()));

	AddBraceEnd();
	check(Indentation.IsEmpty());

	const TCHAR* DumpType = TEXT("");

	switch (GRDGDumpGraph)
	{
	case RDG_DUMP_GRAPH_RESOURCES:
		DumpType = TEXT("_resources");
		break;
	case RDG_DUMP_GRAPH_PRODUCERS:
		DumpType = TEXT("_producers");
		break;
	case RDG_DUMP_GRAPH_TRACKS:
		DumpType = TEXT("_tracks");
		break;
	}

	FFileHelper::SaveStringToFile(File, *(FPaths::ProjectLogDir() / FString::Printf(TEXT("RDG_%s%s.gv"), *GraphName, DumpType)));

	bOpen = false;
}

bool FRDGLogFile::IncludeTransitionEdgeInGraph(FRDGPassHandle Pass) const
{
	return Pass.IsValid() && Pass != ProloguePassHandle && Pass != EpiloguePassHandle;
}

bool FRDGLogFile::IncludeTransitionEdgeInGraph(FRDGPassHandle PassBefore, FRDGPassHandle PassAfter) const
{
	return IncludeTransitionEdgeInGraph(PassBefore) && IncludeTransitionEdgeInGraph(PassAfter) && PassBefore < PassAfter;
}

void FRDGLogFile::AddFirstEdge(const FRDGTextureRef Texture, FRDGPassHandle FirstPass)
{
	if (GRDGDumpGraph == RDG_DUMP_GRAPH_RESOURCES && bOpen && IncludeTransitionEdgeInGraph(FirstPass))
	{
		AddLine(FString::Printf(TEXT("\"%s\" -> \"%s\" [%s]"),
			*GetNodeName(Texture),
			*GetNodeName(FirstPass),
			TextureColorAttributes));
	}
}

void FRDGLogFile::AddFirstEdge(const FRDGBufferRef Buffer, FRDGPassHandle FirstPass)
{
	if (GRDGDumpGraph == RDG_DUMP_GRAPH_RESOURCES && bOpen && IncludeTransitionEdgeInGraph(FirstPass))
	{
		AddLine(FString::Printf(TEXT("\"%s\" -> \"%s\" [%s]"),
			*GetNodeName(Buffer),
			*GetNodeName(FirstPass),
			BufferColorAttributes));
	}
}


void FRDGLogFile::AddAliasEdge(const FRDGTextureRef TextureBefore, FRDGPassHandle BeforePass, const FRDGTextureRef TextureAfter, FRDGPassHandle AfterPass)
{
	if (GRDGDumpGraph == RDG_DUMP_GRAPH_RESOURCES && bOpen && IncludeTransitionEdgeInGraph(BeforePass, AfterPass))
	{
		AddLine(FString::Printf(TEXT("\"%s\" -> \"%s\" [%s, label=<Alias: <b>%s -&gt; %s</b>>]"),
			*GetProducerName(BeforePass),
			*GetConsumerName(AfterPass),
			AliasColorAttributes,
			TextureBefore->Name,
			TextureAfter->Name));
	}
}

void FRDGLogFile::AddAliasEdge(const FRDGBufferRef BufferBefore, FRDGPassHandle BeforePass, const FRDGBufferRef BufferAfter, FRDGPassHandle AfterPass)
{
	if (GRDGDumpGraph == RDG_DUMP_GRAPH_RESOURCES && bOpen && IncludeTransitionEdgeInGraph(BeforePass, AfterPass))
	{
		AddLine(FString::Printf(TEXT("\"%s\" -> \"%s\" [%s, label=<Alias: <b>%s -&gt; %s</b>>]"),
			*GetProducerName(BeforePass),
			*GetConsumerName(AfterPass),
			AliasColorAttributes,
			BufferBefore->Name,
			BufferAfter->Name));
	}
}

void FRDGLogFile::AddTransitionEdge(FRDGPassHandle PassHandle, FRDGSubresourceState StateBefore, FRDGSubresourceState StateAfter, const FRDGTextureRef Texture)
{
	if (GRDGDumpGraph == RDG_DUMP_GRAPH_RESOURCES && bOpen && IncludeTransitionEdgeInGraph(StateBefore.FirstPass, PassHandle))
	{
		if (FRDGSubresourceState::IsTransitionRequired(StateBefore, StateAfter))
		{
			AddLine(FString::Printf(TEXT("\"%s\" -> \"%s\" [%s, label=<%s: <b>%s -&gt; %s</b>>]"),
				*GetProducerName(StateBefore.LastPass),
				*GetConsumerName(StateAfter.FirstPass),
				TextureColorAttributes,
				Texture->Name,
				*GetSubresourceStateLabel(StateBefore),
				*GetSubresourceStateLabel(StateAfter)));
		}
		else
		{
			AddLine(FString::Printf(TEXT("\"%s\" -> \"%s\" [%s, label=<%s: <b>%s</b>>]"),
				*GetProducerName(StateBefore.FirstPass),
				*GetConsumerName(PassHandle),
				TextureColorAttributes,
				Texture->Name,
				*GetSubresourceStateLabel(StateBefore)));
		}
	}
}

void FRDGLogFile::AddTransitionEdge(FRDGPassHandle PassHandle, FRDGSubresourceState StateBefore, FRDGSubresourceState StateAfter, const FRDGTextureRef Texture, FRDGTextureSubresource Subresource)
{
	if (GRDGDumpGraph == RDG_DUMP_GRAPH_RESOURCES && bOpen && IncludeTransitionEdgeInGraph(StateBefore.FirstPass, PassHandle))
	{
		if (FRDGSubresourceState::IsTransitionRequired(StateBefore, StateAfter))
		{
			AddLine(FString::Printf(TEXT("\"%s\" -> \"%s\" [%s, label=<%s[%d][%d][%d]: <b>%s -&gt; %s</b>>]"),
				*GetProducerName(StateBefore.LastPass),
				*GetConsumerName(StateAfter.FirstPass),
				TextureColorAttributes,
				Texture->Name,
				Subresource.MipIndex, Subresource.ArraySlice, Subresource.PlaneSlice,
				*GetSubresourceStateLabel(StateBefore),
				*GetSubresourceStateLabel(StateAfter)));
		}
		else
		{
			AddLine(FString::Printf(TEXT("\"%s\" -> \"%s\" [%s, label=<%s[%d][%d][%d]: <b>%s</b>>]"),
				*GetProducerName(StateBefore.FirstPass),
				*GetConsumerName(PassHandle),
				TextureColorAttributes,
				Texture->Name,
				Subresource.MipIndex, Subresource.ArraySlice, Subresource.PlaneSlice,
				*GetSubresourceStateLabel(StateBefore)));
		}
	}
}

void FRDGLogFile::AddTransitionEdge(FRDGPassHandle PassHandle, FRDGSubresourceState StateBefore, FRDGSubresourceState StateAfter, const FRDGBufferRef Buffer)
{
	if (GRDGDumpGraph == RDG_DUMP_GRAPH_RESOURCES && bOpen && IncludeTransitionEdgeInGraph(StateBefore.FirstPass, PassHandle))
	{
		if (FRDGSubresourceState::IsTransitionRequired(StateBefore, StateAfter))
		{
			AddLine(FString::Printf(TEXT("\"%s\" -> \"%s\" [%s, label=<%s: <b>%s -&gt; %s</b>>]"),
				*GetProducerName(StateBefore.LastPass),
				*GetConsumerName(StateAfter.FirstPass),
				BufferColorAttributes,
				Buffer->Name,
				*GetSubresourceStateLabel(StateBefore),
				*GetSubresourceStateLabel(StateAfter)));
		}
		else
		{
			AddLine(FString::Printf(TEXT("\"%s\" -> \"%s\" [%s, label=<%s: <b>%s</b>>]"),
				*GetProducerName(StateBefore.FirstPass),
				*GetConsumerName(PassHandle),
				BufferColorAttributes,
				Buffer->Name,
				*GetSubresourceStateLabel(StateBefore)));
		}
	}
}

#endif
