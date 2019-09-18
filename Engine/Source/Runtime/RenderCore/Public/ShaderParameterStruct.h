// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ShaderParameterStruct.h: API to submit all shader parameters in single function call.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Shader.h"
#include "RHI.h"
#include "RenderGraphResources.h"

template <typename FParameterStruct>
void BindForLegacyShaderParameters(FShader* Shader, const FShaderParameterMap& ParameterMap, bool bShouldBindEverything = false)
{
	Shader->Bindings.BindForLegacyShaderParameters(Shader, ParameterMap, *FParameterStruct::FTypeInfo::GetStructMetadata(), bShouldBindEverything);
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
		BindForLegacyShaderParameters<FParameters>(this, Initializer.ParameterMap, bShouldBindEverything); \
	} \
	\
	ShaderClass() \
	{ } \

// TODO(RDG): would not even need ShaderParentClass anymore. And in fact should not so Bindings.Bind() is not being called twice.
#define SHADER_USE_PARAMETER_STRUCT(ShaderClass, ShaderParentClass) \
	SHADER_USE_PARAMETER_STRUCT_INTERNAL(ShaderClass, ShaderParentClass, true)

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
		this->Bindings.BindForRootShaderParameters(this, Initializer.ParameterMap); \
	} \
	\
	ShaderClass() \
	{ } \


/** Raise fatal error when a required shader parameter has not been set. */
extern RENDERCORE_API void EmitNullShaderParameterFatalError(const FShader* Shader, const FShaderParametersMetadata* ParametersMetadata, uint16 MemberOffset);


/** Validates that all resource parameters of a shader are set. */
#if DO_CHECK
extern RENDERCORE_API void ValidateShaderParameters(const FShader* Shader, const FShaderParametersMetadata* ParametersMetadata, const void* Parameters);

#else // !DO_CHECK
FORCEINLINE void ValidateShaderParameters(const FShader* Shader, const FShaderParametersMetadata* ParametersMetadata, const void* Parameters)
{ }

#endif // !DO_CHECK

template<typename TShaderClass>
FORCEINLINE void ValidateShaderParameters(const TShaderClass* Shader, const typename TShaderClass::FParameters& Parameters)
{
	const typename TShaderClass::FParameters* ParameterPtr = &Parameters;
	return ValidateShaderParameters(Shader, TShaderClass::FParameters::FTypeInfo::GetStructMetadata(), ParameterPtr);
}


/** Set compute shader UAVs. */
template<typename TRHICmdList, typename TShaderClass, typename TShaderRHI>
inline void SetShaderUAVs(TRHICmdList& RHICmdList, const TShaderClass* Shader, TShaderRHI* ShadeRHI, const typename TShaderClass::FParameters& Parameters)
{
	checkf(
		Shader->Bindings.UAVs.Num() == 0 && Shader->Bindings.GraphUAVs.Num() == 0,
		TEXT("TShaderRHI Can't have compute shader to be set. UAVs are not supported on vertex, tessellation and geometry shaders."));
}

template<typename TRHICmdList, typename TShaderClass>
inline void SetShaderUAVs(TRHICmdList& RHICmdList, const TShaderClass* Shader, FRHIPixelShader* ShadeRHI, const typename TShaderClass::FParameters& Parameters)
{
	// Pixelshader UAVs are bound together with rendertargets using BeginRenderPass
}

template<typename TRHICmdList, typename TShaderClass>
inline void SetShaderUAVs(TRHICmdList& RHICmdList, const TShaderClass* Shader, FRHIComputeShader* ShadeRHI, const typename TShaderClass::FParameters& Parameters)
{
	const FShaderParameterBindings& Bindings = Shader->Bindings;

	const typename TShaderClass::FParameters* ParametersPtr = &Parameters;
	const uint8* Base = reinterpret_cast<const uint8*>(ParametersPtr);

	// UAVs
	for (const FShaderParameterBindings::FResourceParameter& ParameterBinding : Bindings.UAVs)
	{
		FRHIUnorderedAccessView* ShaderParameterRef = *(FRHIUnorderedAccessView**)(Base + ParameterBinding.ByteOffset);

		if (DO_CHECK && !ShaderParameterRef)
		{
			EmitNullShaderParameterFatalError(Shader, TShaderClass::FParameters::FTypeInfo::GetStructMetadata(), ParameterBinding.ByteOffset);
		}

		RHICmdList.SetUAVParameter(ShadeRHI, ParameterBinding.BaseIndex, ShaderParameterRef);
	}

	// Graph UAVs
	for (const FShaderParameterBindings::FResourceParameter& ParameterBinding : Bindings.GraphUAVs)
	{
		auto GraphUAV = *reinterpret_cast<FRDGUnorderedAccessView* const*>(Base + ParameterBinding.ByteOffset);

		if (DO_CHECK)
		{
			if (!GraphUAV)
			{
				EmitNullShaderParameterFatalError(Shader, TShaderClass::FParameters::FTypeInfo::GetStructMetadata(), ParameterBinding.ByteOffset);
			}
			else
			{
				GraphUAV->MarkResourceAsUsed();
			}
		}

		checkSlow(GraphUAV);
		RHICmdList.SetUAVParameter(ShadeRHI, ParameterBinding.BaseIndex, GraphUAV->GetRHI());
	}
}


