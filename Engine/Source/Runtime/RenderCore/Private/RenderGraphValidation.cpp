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
const ERHIAccess AccessMaskCompute = ERHIAccess::SRVCompute | ERHIAccess::UAVCompute;
const ERHIAccess AccessMaskRaster  = ERHIAccess::ResolveSrc | ERHIAccess::ResolveDst | ERHIAccess::DSVRead | ERHIAccess::DSVWrite | ERHIAccess::RTV | ERHIAccess::SRVGraphics | ERHIAccess::UAVGraphics | ERHIAccess::Present | ERHIAccess::VertexOrIndexBuffer;
const ERHIAccess AccessMaskComputeOrRaster = ERHIAccess::IndirectArgs;

/** Validates that we are only executing a single render graph instance in the callstack. Used to catch if a
 *  user creates a second FRDGBuilder instance inside of a pass that is executing.
 */
bool GRDGInExecutePassScope = false;
} //! namespace

struct FRDGResourceDebugData
{
	/** Boolean to track at runtime whether a resource is actually used by the lambda of a pass or not, to detect unnecessary resource dependencies on passes. */
	bool bIsActuallyUsedByPass = false;

	/** Boolean to track at pass execution whether the underlying RHI resource is allowed to be accessed. */
	bool bAllowRHIAccess = false;
};

void FRDGResource::MarkResourceAsUsed()
{
	ValidateRHIAccess();

	if (DebugData)
	{
		DebugData->bIsActuallyUsedByPass = true;
	}
}

void FRDGResource::ValidateRHIAccess() const
{
	// Passthrough resources will not have debug data, since they are not tied to a graph instance.
	if (DebugData)
	{
		checkf(DebugData->bAllowRHIAccess,
			TEXT("Accessing the RHI resource of %s at this time is not allowed. If you hit this check in pass, ")
			TEXT("that is due to this resource not being referenced in the parameters of your pass."),
			Name);
	}
}

FRDGResourceDebugData& FRDGResource::GetDebugData() const
{
	check(DebugData);
	return *DebugData;
}

struct FRDGParentResourceDebugData
{
	/** Pointer towards the pass that is the first to produce it, for even more convenient error message. */
	const FRDGPass* FirstProducer = nullptr;

	/** Count the number of times it has been used by a pass (without culling). */
	uint32 PassAccessCount = 0;

	/** Tracks whether a resource has valid contents. This version is true by default for external resources which have prior unknown contents. */
	bool bHasBeenProduced = false;

	/** Tracks whether this resource was clobbered by the builder prior to use. */
	bool bHasBeenClobbered = false;
};

FRDGParentResourceDebugData& FRDGParentResource::GetParentDebugData() const
{
	check(ParentDebugData);
	return *ParentDebugData;
}

bool FRDGResource::IsPassthrough() const
{
	return DebugData == nullptr;
}

struct FRDGTextureDebugData
{
	/** Tracks whether a UAV has ever been allocated to catch when TexCreate_UAV was unneeded. */
	bool bHasNeededUAV = false;

	/** Tracks whether has ever been bound as a render target to catch when TexCreate_RenderTargetable was unneeded. */
	bool bHasBeenBoundAsRenderTarget = false;
};

FRDGTextureDebugData& FRDGTexture::GetTextureDebugData() const
{
	check(TextureDebugData);
	return *TextureDebugData;
}

struct FRDGBufferDebugData
{
	/** Tracks state changes in order of execution. */
	TArray<TPair<FRDGPassHandle, FRDGSubresourceState>, SceneRenderingAllocator> States;
};

FRDGBufferDebugData& FRDGBuffer::GetBufferDebugData() const
{
	check(BufferDebugData);
	return *BufferDebugData;
}

void FRDGUniformBuffer::MarkResourceAsUsed()
{
	FRDGResource::MarkResourceAsUsed();

	// Individual resources can't be culled from a uniform buffer, so we have to mark them all as used.
	ParameterStruct.Enumerate([](FRDGParameter Parameter)
	{
		if (FRDGResourceRef Resource = Parameter.GetAsResource())
		{
			Resource->MarkResourceAsUsed();
		}
	});
}

FRDGUserValidation::FRDGUserValidation(FRDGAllocator& InAllocator, ERDGBuilderFlags InBuilderFlags)
	: Allocator(InAllocator)
	, BuilderFlags(InBuilderFlags)
	, ExpectedNumMarks(FMemStack::Get().GetNumMarks())
{}

FRDGUserValidation::~FRDGUserValidation()
{
	checkf(bHasExecuted, TEXT("Render graph execution is required to ensure consistency with immediate mode."));
}

void FRDGUserValidation::MemStackGuard()
{
	checkf(ExpectedNumMarks == FMemStack::Get().GetNumMarks(), TEXT("A MemStack mark was added during the FRDGBuilder lifetime. This is not allowed as it will free memory still used by the builder."));
}

void FRDGUserValidation::ExecuteGuard(const TCHAR* Operation, const TCHAR* ResourceName)
{
	checkf(!bHasExecuted, TEXT("Render graph operation '%s' with resource '%s' must be performed prior to graph execution."), Operation, ResourceName);
	MemStackGuard();
}

