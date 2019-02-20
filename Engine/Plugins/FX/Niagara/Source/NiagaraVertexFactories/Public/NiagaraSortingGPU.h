// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*==============================================================================
NiagaraSortingGPU.h: Niagara sorting shaders
==============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"

struct FNiagaraGPUSortInfo;

extern NIAGARAVERTEXFACTORIES_API int32 GNiagaraGPUSorting;
extern NIAGARAVERTEXFACTORIES_API int32 GNiagaraGPUSortingCPUToGPUThreshold;
extern NIAGARAVERTEXFACTORIES_API float GNiagaraGPUSortingBufferSlack;
extern NIAGARAVERTEXFACTORIES_API int32 GNiagaraGPUSortingMinBufferSize;
extern NIAGARAVERTEXFACTORIES_API int32 GNiagaraGPUSortingFrameCountBeforeBufferShrinking;

#define NIAGARA_KEY_GEN_THREAD_COUNT 64
#define NIAGARA_COPY_BUFFER_THREAD_COUNT 64
#define NIAGARA_COPY_BUFFER_BUFFER_COUNT 3

/**
 * Compute shader used to generate particle sort keys.
 */
class NIAGARAVERTEXFACTORIES_API FNiagaraSortKeyGenCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FNiagaraSortKeyGenCS,Global);

public:

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
	void SetOutput(FRHICommandList& RHICmdList, FUnorderedAccessViewRHIParamRef OutKeysUAV, FUnorderedAccessViewRHIParamRef OutIndicesUAV);

	/**
	 * Set input parameters.
	 */
	void SetParameters(FRHICommandList& RHICmdList,	const FNiagaraGPUSortInfo& SortInfo, int32 EmitterIndex, int32 OutputOffset);

	/**
	 * Unbinds any buffers that have been bound.
	 */
	void UnbindBuffers(FRHICommandList& RHICmdList);

private:

	FShaderResourceParameter NiagaraParticleDataFloat;
	FShaderParameter FloatDataOffset;
	FShaderParameter FloatDataStride;
	FShaderResourceParameter GPUParticleCountBuffer;
	FShaderParameter ParticleCountParams;
	FShaderParameter SortParams;
	FShaderParameter CameraPosition;
	FShaderParameter CameraDirection;

	/** Output key buffer. */
	FShaderResourceParameter OutKeys;
	/** Output indices buffer. */
	FShaderResourceParameter OutParticleIndices;
};

/**
 * Compute shader used to copy a reference buffer split in several buffers.
 * Each buffer contains a segment of the buffer, and each buffer is increasingly bigger to hold the next part.
 * Could alternatively use DMA copy, if the RHI provided a way to copy buffer regions.
 */
class NIAGARAVERTEXFACTORIES_API FNiagaraCopyIntBufferRegionCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FNiagaraCopyIntBufferRegionCS,Global);

public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return RHISupportsComputeShaders(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), NIAGARA_COPY_BUFFER_THREAD_COUNT);
		OutEnvironment.SetDefine(TEXT("BUFFER_COUNT"), NIAGARA_COPY_BUFFER_BUFFER_COUNT);
	}

	/** Default constructor. */
	FNiagaraCopyIntBufferRegionCS() {}

	/** Initialization constructor. */
	explicit FNiagaraCopyIntBufferRegionCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer);

	/** Serialization. */
	virtual bool Serialize(FArchive& Ar) override;

	/**
	 * Set parameters.
	 */
	void SetParameters(
		FRHICommandList& RHICmdList,	
		const FShaderResourceViewRHIParamRef InSourceData, 
		const FUnorderedAccessViewRHIParamRef* InDestDatas, 
		const int32* InUsedIndexCounts, 
		int32 StartingIndex,
		int32 DestCount);

	/**
	 * Unbinds any buffers that have been bound.
	 */
	void UnbindBuffers(FRHICommandList& RHICmdList);

private:

	FShaderParameter CopyParams;
	FShaderResourceParameter SourceData;
	FShaderResourceParameter DestData[NIAGARA_COPY_BUFFER_BUFFER_COUNT];
};
