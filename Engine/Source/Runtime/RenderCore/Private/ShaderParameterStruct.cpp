// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ShaderParameterStruct.cpp: Shader parameter struct implementations.
=============================================================================*/

#include "ShaderParameterStruct.h"


/** Context of binding a map. */
struct FShaderParameterStructBindingContext
{
	// Shader having its parameter bound.
	const FShader* Shader;

	// Bindings to bind.
	FShaderParameterBindings* Bindings;

	// The shader parameter map from the compilation.
	const FShaderParameterMap* ParametersMap;

	// Map of global shader name that were bound to C++ members.
	TMap<FString, FString> ShaderGlobalScopeBindings;

	// C++ name of the render target binding slot.
	FString RenderTargetBindingSlotCppName;

	// Shader PermutationId
	int32 PermutationId;

	// Whether this is for legacy shader parameter settings, or root shader parameter structures/
	bool bUseRootShaderParameters;


	void Bind(
		const FShaderParametersMetadata& StructMetaData,
		const TCHAR* MemberPrefix,
		uint32 GeneralByteOffset)
	{
		const TArray<FShaderParametersMetadata::FMember>& StructMembers = StructMetaData.GetMembers();

		for (const FShaderParametersMetadata::FMember& Member : StructMembers)
		{
			EUniformBufferBaseType BaseType = Member.GetBaseType();

			FString CppName = FString::Printf(TEXT("%s::%s"), StructMetaData.GetStructTypeName(), Member.GetName());

			// Ignore rasterizer binding slots entirely since this actually have nothing to do with a shader.
			if (BaseType == UBMT_RENDER_TARGET_BINDING_SLOTS)
			{
				if (!RenderTargetBindingSlotCppName.IsEmpty())
				{
					UE_LOG(LogShaders, Fatal, TEXT("Render target binding slots collision: %s & %s"),
						*RenderTargetBindingSlotCppName, *CppName);
				}
				RenderTargetBindingSlotCppName = CppName;
				continue;
			}

			// Compute the shader member name to look for according to nesting.
			FString ShaderBindingName = FString::Printf(TEXT("%s%s"), MemberPrefix, Member.GetName());

			uint16 ByteOffset = uint16(GeneralByteOffset + Member.GetOffset());
			check(uint32(ByteOffset) == GeneralByteOffset + Member.GetOffset());

			const uint32 ArraySize = Member.GetNumElements();
			const bool bIsArray = ArraySize > 0;
			const bool bIsRHIResource = (
				BaseType == UBMT_TEXTURE ||
				BaseType == UBMT_SRV ||
				BaseType == UBMT_UAV ||
				BaseType == UBMT_SAMPLER);

			const bool bIsRDGResource =
				IsRDGResourceReferenceShaderParameterType(BaseType) &&
				BaseType != UBMT_RDG_BUFFER &&
				BaseType != UBMT_RDG_BUFFER_ACCESS &&
				BaseType != UBMT_RDG_TEXTURE_ACCESS;

			const bool bIsVariableNativeType = (
				BaseType == UBMT_INT32 ||
				BaseType == UBMT_UINT32 ||
				BaseType == UBMT_FLOAT32);

			checkf(BaseType != UBMT_BOOL, TEXT("Should have failed in FShaderParametersMetadata::InitializeLayout()"));

			if (BaseType == UBMT_INCLUDED_STRUCT)
			{
				checkf(!bIsArray, TEXT("Array of included structure is impossible."));
				Bind(
					*Member.GetStructMetadata(),
					/* MemberPrefix = */ MemberPrefix,
					/* GeneralByteOffset = */ ByteOffset);
				continue;
			}
			else if (BaseType == UBMT_NESTED_STRUCT && bIsArray)
			{
				const FShaderParametersMetadata* ChildStruct = Member.GetStructMetadata();
				uint32 StructSize = ChildStruct->GetSize();
				for (uint32 ArrayElementId = 0; ArrayElementId < (bIsArray ? ArraySize : 1u); ArrayElementId++)
				{
					FString NewPrefix = FString::Printf(TEXT("%s%s_%d_"), MemberPrefix, Member.GetName(), ArrayElementId);
					Bind(
						*ChildStruct,
						/* MemberPrefix = */ *NewPrefix,
						/* GeneralByteOffset = */ ByteOffset + ArrayElementId * StructSize);
				}
				continue;
			}
			else if (BaseType == UBMT_NESTED_STRUCT && !bIsArray)
			{
				FString NewPrefix = FString::Printf(TEXT("%s%s_"), MemberPrefix, Member.GetName());
				Bind(
					*Member.GetStructMetadata(),
					/* MemberPrefix = */ *NewPrefix,
					/* GeneralByteOffset = */ ByteOffset);
				continue;
			}
			else if (BaseType == UBMT_REFERENCED_STRUCT || BaseType == UBMT_RDG_UNIFORM_BUFFER)
			{
				checkf(!bIsArray, TEXT("Array of referenced structure is not supported, because the structure is globally uniquely named."));
				// The member name of a globally referenced struct is the not name on the struct.
				ShaderBindingName = Member.GetStructMetadata()->GetShaderVariableName();
			}
			else if (BaseType == UBMT_RDG_BUFFER)
			{
				// RHI does not support setting a buffer as a shader parameter.
				check(!bIsArray);
				if( ParametersMap->ContainsParameterAllocation(*ShaderBindingName) )
				{
					UE_LOG(LogShaders, Fatal, TEXT("%s can't bind shader parameter %s as buffer. Use buffer SRV for reading in shader."), *CppName, *ShaderBindingName);
				}
				continue;
			}
			else if (bUseRootShaderParameters && bIsVariableNativeType)
			{
				// Constants are stored in the root shader parameter cbuffer when bUseRootShaderParameters == true.
				continue;
			}

			const bool bIsResourceArray = bIsArray && (bIsRHIResource || bIsRDGResource);

			for (uint32 ArrayElementId = 0; ArrayElementId < (bIsResourceArray ? ArraySize : 1u); ArrayElementId++)
			{
				FString ElementShaderBindingName;
				if (bIsResourceArray)
				{
					if (0) // HLSLCC does not support array of resources.
						ElementShaderBindingName = FString::Printf(TEXT("%s[%d]"), *ShaderBindingName, ArrayElementId);
					else
						ElementShaderBindingName = FString::Printf(TEXT("%s_%d"), *ShaderBindingName, ArrayElementId);
				}
				else
				{
					ElementShaderBindingName = ShaderBindingName;
				}

				if (ShaderGlobalScopeBindings.Contains(ElementShaderBindingName))
				{
					UE_LOG(LogShaders, Fatal, TEXT("%s can't bind shader parameter %s, because it has already be bound by %s."), *CppName, *ElementShaderBindingName, **ShaderGlobalScopeBindings.Find(ShaderBindingName));
				}

				uint16 BufferIndex, BaseIndex, BoundSize;
				if (!ParametersMap->FindParameterAllocation(*ElementShaderBindingName, BufferIndex, BaseIndex, BoundSize))
				{
					continue;
				}
				ShaderGlobalScopeBindings.Add(ElementShaderBindingName, CppName);

				if (bIsVariableNativeType)
				{
					checkf(ArrayElementId == 0, TEXT("The entire array should be bound instead for RHI parameter submission performance."));
					uint32 ByteSize = Member.GetMemberSize();

					FShaderParameterBindings::FParameter Parameter;
					Parameter.BufferIndex = BufferIndex;
					Parameter.BaseIndex = BaseIndex;
					Parameter.ByteOffset = ByteOffset;
					Parameter.ByteSize = BoundSize;

					if (uint32(BoundSize) > ByteSize)
					{
						UE_LOG(LogShaders, Fatal, TEXT("The size required to bind shader %s's (Permutation Id %d) struct %s parameter %s is %i bytes, smaller than %s's %i bytes."),
							Shader->GetTypeUnfrozen()->GetName(), PermutationId, StructMetaData.GetStructTypeName(),
							*ElementShaderBindingName, BoundSize, *CppName, ByteSize);
					}

					Bindings->Parameters.Add(Parameter);
				}
				else if (BaseType == UBMT_REFERENCED_STRUCT || BaseType == UBMT_RDG_UNIFORM_BUFFER)
				{
					check(!bIsArray);
					FShaderParameterBindings::FParameterStructReference Parameter;
					Parameter.BufferIndex = BufferIndex;
					Parameter.ByteOffset = ByteOffset;

					if (BaseType == UBMT_REFERENCED_STRUCT)
					{
						Bindings->ParameterReferences.Add(Parameter);
					}
					else
					{
						Bindings->GraphUniformBuffers.Add(Parameter);
					}
				}
				else if (bIsRHIResource || bIsRDGResource)
				{
					checkf(BaseIndex < 256, TEXT("BaseIndex does not fit into uint8. Change FResourceParameter::BaseIndex type to uint16"));
										
					FShaderParameterBindings::FResourceParameter Parameter;
					Parameter.BaseIndex = (uint8)BaseIndex;
					Parameter.ByteOffset = ByteOffset + ArrayElementId * SHADER_PARAMETER_POINTER_ALIGNMENT;
					Parameter.BaseType = BaseType;

					if (BoundSize != 1)
					{
						UE_LOG(LogShaders, Fatal,
							TEXT("Error with shader %s's (Permutation Id %d) parameter %s is %i bytes, cpp name = %s.")
							TEXT("The shader compiler should give precisely which elements of an array did not get compiled out, ")
							TEXT("for optimal automatic render graph pass dependency with ClearUnusedGraphResources()."),
							Shader->GetTypeUnfrozen()->GetName(), PermutationId,
							*ElementShaderBindingName, BoundSize, *CppName);
					}

					Bindings->ResourceParameters.Add(Parameter);
				}
				else
				{
					checkf(0, TEXT("Unexpected base type for a shader parameter struct member."));
				}
			}
		}
	}
};

