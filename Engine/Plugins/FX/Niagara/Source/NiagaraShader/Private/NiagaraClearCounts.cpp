// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraClearCounts.h"

#include "RHI.h"
#include "ScreenRendering.h"
#include "CommonRenderResources.h"
#include "Modules/ModuleManager.h"

#include "RenderGraphBuilder.h"
#include "Runtime/Renderer/Private/ScreenPass.h"

//////////////////////////////////////////////////////////////////////////

class NIAGARASHADER_API FNiagaraClearCountsIntCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FNiagaraClearCountsIntCS);
	SHADER_USE_PARAMETER_STRUCT(FNiagaraClearCountsIntCS, FGlobalShader);

	static constexpr uint32 ThreadGroupSize = 64;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return !IsMobilePlatform(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("NIAGARA_CLEAR_COUNTS_INT_CS"), 1);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), ThreadGroupSize);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_UAV(RWBuffer<int>,	CountBuffer)
		SHADER_PARAMETER_SRV(Buffer<int>,	CountsAndValuesBuffer)
		SHADER_PARAMETER(uint32,			NumCounts)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FNiagaraClearCountsIntCS, "/Plugin/FX/Niagara/Private/NiagaraClearCounts.usf", "ClearCountsIntCS", SF_Compute);

void NiagaraClearCounts::ClearCountsInt(FRHICommandList& RHICmdList, FRHIUnorderedAccessView* UAV, TConstArrayView<TPair<uint32, int32>> IndexAndValueArray)
{
	check(UAV != nullptr);
	check(IndexAndValueArray.Num() > 0);

	FReadBuffer IndexAndValueBuffer;
	IndexAndValueBuffer.Initialize(TEXT("NiagaraClearCounts"), sizeof(int32), IndexAndValueArray.Num() * 2, PF_R32_SINT);
	{
		const uint32 BufferSize = IndexAndValueArray.Num() * sizeof(int32) * 2;
		void* GPUMemory = RHILockBuffer(IndexAndValueBuffer.Buffer, 0, BufferSize, RLM_WriteOnly);
		FMemory::Memcpy(GPUMemory, IndexAndValueArray.GetData(), BufferSize);
		RHIUnlockBuffer(IndexAndValueBuffer.Buffer);
	}

	FNiagaraClearCountsIntCS::FParameters ShaderParameters;
	ShaderParameters.CountBuffer			= UAV;
	ShaderParameters.CountsAndValuesBuffer	= IndexAndValueBuffer.SRV;
	ShaderParameters.NumCounts				= IndexAndValueArray.Num();

	TShaderMapRef<FNiagaraClearCountsIntCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	FRHIComputeShader* ShaderRHI = ComputeShader.GetComputeShader();
	const uint32 NumThreadGroups = FMath::DivideAndRoundUp<uint32>(IndexAndValueArray.Num(), FNiagaraClearCountsIntCS::ThreadGroupSize);

	RHICmdList.Transition(FRHITransitionInfo(UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
	RHICmdList.SetComputeShader(ShaderRHI);
	SetShaderParameters(RHICmdList, ComputeShader, ShaderRHI, ShaderParameters);
	RHICmdList.DispatchComputeShader(NumThreadGroups, 1, 1);
	UnsetShaderUAVs(RHICmdList, ComputeShader, ShaderRHI);
	RHICmdList.Transition(FRHITransitionInfo(UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute));
}
