// Copyright Epic Games, Inc. All Rights Reserved.

#include "RenderGraphValidation.h"

#if RDG_ENABLE_DEBUG

/** Validates that we are only executing a single render graph instance in the callstack. Used to catch if a
 *  user creates a second FRDGBuilder instance inside of a pass that is executing.
 */
static bool GRDGInExecutePassScope = false;

/** This utility class tracks read / write events on individual resources within a pass in order to ensure
 *  that sub-resources are not being simultaneously bound for read / write. The RHI currently only supports
 *  disjoint read-write access on a texture for mip-map generation purposes.
 */
class FRDGPassResourceValidator
{
public:
	static const uint32 AllMipLevels = ~0;

	FRDGPassResourceValidator(const FRDGPass& InPass)
		: Pass(InPass)
		, bIsGenerateMips(InPass.IsGenerateMips())
	{}

	~FRDGPassResourceValidator()
	{
		for (const auto& KeyValue : BufferAccessMap)
		{
			const FRDGBufferRef Buffer = KeyValue.Key;
			const FBufferAccess& BufferAccess = KeyValue.Value;
	
			ensureMsgf(!(BufferAccess.Bits.Read && BufferAccess.Bits.Write),
				TEXT("Attempting to use buffer '%s' for both SRV and UAV access in pass '%s'. Only one usage at a time is allowed."),
				Buffer->Name, Pass.GetName());
		}

		for (const auto& KeyValue : TextureAccessMap)
		{
			const FRDGTextureRef Texture = KeyValue.Key;
			const FTextureAccess& TextureAccess = KeyValue.Value;

			// A generate mips pass requires both read / write access to the same texture for barriers to work correctly.
			if (bIsGenerateMips)
			{
				const bool bWriteWithoutRead = TextureAccess.IsAnyWrite() && !TextureAccess.IsAnyRead();
				ensureMsgf(!bWriteWithoutRead,
					TEXT("Attempting to use texture '%s' for write but not read access in pass '%s' which is marked with 'GenerateMips'. This kind of pass only supports textures for simultaneous read and write."),
					Texture->Name, Pass.GetName());
			}
			// A normal pass requires only read OR write access to the same resource for barriers to work correctly.
			else
			{
				const bool bReadAndWrite = TextureAccess.IsAnyRead() && TextureAccess.IsAnyWrite();
				ensureMsgf(!bReadAndWrite,
					TEXT("Attempting to use texture '%s' for both read and write access in pass '%s' which is NOT marked with 'GenerateMips'. You must specify GenerateMips in your pass flags for this to be valid."),
					Texture->Name, Pass.GetName());
			}
		}
	}

	void Read(FRDGBufferRef Buffer)
	{
		check(Buffer);

		ensureMsgf(!bIsGenerateMips,
			TEXT("Attempting to read from Buffer %s on Pass %s which is marked as 'GenerateMips'. Only textures are supported on this type of pass."),
			Buffer->Name, Pass.GetName());

		FBufferAccess& BufferAccess = BufferAccessMap.FindOrAdd(Buffer);
		BufferAccess.Bits.Read = 1;
	}

	void Write(FRDGBufferRef Buffer)
	{
		check(Buffer);

		ensureMsgf(!bIsGenerateMips,
			TEXT("Attempting to write to Buffer %s on Pass %s which is marked as 'GenerateMips'. Only textures are supported on this type of pass."),
			Buffer->Name, Pass.GetName());

		FBufferAccess& BufferAccess = BufferAccessMap.FindOrAdd(Buffer);
		BufferAccess.Bits.Write = 1;
	}

	void Read(FRDGTextureRef Texture, uint32 MipLevel = AllMipLevels)
	{
		check(Texture);

		const uint32 MipMask = (MipLevel == AllMipLevels) ? AllMipLevels : (1 << MipLevel);

		FTextureAccess& TextureAccess = TextureAccessMap.FindOrAdd(Texture);
		TextureAccess.SetAllRead(MipMask);
	}

