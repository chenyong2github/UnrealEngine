// Copyright Epic Games, Inc. All Rights Reserved.

/*==============================================================================
NiagaraDrawIndirect.h: Niagara shader to generate the draw indirect args for Niagara renderers.
==============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderPermutation.h"

#define NIAGARA_DRAW_INDIRECT_ARGS_GEN_THREAD_COUNT 64
#define NIAGARA_DRAW_INDIRECT_ARGS_SIZE 5

// #define NIAGARA_COPY_BUFFER_THREAD_COUNT 64
// #define NIAGARA_COPY_BUFFER_BUFFER_COUNT 3

/**
 * Compute shader used to generate GPU emitter draw indirect args.
 * It also resets unused instance count entries.
 */
class NIAGARAVERTEXFACTORIES_API FNiagaraDrawIndirectArgsGenCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FNiagaraDrawIndirectArgsGenCS);

public:

	/** 
	 * Task info when generating draw indirect frame buffer.
	 * Task is either about generate Niagara renderers drawindirect buffer,
	 * or about resetting released instance counters.
	 */
	struct FArgGenTaskInfo
	{
		FArgGenTaskInfo(uint32 InInstanceCountBufferOffset, uint32 InNumIndicesPerInstance) 
			: InstanceCountBufferOffset(InInstanceCountBufferOffset)
			, NumIndicesPerInstance(InNumIndicesPerInstance)
		{}

		uint32 InstanceCountBufferOffset;
		uint32 NumIndicesPerInstance; // When -1 the counter needs to be reset to 0.

		bool operator==(const FArgGenTaskInfo& Rhs) const
		{
			return InstanceCountBufferOffset == Rhs.InstanceCountBufferOffset && NumIndicesPerInstance == Rhs.NumIndicesPerInstance;
		}
	};

	using FPermutationDomain = TShaderPermutationDomain<>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return RHISupportsComputeShaders(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);

	FNiagaraDrawIndirectArgsGenCS() {}
	FNiagaraDrawIndirectArgsGenCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer);

	bool Serialize(FArchive& Ar);
	void SetOutput(FRHICommandList& RHICmdList, FRHIUnorderedAccessView* DrawIndirectArgsUAV, FRHIUnorderedAccessView* InstanceCountsUAV);
	void SetParameters(FRHICommandList& RHICmdList, FRHIShaderResourceView* TaskInfosBuffer, int32 NumArgGenTasks, int32 NumInstanceCountClearTasks);
	void UnbindBuffers(FRHICommandList& RHICmdList);

protected:
	
	FShaderResourceParameter TaskInfosParam;
	FRWShaderParameter InstanceCountsParam;
	FRWShaderParameter DrawIndirectArgsParam;
	FShaderParameter TaskCountParam;
};
