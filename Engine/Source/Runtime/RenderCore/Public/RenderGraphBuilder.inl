// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

inline FRDGTextureRef FRDGBuilder::RegisterExternalTexture(const TRefCountPtr<IPooledRenderTarget>& ExternalPooledTexture, const TCHAR* Name)
{
#if RDG_ENABLE_DEBUG
	{
		checkf(ExternalPooledTexture.IsValid(), TEXT("Attempted to register NULL external texture: %s"), Name);
		Validation.ExecuteGuard(TEXT("RegisterExternalTexture"), Name);
		checkf(Name, TEXT("Externally allocated texture requires a debug name when registering them to render graph."));
	}
#endif

	if (FRDGTextureRef* Texture = ExternalTextures.Find(ExternalPooledTexture.GetReference()))
	{
		return *Texture;
	}

	FRDGTexture* OutTexture = AllocateForRHILifeTime<FRDGTexture>(Name, ExternalPooledTexture->GetDesc(), ERDGResourceFlags::None);
	OutTexture->PooledRenderTarget = ExternalPooledTexture;
	OutTexture->ResourceRHI = ExternalPooledTexture->GetRenderTargetItem().ShaderResourceTexture;
	AllocatedTextures.Add(OutTexture, ExternalPooledTexture);

	IF_RDG_ENABLE_DEBUG(Validation.ValidateCreateExternalResource(OutTexture));

	ExternalTextures.Add(ExternalPooledTexture.GetReference(), OutTexture);

	return OutTexture;
}

inline FRDGBufferRef FRDGBuilder::RegisterExternalBuffer(const TRefCountPtr<FPooledRDGBuffer>& ExternalPooledBuffer, const TCHAR* Name)
{
#if RDG_ENABLE_DEBUG
	{
		checkf(ExternalPooledBuffer.IsValid(), TEXT("Attempted to register NULL external buffer: %s"), Name);
		Validation.ExecuteGuard(TEXT("RegisterExternalBuffer"), Name);
	}
#endif

	if (FRDGBufferRef* Buffer = ExternalBuffers.Find(ExternalPooledBuffer.GetReference()))
	{
		return *Buffer;
	}

	FRDGBuffer* OutBuffer = AllocateForRHILifeTime<FRDGBuffer>(Name, ExternalPooledBuffer->Desc, ERDGResourceFlags::None);
	OutBuffer->PooledBuffer = ExternalPooledBuffer;
	AllocatedBuffers.Add(OutBuffer, ExternalPooledBuffer);

	IF_RDG_ENABLE_DEBUG(Validation.ValidateCreateExternalResource(OutBuffer));

	ExternalBuffers.Add(ExternalPooledBuffer.GetReference(), OutBuffer);

	return OutBuffer;
}

inline FRDGTextureRef FRDGBuilder::CreateTexture(
	const FPooledRenderTargetDesc& Desc,
	const TCHAR* Name,
	ERDGResourceFlags Flags)
{
#if RDG_ENABLE_DEBUG
	{
		checkf(Name, TEXT("Creating a render graph texture requires a valid debug name."));
		Validation.ExecuteGuard(TEXT("CreateTexture"), Name);
		checkf(Desc.Format != PF_Unknown, TEXT("Illegal to create texture %s with an invalid pixel format."), Name);

		const bool bCanHaveUAV = Desc.TargetableFlags & TexCreate_UAV;
		const bool bIsMSAA = Desc.NumSamples > 1;

		// D3D11 doesn't allow creating a UAV on MSAA texture.
		const bool bIsUAVForMSAATexture = bIsMSAA && bCanHaveUAV;
		checkf(!bIsUAVForMSAATexture, TEXT("TexCreate_UAV is not allowed on MSAA texture %s."), Name);
	}
#endif

	FRDGTexture* Texture = AllocateForRHILifeTime<FRDGTexture>(Name, Desc, Flags);

	IF_RDG_ENABLE_DEBUG(Validation.ValidateCreateResource(Texture));

	return Texture;
}

inline FRDGBufferRef FRDGBuilder::CreateBuffer(
	const FRDGBufferDesc& Desc,
	const TCHAR* Name,
	ERDGResourceFlags Flags)
{
#if RDG_ENABLE_DEBUG
	{
		checkf(Name, TEXT("Creating a render graph buffer requires a valid debug name."));
		Validation.ExecuteGuard(TEXT("CreateBuffer"), Name);
	}
#endif

	FRDGBufferRef Buffer = AllocateForRHILifeTime<FRDGBuffer>(Name, Desc, Flags);

	IF_RDG_ENABLE_DEBUG(Validation.ValidateCreateResource(Buffer));

	return Buffer;
}

inline FRDGTextureSRVRef FRDGBuilder::CreateSRV(const FRDGTextureSRVDesc& Desc)
{
	FRDGTextureRef Texture = Desc.Texture;

#if RDG_ENABLE_DEBUG
	{
		checkf(Texture, TEXT("RenderGraph texture SRV created with a null texture."));
		Validation.ExecuteGuard(TEXT("CreateSRV"), Texture->Name);
		checkf(Desc.Texture->Desc.TargetableFlags & TexCreate_ShaderResource, TEXT("Attempted to create SRV from texture %s which was not created with TexCreate_ShaderResource"), Desc.Texture->Name);
	}
#endif

	return AllocateForRHILifeTime<FRDGTextureSRV>(Texture->Name, Desc);
}

inline FRDGBufferSRVRef FRDGBuilder::CreateSRV(const FRDGBufferSRVDesc& Desc)
{
	FRDGBufferRef Buffer = Desc.Buffer;

#if RDG_ENABLE_DEBUG
	{
		checkf(Buffer, TEXT("RenderGraph buffer SRV created with a null buffer."));
		Validation.ExecuteGuard(TEXT("CreateSRV"), Buffer->Name);
	}
#endif

	return AllocateForRHILifeTime<FRDGBufferSRV>(Buffer->Name, Desc);
}