	void Write(FRDGTextureRef Texture, uint32 MipLevel = AllMipLevels)
	{
		check(Texture);

		const uint32 MipMask = 1 << MipLevel;

		FTextureAccess& TextureAccess = TextureAccessMap.FindOrAdd(Texture);
		TextureAccess.SetAllWrite(MipMask);
	}

private:
	struct FBufferAccess
	{
		union
		{
			uint8 PackedData = 0;

			struct
			{
				uint8 Read : 1;
				uint8 Write : 1;
			} Bits;
		};

		FORCEINLINE bool operator!= (FBufferAccess Other) const
		{
			return PackedData != Other.PackedData;
		}

		FORCEINLINE bool operator< (FBufferAccess Other) const
		{
			return PackedData < Other.PackedData;
		}
	};

	struct FTextureAccess
	{
		FTextureAccess() = default;

		union
		{
			uint64 PackedData = 0;

			struct
			{
				uint32 Read;
				uint32 Write;
			} Mips;
		};

		FORCEINLINE bool IsAnyRead(uint32 MipMask = AllMipLevels) const
		{
			return (Mips.Read & MipMask) != 0;
		}

		FORCEINLINE bool IsAnyWrite(uint32 MipMask = AllMipLevels) const
		{
			return (Mips.Write & MipMask) != 0;
		}

		FORCEINLINE void SetAllRead(uint32 MipMask = AllMipLevels)
		{
			Mips.Read |= MipMask;
		}

		FORCEINLINE void SetAllWrite(uint32 MipMask = AllMipLevels)
		{
			Mips.Write |= MipMask;
		}

		FORCEINLINE bool operator!= (FTextureAccess Other) const
		{
			return PackedData != Other.PackedData;
		}

		FORCEINLINE bool operator< (FTextureAccess Other) const
		{
			return PackedData < Other.PackedData;
		}
	};

	TMap<FRDGBufferRef, FBufferAccess, SceneRenderingSetAllocator> BufferAccessMap;
	TMap<FRDGTextureRef, FTextureAccess, SceneRenderingSetAllocator> TextureAccessMap;
	const FRDGPass& Pass;
	const bool bIsGenerateMips;
};

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
	if (IsRDGDebugEnabled())
	{
		TrackedTextures.Add(Texture);
	}
}

void FRDGUserValidation::ValidateCreateBuffer(FRDGBufferRef Buffer)
{
	check(Buffer);
	if (IsRDGDebugEnabled())
	{
		TrackedBuffers.Add(Buffer);
	}
}

void FRDGUserValidation::ValidateCreateExternalTexture(FRDGTextureRef Texture)
{
	ValidateCreateTexture(Texture);
	Texture->MarkAsExternal();
}

void FRDGUserValidation::ValidateCreateExternalBuffer(FRDGBufferRef Buffer)
{
	ValidateCreateBuffer(Buffer);
	Buffer->MarkAsExternal();
}

void FRDGUserValidation::ValidateExtractResource(FRDGParentResourceRef Resource)
{
	check(Resource);

	checkf(Resource->HasBeenProduced(),
		TEXT("Unable to queue the extraction of the resource %s because it has not been produced by any pass."),
		Resource->Name);

	/** Increment pass access counts for externally registered buffers and textures to avoid
	 *  emitting a 'produced but never used' warning. We don't have the history of registered
	 *  resources to be able to emit a proper warning.
	 */
	Resource->PassAccessCount++;
}