void FShaderParameterBindings::BindForLegacyShaderParameters(const FShader* Shader, int32 PermutationId, const FShaderParameterMap& ParametersMap, const FShaderParametersMetadata& StructMetaData, bool bShouldBindEverything)
{
	const FShaderType* Type = Shader->GetTypeUnfrozen();
	checkf(StructMetaData.GetSize() < (1 << (sizeof(uint16) * 8)), TEXT("Shader parameter structure can only have a size < 65536 bytes."));
	check(this == &Shader->Bindings);
	
	switch (Type->GetFrequency())
	{
	case SF_Vertex:
	case SF_Hull:
	case SF_Domain:
	case SF_Pixel:
	case SF_Geometry:
	case SF_Compute:
		break;
	default:
		checkf(0, TEXT("Invalid shader frequency for this shader binding technique."));
		break;
	}

	FShaderParameterStructBindingContext BindingContext;
	BindingContext.Shader = Shader;
	BindingContext.PermutationId = PermutationId;
	BindingContext.Bindings = this;
	BindingContext.ParametersMap = &ParametersMap;
	BindingContext.bUseRootShaderParameters = false;
	BindingContext.Bind(
		StructMetaData,
		/* MemberPrefix = */ TEXT(""),
		/* ByteOffset = */ 0);

	StructureLayoutHash = StructMetaData.GetLayoutHash();
	RootParameterBufferIndex = kInvalidBufferIndex;

	TArray<FString> AllParameterNames;
	ParametersMap.GetAllParameterNames(AllParameterNames);
	if (bShouldBindEverything && BindingContext.ShaderGlobalScopeBindings.Num() != AllParameterNames.Num())
	{
		FString ErrorString = FString::Printf(
			TEXT("Shader %s, permutation %d has unbound parameters not represented in the parameter struct:"), Type->GetName(), PermutationId);

		for (const FString& GlobalParameterName : AllParameterNames)
		{
			if (!BindingContext.ShaderGlobalScopeBindings.Contains(GlobalParameterName))
			{
				ErrorString += FString::Printf(TEXT("\n  %s"), *GlobalParameterName);
			}
		}

		UE_LOG(LogShaders, Fatal, TEXT("%s"), *ErrorString);
	}
}

