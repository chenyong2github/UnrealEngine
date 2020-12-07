// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ShaderParameterStruct.h: API to submit all shader parameters in single function call.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Shader.h"
#include "RHI.h"
#include "RenderGraphResources.h"

template <typename FParameterStruct>
void BindForLegacyShaderParameters(FShader* Shader, int32 PermutationId, const FShaderParameterMap& ParameterMap, bool bShouldBindEverything = false)
{
	Shader->Bindings.BindForLegacyShaderParameters(Shader, PermutationId, ParameterMap, *FParameterStruct::FTypeInfo::GetStructMetadata(), bShouldBindEverything);
}

/** Tag a shader class to use the structured shader parameters API.
 *
 * class FMyShaderClassCS : public FGlobalShader
 * {
 *		DECLARE_GLOBAL_SHADER(FMyShaderClassCS);
 *		SHADER_USE_PARAMETER_STRUCT(FMyShaderClassCS, FGlobalShader);
 *
 *		BEGIN_SHADER_PARAMETER_STRUCT(FParameters)
 *			SHADER_PARAMETER(FMatrix, ViewToClip)
 *			//...
 *		END_SHADER_PARAMETER_STRUCT()
 * };
 *
 * Notes: Long term, this macro will no longer be needed. Instead, parameter binding will become the default behavior for shader declarations.
 */

#define SHADER_USE_PARAMETER_STRUCT_INTERNAL(ShaderClass, ShaderParentClass, bShouldBindEverything) \
	ShaderClass(const ShaderMetaType::CompiledShaderInitializerType& Initializer) \
		: ShaderParentClass(Initializer) \
	{ \
		BindForLegacyShaderParameters<FParameters>(this, Initializer.PermutationId, Initializer.ParameterMap, bShouldBindEverything); \
	} \
	\
	ShaderClass() \
	{ } \

// TODO(RDG): would not even need ShaderParentClass anymore. And in fact should not so Bindings.Bind() is not being called twice.
#define SHADER_USE_PARAMETER_STRUCT(ShaderClass, ShaderParentClass) \
	SHADER_USE_PARAMETER_STRUCT_INTERNAL(ShaderClass, ShaderParentClass, true) \
	\
	static inline const FShaderParametersMetadata* GetRootParametersMetadata() { return FParameters::FTypeInfo::GetStructMetadata(); }

/** Use when sharing shader parameter binding with legacy parameters in the base class; i.e. FMaterialShader or FMeshMaterialShader.
 *  Note that this disables validation that the parameter struct contains all shader bindings.
 */
#define SHADER_USE_PARAMETER_STRUCT_WITH_LEGACY_BASE(ShaderClass, ShaderParentClass) \
	SHADER_USE_PARAMETER_STRUCT_INTERNAL(ShaderClass, ShaderParentClass, false)

#define SHADER_USE_ROOT_PARAMETER_STRUCT(ShaderClass, ShaderParentClass) \
	static inline const FShaderParametersMetadata* GetRootParametersMetadata() { return FParameters::FTypeInfo::GetStructMetadata(); } \
	\
	ShaderClass(const ShaderMetaType::CompiledShaderInitializerType& Initializer) \
		: ShaderParentClass(Initializer) \
	{ \
		this->Bindings.BindForRootShaderParameters(this, Initializer.PermutationId, Initializer.ParameterMap); \
	} \
	\
	ShaderClass() \
	{ } \


 /** Dereferences the RHI resource from a shader parameter struct. */
inline FRHIResource* GetShaderParameterResourceRHI(const void* Contents, uint16 MemberOffset, EUniformBufferBaseType MemberType)
{
	checkSlow(Contents);
	if (IsShaderParameterTypeIgnoredByRHI(MemberType))
	{
		return nullptr;
	}

	const uint8* MemberPtr = (const uint8*)Contents + MemberOffset;

	if (IsRDGResourceReferenceShaderParameterType(MemberType))
	{
		const FRDGResource* ResourcePtr = *reinterpret_cast<const FRDGResource* const*>(MemberPtr);
		return ResourcePtr ? ResourcePtr->GetRHI() : nullptr;
	}
	else
	{
		return *reinterpret_cast<FRHIResource* const*>(MemberPtr);
	}
}

