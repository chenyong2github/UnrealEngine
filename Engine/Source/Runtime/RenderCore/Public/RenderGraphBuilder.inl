// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

inline FRDGTextureRef FRDGBuilder::CreateTexture(
	const FRDGTextureDesc& Desc,
	const TCHAR* Name,
	ERDGParentResourceFlags Flags)
{
#if RDG_ENABLE_DEBUG
	{
		checkf(Name, TEXT("Creating a render graph texture requires a valid debug name."));
		UserValidation.ExecuteGuard(TEXT("CreateTexture"), Name);

		// Validate the pixel format.
		checkf(Desc.Format != PF_Unknown, TEXT("Illegal to create texture %s with an invalid pixel format."), Name);
		checkf(Desc.Format < PF_MAX, TEXT("Illegal to create texture %s with invalid FPooledRenderTargetDesc::Format."), Name);
		checkf(GPixelFormats[Desc.Format].Supported, TEXT("Failed to create texture %s with pixel format %s because it is not supported."),
			Name, GPixelFormats[Desc.Format].Name);

		const bool bCanHaveUAV = Desc.TargetableFlags & TexCreate_UAV;
		const bool bIsMSAA = Desc.NumSamples > 1;

		// D3D11 doesn't allow creating a UAV on MSAA texture.
		const bool bIsUAVForMSAATexture = bIsMSAA && bCanHaveUAV;
		checkf(!bIsUAVForMSAATexture, TEXT("TexCreate_UAV is not allowed on MSAA texture %s."), Name);
	}
#endif

	FRDGTextureDesc TextureDesc = Desc;
	TextureDesc.DebugName = Name;
	TextureDesc.AutoWritable = false;

	FRDGTexture* Texture = AllocateResource<FRDGTexture>(Name, TextureDesc, Flags);
	TextureCount++;

	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateCreateTexture(Texture));

	return Texture;
}

inline FRDGBufferRef FRDGBuilder::CreateBuffer(
	const FRDGBufferDesc& Desc,
	const TCHAR* Name,
	ERDGParentResourceFlags Flags)
{
#if RDG_ENABLE_DEBUG
	{
		checkf(Name, TEXT("Creating a render graph buffer requires a valid debug name."));
		UserValidation.ExecuteGuard(TEXT("CreateBuffer"), Name);

		checkf(Desc.GetTotalNumBytes() > 0, TEXT("Creating buffer '%s' is zero bytes in size."), Name)

		const bool bIsByteAddress = (Desc.Usage & BUF_ByteAddressBuffer) == BUF_ByteAddressBuffer;

		if (bIsByteAddress && Desc.UnderlyingType == FRDGBufferDesc::EUnderlyingType::StructuredBuffer)
		{
			checkf(Desc.BytesPerElement == 4, TEXT("Creating buffer '%s' as a structured buffer that is also byte addressable, BytesPerElement must be 4! Instead it is %d"), Name, Desc.BytesPerElement);
		}
	}
#endif

	FRDGBufferRef Buffer = AllocateResource<FRDGBuffer>(Name, Desc, Flags);
	BufferCount++;

	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateCreateBuffer(Buffer));

	return Buffer;
}

inline FRDGTextureSRVRef FRDGBuilder::CreateSRV(const FRDGTextureSRVDesc& Desc)
{
	FRDGTextureRef Texture = Desc.Texture;

#if RDG_ENABLE_DEBUG
	{
		checkf(Texture, TEXT("RenderGraph texture SRV created with a null texture."));
		UserValidation.ExecuteGuard(TEXT("CreateSRV"), Texture->Name);
		checkf(Texture->Desc.TargetableFlags & TexCreate_ShaderResource, TEXT("Attempted to create SRV from texture %s which was not created with TexCreate_ShaderResource"), Desc.Texture->Name);
		
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
	}
#endif

	return AllocateResource<FRDGTextureSRV>(Texture->Name, Desc, ERDGChildResourceFlags::None);
}

inline FRDGBufferSRVRef FRDGBuilder::CreateSRV(const FRDGBufferSRVDesc& Desc)
{
	FRDGBufferRef Buffer = Desc.Buffer;

#if RDG_ENABLE_DEBUG
	{
		checkf(Buffer, TEXT("RenderGraph buffer SRV created with a null buffer."));
		UserValidation.ExecuteGuard(TEXT("CreateSRV"), Buffer->Name);
	}
#endif

	return AllocateResource<FRDGBufferSRV>(Buffer->Name, Desc, ERDGChildResourceFlags::None);
}

inline FRDGTextureUAVRef FRDGBuilder::CreateUAV(const FRDGTextureUAVDesc& Desc, ERDGChildResourceFlags InFlags)
{
	FRDGTextureRef Texture = Desc.Texture;

#if RDG_ENABLE_DEBUG
	{
		checkf(Texture, TEXT("RenderGraph texture UAV created with a null texture."));
		UserValidation.ExecuteGuard(TEXT("CreateUAV"), Texture->Name);
		checkf(Texture->Desc.TargetableFlags & TexCreate_UAV, TEXT("Attempted to create UAV from texture %s which was not created with TexCreate_UAV"), Texture->Name);
		checkf(Desc.MipLevel < Texture->Desc.NumMips, TEXT("Failed to create UAV at mip %d: the texture %s has only %d mip levels."), Desc.MipLevel, Texture->Name, Texture->Desc.NumMips);
	}
#endif

	return AllocateResource<FRDGTextureUAV>(Texture->Name, Desc, InFlags);
}