void FShaderParameterBindings::BindForRootShaderParameters(const FShader* Shader, int32 PermutationId, const FShaderParameterMap& ParametersMap)
{
	const FShaderType* Type = Shader->GetTypeUnfrozen();
	check(this == &Shader->Bindings);
	check(Type->GetRootParametersMetadata());

	const FShaderParametersMetadata& StructMetaData = *Type->GetRootParametersMetadata();
	checkf(StructMetaData.GetSize() < (1 << (sizeof(uint16) * 8)), TEXT("Shader parameter structure can only have a size < 65536 bytes."));

	switch (Type->GetFrequency())
	{
	case SF_RayGen:
	case SF_RayMiss:
	case SF_RayHitGroup:
	case SF_RayCallable:
		break;
	default:
		checkf(0, TEXT("Invalid shader frequency for this shader binding technic."));
		break;
	}

	FShaderParameterStructBindingContext BindingContext;
	BindingContext.Shader = Shader;
	BindingContext.PermutationId = PermutationId;
	BindingContext.Bindings = this;
	BindingContext.ParametersMap = &ParametersMap;
	BindingContext.bUseRootShaderParameters = true;
	BindingContext.Bind(
		StructMetaData,
		/* MemberPrefix = */ TEXT(""),
		/* ByteOffset = */ 0);

	StructureLayoutHash = StructMetaData.GetLayoutHash();

	// Binds the uniform buffer that contains the root shader parameters.
	{
		const TCHAR* ShaderBindingName = FShaderParametersMetadata::kRootUniformBufferBindingName;
		uint16 BufferIndex, BaseIndex, BoundSize;
		if (ParametersMap.FindParameterAllocation(ShaderBindingName, BufferIndex, BaseIndex, BoundSize))
		{
			BindingContext.ShaderGlobalScopeBindings.Add(ShaderBindingName, ShaderBindingName);
			RootParameterBufferIndex = BufferIndex;
		}
		else
		{
			check(RootParameterBufferIndex == FShaderParameterBindings::kInvalidBufferIndex);
		}
	}

	TArray<FString> AllParameterNames;
	ParametersMap.GetAllParameterNames(AllParameterNames);
	if (BindingContext.ShaderGlobalScopeBindings.Num() != AllParameterNames.Num())
	{
		FString ErrorString = FString::Printf(
			TEXT("Shader %s, permutation %d has unbound parameters not represented in the parameter struct:"),
			Type->GetName(), PermutationId);

		for (const FString& GlobalParameterName : AllParameterNames)
		{
			if (!BindingContext.ShaderGlobalScopeBindings.Contains(GlobalParameterName))
			{
				ErrorString += FString::Printf(TEXT("\n  %s"), *GlobalParameterName);
			}
		}

		UE_LOG(LogShaders, Fatal, TEXT("%s"), *ErrorString);
	}
}

