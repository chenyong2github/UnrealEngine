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

inline FRDGTextureRef FRDGBuilder::CreateTexture(
	const FRDGTextureDesc& Desc,
	const TCHAR* Name,
	ERDGTextureFlags Flags)
{
	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateCreateTexture(Desc, Name, Flags));
	FRDGTextureRef Texture = Textures.Allocate(Allocator, Name, Desc, Flags, ERenderTargetTexture::ShaderResource);
	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateCreateTexture(Texture));
	return Texture;
}

inline FRDGBufferRef FRDGBuilder::CreateBuffer(
	const FRDGBufferDesc& Desc,
	const TCHAR* Name,
	ERDGBufferFlags Flags)
{
	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateCreateBuffer(Desc, Name, Flags));
	FRDGBufferRef Buffer = Buffers.Allocate(Allocator, Name, Desc, Flags);
	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateCreateBuffer(Buffer));
	return Buffer;
}

inline FRDGTextureSRVRef FRDGBuilder::CreateSRV(const FRDGTextureSRVDesc& Desc)
{
	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateCreateSRV(Desc));
	FRDGTextureSRVRef SRV = Views.Allocate<FRDGTextureSRV>(Allocator, Desc.Texture->Name, Desc);
	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateCreateSRV(SRV));
	return SRV;
}

inline FRDGBufferSRVRef FRDGBuilder::CreateSRV(const FRDGBufferSRVDesc& Desc)
{
	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateCreateSRV(Desc));
	FRDGBufferSRVRef SRV = Views.Allocate<FRDGBufferSRV>(Allocator, Desc.Buffer->Name, Desc);
	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateCreateSRV(SRV));
	return SRV;
}

inline FRDGTextureUAVRef FRDGBuilder::CreateUAV(const FRDGTextureUAVDesc& Desc, ERDGUnorderedAccessViewFlags InFlags)
{
	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateCreateUAV(Desc));
	FRDGTextureUAVRef UAV = Views.Allocate<FRDGTextureUAV>(Allocator, Desc.Texture->Name, Desc, InFlags);
	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateCreateUAV(UAV));
	return UAV;
}

inline FRDGBufferUAVRef FRDGBuilder::CreateUAV(const FRDGBufferUAVDesc& Desc, ERDGUnorderedAccessViewFlags InFlags)
{
	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateCreateUAV(Desc));
	FRDGBufferUAVRef UAV = Views.Allocate<FRDGBufferUAV>(Allocator, Desc.Buffer->Name, Desc, InFlags);
	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateCreateUAV(UAV));
	return UAV;
}

FORCEINLINE void* FRDGBuilder::Alloc(uint32 SizeInBytes, uint32 AlignInBytes)
{
	return Allocator.Alloc(SizeInBytes, AlignInBytes);
}

template <typename PODType>
FORCEINLINE PODType* FRDGBuilder::AllocPOD()
{
	return Allocator.AllocUninitialized<PODType>();
}

template <typename ObjectType, typename... TArgs>
FORCEINLINE ObjectType* FRDGBuilder::AllocObject(TArgs&&... Args)
{
	return Allocator.Alloc<ObjectType>(Forward<TArgs&&>(Args)...);
}

template <typename ParameterStructType>
FORCEINLINE ParameterStructType* FRDGBuilder::AllocParameters()
{
	return Allocator.Alloc<ParameterStructType>();
}

FORCEINLINE FRDGSubresourceState* FRDGBuilder::AllocSubresource(const FRDGSubresourceState& Other)
{
	return Allocator.AllocNoDestruct<FRDGSubresourceState>(Other);
}

template <typename ParameterStructType>
TRDGUniformBufferRef<ParameterStructType> FRDGBuilder::CreateUniformBuffer(ParameterStructType* ParameterStruct)
{
	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateCreateUniformBuffer(ParameterStruct, &ParameterStructType::StaticStructMetadata));
	auto* UniformBuffer = UniformBuffers.Allocate<TRDGUniformBuffer<ParameterStructType>>(Allocator, ParameterStruct, ParameterStructType::StaticStructMetadata.GetShaderVariableName());
	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateCreateUniformBuffer(UniformBuffer));
	return UniformBuffer;
}