void FRDGUserValidation::ValidateCreateResource(FRDGResourceRef Resource)
{
	MemStackGuard();
	check(Resource);
	Resource->DebugData = Allocator.AllocObject<FRDGResourceDebugData>();
}

void FRDGUserValidation::ValidateCreateParentResource(FRDGParentResourceRef Resource)
{
	ValidateCreateResource(Resource);
	Resource->ParentDebugData = Allocator.AllocObject<FRDGParentResourceDebugData>();
}

void FRDGUserValidation::ValidateCreateTexture(FRDGTextureRef Texture)
{
	ValidateCreateParentResource(Texture);
	Texture->TextureDebugData = Allocator.AllocObject<FRDGTextureDebugData>();
	if (GRDGDebug)
	{
		TrackedTextures.Add(Texture);
	}
}

void FRDGUserValidation::ValidateCreateBuffer(FRDGBufferRef Buffer)
{
	ValidateCreateParentResource(Buffer);
	Buffer->BufferDebugData = Allocator.AllocObject<FRDGBufferDebugData>();
	if (GRDGDebug)
	{
		TrackedBuffers.Add(Buffer);
	}
}

void FRDGUserValidation::ValidateCreateSRV(FRDGTextureSRVRef SRV)
{
	ValidateCreateResource(SRV);
}

void FRDGUserValidation::ValidateCreateSRV(FRDGBufferSRVRef SRV)
{
	ValidateCreateResource(SRV);
}

void FRDGUserValidation::ValidateCreateUAV(FRDGTextureUAVRef UAV)
{
	ValidateCreateResource(UAV);
}

void FRDGUserValidation::ValidateCreateUAV(FRDGBufferUAVRef UAV)
{
	ValidateCreateResource(UAV);
}

void FRDGUserValidation::ValidateCreateUniformBuffer(FRDGUniformBufferRef UniformBuffer)
{
	ValidateCreateResource(UniformBuffer);
}

void FRDGUserValidation::ValidateRegisterExternalTexture(
	const TRefCountPtr<IPooledRenderTarget>& ExternalPooledTexture,
	const TCHAR* Name,
	ERenderTargetTexture RenderTargetTexture,
	ERDGTextureFlags Flags)
{
	checkf(Name, TEXT("Attempted to register external texture with NULL name."));
	checkf(ExternalPooledTexture.IsValid(), TEXT("Attempted to register NULL external texture."));
	checkf(ExternalPooledTexture->IsCompatibleWithRDG(), TEXT("Pooled render target %s is not a compatible type for RDG."), Name);
	ExecuteGuard(TEXT("RegisterExternalTexture"), Name);
}

void FRDGUserValidation::ValidateRegisterExternalBuffer(const TRefCountPtr<FRDGPooledBuffer>& ExternalPooledBuffer, const TCHAR* Name, ERDGBufferFlags Flags)
{
	checkf(Name, TEXT("Attempted to register external buffer with NULL name."));
	checkf(ExternalPooledBuffer.IsValid(), TEXT("Attempted to register NULL external buffer."));
	ExecuteGuard(TEXT("RegisterExternalBuffer"), Name);
}

void FRDGUserValidation::ValidateRegisterExternalTexture(FRDGTextureRef Texture)
{
	ValidateCreateTexture(Texture);
	Texture->GetParentDebugData().bHasBeenProduced = true;
}

void FRDGUserValidation::ValidateRegisterExternalBuffer(FRDGBufferRef Buffer)
{
	ValidateCreateBuffer(Buffer);
	Buffer->GetParentDebugData().bHasBeenProduced = true;
}

void FRDGUserValidation::ValidateCreateTexture(const FRDGTextureDesc& Desc, const TCHAR* Name, ERDGTextureFlags Flags)
{
	checkf(Name, TEXT("Creating a texture requires a valid debug name."));
	ExecuteGuard(TEXT("CreateTexture"), Name);

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

	checkf(!EnumHasAnyFlags(Flags, ERDGTextureFlags::ReadOnly), TEXT("Cannot create texture %s with the ReadOnly flag. Only registered textures can use this flag."), Name);

	checkf(!EnumHasAnyFlags(BuilderFlags, ERDGBuilderFlags::SkipBarriers), TEXT("Cannot create texture '%s' because RDG builder has 'SkipBarriers' flag."), Name);
}

void FRDGUserValidation::ValidateCreateBuffer(const FRDGBufferDesc& Desc, const TCHAR* Name, ERDGBufferFlags Flags)
{
	checkf(Name, TEXT("Creating a buffer requires a valid debug name."));
	ExecuteGuard(TEXT("CreateBuffer"), Name);

	checkf(Desc.GetTotalNumBytes() > 0, TEXT("Creating buffer '%s' is zero bytes in size."), Name);

	const bool bIsByteAddress = (Desc.Usage & BUF_ByteAddressBuffer) == BUF_ByteAddressBuffer;

	if (bIsByteAddress && Desc.UnderlyingType == FRDGBufferDesc::EUnderlyingType::StructuredBuffer)
	{
		checkf(Desc.BytesPerElement == 4, TEXT("Creating buffer '%s' as a structured buffer that is also byte addressable, BytesPerElement must be 4! Instead it is %d"), Name, Desc.BytesPerElement);
	}

	checkf(!EnumHasAnyFlags(Flags, ERDGBufferFlags::ReadOnly), TEXT("Cannot create buffer %s with the ReadOnly flag. Only registered buffers can use this flag."), Name);

	checkf(!EnumHasAnyFlags(BuilderFlags, ERDGBuilderFlags::SkipBarriers), TEXT("Cannot create buffer '%s' because RDG builder has 'SkipBarriers' flag."), Name);
}

