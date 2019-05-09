// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

inline FRDGTextureRef FRDGBuilder::RegisterExternalTexture(const TRefCountPtr<IPooledRenderTarget>& ExternalPooledTexture, const TCHAR* Name)
{
#if RDG_ENABLE_DEBUG
	{
		ensureMsgf(ExternalPooledTexture.IsValid(), TEXT("Attempted to register NULL external texture: %s"), Name);
		checkf(Name, TEXT("Externally allocated texture requires a debug name when registering them to render graph."));
	}
#endif

	FRDGTexture* OutTexture = AllocateForRHILifeTime<FRDGTexture>(Name, ExternalPooledTexture->GetDesc(), ERDGResourceFlags::None);
	OutTexture->PooledRenderTarget = ExternalPooledTexture;
	OutTexture->ResourceRHI = ExternalPooledTexture->GetRenderTargetItem().ShaderResourceTexture;
	AllocatedTextures.Add(OutTexture, ExternalPooledTexture);

#if RDG_ENABLE_DEBUG
	{
		OutTexture->MarkAsExternal();
		TrackedResources.Add(OutTexture);
	}
#endif

	return OutTexture;
}

inline FRDGBufferRef FRDGBuilder::RegisterExternalBuffer(const TRefCountPtr<FPooledRDGBuffer>& ExternalPooledBuffer, const TCHAR* Name)
{
#if RDG_ENABLE_DEBUG
	{
		ensureMsgf(ExternalPooledBuffer.IsValid(), TEXT("Attempted to register NULL external buffer: %s"), Name);
	}
#endif

	FRDGBuffer* OutBuffer = AllocateForRHILifeTime<FRDGBuffer>(Name, ExternalPooledBuffer->Desc, ERDGResourceFlags::None);
	OutBuffer->PooledBuffer = ExternalPooledBuffer;
	AllocatedBuffers.Add(OutBuffer, ExternalPooledBuffer);

#if RDG_ENABLE_DEBUG
	{
		OutBuffer->MarkAsExternal();
		TrackedResources.Add(OutBuffer);
	}
#endif

	return OutBuffer;
}

inline FRDGTextureRef FRDGBuilder::CreateTexture(
	const FPooledRenderTargetDesc& Desc,
	const TCHAR* DebugName,
	ERDGResourceFlags Flags)
{
#if RDG_ENABLE_DEBUG
	{
		ensureMsgf(!bHasExecuted, TEXT("Render graph texture %s needs to be created before the builder execution."), DebugName);
		checkf(DebugName, TEXT("Creating a render graph texture requires a valid debug name."));
		checkf(Desc.Format != PF_Unknown, TEXT("Illegal to create texture %s with an invalid pixel format."), DebugName);
	}
#endif

	FRDGTexture* Texture = AllocateForRHILifeTime<FRDGTexture>(DebugName, Desc, Flags);

#if RDG_ENABLE_DEBUG
	{
		TrackedResources.Add(Texture);
	}
#endif

	return Texture;
}

inline FRDGBufferRef FRDGBuilder::CreateBuffer(
	const FRDGBufferDesc& Desc,
	const TCHAR* Name,
	ERDGResourceFlags Flags)
{
#if RDG_ENABLE_DEBUG
	{
		ensureMsgf(!bHasExecuted, TEXT("Render graph buffer %s needs to be created before the builder execution."), Name);
		checkf(Name, TEXT("Creating a render graph buffer requires a valid debug name."));
	}
#endif

	FRDGBufferRef Buffer = AllocateForRHILifeTime<FRDGBuffer>(Name, Desc, Flags);

#if RDG_ENABLE_DEBUG
	{
		TrackedResources.Add(Buffer);
	}
#endif

	return Buffer;
}

inline FRDGTextureSRVRef FRDGBuilder::CreateSRV(const FRDGTextureSRVDesc& Desc)
{
	FRDGTextureRef Texture = Desc.Texture;

#if RDG_ENABLE_DEBUG
	{
		if (!Texture)
		{
			ensureMsgf(false, TEXT("RenderGraph texture SRV created with a null texture."));
			return nullptr;
		}

		ensureMsgf(!bHasExecuted, TEXT("Render graph SRV %s needs to be created before the builder execution."), Desc.Texture->Name);
		ensureMsgf(Desc.Texture->Desc.TargetableFlags & TexCreate_ShaderResource, TEXT("Attempted to create SRV from texture %s which was not created with TexCreate_ShaderResource"), Desc.Texture->Name);
	}
#endif

	return AllocateForRHILifeTime<FRDGTextureSRV>(Desc.Texture->Name, Desc);
}