template <typename ExecuteLambdaType>
FRDGPassRef FRDGBuilder::AddPass(
	FRDGEventName&& Name,
	ERDGPassFlags Flags,
	ExecuteLambdaType&& ExecuteLambda)
{
	using LambdaPassType = TRDGEmptyLambdaPass<ExecuteLambdaType>;

	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateAddPass(Name, Flags));

	Flags |= ERDGPassFlags::NeverCull;

	LambdaPassType* Pass = Passes.Allocate<LambdaPassType>(Allocator, MoveTemp(Name), Flags, MoveTemp(ExecuteLambda));
	SetupEmptyPass(Pass);
	return Pass;
}

template <typename ParameterStructType, typename ExecuteLambdaType>
FRDGPassRef FRDGBuilder::AddPass(
	FRDGEventName&& Name,
	const FShaderParametersMetadata* ParametersMetadata,
	const ParameterStructType* ParameterStruct,
	ERDGPassFlags Flags,
	ExecuteLambdaType&& ExecuteLambda)
{
	using LambdaPassType = TRDGLambdaPass<ParameterStructType, ExecuteLambdaType>;

	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateAddPass(ParameterStruct, ParametersMetadata, Name, Flags));

	FRDGPass* Pass = Allocator.Alloc<LambdaPassType>(
		MoveTemp(Name),
		ParametersMetadata,
		ParameterStruct,
		OverridePassFlags(Name.GetTCHAR(), Flags, LambdaPassType::kSupportsAsyncCompute),
		MoveTemp(ExecuteLambda));

	IF_RDG_ENABLE_DEBUG(ClobberPassOutputs(Pass));
	Passes.Insert(Pass);
	SetupPass(Pass);
	return Pass;
}

template <typename ParameterStructType, typename ExecuteLambdaType>
FRDGPassRef FRDGBuilder::AddPass(
	FRDGEventName&& Name,
	const ParameterStructType* ParameterStruct,
	ERDGPassFlags Flags,
	ExecuteLambdaType&& ExecuteLambda)
{
	return AddPass(Forward<FRDGEventName>(Name), ParameterStructType::FTypeInfo::GetStructMetadata(), ParameterStruct, Flags, Forward<ExecuteLambdaType>(ExecuteLambda));
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
	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateExtractTexture(Texture, OutTexturePtr));

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
	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateExtractBuffer(Buffer, OutBufferPtr));

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

inline void FRDGBuilder::SetCommandListStat(TStatId StatId)
{
#if RDG_CMDLIST_STATS
	CommandListStat = StatId;
	RHICmdList.SetCurrentStat(StatId);
#endif
}

inline const TRefCountPtr<IPooledRenderTarget>& FRDGBuilder::GetPooledTexture(FRDGTextureRef Texture) const
{
	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateGetPooledTexture(Texture));
	return Texture->Allocation;
}

inline const TRefCountPtr<FRDGPooledBuffer>& FRDGBuilder::GetPooledBuffer(FRDGBufferRef Buffer) const
{
	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateGetPooledBuffer(Buffer));
	return Buffer->Allocation;
}

inline void FRDGBuilder::SetTextureAccessFinal(FRDGTextureRef Texture, ERHIAccess AccessFinal)
{
	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateSetTextureAccessFinal(Texture, AccessFinal));
	Texture->AccessFinal = AccessFinal;
}

inline void FRDGBuilder::SetBufferAccessFinal(FRDGBufferRef Buffer, ERHIAccess AccessFinal)
{
	IF_RDG_ENABLE_DEBUG(UserValidation.ValidateSetBufferAccessFinal(Buffer, AccessFinal));
	Buffer->AccessFinal = AccessFinal;
}

inline void FRDGBuilder::RemoveUnusedTextureWarning(FRDGTextureRef Texture)
{
	IF_RDG_ENABLE_DEBUG(UserValidation.RemoveUnusedWarning(Texture));
}

inline void FRDGBuilder::RemoveUnusedBufferWarning(FRDGBufferRef Buffer)
{
	IF_RDG_ENABLE_DEBUG(UserValidation.RemoveUnusedWarning(Buffer));
}

inline void FRDGBuilder::BeginEventScope(FRDGEventName&& ScopeName)
{
#if RDG_GPU_SCOPES
	GPUScopeStacks.BeginEventScope(MoveTemp(ScopeName));
#endif
}

inline void FRDGBuilder::EndEventScope()
{
#if RDG_GPU_SCOPES
	GPUScopeStacks.EndEventScope();
#endif
}