inline FRDGTextureUAVRef FRDGBuilder::CreateUAV(const FRDGTextureUAVDesc& Desc)
{
	FRDGTextureRef Texture = Desc.Texture;

#if RDG_ENABLE_DEBUG
	{
		checkf(Texture, TEXT("RenderGraph texture UAV created with a null texture."));
		Validation.ExecuteGuard(TEXT("CreateUAV"), Texture->Name);
		checkf(Texture->Desc.TargetableFlags & TexCreate_UAV, TEXT("Attempted to create UAV from texture %s which was not created with TexCreate_UAV"), Texture->Name);
	}
#endif

	return AllocateForRHILifeTime<FRDGTextureUAV>(Texture->Name, Desc);
}

inline FRDGBufferUAVRef FRDGBuilder::CreateUAV(const FRDGBufferUAVDesc& Desc)
{
	FRDGBufferRef Buffer = Desc.Buffer;

#if RDG_ENABLE_DEBUG
	{
		checkf(Buffer, TEXT("RenderGraph buffer UAV created with a null buffer."));
		Validation.ExecuteGuard(TEXT("CreateUAV"), Buffer->Name);
	}
#endif

	return AllocateForRHILifeTime<FRDGBufferUAV>(Buffer->Name, Desc);
}

template <typename ParameterStructType>
ParameterStructType* FRDGBuilder::AllocParameters()
{
	// TODO(RDG): could allocate using AllocateForRHILifeTime() to avoid the copy done when using FRHICommandList::BuildLocalUniformBuffer()
	ParameterStructType* OutParameterPtr = new(MemStack) ParameterStructType;
	FMemory::Memzero(OutParameterPtr, sizeof(ParameterStructType));
	IF_RDG_ENABLE_DEBUG(Validation.ValidateAllocPassParameters(OutParameterPtr));
	return OutParameterPtr;
}

template <typename ParameterStructType, typename ExecuteLambdaType>
void FRDGBuilder::AddPass(
	FRDGEventName&& Name,
	ParameterStructType* ParameterStruct,
	ERDGPassFlags Flags,
	ExecuteLambdaType&& ExecuteLambda)
{
	// Verify that the amount of stuff captured by a pass's lambda is reasonable. Changing this maximum as desired without review is not OK,
	// the all point being to catch things that should be captured by referenced instead of captured by copy.
	constexpr int32 kMaximumLambdaCaptureSize = 1024;
	static_assert(sizeof(ExecuteLambdaType) <= kMaximumLambdaCaptureSize, "The amount of data of captured for the pass looks abnormally high.");

	auto NewPass = new(MemStack) TRDGLambdaPass<ParameterStructType, ExecuteLambdaType>(
		MoveTemp(Name),
		FRDGPassParameterStruct(ParameterStruct),
		Flags,
		MoveTemp(ExecuteLambda));

	AddPassInternal(NewPass);
}

inline void FRDGBuilder::QueueTextureExtraction(
	FRDGTextureRef Texture,
	TRefCountPtr<IPooledRenderTarget>* OutTexturePtr,
	bool bTransitionToRead)
{
	IF_RDG_ENABLE_DEBUG(Validation.ValidateExtractResource(Texture));

	check(OutTexturePtr);

	FDeferredInternalTextureQuery Query;
	Query.Texture = Texture;
	Query.OutTexturePtr = OutTexturePtr;
	Query.bTransitionToRead = bTransitionToRead;
	DeferredInternalTextureQueries.Emplace(Query);
}

inline void FRDGBuilder::QueueBufferExtraction(FRDGBufferRef Buffer, TRefCountPtr<FPooledRDGBuffer>* OutBufferPtr)
{
	IF_RDG_ENABLE_DEBUG(Validation.ValidateExtractResource(Buffer));

	check(OutBufferPtr);

	FDeferredInternalBufferQuery Query;
	Query.Buffer = Buffer;
	Query.OutBufferPtr = OutBufferPtr;
	DeferredInternalBufferQueries.Emplace(Query);
}

inline void FRDGBuilder::RemoveUnusedTextureWarning(FRDGTextureRef Texture)
{
#if RDG_ENABLE_DEBUG
	check(Texture);
	Validation.ExecuteGuard(TEXT("RemoveUnusedTextureWarning"), Texture->Name);
	Validation.RemoveUnusedWarning(Texture);
#endif
}

inline void FRDGBuilder::RemoveUnusedBufferWarning(FRDGBufferRef Buffer)
{
#if RDG_ENABLE_DEBUG
	check(Buffer);
	Validation.ExecuteGuard(TEXT("RemoveUnusedBufferWarning"), Buffer->Name);
	Validation.RemoveUnusedWarning(Buffer);
#endif
}

template<class Type, class ...ConstructorParameterTypes>
Type* FRDGBuilder::AllocateForRHILifeTime(ConstructorParameterTypes&&... ConstructorParameters)
{
	check(IsInRenderingThread());
	// When bypassing the RHI command queuing, can allocate directly on render thread memory stack allocator, otherwise allocate
	// on the RHI's stack allocator so RHICreateUniformBuffer() can dereference render graph resources.
	if (RHICmdList.Bypass() || 1) // TODO: UE-68018
	{
		return new (MemStack) Type(Forward<ConstructorParameterTypes>(ConstructorParameters)...);
	}
	else
	{
		void* UnitializedType = RHICmdList.Alloc<Type>();
		return new (UnitializedType) Type(Forward<ConstructorParameterTypes>(ConstructorParameters)...);
	}
}