inline FRDGBufferSRVRef FRDGBuilder::CreateSRV(const FRDGBufferSRVDesc& Desc)
{
	FRDGBufferRef Buffer = Desc.Buffer;

#if RDG_ENABLE_DEBUG
	{
		if (!Buffer)
		{
			ensureMsgf(false, TEXT("RenderGraph buffer SRV created with a null buffer."));
			return nullptr;
		}

		ensureMsgf(!bHasExecuted, TEXT("Render graph SRV %s needs to be created before the builder execution."), Desc.Buffer->Name);
	}
#endif

	return AllocateForRHILifeTime<FRDGBufferSRV>(Buffer->Name, Desc);
}

inline FRDGTextureUAVRef FRDGBuilder::CreateUAV(const FRDGTextureUAVDesc& Desc)
{
	FRDGTextureRef Texture = Desc.Texture;

#if RDG_ENABLE_DEBUG
	{
		if (!Texture)
		{
			checkf(false, TEXT("RenderGraph texture UAV created with a null texture."));
			return nullptr;
		}

		ensureMsgf(!bHasExecuted, TEXT("Render graph UAV %s needs to be created before the builder execution."), Texture->Name);
		ensureMsgf(Texture->Desc.TargetableFlags & TexCreate_UAV, TEXT("Attempted to create UAV from texture %s which was not created with TexCreate_UAV"), Texture->Name);
	}
#endif

	return AllocateForRHILifeTime<FRDGTextureUAV>(Texture->Name, Desc);
}

inline FRDGBufferUAVRef FRDGBuilder::CreateUAV(const FRDGBufferUAVDesc& Desc)
{
	FRDGBufferRef Buffer = Desc.Buffer;

#if RDG_ENABLE_DEBUG
	{
		if (!Buffer)
		{
			checkf(false, TEXT("RenderGraph buffer UAV created with a null buffer."));
			return nullptr;
		}

		ensureMsgf(!bHasExecuted, TEXT("Render graph UAV %s needs to be created before the builder execution."), Buffer->Name);
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
#if RDG_ENABLE_DEBUG
	{
		AllocatedUnusedPassParameters.Add(static_cast<void *>(OutParameterPtr));
	}
#endif
	return OutParameterPtr;
}

template <typename ParameterStructType, typename ExecuteLambdaType>
void FRDGBuilder::AddPass(
	FRDGEventName&& Name,
	ParameterStructType* ParameterStruct,
	ERDGPassFlags Flags,
	ExecuteLambdaType&& ExecuteLambda)
{
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
	check(Texture);
	check(OutTexturePtr);
#if RDG_ENABLE_DEBUG
	{
		checkf(!bHasExecuted,
			TEXT("Accessing render graph internal texture %s with QueueTextureExtraction() needs to happen before the builder's execution."),
			Texture->Name);

		checkf(Texture->HasBeenProduced(),
			TEXT("Unable to queue the extraction of the texture %s because it has not been produced by any pass."),
			Texture->Name);
	}
#endif
	FDeferredInternalTextureQuery Query;
	Query.Texture = Texture;
	Query.OutTexturePtr = OutTexturePtr;
	Query.bTransitionToRead = bTransitionToRead;
	DeferredInternalTextureQueries.Emplace(Query);
}

inline void FRDGBuilder::QueueBufferExtraction(FRDGBufferRef Buffer, TRefCountPtr<FPooledRDGBuffer>* OutBufferPtr)
{
	check(Buffer);
	check(OutBufferPtr);
#if RDG_ENABLE_DEBUG
	{
		checkf(!bHasExecuted,
			TEXT("Accessing render graph internal buffer %s with QueueBufferExtraction() needs to happen before the builder's execution."),
			Buffer->Name);

		checkf(Buffer->HasBeenProduced(),
			TEXT("Unable to queue the extraction of the buffer %s because it has not been produced by any pass."),
			Buffer->Name);
	}
#endif
	FDeferredInternalBufferQuery Query;
	Query.Buffer = Buffer;
	Query.OutBufferPtr = OutBufferPtr;
	DeferredInternalBufferQueries.Emplace(Query);
}

inline void FRDGBuilder::RemoveUnusedTextureWarning(FRDGTextureRef Texture)
{
	check(Texture);
#if RDG_ENABLE_DEBUG
	{
		checkf(!bHasExecuted,
			TEXT("Flaging texture %s with FlagUnusedTexture() needs to happen before the builder's execution."),
			Texture->Name);

		// Increment the number of time the texture has been accessed to avoid warning on produced but never used resources that were produced
		// only to be extracted for the graph.
		Texture->PassAccessCount += 1;
	}
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