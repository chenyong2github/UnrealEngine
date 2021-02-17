// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

inline FRDGTextureRef FRDGBuilder::FindExternalTexture(FRHITexture* ExternalTexture) const
{
	if (const FRDGTextureRef* FoundTexturePtr = ExternalTextures.Find(ExternalTexture))
	{
		return *FoundTexturePtr;
	}
	return nullptr;
}

inline FRDGTextureRef FRDGBuilder::FindExternalTexture(IPooledRenderTarget* ExternalTexture, ERenderTargetTexture Texture) const
{
	if (ExternalTexture)
	{
		return FindExternalTexture(ExternalTexture->GetRenderTargetItem().GetRHI(Texture));
	}
	return nullptr;
}

inline FRDGBufferRef FRDGBuilder::CreateBuffer(
	const FRDGBufferDesc& Desc,
	const TCHAR* Name,
	ERDGBufferFlags Flags)
{
#if RDG_ENABLE_DEBUG
	{
		checkf(Name, TEXT("Creating a buffer requires a valid debug name."));
		UserValidation.ExecuteGuard(TEXT("CreateBuffer"), Name);

		checkf(Desc.GetTotalNumBytes() > 0, TEXT("Creating buffer '%s' is zero bytes in size."), Name);

		const bool bIsByteAddress = (Desc.Usage & BUF_ByteAddressBuffer) == BUF_ByteAddressBuffer;

		if (bIsByteAddress && Desc.UnderlyingType == FRDGBufferDesc::EUnderlyingType::StructuredBuffer)
		{
			checkf(Desc.BytesPerElement == 4, TEXT("Creating buffer '%s' as a structured buffer that is also byte addressable, BytesPerElement must be 4! Instead it is %d"), Name, Desc.BytesPerElement);
		}
	}
#endif

	FRDGBufferRef Buffer = Buffers.Allocate(Allocator, Name, Desc, Flags);
	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateCreateBuffer(Buffer));
	return Buffer;
}

