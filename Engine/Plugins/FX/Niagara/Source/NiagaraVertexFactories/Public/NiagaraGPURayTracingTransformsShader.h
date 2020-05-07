// Copyright Epic Games, Inc. All Rights Reserved.

/*==============================================================================
NiagaraGPURayTracingTransformsShader.h: Niagara shader to generate the ray tracing instances information.
==============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderPermutation.h"

/**
 * Compute shader used to pass GPU instances transforms to the ray tracing TLAS.
 */
class NIAGARAVERTEXFACTORIES_API FNiagaraGPURayTracingTransformsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FNiagaraGPURayTracingTransformsCS);

public:
	static constexpr uint32 ThreadGroupSize = 64;

	using FPermutationDomain = TShaderPermutationDomain<>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);

	FNiagaraGPURayTracingTransformsCS() {}
	FNiagaraGPURayTracingTransformsCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer);

	void SetParameters(
		FRHICommandList& RHICmdList, 
		uint32 InstancesCount,
		FRHIShaderResourceView* NiagaraFloatBuffer,
		uint32 FloatDataOffsetValue,
		uint32 FloatDataStrideValue,
		uint32 GPUInstanceCountOffset,
		FRHIShaderResourceView* GPUInstanceCountInputSRV,
		const FUintVector4& NiagaraOffsets,
		const FMatrix& PrimitiveLocalToWorld, 
		FRHIUnorderedAccessView* GPUInstancesTransformsUAV);
	void UnbindBuffers(FRHICommandList& RHICmdList);

protected:
	LAYOUT_FIELD(FShaderParameter, NiagaraOffsetsParam);

	LAYOUT_FIELD(FShaderParameter, LocalToWorldParam);
	LAYOUT_FIELD(FShaderResourceParameter, TLASTransformsParam);

	LAYOUT_FIELD(FShaderResourceParameter, NiagaraParticleDataFloat);
	LAYOUT_FIELD(FShaderParameter, FloatDataOffset);
	LAYOUT_FIELD(FShaderParameter, FloatDataStride);
	LAYOUT_FIELD(FShaderParameter, GPUInstanceCountParams);
	LAYOUT_FIELD(FShaderResourceParameter, GPUInstanceCountInputBuffer);
};