/** Validates that all resource parameters of a uniform buffer are set. */
#if DO_CHECK
extern RENDERCORE_API void ValidateShaderParameterResourcesRHI(const void* Contents, const FRHIUniformBufferLayout& Layout);
#else
FORCEINLINE void ValidateShaderParameterResourcesRHI(const void* Contents, const FRHIUniformBufferLayout& Layout) {}
#endif


/** Raise fatal error when a required shader parameter has not been set. */
extern RENDERCORE_API void EmitNullShaderParameterFatalError(const TShaderRef<FShader>& Shader, const FShaderParametersMetadata* ParametersMetadata, uint16 MemberOffset);

/** Validates that all resource parameters of a shader are set. */
#if DO_CHECK
extern RENDERCORE_API void ValidateShaderParameters(const TShaderRef<FShader>& Shader, const FShaderParametersMetadata* ParametersMetadata, const void* Parameters);
#else
FORCEINLINE void ValidateShaderParameters(const TShaderRef<FShader>& Shader, const FShaderParametersMetadata* ParametersMetadata, const void* Parameters) {}
#endif

template<typename TShaderClass>
FORCEINLINE void ValidateShaderParameters(const TShaderRef<TShaderClass>& Shader, const typename TShaderClass::FParameters& Parameters)
{
	const typename TShaderClass::FParameters* ParameterPtr = &Parameters;
	return ValidateShaderParameters(Shader, TShaderClass::FParameters::FTypeInfo::GetStructMetadata(), ParameterPtr);
}

/** Set compute shader UAVs. */
template<typename TRHICmdList, typename TShaderClass, typename TShaderRHI>
inline void SetShaderUAV(TRHICmdList& RHICmdList, const TShaderRef<TShaderClass>& Shader, TShaderRHI* ShadeRHI, const uint8* Base, const FShaderParameterBindings::FResourceParameter& ParameterBinding)
{
	checkf(false, TEXT("TShaderRHI Can't have compute shader to be set. UAVs are not supported on vertex, tessellation and geometry shaders."));
}

template<typename TRHICmdList, typename TShaderClass>
inline void SetShaderUAV(TRHICmdList& RHICmdList, const TShaderRef<TShaderClass>& Shader, FRHIPixelShader* ShadeRHI, const uint8* Base, const FShaderParameterBindings::FResourceParameter& ParameterBinding)
{
	if (ParameterBinding.BaseType == UBMT_UAV)
	{
		FRHIUnorderedAccessView* ShaderParameterRef = *(FRHIUnorderedAccessView**)(Base + ParameterBinding.ByteOffset);
		RHICmdList.SetUAVParameter(ShadeRHI, ParameterBinding.BaseIndex, ShaderParameterRef);
	}
	else if (ParameterBinding.BaseType == UBMT_RDG_TEXTURE_UAV || ParameterBinding.BaseType == UBMT_RDG_BUFFER_UAV)
	{
		auto GraphUAV = *reinterpret_cast<FRDGUnorderedAccessView* const*>(Base + ParameterBinding.ByteOffset);

		checkSlow(GraphUAV);
		GraphUAV->MarkResourceAsUsed();
		RHICmdList.SetUAVParameter(ShadeRHI, ParameterBinding.BaseIndex, GraphUAV->GetRHI());
	}
}

template<typename TRHICmdList, typename TShaderClass>
inline void SetShaderUAV(TRHICmdList& RHICmdList, const TShaderRef<TShaderClass>& Shader, FRHIComputeShader* ShadeRHI, const uint8* Base, const FShaderParameterBindings::FResourceParameter& ParameterBinding)
{
	if (ParameterBinding.BaseType == UBMT_UAV)
	{
		FRHIUnorderedAccessView* ShaderParameterRef = *(FRHIUnorderedAccessView**)(Base + ParameterBinding.ByteOffset);
		RHICmdList.SetUAVParameter(ShadeRHI, ParameterBinding.BaseIndex, ShaderParameterRef);
	}
	else if (ParameterBinding.BaseType == UBMT_RDG_TEXTURE_UAV || ParameterBinding.BaseType == UBMT_RDG_BUFFER_UAV)
	{
		auto GraphUAV = *reinterpret_cast<FRDGUnorderedAccessView* const*>(Base + ParameterBinding.ByteOffset);

		checkSlow(GraphUAV);
		GraphUAV->MarkResourceAsUsed();
		RHICmdList.SetUAVParameter(ShadeRHI, ParameterBinding.BaseIndex, GraphUAV->GetRHI());
	}
}