void FRDGUserValidation::ValidateCreateSRV(const FRDGTextureSRVDesc& Desc)
{
	FRDGTextureRef Texture = Desc.Texture;
	checkf(Texture, TEXT("Texture SRV created with a null texture."));
	checkf(!Texture->IsPassthrough(), TEXT("Texture SRV created with passthrough texture '%s'."), Texture->Name);
	ExecuteGuard(TEXT("CreateSRV"), Texture->Name);
	checkf(Texture->Desc.Flags & TexCreate_ShaderResource, TEXT("Attempted to create SRV from texture %s which was not created with TexCreate_ShaderResource"), Desc.Texture->Name);

	// Validate the pixel format if overridden by the SRV's descriptor.
	if (Desc.Format == PF_X24_G8)
	{
		// PF_X24_G8 is a bit of mess in the RHI, used to read the stencil, but have varying BlockBytes.
		checkf(Texture->Desc.Format == PF_DepthStencil, TEXT("PF_X24_G8 is only to read stencil from a PF_DepthStencil texture"));
	}
	else if (Desc.Format != PF_Unknown)
	{
		checkf(Desc.Format < PF_MAX, TEXT("Illegal to create SRV for texture %s with invalid FPooledRenderTargetDesc::Format."), Texture->Name);
		checkf(GPixelFormats[Desc.Format].Supported, TEXT("Failed to create SRV for texture %s with pixel format %s because it is not supported."),
			Texture->Name, GPixelFormats[Desc.Format].Name);

		EPixelFormat ResourcePixelFormat = Texture->Desc.Format;

		checkf(
			GPixelFormats[Desc.Format].BlockBytes == GPixelFormats[ResourcePixelFormat].BlockBytes &&
			GPixelFormats[Desc.Format].BlockSizeX == GPixelFormats[ResourcePixelFormat].BlockSizeX &&
			GPixelFormats[Desc.Format].BlockSizeY == GPixelFormats[ResourcePixelFormat].BlockSizeY &&
			GPixelFormats[Desc.Format].BlockSizeZ == GPixelFormats[ResourcePixelFormat].BlockSizeZ,
			TEXT("Failed to create SRV for texture %s with pixel format %s because it does not match the byte size of the texture's pixel format %s."),
			Texture->Name, GPixelFormats[Desc.Format].Name, GPixelFormats[ResourcePixelFormat].Name);
	}

	checkf((Desc.MipLevel + Desc.NumMipLevels) <= Texture->Desc.NumMips, TEXT("Failed to create SRV at mips %d-%d: the texture %s has only %d mip levels."),
		Desc.MipLevel, (Desc.MipLevel + Desc.NumMipLevels), Texture->Name, Texture->Desc.NumMips);

	checkf(Desc.MetaData != ERDGTextureMetaDataAccess::FMask || GRHISupportsExplicitFMask,
		TEXT("Failed to create FMask SRV for texture %s because the current RHI doesn't support it. Be sure to gate the call with GRHISupportsExplicitFMask."),
		Texture->Name);

	checkf(Desc.MetaData != ERDGTextureMetaDataAccess::HTile || GRHISupportsExplicitHTile,
		TEXT("Failed to create HTile SRV for texture %s because the current RHI doesn't support it. Be sure to gate the call with GRHISupportsExplicitHTile."),
		Texture->Name);
}

void FRDGUserValidation::ValidateCreateSRV(const FRDGBufferSRVDesc& Desc)
{
	FRDGBufferRef Buffer = Desc.Buffer;
	checkf(Buffer, TEXT("Buffer SRV created with a null buffer."));
	checkf(!Buffer->IsPassthrough(), TEXT("Buffer SRV created with passthrough buffer '%s'."), Buffer->Name);
	ExecuteGuard(TEXT("CreateSRV"), Buffer->Name);
}

void FRDGUserValidation::ValidateCreateUAV(const FRDGTextureUAVDesc& Desc)
{
	FRDGTextureRef Texture = Desc.Texture;

	checkf(Texture, TEXT("Texture UAV created with a null texture."));
	checkf(!Texture->IsPassthrough(), TEXT("Texture UAV created with passthrough texture '%s'."), Texture->Name);
	ExecuteGuard(TEXT("CreateUAV"), Texture->Name);
	checkf(Texture->Desc.Flags & TexCreate_UAV, TEXT("Attempted to create UAV from texture %s which was not created with TexCreate_UAV"), Texture->Name);
	checkf(Desc.MipLevel < Texture->Desc.NumMips, TEXT("Failed to create UAV at mip %d: the texture %s has only %d mip levels."), Desc.MipLevel, Texture->Name, Texture->Desc.NumMips);
}

void FRDGUserValidation::ValidateCreateUAV(const FRDGBufferUAVDesc& Desc)
{
	FRDGBufferRef Buffer = Desc.Buffer;
	checkf(Buffer, TEXT("Buffer UAV created with a null buffer."));
	checkf(!Buffer->IsPassthrough(), TEXT("Buffer UAV created with passthrough buffer '%s'."), Buffer->Name);
	ExecuteGuard(TEXT("CreateUAV"), Buffer->Name);
}