bool FRenderTargetBinding::Validate() const
{
	if (!Texture)
	{
		checkf(LoadAction == ERenderTargetLoadAction::ENoAction,
			TEXT("Can't have a load action when no texture is bound."));

		checkf(!ResolveTexture, TEXT("Can't have a resolve texture when no render target texture is bound."));
	}
	
	return true;
}

bool FDepthStencilBinding::Validate() const
{
	if (Texture)
	{
		EPixelFormat PixelFormat = Texture->Desc.Format;
		const TCHAR* FormatString = GetPixelFormatString(PixelFormat);

		bool bIsDepthFormat = PixelFormat == PF_DepthStencil || PixelFormat == PF_ShadowDepth || PixelFormat == PF_D24;
		checkf(bIsDepthFormat,
			TEXT("Can't bind texture %s as a depth stencil because its pixel format is %s."),
			Texture->Name, FormatString);
		
		checkf(DepthStencilAccess != FExclusiveDepthStencil::DepthNop_StencilNop,
			TEXT("Texture %s is bound but both depth / stencil are set to no-op."),
			Texture->Name);

		bool bHasStencil = PixelFormat == PF_DepthStencil;
		if (!bHasStencil)
		{
			checkf(StencilLoadAction == ERenderTargetLoadAction::ENoAction,
				TEXT("Unable to load stencil of texture %s that have a pixel format %s that does not support stencil."),
				Texture->Name, FormatString);
		
			checkf(!DepthStencilAccess.IsUsingStencil(),
				TEXT("Unable to have stencil access on texture %s that have a pixel format %s that does not support stencil."),
				Texture->Name, FormatString);
		}

		bool bReadDepth = DepthStencilAccess.IsUsingDepth() && !DepthStencilAccess.IsDepthWrite();
		bool bReadStencil = DepthStencilAccess.IsUsingStencil() && !DepthStencilAccess.IsStencilWrite();

		checkf(!(bReadDepth && DepthLoadAction != ERenderTargetLoadAction::ELoad),
			TEXT("Depth read access without depth load action on texture %s."),
			Texture->Name);

		checkf(!(bReadStencil && StencilLoadAction != ERenderTargetLoadAction::ELoad),
			TEXT("Stencil read access without stencil load action on texture %s."),
			Texture->Name);
	}
	else
	{
		checkf(DepthLoadAction == ERenderTargetLoadAction::ENoAction,
			TEXT("Can't have a depth load action when no texture is bound."));
		checkf(StencilLoadAction == ERenderTargetLoadAction::ENoAction,
			TEXT("Can't have a stencil load action when no texture is bound."));
		checkf(DepthStencilAccess == FExclusiveDepthStencil::DepthNop_StencilNop,
			TEXT("Can't have a depth stencil access when no texture is bound."));
	}

	return true;
}

void EmitNullShaderParameterFatalError(const TShaderRef<FShader>& Shader, const FShaderParametersMetadata* ParametersMetadata, uint16 MemberOffset)
{
	FString MemberName = ParametersMetadata->GetFullMemberCodeName(MemberOffset);

	const TCHAR* ShaderClassName = Shader.GetType()->GetName();

	UE_LOG(LogShaders, Fatal,
		TEXT("%s's required shader parameter %s::%s was not set."),
		ShaderClassName,
		ParametersMetadata->GetStructTypeName(),
		*MemberName);
}