/** Unset compute shader UAVs. */
template<typename TRHICmdList, typename TShaderClass>
inline void UnsetShaderUAVs(TRHICmdList& RHICmdList, const TShaderRef<TShaderClass>& Shader, FRHIComputeShader* ShadeRHI)
{
	// TODO(RDG): Once all shader sets their parameter through this, can refactor RHI so all UAVs of a shader get unset through a single RHI function call.
	const FShaderParameterBindings& Bindings = Shader->Bindings;

	checkf(Bindings.RootParameterBufferIndex == FShaderParameterBindings::kInvalidBufferIndex, TEXT("Can't use UnsetShaderUAVs() for root parameter buffer index."));

	for (const FShaderParameterBindings::FResourceParameter& ParameterBinding : Bindings.ResourceParameters)
	{
		if (ParameterBinding.BaseType == UBMT_UAV ||
			ParameterBinding.BaseType == UBMT_RDG_TEXTURE_UAV ||
			ParameterBinding.BaseType == UBMT_RDG_BUFFER_UAV)
		{
			RHICmdList.SetUAVParameter(ShadeRHI, ParameterBinding.BaseIndex, nullptr);
		}
	}
}


/** Set shader's parameters from its parameters struct. */
template<typename TRHICmdList, typename TShaderClass, typename TShaderRHI>
inline void SetShaderParameters(
	TRHICmdList& RHICmdList, 
	const TShaderRef<TShaderClass>& Shader, 
	TShaderRHI* ShadeRHI, 
	const FShaderParametersMetadata* ParametersMetadata,
	const typename TShaderClass::FParameters& Parameters)
{
	ValidateShaderParameters(Shader, ParametersMetadata, &Parameters);

	// TODO(RDG): Once all shader sets their parameter through this, can refactor RHI so all shader parameters get sets through a single RHI function call.
	const FShaderParameterBindings& Bindings = Shader->Bindings;

	const typename TShaderClass::FParameters* ParametersPtr = &Parameters;
	const uint8* Base = reinterpret_cast<const uint8*>(ParametersPtr);

	checkf(Bindings.RootParameterBufferIndex == FShaderParameterBindings::kInvalidBufferIndex, TEXT("Can't use SetShaderParameters() for root parameter buffer index."));

	// Parameters
	for (const FShaderParameterBindings::FParameter& ParameterBinding : Bindings.Parameters)
	{
		const void* DataPtr = reinterpret_cast<const char*>(&Parameters) + ParameterBinding.ByteOffset;
		RHICmdList.SetShaderParameter(ShadeRHI, ParameterBinding.BufferIndex, ParameterBinding.BaseIndex, ParameterBinding.ByteSize, DataPtr);
	}

	TArray<FShaderParameterBindings::FResourceParameter, TInlineAllocator<16>> GraphSRVs;

	for (const FShaderParameterBindings::FResourceParameter& ParameterBinding : Bindings.ResourceParameters)
	{
		EUniformBufferBaseType BaseType = (EUniformBufferBaseType)ParameterBinding.BaseType;
		switch (BaseType)
		{
			case UBMT_TEXTURE:
			{
				FRHITexture* TexRef = *(FRHITexture**)(Base + ParameterBinding.ByteOffset);
				RHICmdList.SetShaderTexture(ShadeRHI, ParameterBinding.BaseIndex, TexRef);
			}
			break;
			case UBMT_SRV:
			{
				FRHIShaderResourceView* SRVRef = *(FRHIShaderResourceView**)(Base + ParameterBinding.ByteOffset);
				RHICmdList.SetShaderResourceViewParameter(ShadeRHI, ParameterBinding.BaseIndex, SRVRef);
			}
			break;
			case UBMT_SAMPLER:
			{
				FRHISamplerState* SamplerRef = *(FRHISamplerState**)(Base + ParameterBinding.ByteOffset);
				RHICmdList.SetShaderSampler(ShadeRHI, ParameterBinding.BaseIndex, SamplerRef);
			}
			break;
			case UBMT_RDG_TEXTURE:
			{
				auto GraphTexture = *reinterpret_cast<FRDGTexture* const*>(Base + ParameterBinding.ByteOffset);
				checkSlow(GraphTexture);
				GraphTexture->MarkResourceAsUsed();
				RHICmdList.SetShaderTexture(ShadeRHI, ParameterBinding.BaseIndex, GraphTexture->GetRHI());
			}
			break;
			case UBMT_RDG_TEXTURE_SRV:
			case UBMT_RDG_BUFFER_SRV:
			{
				//HACKHACK: defer SRVs binding after UAVs 
				GraphSRVs.Add(ParameterBinding);
			}
			break;
			case UBMT_UAV:
			case UBMT_RDG_TEXTURE_UAV:
			case UBMT_RDG_BUFFER_UAV:
			{
				SetShaderUAV(RHICmdList, Shader, ShadeRHI, Base, ParameterBinding);
			}
			break;
			default:
				checkf(false, TEXT("Unhandled resource type?"));
				break;
		}
	}

	//HACKHACK: Bind SRVs after UAVs as a workaround for D3D11 RHI unbinding SRVs when binding a UAV on the same resource even when the views don't overlap.
	for (const FShaderParameterBindings::FResourceParameter& ParameterBinding : GraphSRVs)
	{
		auto GraphSRV = *reinterpret_cast<FRDGShaderResourceView* const*>(Base + ParameterBinding.ByteOffset);

		checkSlow(GraphSRV);
		GraphSRV->MarkResourceAsUsed();
		RHICmdList.SetShaderResourceViewParameter(ShadeRHI, ParameterBinding.BaseIndex, GraphSRV->GetRHI());
	}

	// Graph Uniform Buffers
	for (const FShaderParameterBindings::FParameterStructReference& ParameterBinding : Bindings.GraphUniformBuffers)
	{
		const FRDGUniformBufferBinding& UniformBufferBinding = *reinterpret_cast<const FRDGUniformBufferBinding*>(Base + ParameterBinding.ByteOffset);

		if (UniformBufferBinding.IsShader())
		{
			UniformBufferBinding->MarkResourceAsUsed();
			RHICmdList.SetShaderUniformBuffer(ShadeRHI, ParameterBinding.BufferIndex, UniformBufferBinding->GetRHI());
		}
	}

	// Reference structures
	for (const FShaderParameterBindings::FParameterStructReference& ParameterBinding : Bindings.ParameterReferences)
	{
		const FUniformBufferBinding& UniformBufferBinding = *reinterpret_cast<const FUniformBufferBinding*>(Base + ParameterBinding.ByteOffset);

		if (UniformBufferBinding.IsShader())
		{
			RHICmdList.SetShaderUniformBuffer(ShadeRHI, ParameterBinding.BufferIndex, UniformBufferBinding.GetUniformBuffer());
		}
	}
}