inline FRDGTextureSRVRef FRDGBuilder::CreateSRV(const FRDGTextureSRVDesc& Desc)
{
	FRDGTextureRef Texture = Desc.Texture;

#if RDG_ENABLE_DEBUG
	{
		checkf(Texture, TEXT("Texture SRV created with a null texture."));
		checkf(!Texture->IsPassthrough(), TEXT("Texture SRV created with passthrough texture '%s'."), Texture->Name);
		UserValidation.ExecuteGuard(TEXT("CreateSRV"), Texture->Name);
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
#endif

	return Views.Allocate<FRDGTextureSRV>(Allocator, Texture->Name, Desc);
}

inline FRDGBufferSRVRef FRDGBuilder::CreateSRV(const FRDGBufferSRVDesc& Desc)
{
	FRDGBufferRef Buffer = Desc.Buffer;

#if RDG_ENABLE_DEBUG
	{
		checkf(Buffer, TEXT("Buffer SRV created with a null buffer."));
		UserValidation.ExecuteGuard(TEXT("CreateSRV"), Buffer->Name);
	}
#endif

	return Views.Allocate<FRDGBufferSRV>(Allocator, Buffer->Name, Desc);
}

inline FRDGTextureUAVRef FRDGBuilder::CreateUAV(const FRDGTextureUAVDesc& Desc, ERDGUnorderedAccessViewFlags InFlags)
{
	FRDGTextureRef Texture = Desc.Texture;

#if RDG_ENABLE_DEBUG
	{
		checkf(Texture, TEXT("Texture UAV created with a null texture."));
		checkf(!Texture->IsPassthrough(), TEXT("Texture UAV created with passthrough texture '%s'."), Texture->Name);
		UserValidation.ExecuteGuard(TEXT("CreateUAV"), Texture->Name);
		checkf(Texture->Desc.Flags & TexCreate_UAV, TEXT("Attempted to create UAV from texture %s which was not created with TexCreate_UAV"), Texture->Name);
		checkf(Desc.MipLevel < Texture->Desc.NumMips, TEXT("Failed to create UAV at mip %d: the texture %s has only %d mip levels."), Desc.MipLevel, Texture->Name, Texture->Desc.NumMips);
	}
#endif

	return Views.Allocate<FRDGTextureUAV>(Allocator, Texture->Name, Desc, InFlags);
}

inline FRDGBufferUAVRef FRDGBuilder::CreateUAV(const FRDGBufferUAVDesc& Desc, ERDGUnorderedAccessViewFlags InFlags)
{
	FRDGBufferRef Buffer = Desc.Buffer;

#if RDG_ENABLE_DEBUG
	{
		checkf(Buffer, TEXT("Buffer UAV created with a null buffer."));
		UserValidation.ExecuteGuard(TEXT("CreateUAV"), Buffer->Name);
	}
#endif

	return Views.Allocate<FRDGBufferUAV>(Allocator, Buffer->Name, Desc, InFlags);
}

FORCEINLINE void* FRDGBuilder::Alloc(uint32 SizeInBytes, uint32 AlignInBytes)
{
	return Allocator.Alloc(SizeInBytes, AlignInBytes);
}

template <typename PODType>
FORCEINLINE PODType* FRDGBuilder::AllocPOD()
{
	return Allocator.AllocPOD<PODType>();
}

template <typename ObjectType, typename... TArgs>
FORCEINLINE ObjectType* FRDGBuilder::AllocObject(TArgs&&... Args)
{
	return Allocator.AllocObject<ObjectType>(Forward<TArgs&&>(Args)...);
}

template <typename ParameterStructType>
FORCEINLINE ParameterStructType* FRDGBuilder::AllocParameters()
{
	return Allocator.AllocObject<ParameterStructType>();
}

FORCEINLINE FRDGSubresourceState* FRDGBuilder::AllocSubresource(const FRDGSubresourceState& Other)
{
	FRDGSubresourceState* State = Allocator.AllocPOD<FRDGSubresourceState>();
	*State = Other;
	return State;
}

template <typename ParameterStructType>
TRDGUniformBufferRef<ParameterStructType> FRDGBuilder::CreateUniformBuffer(ParameterStructType* ParameterStruct)
{
	const TCHAR* Name = ParameterStructType::StaticStructMetadata.GetShaderVariableName();

#if RDG_ENABLE_DEBUG
	{
		checkf(ParameterStruct, TEXT("Uniform buffer '%s' created with null parameters."), Name);
		UserValidation.ExecuteGuard(TEXT("CreateUniformBuffer"), Name);
	}
#endif

	return UniformBuffers.Allocate<TRDGUniformBuffer<ParameterStructType>>(Allocator, ParameterStruct, Name);
}

template <typename ExecuteLambdaType>
FRDGPassRef FRDGBuilder::AddPass(
	FRDGEventName&& Name,
	ERDGPassFlags Flags,
	ExecuteLambdaType&& ExecuteLambda)
{
	using LambdaPassType = TRDGEmptyLambdaPass<ExecuteLambdaType>;

#if RDG_ENABLE_DEBUG
	{
		UserValidation.ExecuteGuard(TEXT("AddPass"), Name.GetTCHAR());

		checkf(!EnumHasAnyFlags(Flags, ERDGPassFlags::Copy | ERDGPassFlags::Compute | ERDGPassFlags::AsyncCompute | ERDGPassFlags::Raster),
			TEXT("Pass %s may not specify any of the (Copy, Compute, AsyncCompute, Raster) flags, because it has no parameters. Use None instead."), Name.GetTCHAR());
	}
#endif

	Flags |= ERDGPassFlags::NeverCull;

	LambdaPassType* Pass = Passes.Allocate<LambdaPassType>(Allocator, MoveTemp(Name), Flags, MoveTemp(ExecuteLambda));
	SetupEmptyPass(Pass);
	return Pass;
}

template <typename ParameterStructType, typename ExecuteLambdaType>
FRDGPassRef FRDGBuilder::AddPass(
	FRDGEventName&& Name,
	const ParameterStructType* ParameterStruct,
	ERDGPassFlags Flags,
	ExecuteLambdaType&& ExecuteLambda)
{
	using LambdaPassType = TRDGLambdaPass<ParameterStructType, ExecuteLambdaType>;

#if RDG_ENABLE_DEBUG
	{
		checkf(ParameterStruct, TEXT("Pass '%s' created with null parameters."), Name.GetTCHAR());
		UserValidation.ExecuteGuard(TEXT("AddPass"), Name.GetTCHAR());

		checkf(EnumHasAnyFlags(Flags, ERDGPassFlags::CommandMask),
			TEXT("Pass %s must specify at least one of the following flags: (Copy, Compute, AsyncCompute, Raster)"), Name.GetTCHAR());

		checkf(!EnumHasAllFlags(Flags, ERDGPassFlags::Compute | ERDGPassFlags::AsyncCompute),
			TEXT("Pass %s specified both Compute and AsyncCompute. They are mutually exclusive."), Name.GetTCHAR());

		checkf(!EnumHasAllFlags(Flags, ERDGPassFlags::Raster | ERDGPassFlags::AsyncCompute),
			TEXT("Pass %s specified both Raster and AsyncCompute. They are mutually exclusive."), Name.GetTCHAR());

		checkf(!EnumHasAllFlags(Flags, ERDGPassFlags::SkipRenderPass) || EnumHasAllFlags(Flags, ERDGPassFlags::Raster),
			TEXT("Pass %s specified SkipRenderPass without Raster. Only raster passes support this flag."));
	}
#endif

	FRDGPass* Pass = Allocator.AllocObject<LambdaPassType>(
		MoveTemp(Name),
		ParameterStruct,
		OverridePassFlags(Name.GetTCHAR(), Flags, LambdaPassType::kSupportsAsyncCompute),
		MoveTemp(ExecuteLambda));

	IF_RDG_ENABLE_DEBUG(ClobberPassOutputs(Pass));
	Passes.Insert(Pass);
	SetupPass(Pass);
	return Pass;
}

inline void FRDGBuilder::QueueTextureExtraction(
	FRDGTextureRef Texture,
	TRefCountPtr<IPooledRenderTarget>* OutTexturePtr,
	bool bTransitionToRead)
{
	QueueTextureExtraction(Texture, OutTexturePtr, bTransitionToRead ? kDefaultAccessFinal : ERHIAccess::Unknown);
}

inline void FRDGBuilder::QueueTextureExtraction(FRDGTextureRef Texture, TRefCountPtr<IPooledRenderTarget>* OutTexturePtr, ERHIAccess AccessFinal)
{
	QueueTextureExtraction(Texture, OutTexturePtr);
	SetTextureAccessFinal(Texture, AccessFinal);
}

inline void FRDGBuilder::QueueTextureExtraction(FRDGTextureRef Texture, TRefCountPtr<IPooledRenderTarget>* OutTexturePtr)
{
#if RDG_ENABLE_DEBUG
	check(OutTexturePtr);
	UserValidation.ValidateExtractResource(Texture);
#endif

	Texture->bExtracted = true;
	Texture->bCulled = false;
	ExtractedTextures.Emplace(Texture, OutTexturePtr);

	if (Texture->AccessFinal == ERHIAccess::Unknown)
	{
		Texture->AccessFinal = kDefaultAccessFinal;
	}
}

inline void FRDGBuilder::QueueBufferExtraction(FRDGBufferRef Buffer, TRefCountPtr<FRDGPooledBuffer>* OutBufferPtr)
{
#if RDG_ENABLE_DEBUG
	check(OutBufferPtr);
	UserValidation.ValidateExtractResource(Buffer);
#endif

	Buffer->bExtracted = true;
	Buffer->bCulled = false;
	ExtractedBuffers.Emplace(Buffer, OutBufferPtr);

	if (Buffer->AccessFinal == ERHIAccess::Unknown)
	{
		Buffer->AccessFinal = kDefaultAccessFinal;
	}
}

inline void FRDGBuilder::QueueBufferExtraction(FRDGBufferRef Buffer, TRefCountPtr<FRDGPooledBuffer>* OutBufferPtr, ERHIAccess AccessFinal)
{
	QueueBufferExtraction(Buffer, OutBufferPtr);
	SetBufferAccessFinal(Buffer, AccessFinal);
}

inline const TRefCountPtr<IPooledRenderTarget>& FRDGBuilder::GetPooledTexture(FRDGTextureRef Texture) const
{
#if RDG_ENABLE_DEBUG
	check(Texture);
	checkf(Texture->bExternal, TEXT("GetPooledTexture called on texture %s, but it is not external. Call PreallocateTexture or register as an external texture instead."), Texture->Name);
#endif

	return Texture->Allocation;
}

inline const TRefCountPtr<FRDGPooledBuffer>& FRDGBuilder::GetPooledBuffer(FRDGBufferRef Buffer) const
{
#if RDG_ENABLE_DEBUG
	check(Buffer);
	checkf(Buffer->bExternal, TEXT("GetPooledBuffer called on buffer %s, but it is not external. Call PreallocateBuffer or register as an external buffer instead."), Buffer->Name);
#endif

	return Buffer->Allocation;
}

inline void FRDGBuilder::SetTextureAccessFinal(FRDGTextureRef Texture, ERHIAccess AccessFinal)
{
#if RDG_ENABLE_DEBUG
	check(Texture);
	check(AccessFinal != ERHIAccess::Unknown && IsValidAccess(AccessFinal));
	checkf(Texture->bExternal || Texture->bExtracted, TEXT("Cannot set final access on nont-external texture '%s' unless it is first extracted."), Texture->Name);
#endif

	Texture->AccessFinal = AccessFinal;
}

inline void FRDGBuilder::SetBufferAccessFinal(FRDGBufferRef Buffer, ERHIAccess AccessFinal)
{
#if RDG_ENABLE_DEBUG
	check(Buffer);
	check(AccessFinal != ERHIAccess::Unknown && IsValidAccess(AccessFinal));
	checkf(Buffer->bExternal || Buffer->bExtracted, TEXT("Cannot set final access on nont-external buffer '%s' unless it is first extracted."), Buffer->Name);
#endif

	Buffer->AccessFinal = AccessFinal;
}

inline void FRDGBuilder::RemoveUnusedTextureWarning(FRDGTextureRef Texture)
{
#if RDG_ENABLE_DEBUG
	check(Texture);
	UserValidation.ExecuteGuard(TEXT("RemoveUnusedTextureWarning"), Texture->Name);
	UserValidation.RemoveUnusedWarning(Texture);
#endif
}

inline void FRDGBuilder::RemoveUnusedBufferWarning(FRDGBufferRef Buffer)
{
#if RDG_ENABLE_DEBUG
	check(Buffer);
	UserValidation.ExecuteGuard(TEXT("RemoveUnusedBufferWarning"), Buffer->Name);
	UserValidation.RemoveUnusedWarning(Buffer);
#endif
}