void FRDGUserValidation::ValidateCreateUniformBuffer(const void* ParameterStruct, const FShaderParametersMetadata* Metadata)
{
	check(Metadata);
	const TCHAR* Name = Metadata->GetShaderVariableName();
	checkf(ParameterStruct, TEXT("Uniform buffer '%s' created with null parameters."), Name);
	ExecuteGuard(TEXT("CreateUniformBuffer"), Name);
}

void FRDGUserValidation::ValidateExtractTexture(FRDGTextureRef Texture, TRefCountPtr<IPooledRenderTarget>* OutTexturePtr)
{
	ValidateExtractResource(Texture);
	checkf(OutTexturePtr, TEXT("Texture %s was extracted, but the output texture pointer is null."), Texture->Name);
}

void FRDGUserValidation::ValidateExtractBuffer(FRDGBufferRef Buffer, TRefCountPtr<FRDGPooledBuffer>* OutBufferPtr)
{
	ValidateExtractResource(Buffer);
	checkf(OutBufferPtr, TEXT("Texture %s was extracted, but the output texture pointer is null."), Buffer->Name);
}

void FRDGUserValidation::ValidateExtractResource(FRDGParentResourceRef Resource)
{
	MemStackGuard();
	check(Resource);

	checkf(Resource->bProduced || Resource->bExternal,
		TEXT("Unable to queue the extraction of the resource %s because it has not been produced by any pass."),
		Resource->Name);

	/** Increment pass access counts for externally registered buffers and textures to avoid
	 *  emitting a 'produced but never used' warning. We don't have the history of registered
	 *  resources to be able to emit a proper warning.
	 */
	Resource->GetParentDebugData().PassAccessCount++;
}

void FRDGUserValidation::RemoveUnusedWarning(FRDGParentResourceRef Resource)
{
	check(Resource);
	ExecuteGuard(TEXT("RemoveUnusedResourceWarning"), Resource->Name);

	// Removes 'produced but not used' warning.
	Resource->GetParentDebugData().PassAccessCount++;

	// Removes 'not used' warning.
	Resource->GetDebugData().bIsActuallyUsedByPass = true;
}

bool FRDGUserValidation::TryMarkForClobber(FRDGParentResourceRef Resource) const
{
	check(Resource);
	FRDGParentResourceDebugData& DebugData = Resource->GetParentDebugData();

	const bool bClobber = !DebugData.bHasBeenClobbered && !Resource->bExternal && IsDebugAllowedForResource(Resource->Name);

	if (bClobber)
	{
		DebugData.bHasBeenClobbered = true;
	}

	return bClobber;
}

void FRDGUserValidation::ValidateGetPooledTexture(FRDGTextureRef Texture) const
{
	check(Texture);
	checkf(Texture->bExternal, TEXT("GetPooledTexture called on texture %s, but it is not external. Call PreallocateTexture or register as an external texture instead."), Texture->Name);
}

void FRDGUserValidation::ValidateGetPooledBuffer(FRDGBufferRef Buffer) const
{
	check(Buffer);
	checkf(Buffer->bExternal, TEXT("GetPooledBuffer called on buffer %s, but it is not external. Call PreallocateBuffer or register as an external buffer instead."), Buffer->Name);
}

void FRDGUserValidation::ValidateSetTextureAccessFinal(FRDGTextureRef Texture, ERHIAccess AccessFinal)
{
	check(Texture);
	check(AccessFinal != ERHIAccess::Unknown && IsValidAccess(AccessFinal));
	checkf(Texture->bExternal || Texture->bExtracted, TEXT("Cannot set final access on nont-external texture '%s' unless it is first extracted."), Texture->Name);
}

void FRDGUserValidation::ValidateSetBufferAccessFinal(FRDGBufferRef Buffer, ERHIAccess AccessFinal)
{
	check(Buffer);
	check(AccessFinal != ERHIAccess::Unknown && IsValidAccess(AccessFinal));
	checkf(Buffer->bExternal || Buffer->bExtracted, TEXT("Cannot set final access on nont-external buffer '%s' unless it is first extracted."), Buffer->Name);
}

void FRDGUserValidation::ValidateAddPass(const FRDGEventName& Name, ERDGPassFlags Flags)
{
	ExecuteGuard(TEXT("AddPass"), Name.GetTCHAR());

	checkf(!EnumHasAnyFlags(Flags, ERDGPassFlags::Copy | ERDGPassFlags::Compute | ERDGPassFlags::AsyncCompute | ERDGPassFlags::Raster),
		TEXT("Pass %s may not specify any of the (Copy, Compute, AsyncCompute, Raster) flags, because it has no parameters. Use None instead."), Name.GetTCHAR());
}