template<typename TRHICmdList, typename TShaderClass, typename TShaderRHI>
inline void SetShaderParameters(TRHICmdList& RHICmdList, const TShaderRef<TShaderClass>& Shader, TShaderRHI* ShaderRHI, const typename TShaderClass::FParameters& Parameters)
{
	const FShaderParametersMetadata* ParametersMetadata = TShaderClass::FParameters::FTypeInfo::GetStructMetadata();
	SetShaderParameters(RHICmdList, Shader, ShaderRHI, ParametersMetadata, Parameters);
}

#if RHI_RAYTRACING

/** Set shader's parameters from its parameters struct. */
template<typename TShaderClass>
void SetShaderParameters(FRayTracingShaderBindingsWriter& RTBindingsWriter, const TShaderRef<TShaderClass>& Shader, const typename TShaderClass::FParameters& Parameters)
{
	ValidateShaderParameters(Shader, Parameters);

	const FShaderParameterBindings& Bindings = Shader->Bindings;

	checkf(Bindings.Parameters.Num() == 0, TEXT("Ray tracing shader should use SHADER_USE_ROOT_PARAMETER_STRUCT() to passdown the cbuffer layout to the shader compiler."));

	const typename TShaderClass::FParameters* ParametersPtr = &Parameters;
	const uint8* Base = reinterpret_cast<const uint8*>(ParametersPtr);

	for (const FShaderParameterBindings::FResourceParameter& ParameterBinding : Bindings.ResourceParameters)
	{
		EUniformBufferBaseType BaseType = (EUniformBufferBaseType)ParameterBinding.BaseType;
		switch (BaseType)
		{
			case UBMT_TEXTURE:
			{
				auto ShaderParameterRef = *(FRHITexture**)(Base + ParameterBinding.ByteOffset);
				RTBindingsWriter.SetTexture(ParameterBinding.BaseIndex, ShaderParameterRef);
			}
			break;
			case UBMT_SRV:
			{
				FRHIShaderResourceView* ShaderParameterRef = *(FRHIShaderResourceView**)(Base + ParameterBinding.ByteOffset);
				RTBindingsWriter.SetSRV(ParameterBinding.BaseIndex, ShaderParameterRef);
			}
			break;
			case UBMT_UAV:
			{
				FRHIUnorderedAccessView* ShaderParameterRef = *(FRHIUnorderedAccessView**)(Base + ParameterBinding.ByteOffset);
				RTBindingsWriter.SetUAV(ParameterBinding.BaseIndex, ShaderParameterRef);
			}
			break;
			case UBMT_SAMPLER:
			{
				FRHISamplerState* ShaderParameterRef = *(FRHISamplerState**)(Base + ParameterBinding.ByteOffset);
				RTBindingsWriter.SetSampler(ParameterBinding.BaseIndex, ShaderParameterRef);
			}
			break;
			case UBMT_RDG_TEXTURE:
			{
				auto GraphTexture = *reinterpret_cast<FRDGTexture* const*>(Base + ParameterBinding.ByteOffset);
				checkSlow(GraphTexture);
				GraphTexture->MarkResourceAsUsed();
				RTBindingsWriter.SetTexture(ParameterBinding.BaseIndex, GraphTexture->GetRHI());
			}
			break;
			case UBMT_RDG_TEXTURE_SRV:
			case UBMT_RDG_BUFFER_SRV:
			{
				auto GraphSRV = *reinterpret_cast<FRDGShaderResourceView* const*>(Base + ParameterBinding.ByteOffset);

				checkSlow(GraphSRV);
				GraphSRV->MarkResourceAsUsed();
				RTBindingsWriter.SetSRV(ParameterBinding.BaseIndex, GraphSRV->GetRHI());
			}
			break;
			case UBMT_RDG_TEXTURE_UAV:
			case UBMT_RDG_BUFFER_UAV:
			{
				auto UAV = *reinterpret_cast<FRDGUnorderedAccessView* const*>(Base + ParameterBinding.ByteOffset);

				checkSlow(UAV);
				UAV->MarkResourceAsUsed();
				RTBindingsWriter.SetUAV(ParameterBinding.BaseIndex, UAV->GetRHI());
			}
			break;
			default:
				checkf(false, TEXT("Unhandled resource type?"));
				break;
		}
	}

	// Graph Uniform Buffers
	for (const FShaderParameterBindings::FParameterStructReference& ParameterBinding : Bindings.GraphUniformBuffers)
	{
		const FRDGUniformBufferBinding& UniformBufferBinding = *reinterpret_cast<const FRDGUniformBufferBinding*>(Base + ParameterBinding.ByteOffset);

		checkSlow(UniformBufferBinding);
		UniformBufferBinding->MarkResourceAsUsed();
		RTBindingsWriter.SetUniformBuffer(ParameterBinding.BufferIndex, UniformBufferBinding->GetRHI());
	}

	// Referenced uniform buffers
	for (const FShaderParameterBindings::FParameterStructReference& ParameterBinding : Bindings.ParameterReferences)
	{
		const FUniformBufferBinding& UniformBufferBinding = *reinterpret_cast<const FUniformBufferBinding*>(Base + ParameterBinding.ByteOffset);
		RTBindingsWriter.SetUniformBuffer(ParameterBinding.BufferIndex, UniformBufferBinding.GetUniformBuffer());
	}

	// Root uniform buffer.
	if (Bindings.RootParameterBufferIndex != FShaderParameterBindings::kInvalidBufferIndex)
	{
		// Do not do any validation at some resources may have been removed from the structure because known to not be used by the shader.
		EUniformBufferValidation Validation = EUniformBufferValidation::None;

		RTBindingsWriter.RootUniformBuffer = CreateUniformBufferImmediate(Parameters, UniformBuffer_SingleDraw, Validation);
		RTBindingsWriter.SetUniformBuffer(Bindings.RootParameterBufferIndex, RTBindingsWriter.RootUniformBuffer);
	}
}

#endif // RHI_RAYTRACING