void FRDGUserValidation::RemoveUnusedWarning(FRDGParentResourceRef Resource)
{
	check(Resource);

	// Removes 'produced but not used' warning.
	Resource->PassAccessCount++;

	// Removes 'not used' warning.
	Resource->bIsActuallyUsedByPass = true;
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

	const auto MarkAsProduced = [Pass, bSkipPassAccessMarking] (FRDGParentResourceRef Resource)
	{
		if (!bSkipPassAccessMarking)
		{
			Resource->MarkAsProducedBy(Pass);
			Resource->PassAccessCount++;
		}
	};

	const auto MarkAsConsumed = [Pass, bSkipPassAccessMarking] (FRDGParentResourceRef Resource)
	{
		if (!bSkipPassAccessMarking)
		{
			Resource->PassAccessCount++;
		}
	};

	const FRenderTargetBindingSlots* RenderTargetBindingSlots = nullptr;

	const TCHAR* PassName = Pass->GetName();
	const bool bIsRaster = Pass->IsRaster();
	const bool bIsCompute = Pass->IsCompute();
	const bool bIsCopy = Pass->IsCopy();
	const bool bIsGeneratingMips = Pass->IsGenerateMips();

	/** Validate that the user set the correct pass flags. */
	{
		uint32 Count = 0;
		Count += bIsRaster ? 1 : 0;
		Count += bIsCompute ? 1 : 0;
		Count += bIsCopy ? 1 : 0;
		checkf(Count == 1,
			TEXT("Pass %s must be declared as either Raster, Compute, or Copy. These flags cannot be combined."),
			PassName);

		if (bIsGeneratingMips)
		{
			checkf(bIsRaster || bIsCompute,
				TEXT("Pass %s is declared as generating mips. This is only supported by Raster or Compute passes."),
				PassName);
		}
	}

	FRDGPassResourceValidator PassResourceValidator(*Pass);

	FRDGPassParameterStruct ParameterStruct = Pass->GetParameters();

	const uint32 ParameterCount = ParameterStruct.GetParameterCount();

	for (uint32 ParameterIndex = 0; ParameterIndex < ParameterCount; ++ParameterIndex)
	{
		FRDGPassParameter Parameter = ParameterStruct.GetParameter(ParameterIndex);

		switch (Parameter.GetType())
		{
		case UBMT_RDG_TEXTURE:
		{
			if (FRDGTextureRef Texture = Parameter.GetAsTexture())
			{
				PassResourceValidator.Read(Texture);

				ensureMsgf(Texture->HasBeenProduced(),
					TEXT("Pass %s has a dependency on the texture %s that has never been produced."),
					PassName, Texture->Name);

				MarkAsConsumed(Texture);
			}
		}
		break;
		case UBMT_RDG_TEXTURE_SRV:
		{
			if (FRDGTextureSRVRef SRV = Parameter.GetAsTextureSRV())
			{
				FRDGTextureRef Texture = SRV->GetParent();

				PassResourceValidator.Read(Texture, SRV->Desc.MipLevel);

				ensureMsgf(Texture->HasBeenProduced(),
					TEXT("Pass %s has a dependency on the texture %s that has never been produced."),
					PassName, Texture->Name);

				MarkAsConsumed(Texture);
			}
		}
		break;
		case UBMT_RDG_TEXTURE_UAV:
		{
			if (FRDGTextureUAVRef UAV = Parameter.GetAsTextureUAV())
			{
				FRDGTextureRef Texture = UAV->GetParent();

				PassResourceValidator.Write(Texture, UAV->Desc.MipLevel);

				MarkAsProduced(Texture);
			}
		}
		break;
		case UBMT_RDG_BUFFER:
		{
			if (FRDGBufferRef Buffer = Parameter.GetAsBuffer())
			{
				PassResourceValidator.Read(Buffer);

				ensureMsgf(Buffer->HasBeenProduced(),
					TEXT("Pass %s has a dependency on the buffer %s that has never been produced."),
					PassName, Buffer->Name);

				MarkAsConsumed(Buffer);
			}
		}
		break;
		case UBMT_RDG_BUFFER_SRV:
		{
			if (FRDGBufferSRVRef SRV = Parameter.GetAsBufferSRV())
			{
				FRDGBufferRef Buffer = SRV->GetParent();

				PassResourceValidator.Read(Buffer);

				ensureMsgf(Buffer->HasBeenProduced(),
					TEXT("Pass %s has a dependency on the buffer %s that has never been produced."),
					PassName, SRV->Desc.Buffer->Name);

				MarkAsConsumed(Buffer);
			}
		}
		break;
		case UBMT_RDG_BUFFER_UAV:
		{
			if (FRDGBufferUAVRef UAV = Parameter.GetAsBufferUAV())
			{
				FRDGBufferRef Buffer = UAV->GetParent();

				PassResourceValidator.Write(Buffer);

				MarkAsProduced(Buffer);
			}
		}
		break;
		case UBMT_RDG_TEXTURE_COPY_DEST:
		{
			if (FRDGTextureRef Texture = Parameter.GetAsTexture())
			{
				PassResourceValidator.Write(Texture);

				MarkAsProduced(Texture);
			}
		}
		break;
		case UBMT_RDG_BUFFER_COPY_DEST:
		{
			if (FRDGBufferRef Buffer = Parameter.GetAsBuffer())
			{
				PassResourceValidator.Write(Buffer);

				MarkAsProduced(Buffer);
			}
		}
		break;
		case UBMT_RENDER_TARGET_BINDING_SLOTS:
		{
			if (!RenderTargetBindingSlots)
			{
				RenderTargetBindingSlots = &Parameter.GetAsRenderTargetBindingSlots();
			}
			else if (IsRDGDebugEnabled())
			{
				EmitRDGWarningf(TEXT("Pass %s have duplicated render target binding slots."), PassName);
			}
		}
		break;
		}
	}

	/** Validate that raster passes have render target binding slots and compute passes don't. */
	if (RenderTargetBindingSlots)
	{
		checkf(bIsRaster, TEXT("Pass '%s' has render target binding slots but is flagged as 'Compute'."), PassName);
	}
	else
	{
		checkf(!bIsRaster, TEXT("Pass '%s' is missing render target binding slots. Set the 'Compute' or 'Copy' flag if render targets are not required."), PassName);
	}

	/** Validate render target / depth stencil binding usage. */
	if (RenderTargetBindingSlots)
	{
		const auto& RenderTargets = RenderTargetBindingSlots->Output;

		{
			const auto& DepthStencil = RenderTargetBindingSlots->DepthStencil;

			if (FRDGTextureRef Texture = DepthStencil.GetTexture())
			{
				//ensureMsgf(Texture->Desc.NumSamples == 1,
				//	TEXT("Pass '%s' uses an MSAA depth-stencil render target. This is not yet supported."),
				//	Pass->GetName());

				ensureMsgf(!bIsGeneratingMips,
					TEXT("Pass '%s' is marked to generate mips but has a depth stencil texture. This is not supported."),
					Pass->GetName());

				checkf(
					Texture->Desc.TargetableFlags & TexCreate_DepthStencilTargetable,
					TEXT("Pass '%s' attempted to bind texture '%s' as a depth stencil render target, but the texture has not been created with TexCreate_DepthStencilTargetable."),
					PassName, Texture->Name);

				// Depth stencil only supports one mip, since there isn't actually a way to select the mip level.
				const uint32 MipLevel = 0;
				check(Texture->Desc.NumMips == 1);

				if (DepthStencil.GetDepthStencilAccess().IsAnyWrite())
				{
					PassResourceValidator.Write(Texture, MipLevel);
					MarkAsProduced(Texture);
				}
				else
				{
					PassResourceValidator.Read(Texture, MipLevel);
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
					PassResourceValidator.Write(Texture, RenderTarget.GetMipIndex());

					const bool bIsLoadAction = RenderTarget.GetLoadAction() == ERenderTargetLoadAction::ELoad;

					ensureMsgf(
						Texture->Desc.TargetableFlags & TexCreate_RenderTargetable,
						TEXT("Pass '%s' attempted to bind texture '%s' as a render target, but the texture has not been created with TexCreate_RenderTargetable."),
						PassName, Texture->Name);

					/** Validate that load action is correct. We can only load contents if a pass previously produced something. */
					{
						const bool bIsLoadActionInvalid = bIsLoadAction && !Texture->HasBeenProduced();
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
						const bool bHasBeenProduced = Texture->HasBeenProduced() && !Texture->IsExternal();

						// We only validate single-mip textures since we don't track production at the subresource level.
						const bool bFailedToLoadProducedContent = !bIsLoadAction && bHasBeenProduced && Texture->Desc.NumMips == 1;

						// Untracked render targets aren't actually managed by the render target pool.
						const bool bIsUntrackedRenderTarget = Texture->PooledRenderTarget && !Texture->PooledRenderTarget->IsTracked();

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

	if (IsRDGDebugEnabled())
	{
		auto ValidateResourceAtExecuteEnd = [](const FRDGParentResourceRef Resource)
		{
			check(Resource->ReferenceCount == 0);

			const bool bProducedButNeverUsed = Resource->PassAccessCount == 1 && Resource->FirstProducer;

			if (bProducedButNeverUsed)
			{
				check(Resource->HasBeenProduced());

				EmitRDGWarningf(
					TEXT("Resource %s has been produced by the pass %s, but never used by another pass."),
					Resource->Name, Resource->FirstProducer->GetName());
			}
		};

		for (const FRDGTextureRef Texture : TrackedTextures)
		{
			ValidateResourceAtExecuteEnd(Texture);

			bool bHasBeenProducedByGraph = !Texture->IsExternal() && Texture->PassAccessCount > 0;

			if (bHasBeenProducedByGraph && !Texture->bHasNeededUAV && (Texture->Desc.TargetableFlags & TexCreate_UAV))
			{
				EmitRDGWarningf(
					TEXT("Resource %s first produced by the pass %s had the TexCreate_UAV flag, but no UAV has been used."),
					Texture->Name, Texture->FirstProducer->GetName());
			}

			if (bHasBeenProducedByGraph && !Texture->bHasBeenBoundAsRenderTarget && (Texture->Desc.TargetableFlags & TexCreate_RenderTargetable))
			{
				EmitRDGWarningf(
					TEXT("Resource %s first produced by the pass %s had the TexCreate_RenderTargetable flag, but has never been bound as a render target of a pass."),
					Texture->Name, Texture->FirstProducer->GetName());
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
	checkf(!GRDGInExecutePassScope, TEXT("Render graph is being executed recursively. This usually means a separate FRDGBuilder instance was created inside of an executing pass."));

	GRDGInExecutePassScope = true;

	SetAllowRHIAccess(Pass, true);

	if (IsRDGDebugEnabled())
	{
		FRDGPassParameterStruct ParameterStruct = Pass->GetParameters();

		const uint32 ParameterCount = ParameterStruct.GetParameterCount();

		for (uint32 ParameterIndex = 0; ParameterIndex < ParameterCount; ++ParameterIndex)
		{
			FRDGPassParameter Parameter = ParameterStruct.GetParameter(ParameterIndex);

			if (Parameter.GetType() == UBMT_RDG_TEXTURE_UAV)
			{
				if (FRDGTextureUAVRef UAV = Parameter.GetAsTextureUAV())
				{
					FRDGTextureRef Texture = UAV->Desc.Texture;
					Texture->bHasNeededUAV = true;
				}
			}
			else if (Parameter.GetType() == UBMT_RENDER_TARGET_BINDING_SLOTS)
			{
				const FRenderTargetBindingSlots& RenderTargetBindingSlots = Parameter.GetAsRenderTargetBindingSlots();
				const auto& RenderTargets = RenderTargetBindingSlots.Output;
				const auto& DepthStencil = RenderTargetBindingSlots.DepthStencil;
				const uint32 RenderTargetCount = RenderTargets.Num();

				for (uint32 RenderTargetIndex = 0; RenderTargetIndex < RenderTargetCount; RenderTargetIndex++)
				{
					const FRenderTargetBinding& RenderTarget = RenderTargets[RenderTargetIndex];

					if (FRDGTextureRef Texture = RenderTarget.GetTexture())
					{
						Texture->bHasBeenBoundAsRenderTarget = true;
					}
					else
					{
						break;
					}
				}

				if (FRDGTextureRef Texture = DepthStencil.GetTexture())
				{
					Texture->bHasBeenBoundAsRenderTarget = true;
				}
			}
		}
	} 
}

void FRDGUserValidation::ValidateExecutePassEnd(const FRDGPass* Pass)
{
	SetAllowRHIAccess(Pass, false);

	FRDGPassParameterStruct ParameterStruct = Pass->GetParameters();

	const uint32 ParameterCount = ParameterStruct.GetParameterCount();

	if (IsRDGDebugEnabled())
	{
		uint32 TrackedResourceCount = 0;
		uint32 UsedResourceCount = 0;

		for (uint32 ParameterIndex = 0; ParameterIndex < ParameterCount; ++ParameterIndex)
		{
			FRDGPassParameter Parameter = ParameterStruct.GetParameter(ParameterIndex);

			if (Parameter.IsResource())
			{
				if (FRDGResourceRef Resource = Parameter.GetAsResource())
				{
					TrackedResourceCount++;
					UsedResourceCount += Resource->bIsActuallyUsedByPass ? 1 : 0;
				}
			}
		}

		if (TrackedResourceCount != UsedResourceCount)
		{
			FString WarningMessage = FString::Printf(
				TEXT("'%d' of the '%d' resources of the pass '%s' were not actually used."),
				TrackedResourceCount - UsedResourceCount, TrackedResourceCount, Pass->GetName());

			for (uint32 ParameterIndex = 0; ParameterIndex < ParameterCount; ++ParameterIndex)
			{
				FRDGPassParameter Parameter = ParameterStruct.GetParameter(ParameterIndex);

				if (Parameter.IsResource())
				{
					if (const FRDGResourceRef Resource = Parameter.GetAsResource())
					{
						if (!Resource->bIsActuallyUsedByPass)
						{
							WarningMessage += FString::Printf(TEXT("\n    %s"), Resource->Name);
						}
					}
				}
			}

			EmitRDGWarning(WarningMessage);
		}
	}

	for (uint32 ParameterIndex = 0; ParameterIndex < ParameterCount; ++ParameterIndex)
	{
		FRDGPassParameter Parameter = ParameterStruct.GetParameter(ParameterIndex);

		if (Parameter.IsResource())
		{
			if (const FRDGResourceRef Resource = Parameter.GetAsResource())
			{
				Resource->bIsActuallyUsedByPass = false;
			}
		}
	}

	GRDGInExecutePassScope = false;
}

void FRDGUserValidation::SetAllowRHIAccess(const FRDGPass* Pass, bool bAllowAccess)
{
	FRDGPassParameterStruct ParameterStruct = Pass->GetParameters();

	const uint32 ParameterCount = ParameterStruct.GetParameterCount();

	for (uint32 ParameterIndex = 0; ParameterIndex < ParameterCount; ++ParameterIndex)
	{
		FRDGPassParameter Parameter = ParameterStruct.GetParameter(ParameterIndex);

		if (Parameter.IsResource())
		{
			if (FRDGResourceRef Resource = Parameter.GetAsResource())
			{
				Resource->bAllowRHIAccess = bAllowAccess;
			}
		}
		else if (Parameter.IsRenderTargetBindingSlots())
		{
			const FRenderTargetBindingSlots& RenderTargetBindingSlots = Parameter.GetAsRenderTargetBindingSlots();
			const auto& RenderTargets = RenderTargetBindingSlots.Output;
			const auto& DepthStencil = RenderTargetBindingSlots.DepthStencil;
			const uint32 RenderTargetCount = RenderTargets.Num();

			for (uint32 RenderTargetIndex = 0; RenderTargetIndex < RenderTargetCount; RenderTargetIndex++)
			{
				const FRenderTargetBinding& RenderTarget = RenderTargets[RenderTargetIndex];

				if (FRDGTextureRef Texture = RenderTarget.GetTexture())
				{
					Texture->bAllowRHIAccess = bAllowAccess;
				}
				else
				{
					break;
				}
			}

			if (FRDGTextureRef Texture = DepthStencil.GetTexture())
			{
				Texture->bAllowRHIAccess = bAllowAccess;
			}
		}
	}
}

#endif