/** Unset compute shader UAVs. */
template<typename TRHICmdList, typename TShaderClass>
inline void UnsetShaderUAVs(TRHICmdList& RHICmdList, const TShaderClass* Shader, FRHIComputeShader* ShadeRHI)
{
	// TODO(RDG): Once all shader sets their parameter through this, can refactor RHI so all UAVs of a shader get unset through a single RHI function call.
	const FShaderParameterBindings& Bindings = Shader->Bindings;

	checkf(Bindings.RootParameterBufferIndex == FShaderParameterBindings::kInvalidBufferIndex, TEXT("Can't use UnsetShaderUAVs() for root parameter buffer index."));

	for (const FShaderParameterBindings::FResourceParameter& ParameterBinding : Bindings.UAVs)
	{
		RHICmdList.SetUAVParameter(ShadeRHI, ParameterBinding.BaseIndex, nullptr);
	}

	for (const FShaderParameterBindings::FResourceParameter& ParameterBinding : Bindings.GraphUAVs)
	{
		RHICmdList.SetUAVParameter(ShadeRHI, ParameterBinding.BaseIndex, nullptr);
	}
}


/** Set shader's parameters from its parameters struct. */
template<typename TRHICmdList, typename TShaderClass, typename TShaderRHI>
inline void SetShaderParameters(TRHICmdList& RHICmdList, const TShaderClass* Shader, TShaderRHI* ShadeRHI, const typename TShaderClass::FParameters& Parameters)
{
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

	// Textures
	for (const FShaderParameterBindings::FResourceParameter& ParameterBinding : Bindings.Textures)
	{
		auto ShaderParameterRef = *(FRHITexture**)(Base + ParameterBinding.ByteOffset);

		if (DO_CHECK && !ShaderParameterRef)
		{
			EmitNullShaderParameterFatalError(Shader, TShaderClass::FParameters::FTypeInfo::GetStructMetadata(), ParameterBinding.ByteOffset);
		}

		RHICmdList.SetShaderTexture(ShadeRHI, ParameterBinding.BaseIndex, ShaderParameterRef);
	}

	// SRVs
	for (const FShaderParameterBindings::FResourceParameter& ParameterBinding : Bindings.SRVs)
	{
		FRHIShaderResourceView* ShaderParameterRef = *(FRHIShaderResourceView**)(Base + ParameterBinding.ByteOffset);

		if (DO_CHECK && !ShaderParameterRef)
		{
			EmitNullShaderParameterFatalError(Shader, TShaderClass::FParameters::FTypeInfo::GetStructMetadata(), ParameterBinding.ByteOffset);
		}

		RHICmdList.SetShaderResourceViewParameter(ShadeRHI, ParameterBinding.BaseIndex, ShaderParameterRef);
	}

	// Samplers
	for (const FShaderParameterBindings::FResourceParameter& ParameterBinding : Bindings.Samplers)
	{
		FRHISamplerState* ShaderParameterRef = *(FRHISamplerState**)(Base + ParameterBinding.ByteOffset);

		if (DO_CHECK && !ShaderParameterRef)
		{
			EmitNullShaderParameterFatalError(Shader, TShaderClass::FParameters::FTypeInfo::GetStructMetadata(), ParameterBinding.ByteOffset);
		}

		RHICmdList.SetShaderSampler(ShadeRHI, ParameterBinding.BaseIndex, ShaderParameterRef);
	}

	// Graph Textures
	for (const FShaderParameterBindings::FResourceParameter& ParameterBinding : Bindings.GraphTextures)
	{
		auto GraphTexture = *reinterpret_cast<FRDGTexture* const*>(Base + ParameterBinding.ByteOffset);

		if (DO_CHECK)
		{
			if (!GraphTexture)
			{
				EmitNullShaderParameterFatalError(Shader, TShaderClass::FParameters::FTypeInfo::GetStructMetadata(), ParameterBinding.ByteOffset);
			}
			else
			{
				GraphTexture->MarkResourceAsUsed();
			}
		}

		checkSlow(GraphTexture);
		RHICmdList.SetShaderTexture(ShadeRHI, ParameterBinding.BaseIndex, GraphTexture->GetRHI());
	}
	
	// UAVs for compute shaders
	SetShaderUAVs(RHICmdList, Shader, ShadeRHI, Parameters);	//HACKHACK: Bind UAVs before SRVs as a workaround for D3D11 RHI unbinding SRVs when binding a UAV on the same resource even when the views don't overlap.

	// Graph SRVs
	for (const FShaderParameterBindings::FResourceParameter& ParameterBinding : Bindings.GraphSRVs)
	{
		auto GraphSRV = *reinterpret_cast<FRDGShaderResourceView* const*>(Base + ParameterBinding.ByteOffset);

		if (DO_CHECK)
		{
			if (!GraphSRV)
			{
				EmitNullShaderParameterFatalError(Shader, TShaderClass::FParameters::FTypeInfo::GetStructMetadata(), ParameterBinding.ByteOffset);
			}
			else
			{
				GraphSRV->MarkResourceAsUsed();
			}
		}

		checkSlow(GraphSRV);
		RHICmdList.SetShaderResourceViewParameter(ShadeRHI, ParameterBinding.BaseIndex, GraphSRV->GetRHI());
	}

	// Reference structures
	for (const FShaderParameterBindings::FParameterStructReference& ParameterBinding : Bindings.ParameterReferences)
	{
		const TRefCountPtr<FRHIUniformBuffer>& ShaderParameterRef = *reinterpret_cast<const TRefCountPtr<FRHIUniformBuffer>*>(Base + ParameterBinding.ByteOffset);

		if (DO_CHECK && !ShaderParameterRef.IsValid())
		{
			EmitNullShaderParameterFatalError(Shader, TShaderClass::FParameters::FTypeInfo::GetStructMetadata(), ParameterBinding.ByteOffset);
		}

		RHICmdList.SetShaderUniformBuffer(ShadeRHI, ParameterBinding.BufferIndex, ShaderParameterRef);
	}
}