inline FRDGBufferUAVRef FRDGBuilder::CreateUAV(const FRDGBufferUAVDesc& Desc, ERDGChildResourceFlags InFlags)
{
	FRDGBufferRef Buffer = Desc.Buffer;

#if RDG_ENABLE_DEBUG
	{
		checkf(Buffer, TEXT("RenderGraph buffer UAV created with a null buffer."));
		UserValidation.ExecuteGuard(TEXT("CreateUAV"), Buffer->Name);
	}
#endif

	return AllocateResource<FRDGBufferUAV>(Buffer->Name, Desc, InFlags);
}

template <typename ParameterStructType>
ParameterStructType* FRDGBuilder::AllocParameters()
{
	ParameterStructType* OutParameterPtr = new(MemStack) ParameterStructType;
	FMemory::Memzero(OutParameterPtr, sizeof(ParameterStructType));
	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateAllocPassParameters(OutParameterPtr));
	return OutParameterPtr;
}

template <typename ParameterStructType, typename ExecuteLambdaType>
FRDGPassRef FRDGBuilder::AddPass(
	FRDGEventName&& Name,
	ParameterStructType* ParameterStruct,
	ERDGPassFlags Flags,
	ExecuteLambdaType&& ExecuteLambda)
{
	const bool bCompute = EnumHasAnyFlags(Flags, ERDGPassFlags::Compute);
	const bool bAsyncCompute = EnumHasAnyFlags(Flags, ERDGPassFlags::AsyncCompute);
	const bool bRaster = EnumHasAnyFlags(Flags, ERDGPassFlags::Raster);

	checkf(!EnumHasAllFlags(Flags, ERDGPassFlags::Compute | ERDGPassFlags::AsyncCompute),
		TEXT("Pass %s specified both Compute and AsyncCompute. They are mutually exclusive."), Name.GetTCHAR());

	checkf(!EnumHasAllFlags(Flags, ERDGPassFlags::Raster | ERDGPassFlags::AsyncCompute),
		TEXT("Pass %s specified both Raster and AsyncCompute. They are mutually exclusive."), Name.GetTCHAR());

	// Verify that the amount of stuff captured by a pass's lambda is reasonable. Changing this maximum as desired without review is not OK,
	// the all point being to catch things that should be captured by referenced instead of captured by copy.
	constexpr int32 kMaximumLambdaCaptureSize = 1024;
	static_assert(sizeof(ExecuteLambdaType) <= kMaximumLambdaCaptureSize, "The amount of data of captured for the pass looks abnormally high.");

	using LambdaPassType = TRDGLambdaPass<ParameterStructType, ExecuteLambdaType>;

	const TCHAR* NameTCHAR = Name.GetTCHAR();

	FRDGPass* NewPass = new(MemStack) LambdaPassType(
		MoveTemp(Name),
		FRDGPassParameterStruct(ParameterStruct),
		OverridePassFlags(NameTCHAR, Flags, LambdaPassType::kSupportsAsyncCompute),
		MoveTemp(ExecuteLambda));

	return AddPass(NewPass);
}

inline void FRDGBuilder::QueueTextureExtraction(
	FRDGTextureRef Texture,
	TRefCountPtr<IPooledRenderTarget>* OutTexturePtr,
	bool bTransitionToRead)
{
	QueueTextureExtraction(Texture, OutTexturePtr, bTransitionToRead ? EResourceTransitionAccess::EReadable : EResourceTransitionAccess::Unknown);
}

inline void FRDGBuilder::QueueTextureExtraction(
	FRDGTextureRef Texture,
	TRefCountPtr<IPooledRenderTarget>* OutTexturePtr,
	EResourceTransitionAccess AccessFinal)
{
#if RDG_ENABLE_DEBUG
	check(OutTexturePtr);
	UserValidation.ValidateExtractResource(Texture);
	checkf(Texture->AccessFinal == AccessFinal || Texture->AccessFinal == EResourceTransitionAccess::Unknown,
		TEXT("QueueTextureExtraction (%s): Texture final access has already been set (%s) and is different from requested (%s)."),
		Texture->Name, *GetResourceTransitionAccessName(Texture->AccessFinal), *GetResourceTransitionAccessName(AccessFinal));
#endif

	Texture->AccessFinal = AccessFinal;

	FDeferredInternalTextureQuery Query;
	Query.Texture = Texture;
	Query.OutTexturePtr = OutTexturePtr;
	DeferredInternalTextureQueries.Emplace(Query);
}

inline void FRDGBuilder::QueueBufferExtraction(
	FRDGBufferRef Buffer,
	TRefCountPtr<FPooledRDGBuffer>* OutBufferPtr,
	EResourceTransitionAccess AccessFinal)
{
#if RDG_ENABLE_DEBUG
	check(OutBufferPtr);
	UserValidation.ValidateExtractResource(Buffer);
	checkf(Buffer->AccessFinal == AccessFinal || Buffer->AccessFinal == EResourceTransitionAccess::Unknown,
		TEXT("QueueBufferExtraction (%s): Buffer final access has already been set (%s) and is different from requested (%s)."),
		Buffer->Name, *GetResourceTransitionAccessName(Buffer->AccessFinal), *GetResourceTransitionAccessName(AccessFinal));
#endif

	Buffer->AccessFinal = AccessFinal;

	FDeferredInternalBufferQuery Query;
	Query.Buffer = Buffer;
	Query.OutBufferPtr = OutBufferPtr;
	DeferredInternalBufferQueries.Emplace(Query);
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

template <typename T, typename... TArgs>
T* FRDGBuilder::Allocate(TArgs&&... Args)
{
	check(IsInRenderingThread());
	return new (MemStack) T(Forward<TArgs>(Args)...);
}

template<class TRDGResource, class ...TArgs>
TRDGResource* FRDGBuilder::AllocateResource(TArgs&&... Args)
{
	TRDGResource* Resource = Allocate<TRDGResource>(Forward<TArgs>(Args)...);
	Resource->Handle = Resources.Insert(Resource);
	return Resource;
}