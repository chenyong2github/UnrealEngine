// Copyright Epic Games, Inc. All Rights Reserved.

/*==============================================================================
NiagaraGPURayTracingTransformsShader.h: Niagara shader to generate the ray tracing instances information.
==============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderPermutation.h"
#include "ShaderParameterStruct.h"

/**
 * Compute shader used to pass GPU instances transforms to the ray tracing TLAS.
 */
class NIAGARAVERTEXFACTORIES_API FNiagaraGPURayTracingTransformsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FNiagaraGPURayTracingTransformsCS);
	SHADER_USE_PARAMETER_STRUCT(FNiagaraGPURayTracingTransformsCS, FGlobalShader);

	static constexpr uint32 ThreadGroupSize = 64;

	using FPermutationDomain = TShaderPermutationDomain<>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32,	ParticleDataFloatStride)
		//SHADER_PARAMETER(uint32,	ParticleDataHalfStride)
		SHADER_PARAMETER(uint32,	ParticleDataIntStride)
		SHADER_PARAMETER(uint32,	CPUNumInstances)
		SHADER_PARAMETER(uint32,	InstanceCountOffset)

		SHADER_PARAMETER(uint32,	PositionDataOffset)
		SHADER_PARAMETER(uint32,	RotationDataOffset)
		SHADER_PARAMETER(uint32,	ScaleDataOffset)
		SHADER_PARAMETER(uint32,	bLocalSpace)
		SHADER_PARAMETER(uint32,	RenderVisibilityOffset)
		SHADER_PARAMETER(uint32,	MeshIndexOffset)

		SHADER_PARAMETER(uint32,	RenderVisibilityValue)
		SHADER_PARAMETER(uint32,	MeshIndexValue)

		SHADER_PARAMETER(FVector,	DefaultPosition)
		SHADER_PARAMETER(FVector4,	DefaultRotation)
		SHADER_PARAMETER(FVector,	DefaultScale)
		SHADER_PARAMETER(FVector,	MeshScale)

		SHADER_PARAMETER(FMatrix,	LocalTransform)

		SHADER_PARAMETER_SRV(Buffer<float>,	ParticleDataFloatBuffer)
		//SHADER_PARAMETER_SRV(Buffer<half>,	ParticleDataHalfBuffer)
		SHADER_PARAMETER_SRV(Buffer<int>,	ParticleDataIntBuffer)
		SHADER_PARAMETER_SRV(Buffer<uint>, GPUInstanceCountBuffer)

		SHADER_PARAMETER_UAV(RWStructuredBuffer<float3x4>, TLASTransforms)
	END_SHADER_PARAMETER_STRUCT()

};