#if RHI_RAYTRACING

/** Set shader's parameters from its parameters struct. */
template<typename TShaderClass>
void SetShaderParameters(FRayTracingShaderBindingsWriter& RTBindingsWriter, const TShaderClass* Shader, const typename TShaderClass::FParameters& Parameters)
{
	const FShaderParameterBindings& Bindings = Shader->Bindings;

	checkf(Bindings.Parameters.Num() == 0, TEXT("Ray tracing shader should use SHADER_USE_ROOT_PARAMETER_STRUCT() to passdown the cbuffer layout to the shader compiler."));

	const typename TShaderClass::FParameters* ParametersPtr = &Parameters;
	const uint8* Base = reinterpret_cast<const uint8*>(ParametersPtr);

	// Textures
	for (const FShaderParameterBindings::FResourceParameter& ParameterBinding : Bindings.Textures)
	{
		auto ShaderParameterRef = *(FRHITexture**)(Base + ParameterBinding.ByteOffset);

		if (DO_CHECK && !ShaderParameterRef)
		{
			EmitNullShaderParameterFatalError(Shader, TShaderClass::FParameters::FTypeInfo::GetStructMetadata(), ParameterBinding.ByteOffset);
		}

		RTBindingsWriter.SetTexture(ParameterBinding.BaseIndex, ShaderParameterRef);
	}

	// SRVs
	for (const FShaderParameterBindings::FResourceParameter& ParameterBinding : Bindings.SRVs)
	{
		FRHIShaderResourceView* ShaderParameterRef = *(FRHIShaderResourceView**)(Base + ParameterBinding.ByteOffset);

		if (DO_CHECK && !ShaderParameterRef)
		{
			EmitNullShaderParameterFatalError(Shader, TShaderClass::FParameters::FTypeInfo::GetStructMetadata(), ParameterBinding.ByteOffset);
		}

		RTBindingsWriter.SetSRV(ParameterBinding.BaseIndex, ShaderParameterRef);
	}

	// UAVs
	for (const FShaderParameterBindings::FResourceParameter& ParameterBinding : Bindings.UAVs)
	{
		FRHIUnorderedAccessView* ShaderParameterRef = *(FRHIUnorderedAccessView**)(Base + ParameterBinding.ByteOffset);

		if (DO_CHECK && !ShaderParameterRef)
		{
			EmitNullShaderParameterFatalError(Shader, TShaderClass::FParameters::FTypeInfo::GetStructMetadata(), ParameterBinding.ByteOffset);
		}

		RTBindingsWriter.SetUAV(ParameterBinding.BaseIndex, ShaderParameterRef);
	}

	// Samplers
	for (const FShaderParameterBindings::FResourceParameter& ParameterBinding : Bindings.Samplers)
	{
		FRHISamplerState* ShaderParameterRef = *(FRHISamplerState**)(Base + ParameterBinding.ByteOffset);

		if (DO_CHECK && !ShaderParameterRef)
		{
			EmitNullShaderParameterFatalError(Shader, TShaderClass::FParameters::FTypeInfo::GetStructMetadata(), ParameterBinding.ByteOffset);
		}

		RTBindingsWriter.SetSampler(ParameterBinding.BaseIndex, ShaderParameterRef);
	}

	// Graph Textures
	for (const FShaderParameterBindings::FResourceParameter& ParameterBinding : Bindings.GraphTextures)
	{
		auto GraphTexture = *reinterpret_cast<FRDGTexture* const*>(Base + ParameterBinding.ByteOffset);

		if (DO_CHECK)
		{
			if (!GraphTexture)
			{
				EmitNullShaderParameterFatalError(Shader, TShaderClass::FParameters::FTypeInfo::GetStructMetadata(), ParameterBinding.ByteOffset);
			}
			else
			{
				GraphTexture->MarkResourceAsUsed();
			}
		}

		checkSlow(GraphTexture);
		RTBindingsWriter.SetTexture(ParameterBinding.BaseIndex, GraphTexture->GetRHI());
	}

	// Graph SRVs
	for (const FShaderParameterBindings::FResourceParameter& ParameterBinding : Bindings.GraphSRVs)
	{
		auto GraphSRV = *reinterpret_cast<FRDGShaderResourceView* const*>(Base + ParameterBinding.ByteOffset);

		if (DO_CHECK)
		{
			if (!GraphSRV)
			{
				EmitNullShaderParameterFatalError(Shader, TShaderClass::FParameters::FTypeInfo::GetStructMetadata(), ParameterBinding.ByteOffset);
			}
			else
			{
				GraphSRV->MarkResourceAsUsed();
			}
		}

		checkSlow(GraphSRV);
		RTBindingsWriter.SetSRV(ParameterBinding.BaseIndex, GraphSRV->GetRHI());
	}

	// Render graph UAVs
	for (const FShaderParameterBindings::FResourceParameter& ParameterBinding : Bindings.GraphUAVs)
	{
		auto UAV = *reinterpret_cast<FRDGUnorderedAccessView* const*>(Base + ParameterBinding.ByteOffset);

		if (DO_CHECK)
		{
			if (!UAV)
			{
				EmitNullShaderParameterFatalError(Shader, TShaderClass::FParameters::FTypeInfo::GetStructMetadata(), ParameterBinding.ByteOffset);
			}
			else
			{
				UAV->MarkResourceAsUsed();
			}
		}

		checkSlow(UAV);
		RTBindingsWriter.SetUAV(ParameterBinding.BaseIndex, UAV->GetRHI());
	}

	// Referenced uniform buffers
	for (const FShaderParameterBindings::FParameterStructReference& ParameterBinding : Bindings.ParameterReferences)
	{
		const TRefCountPtr<FRHIUniformBuffer>& ShaderParameterRef = *reinterpret_cast<const TRefCountPtr<FRHIUniformBuffer>*>(Base + ParameterBinding.ByteOffset);

		if (DO_CHECK && !ShaderParameterRef.IsValid())
		{
			EmitNullShaderParameterFatalError(Shader, TShaderClass::FParameters::FTypeInfo::GetStructMetadata(), ParameterBinding.ByteOffset);
		}

		RTBindingsWriter.SetUniformBuffer(ParameterBinding.BufferIndex, ShaderParameterRef);
	}

	// Root uniform buffer.
	if (Bindings.RootParameterBufferIndex != FShaderParameterBindings::kInvalidBufferIndex)
	{
		// Do not do any validation at some resources may have been removed from the structure because known to not be used by the shader.
		EUniformBufferValidation Validation = EUniformBufferValidation::None;

		FRHIUniformBuffer* RootUniformBuffer = CreateUniformBufferImmediate(Parameters, UniformBuffer_SingleDraw, Validation);
		RTBindingsWriter.SetUniformBuffer(Bindings.RootParameterBufferIndex, RootUniformBuffer);
	}
}

#endif // RHI_RAYTRACING