#if DO_CHECK

void ValidateShaderParameters(const TShaderRef<FShader>& Shader, const FShaderParametersMetadata* ParametersMetadata, const void* Parameters)
{
	const FShaderParameterBindings& Bindings = Shader->Bindings;

	checkf(
		Bindings.StructureLayoutHash == ParametersMetadata->GetLayoutHash(),
		TEXT("Shader %s's parameter structure has changed without recompilation of the shader"),
		Shader->GetTypeUnfrozen()->GetName());

	const uint8* Base = reinterpret_cast<const uint8*>(Parameters);

	const TCHAR* ShaderClassName = Shader.GetType()->GetName();
	const TCHAR* ShaderParameterStructName = ParametersMetadata->GetStructTypeName();

	for (const FShaderParameterBindings::FResourceParameter& ParameterBinding : Bindings.ResourceParameters)
	{
		EUniformBufferBaseType BaseType = (EUniformBufferBaseType)ParameterBinding.BaseType;
		switch (BaseType)
		{
			case UBMT_TEXTURE:
			case UBMT_SRV:
			case UBMT_UAV:
			case UBMT_SAMPLER:
			{
				FRHIResource* ShaderParameterRef = *(FRHIResource**)(Base + ParameterBinding.ByteOffset);
				if (!ShaderParameterRef)
				{
					EmitNullShaderParameterFatalError(Shader, ParametersMetadata, ParameterBinding.ByteOffset);
				}
			}
			break;
			case UBMT_RDG_TEXTURE:
			{
				const FRDGTexture* GraphTexture = *reinterpret_cast<const FRDGTexture* const*>(Base + ParameterBinding.ByteOffset);
				if (!GraphTexture)
				{
					EmitNullShaderParameterFatalError(Shader, ParametersMetadata, ParameterBinding.ByteOffset);
				}
			}
			break;
			case UBMT_RDG_TEXTURE_SRV:
			case UBMT_RDG_TEXTURE_UAV:
			case UBMT_RDG_BUFFER_SRV:
			case UBMT_RDG_BUFFER_UAV:
			{
				const FRDGResource* GraphResource = *reinterpret_cast<const FRDGResource* const*>(Base + ParameterBinding.ByteOffset);
				if (!GraphResource)
				{
					EmitNullShaderParameterFatalError(Shader, ParametersMetadata, ParameterBinding.ByteOffset);
				}
			}
			break;
			default:
				break;
		}
	}

	// Graph Uniform Buffers
	for (const FShaderParameterBindings::FParameterStructReference& ParameterBinding : Bindings.GraphUniformBuffers)
	{
		auto GraphUniformBuffer = *reinterpret_cast<const FRDGUniformBuffer* const*>(Base + ParameterBinding.ByteOffset);
		if (!GraphUniformBuffer)
		{
			EmitNullShaderParameterFatalError(Shader, ParametersMetadata, ParameterBinding.ByteOffset);
		}
	}

	// Reference structures
	for (const FShaderParameterBindings::FParameterStructReference& ParameterBinding : Bindings.ParameterReferences)
	{
		const TRefCountPtr<FRHIUniformBuffer>& ShaderParameterRef = *reinterpret_cast<const TRefCountPtr<FRHIUniformBuffer>*>(Base + ParameterBinding.ByteOffset);

		if (!ShaderParameterRef.IsValid())
		{
			EmitNullShaderParameterFatalError(Shader, ParametersMetadata, ParameterBinding.ByteOffset);
		}
	}
}

void ValidateShaderParameterResourcesRHI(const void* Contents, const FRHIUniformBufferLayout& Layout)
{
	for (int32 Index = 0, Count = Layout.Resources.Num(); Index < Count; ++Index)
	{
		const auto Parameter = Layout.Resources[Index];

		FRHIResource* Resource = GetShaderParameterResourceRHI(Contents, Parameter.MemberOffset, Parameter.MemberType);

		const bool bSRV =
			Parameter.MemberType == UBMT_SRV ||
			Parameter.MemberType == UBMT_RDG_TEXTURE_SRV ||
			Parameter.MemberType == UBMT_RDG_BUFFER_SRV;

		// Allow null SRV's in uniform buffers for feature levels that don't support SRV's in shaders
		if (GMaxRHIFeatureLevel <= ERHIFeatureLevel::ES3_1 && bSRV)
		{
			continue;
		}

		checkf(Resource, TEXT("Null resource entry in uniform buffer parameters: %s.Resources[%u], ResourceType 0x%x."), *Layout.GetDebugName(), Index, Parameter.MemberType);
	}
}

#endif // DO_CHECK