void FRDGUserValidation::ValidateAddPass(const void* ParameterStruct, const FShaderParametersMetadata* Metadata, const FRDGEventName& Name, ERDGPassFlags Flags)
{
	checkf(ParameterStruct, TEXT("Pass '%s' created with null parameters."), Name.GetTCHAR());
	ExecuteGuard(TEXT("AddPass"), Name.GetTCHAR());

	checkf(EnumHasAnyFlags(Flags, ERDGPassFlags::CommandMask),
		TEXT("Pass %s must specify at least one of the following flags: (Copy, Compute, AsyncCompute, Raster)"), Name.GetTCHAR());

	checkf(!EnumHasAllFlags(Flags, ERDGPassFlags::Compute | ERDGPassFlags::AsyncCompute),
		TEXT("Pass %s specified both Compute and AsyncCompute. They are mutually exclusive."), Name.GetTCHAR());

	checkf(!EnumHasAllFlags(Flags, ERDGPassFlags::Raster | ERDGPassFlags::AsyncCompute),
		TEXT("Pass %s specified both Raster and AsyncCompute. They are mutually exclusive."), Name.GetTCHAR());

	checkf(!EnumHasAllFlags(Flags, ERDGPassFlags::SkipRenderPass) || EnumHasAllFlags(Flags, ERDGPassFlags::Raster),
		TEXT("Pass %s specified SkipRenderPass without Raster. Only raster passes support this flag."));
}

