// Copyright Epic Games, Inc. All Rights Reserved.

/*==============================================================================
NiagaraSortingGPU.h: Niagara sorting shaders
==============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderPermutation.h"

struct FNiagaraGPUSortInfo;

extern NIAGARAVERTEXFACTORIES_API int32 GNiagaraGPUSortingUseMaxPrecision;
extern NIAGARAVERTEXFACTORIES_API int32 GNiagaraGPUSortingCPUToGPUThreshold;

#define NIAGARA_KEY_GEN_THREAD_COUNT 64
#define NIAGARA_COPY_BUFFER_THREAD_COUNT 64
#define NIAGARA_COPY_BUFFER_BUFFER_COUNT 3

/**
 * Compute shader used to generate particle sort keys.
 */
class NIAGARAVERTEXFACTORIES_API FNiagaraSortKeyGenCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FNiagaraSortKeyGenCS);

public:

	class FSortUsingMaxPrecision : SHADER_PERMUTATION_BOOL("SORT_MAX_PRECISION");
	using FPermutationDomain = TShaderPermutationDomain<FSortUsingMaxPrecision>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return RHISupportsComputeShaders(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);

	/** Default constructor. */
	FNiagaraSortKeyGenCS() {}

	/** Initialization constructor. */
	explicit FNiagaraSortKeyGenCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer);

	/** Serialization. */
	virtual bool Serialize( FArchive& Ar ) override;

	/**
	 * Set output buffers for this shader.
	 */
	void SetOutput(FRHICommandList& RHICmdList, FRHIUnorderedAccessView* OutKeysUAV, FRHIUnorderedAccessView* OutIndicesUAV);

	/**
	 * Set input parameters.
	 */
	void SetParameters(FRHICommandList& RHICmdList,	const FNiagaraGPUSortInfo& SortInfo, uint32 EmitterKey, int32 OutputOffset, const FUintVector4& SortKeyParamsValue);

	/**
	 * Unbinds any buffers that have been bound.
	 */
	void UnbindBuffers(FRHICommandList& RHICmdList);

private:

	FShaderResourceParameter NiagaraParticleDataFloat;
	FShaderParameter FloatDataStride;
	FShaderResourceParameter GPUParticleCountBuffer;
	FShaderParameter ParticleCountParams;
	FShaderParameter SortParams;
	FShaderParameter SortKeyParams;
	FShaderParameter CameraPosition;
	FShaderParameter CameraDirection;

	/** Output key buffer. */
	FShaderResourceParameter OutKeys;
	/** Output indices buffer. */
	FShaderResourceParameter OutParticleIndices;
};