void FRDGUserValidation::ValidateAddPass(const FRDGPass* Pass, bool bSkipPassAccessMarking)
{
	MemStackGuard();

	const FRenderTargetBindingSlots* RenderTargetBindingSlots = nullptr;

	// Pass flags are validated as early as possible by the builder in AddPass.
	const ERDGPassFlags PassFlags = Pass->GetFlags();
	const FRDGParameterStruct PassParameters = Pass->GetParameters();


	const TCHAR* PassName = Pass->GetName();
	const bool bIsRaster = EnumHasAnyFlags(PassFlags, ERDGPassFlags::Raster);
	const bool bIsCopy = EnumHasAnyFlags(PassFlags, ERDGPassFlags::Copy);
	const bool bIsAnyCompute = EnumHasAnyFlags(PassFlags, ERDGPassFlags::Compute | ERDGPassFlags::AsyncCompute);
	const bool bSkipRenderPass = EnumHasAnyFlags(PassFlags, ERDGPassFlags::SkipRenderPass);

	const auto MarkAsProduced = [&](FRDGParentResourceRef Resource)
	{
		if (!bSkipPassAccessMarking)
		{
			auto& Debug = Resource->GetParentDebugData();
			if (!Debug.FirstProducer)
			{
				Debug.FirstProducer = Pass;
			}
			Debug.PassAccessCount++;
		}
	};

	const auto MarkTextureAsProduced = [&](FRDGTextureRef Texture)
	{
		checkf(!EnumHasAnyFlags(Texture->Flags, ERDGTextureFlags::ReadOnly), TEXT("Pass %s is attempting to write to texture %s which is marked as ReadOnly."), Pass->GetName(), Texture->Name);
		MarkAsProduced(Texture);
	};

	const auto MarkBufferAsProduced = [&](FRDGBufferRef Buffer)
	{
		checkf(!EnumHasAnyFlags(Buffer->Flags, ERDGBufferFlags::ReadOnly), TEXT("Pass %s is attempting to write to buffer %s which is marked as ReadOnly."), Pass->GetName(), Buffer->Name);
		MarkAsProduced(Buffer);
	};

	const auto MarkAsConsumed = [&] (FRDGParentResourceRef Resource)
	{
		ensureMsgf(Resource->bProduced || Resource->bExternal,
			TEXT("Pass %s has a read dependency on %s, but it was never written to."),
			PassName, Resource->Name);

		if (!bSkipPassAccessMarking)
		{
			Resource->GetParentDebugData().PassAccessCount++;
		}
	};

	const auto CheckNotPassthrough = [&](FRDGParentResourceRef Resource)
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
				MarkTextureAsProduced(Texture);
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
				MarkBufferAsProduced(Buffer);
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
				checkf(bIsCopy       || !EnumHasAnyFlags(Access, AccessMaskCopy),    TEXT("Pass '%s' uses texture '%s' with access '%s' containing states which require the 'ERDGPass::Copy' flag."), Pass->GetName(), Texture->Name, *GetRHIAccessName(Access));
				checkf(bIsAnyCompute || !EnumHasAnyFlags(Access, AccessMaskCompute), TEXT("Pass '%s' uses texture '%s' with access '%s' containing states which require the 'ERDGPass::Compute' or 'ERDGPassFlags::AsyncCompute' flag."), Pass->GetName(), Texture->Name, *GetRHIAccessName(Access));
				checkf(bIsRaster     || !EnumHasAnyFlags(Access, AccessMaskRaster),  TEXT("Pass '%s' uses texture '%s' with access '%s' containing states which require the 'ERDGPass::Raster' flag."), Pass->GetName(), Texture->Name, *GetRHIAccessName(Access));
				checkf(bIsAnyCompute || bIsRaster || !EnumHasAnyFlags(Access, AccessMaskComputeOrRaster), TEXT("Pass '%s' uses texture '%s' with access '%s' containing states which require the 'ERDGPassFlags::Compute' or 'ERDGPassFlags::AsyncCompute' or 'ERDGPass::Raster' flag."), Pass->GetName(), Texture->Name, *GetRHIAccessName(Access));

				if (bIsWriteAccess)
				{
					MarkTextureAsProduced(Texture);
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
				checkf(bIsCopy       || !EnumHasAnyFlags(Access, AccessMaskCopy),    TEXT("Pass '%s' uses buffer '%s' with access '%s' containing states which require the 'ERDGPass::Copy' flag."), Pass->GetName(), Buffer->Name, *GetRHIAccessName(Access));
				checkf(bIsAnyCompute || !EnumHasAnyFlags(Access, AccessMaskCompute), TEXT("Pass '%s' uses buffer '%s' with access '%s' containing states which require the 'ERDGPass::Compute' or 'ERDGPassFlags::AsyncCompute' flag."), Pass->GetName(), Buffer->Name, *GetRHIAccessName(Access));
				checkf(bIsRaster     || !EnumHasAnyFlags(Access, AccessMaskRaster),  TEXT("Pass '%s' uses buffer '%s' with access '%s' containing states which require the 'ERDGPass::Raster' flag."), Pass->GetName(), Buffer->Name, *GetRHIAccessName(Access));
				checkf(bIsAnyCompute || bIsRaster || !EnumHasAnyFlags(Access, AccessMaskComputeOrRaster), TEXT("Pass '%s' uses buffer '%s' with access '%s' containing states which require the 'ERDGPassFlags::Compute' or 'ERDGPassFlags::AsyncCompute' or 'ERDGPass::Raster' flag."), Pass->GetName(), Buffer->Name, *GetRHIAccessName(Access));

				if (IsWritableAccess(BufferAccess.GetAccess()))
				{
					MarkBufferAsProduced(Buffer);
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
			const auto& DepthStencil = RenderTargetBindingSlots->DepthStencil;

			const auto CheckDepthStencil = [&](FRDGTextureRef Texture)
			{
				// Depth stencil only supports one mip, since there isn't actually a way to select the mip level.
				check(Texture->Desc.NumMips == 1);
				CheckNotPassthrough(Texture);
				if (DepthStencil.GetDepthStencilAccess().IsAnyWrite())
				{
					MarkTextureAsProduced(Texture);
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
			uint32 ValidRenderTargetCount = 0;

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
					MarkTextureAsProduced(ResolveTexture);
				}

				if (Texture)
				{
					ensureMsgf(
						EnumHasAnyFlags(Texture->Desc.Flags, TexCreate_RenderTargetable | TexCreate_ResolveTargetable),
						TEXT("Pass '%s' attempted to bind texture '%s' as a render target, but the texture has not been created with TexCreate_RenderTargetable."),
						PassName, Texture->Name);

					CheckNotPassthrough(Texture);

					/** Validate that any previously produced texture contents are loaded. This occurs if the user failed to specify a load action
					 *  on a texture that was produced by a previous pass, effectively losing that data. This can also happen if the user 're-uses'
					 *  a texture for some other purpose. The latter is considered bad practice, since it increases memory pressure on the render
					 *  target pool. Instead, the user should create a new texture instance. An exception to this rule are untracked render targets,
					 *  which are not actually managed by the render target pool and likely represent the frame buffer.
					 */
					{
						const bool bIsLoadAction = RenderTarget.GetLoadAction() == ERenderTargetLoadAction::ELoad;

						// Ignore external textures which are always marked as produced. We don't need to enforce this warning on them.
						const bool bHasBeenProduced = Texture->bProduced && !Texture->bExternal;

						// We only validate single-mip textures since we don't track production at the subresource level.
						const bool bFailedToLoadProducedContent = !bIsLoadAction && bHasBeenProduced && Texture->Desc.NumMips == 1;

						// Untracked render targets aren't actually managed by the render target pool.
						const bool bIsUntrackedRenderTarget = Texture->PooledRenderTarget && !Texture->PooledRenderTarget->IsTracked();

						// In multi-gpu, when running with "r.EnableMultiGPUForkAndJoin", it's possible for each GPU to clear the same RT in turn.
						// When this happens, they are not actually working on the same resource, see for example the implementation of FD3D12MultiNodeGPUObject.
						ensureMsgf((!bFailedToLoadProducedContent || bIsUntrackedRenderTarget) || (GNumExplicitGPUsForRendering > 1 && RenderTarget.GetLoadAction() == ERenderTargetLoadAction::EClear),
							TEXT("Pass '%s' attempted to bind texture '%s' as a render target without the 'Load' action specified, despite a prior pass having produced it. It's invalid to completely clobber the contents of a resource. Create a new texture instance instead."),
							PassName,
							Texture->Name);
					}

					/** Mark the pass as a producer for render targets with a store action. */
					MarkTextureAsProduced(Texture);
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
	MemStackGuard();
	checkf(!bHasExecuted, TEXT("Render graph execution should only happen once to ensure consistency with immediate mode."));
}

void FRDGUserValidation::ValidateExecuteEnd()
{
	MemStackGuard();

	bHasExecuted = true;

	if (GRDGDebug)
	{
		auto ValidateResourceAtExecuteEnd = [](const FRDGParentResourceRef Resource)
		{
			check(Resource->ReferenceCount == 0);

			const auto& ParentDebugData = Resource->GetParentDebugData();
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

			const auto& ParentDebugData = Texture->GetParentDebugData();
			const auto& TextureDebugData = Texture->GetTextureDebugData();

			const bool bHasBeenProducedByGraph = !Texture->bExternal && ParentDebugData.PassAccessCount > 0;

			if (bHasBeenProducedByGraph && !TextureDebugData.bHasNeededUAV && EnumHasAnyFlags(Texture->Desc.Flags, TexCreate_UAV))
			{
				EmitRDGWarningf(
					TEXT("Resource %s first produced by the pass %s had the TexCreate_UAV flag, but no UAV has been used."),
					Texture->Name, ParentDebugData.FirstProducer->GetName());
			}

			if (bHasBeenProducedByGraph && !TextureDebugData.bHasBeenBoundAsRenderTarget && EnumHasAnyFlags(Texture->Desc.Flags, TexCreate_RenderTargetable))
			{
				EmitRDGWarningf(
					TEXT("Resource %s first produced by the pass %s had the TexCreate_RenderTargetable flag, but has never been bound as a render target of a pass."),
					Texture->Name, ParentDebugData.FirstProducer->GetName());
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
					Texture->GetTextureDebugData().bHasNeededUAV = true;
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
						Texture->GetTextureDebugData().bHasNeededUAV = true;
					}
					if (EnumHasAnyFlags(Access, ERHIAccess::RTV | ERHIAccess::DSVRead | ERHIAccess::DSVWrite))
					{
						Texture->GetTextureDebugData().bHasBeenBoundAsRenderTarget = true;
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
					Texture->GetTextureDebugData().bHasBeenBoundAsRenderTarget = true;
					Texture->MarkResourceAsUsed();
				});

				if (FRDGTextureRef Texture = RenderTargets.DepthStencil.GetTexture())
				{
					Texture->GetTextureDebugData().bHasBeenBoundAsRenderTarget = true;
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
					UsedResourceCount += Resource->GetDebugData().bIsActuallyUsedByPass ? 1 : 0;
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
						if (!Resource->GetDebugData().bIsActuallyUsedByPass)
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
				Resource->GetDebugData().bIsActuallyUsedByPass = false;
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
				Resource->GetDebugData().bAllowRHIAccess = bAllowAccess;
			}
		}
		else if (Parameter.IsRenderTargetBindingSlots())
		{
			const FRenderTargetBindingSlots& RenderTargets = Parameter.GetAsRenderTargetBindingSlots();

			RenderTargets.Enumerate([&](FRenderTargetBinding RenderTarget)
			{
				RenderTarget.GetTexture()->GetDebugData().bAllowRHIAccess = bAllowAccess;

				if (FRDGTextureRef ResolveTexture = RenderTarget.GetResolveTexture())
				{
					ResolveTexture->GetDebugData().bAllowRHIAccess = bAllowAccess;
				}
			});

			if (FRDGTextureRef Texture = RenderTargets.DepthStencil.GetTexture())
			{
				Texture->GetDebugData().bAllowRHIAccess = bAllowAccess;
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
	if (!GRDGTransitionLog)
	{
		return;
	}

	FResourceMap* ResourceMap = BatchMap.Find(&Batch);

	if (!ResourceMap)
	{
		ResourceMap = &BatchMap.Emplace(&Batch);

		for (int32 Index = 0; Index < Batch.Transitions.Num(); ++Index)
		{
			FRDGParentResourceRef Resource = Batch.DebugResources[Index];
			const FRHITransitionInfo& Transition = Batch.Transitions[Index];

			if (Resource->Type == ERDGParentResourceType::Texture)
			{
				ResourceMap->Textures.FindOrAdd(static_cast<FRDGTextureRef>(Resource)).Add(Transition);
			}
			else
			{
				check(Resource->Type == ERDGParentResourceType::Buffer);
				ResourceMap->Buffers.Emplace(static_cast<FRDGBufferRef>(Resource), Transition);
			}
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
			UE_CLOG(bAllowedForPass, LogRDG, Display, TEXT("[%s(Index: %d, Pipeline: %s): %s] (Begin):"), Pass->GetName(), Pass->GetHandle().GetIndex(), *GetRHIPipelineName(Pass->GetPipeline()), Batch.DebugName);
		}
	};

	for (const auto& Pair : ResourceMap->Textures)
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

				UE_CLOG(bAllowedForResource, LogRDG, Display, TEXT("\t\tMip(%d), Array(%d), Slice(%d): [%s, %s] -> [%s, %s]"),
					Subresource.MipIndex, Subresource.ArraySlice, Subresource.PlaneSlice,
					*GetRHIAccessName(Transition.AccessBefore),
					*GetRHIPipelineName(Batch.DebugPipelinesToBegin),
					*GetRHIAccessName(Transition.AccessAfter),
					*GetRHIPipelineName(Batch.DebugPipelinesToEnd));
			});
		}
	}

	if (bAllowedForPass)
	{
		for (const auto& Pair : ResourceMap->Buffers)
		{
			FRDGBufferRef Buffer = Pair.Key;
			const FRHITransitionInfo& Transition = Pair.Value;

			if (!IsDebugAllowedForResource(Buffer->Name))
			{
				continue;
			}

			LogHeader();

			UE_LOG(LogRDG, Display, TEXT("\t(%p) %s: [%s, %s] -> [%s, %s]"),
				Buffer,
				Buffer->Name,
				*GetRHIAccessName(Transition.AccessBefore),
				*GetRHIPipelineName(Batch.DebugPipelinesToBegin),
				*GetRHIAccessName(Transition.AccessAfter),
				*GetRHIPipelineName(Batch.DebugPipelinesToEnd));
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
		if (Dependent->PipelinesToEnd == ERHIPipeline::None)
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
				UE_LOG(LogRDG, Display, TEXT("[%s(Index: %d, Pipeline: %s) %s] (End):"), Pass->GetName(), Pass->GetHandle().GetIndex(), Dependent->DebugName, *GetRHIPipelineName(Pass->GetPipeline()));
				bFoundFirstBatch = true;
			}
		}

		for (FRDGTextureRef Texture : Textures)
		{
			if (IsDebugAllowedForResource(Texture->Name))
			{
				UE_LOG(LogRDG, Display, TEXT("\t(%p) %s"), Texture, Texture->Name);
			}
		}

		for (FRDGBufferRef Buffer : Buffers)
		{
			if (IsDebugAllowedForResource(Buffer->Name))
			{
				UE_LOG(LogRDG, Display, TEXT("\t(%p) %s"), Buffer, Buffer->Name);
			}
		}
	}
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
	const TCHAR* AllPipelinesColorName = TEXT("#f170ff");

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
		const ERHIPipeline Pipelines = State.GetPipelines();
		const TCHAR* FontColor = nullptr;
		switch (Pipelines)
		{
		default:
			checkNoEntry();
		case ERHIPipeline::Graphics:
			FontColor = RasterColorName;
			break;
		case ERHIPipeline::AsyncCompute:
			FontColor = AsyncComputeColorName;
			break;
		case ERHIPipeline::All:
			FontColor = AllPipelinesColorName;
			break;
		}
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
	if (GRDGDumpGraph == RDG_DUMP_GRAPH_RESOURCES && bOpen && IncludeTransitionEdgeInGraph(StateBefore.GetFirstPass(), PassHandle))
	{
		if (FRDGSubresourceState::IsTransitionRequired(StateBefore, StateAfter))
		{
			AddLine(FString::Printf(TEXT("\"%s\" -> \"%s\" [%s, label=<%s: <b>%s -&gt; %s</b>>]"),
				*GetProducerName(StateBefore.GetLastPass()),
				*GetConsumerName(StateAfter.GetFirstPass()),
				TextureColorAttributes,
				Texture->Name,
				*GetSubresourceStateLabel(StateBefore),
				*GetSubresourceStateLabel(StateAfter)));
		}
		else
		{
			AddLine(FString::Printf(TEXT("\"%s\" -> \"%s\" [%s, label=<%s: <b>%s</b>>]"),
				*GetProducerName(StateBefore.GetFirstPass()),
				*GetConsumerName(PassHandle),
				TextureColorAttributes,
				Texture->Name,
				*GetSubresourceStateLabel(StateBefore)));
		}
	}
}

void FRDGLogFile::AddTransitionEdge(FRDGPassHandle PassHandle, FRDGSubresourceState StateBefore, FRDGSubresourceState StateAfter, const FRDGTextureRef Texture, FRDGTextureSubresource Subresource)
{
	if (GRDGDumpGraph == RDG_DUMP_GRAPH_RESOURCES && bOpen && IncludeTransitionEdgeInGraph(StateBefore.GetFirstPass(), PassHandle))
	{
		if (FRDGSubresourceState::IsTransitionRequired(StateBefore, StateAfter))
		{
			AddLine(FString::Printf(TEXT("\"%s\" -> \"%s\" [%s, label=<%s[%d][%d][%d]: <b>%s -&gt; %s</b>>]"),
				*GetProducerName(StateBefore.GetLastPass()),
				*GetConsumerName(StateAfter.GetFirstPass()),
				TextureColorAttributes,
				Texture->Name,
				Subresource.MipIndex, Subresource.ArraySlice, Subresource.PlaneSlice,
				*GetSubresourceStateLabel(StateBefore),
				*GetSubresourceStateLabel(StateAfter)));
		}
		else
		{
			AddLine(FString::Printf(TEXT("\"%s\" -> \"%s\" [%s, label=<%s[%d][%d][%d]: <b>%s</b>>]"),
				*GetProducerName(StateBefore.GetFirstPass()),
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
	if (GRDGDumpGraph == RDG_DUMP_GRAPH_RESOURCES && bOpen && IncludeTransitionEdgeInGraph(StateBefore.GetFirstPass(), PassHandle))
	{
		if (FRDGSubresourceState::IsTransitionRequired(StateBefore, StateAfter))
		{
			AddLine(FString::Printf(TEXT("\"%s\" -> \"%s\" [%s, label=<%s: <b>%s -&gt; %s</b>>]"),
				*GetProducerName(StateBefore.GetLastPass()),
				*GetConsumerName(StateAfter.GetFirstPass()),
				BufferColorAttributes,
				Buffer->Name,
				*GetSubresourceStateLabel(StateBefore),
				*GetSubresourceStateLabel(StateAfter)));
		}
		else
		{
			AddLine(FString::Printf(TEXT("\"%s\" -> \"%s\" [%s, label=<%s: <b>%s</b>>]"),
				*GetProducerName(StateBefore.GetFirstPass()),
				*GetConsumerName(PassHandle),
				BufferColorAttributes,
				Buffer->Name,
				*GetSubresourceStateLabel(StateBefore)));
		}
	}
}

#endif