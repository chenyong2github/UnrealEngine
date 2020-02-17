// Copyright Epic Games, Inc. All Rights Reserved.

/*==============================================================================
NiagaraSortingGPU.cpp: Niagara sorting shaders
==============================================================================*/

#include "NiagaraSortingGPU.h"
#include "NiagaraGPUSortInfo.h"
#include "ShaderParameterUtils.h"

int32 GNiagaraGPUSortingUseMaxPrecision = 0;
static FAutoConsoleVariableRef CVarNiagaraGPUSortinUseMaxPrecision(
	TEXT("Niagara.GPUSorting.UseMaxPrecision"),
	GNiagaraGPUSortingUseMaxPrecision,
	TEXT("Wether sorting using fp32 instead of fp16. (default=0)"),
	ECVF_Default
);

int32 GNiagaraGPUSortingCPUToGPUThreshold = -1;
static FAutoConsoleVariableRef CVarNiagaraGPUSortinCPUToGPUThreshold(
	TEXT("Niagara.GPUSorting.CPUToGPUThreshold"),
	GNiagaraGPUSortingCPUToGPUThreshold,
	TEXT("Particle count to move from a CPU sort to a GPU sort. -1 disables. (default=-1)"),
	ECVF_Default
);

IMPLEMENT_GLOBAL_SHADER(FNiagaraSortKeyGenCS, "/Plugin/FX/Niagara/Private/NiagaraSortKeyGen.usf", "GenerateParticleSortKeys", SF_Compute);

void FNiagaraSortKeyGenCS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), NIAGARA_KEY_GEN_THREAD_COUNT);
	OutEnvironment.SetDefine(TEXT("SORT_VIEW_DEPTH"), (uint8)ENiagaraSortMode::ViewDepth);
	OutEnvironment.SetDefine(TEXT("SORT_VIEW_DISTANCE"), (uint8)ENiagaraSortMode::ViewDistance);
	OutEnvironment.SetDefine(TEXT("SORT_CUSTOM_ASCENDING"), (uint8)ENiagaraSortMode::CustomAscending);
	OutEnvironment.SetDefine(TEXT("SORT_CUSTOM_DESCENDING"), (uint8)ENiagaraSortMode::CustomDecending);
}

FNiagaraSortKeyGenCS::FNiagaraSortKeyGenCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	: FGlobalShader(Initializer)
{
	NiagaraParticleDataFloat.Bind(Initializer.ParameterMap, TEXT("NiagaraParticleDataFloat"));
	FloatDataStride.Bind(Initializer.ParameterMap, TEXT("NiagaraFloatDataStride"));
	GPUParticleCountBuffer.Bind(Initializer.ParameterMap, TEXT("GPUParticleCountBuffer"));
	ParticleCountParams.Bind(Initializer.ParameterMap, TEXT("ParticleCountParams"));
	SortParams.Bind(Initializer.ParameterMap, TEXT("SortParams"));
	SortKeyParams.Bind(Initializer.ParameterMap, TEXT("SortKeyParams"));
	CameraPosition.Bind(Initializer.ParameterMap, TEXT("CameraPosition"));
	CameraDirection.Bind(Initializer.ParameterMap, TEXT("CameraDirection"));

	OutKeys.Bind(Initializer.ParameterMap, TEXT("OutKeys"));
	OutParticleIndices.Bind(Initializer.ParameterMap, TEXT("OutParticleIndices"));
}

/*bool FNiagaraSortKeyGenCS::Serialize(FArchive& Ar)
{
	bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
	Ar << NiagaraParticleDataFloat;
	Ar << FloatDataStride;
	Ar << GPUParticleCountBuffer;
	Ar << ParticleCountParams;
	Ar << SortParams;
	Ar << SortKeyParams;
	Ar << CameraPosition;
	Ar << CameraDirection;
	Ar << OutKeys;
	Ar << OutParticleIndices;
	return bShaderHasOutdatedParameters;
}*/

void FNiagaraSortKeyGenCS::SetOutput(FRHICommandList& RHICmdList, FRHIUnorderedAccessView* OutKeysUAV, FRHIUnorderedAccessView* OutIndicesUAV)
{
	FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();
	SetUAVParameter(RHICmdList, ComputeShaderRHI, OutKeys, OutKeysUAV);
	SetUAVParameter(RHICmdList, ComputeShaderRHI, OutParticleIndices, OutIndicesUAV);
}

void FNiagaraSortKeyGenCS::SetParameters(FRHICommandList& RHICmdList, const FNiagaraGPUSortInfo& SortInfo, uint32 EmitterKey, int32 OutputOffset, const FUintVector4& SortKeyParamsValue)
{
	FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();

	SetSRVParameter(RHICmdList, ComputeShaderRHI, NiagaraParticleDataFloat, SortInfo.ParticleDataFloatSRV);
	SetShaderValue(RHICmdList, ComputeShaderRHI, FloatDataStride, SortInfo.FloatDataStride);

	SetSRVParameter(RHICmdList, ComputeShaderRHI, GPUParticleCountBuffer, SortInfo.GPUParticleCountSRV);

	const FUintVector4 ParticleCountParamsValue(SortInfo.ParticleCount, SortInfo.GPUParticleCountOffset, 0, 0);
	SetShaderValue(RHICmdList, ComputeShaderRHI, ParticleCountParams, ParticleCountParamsValue);

	// (EmitterKey, OutputOffset, SortMode, SortAttributeOffset)
	const FUintVector4 SortParamsValue(EmitterKey, OutputOffset, (uint8)SortInfo.SortMode, SortInfo.SortAttributeOffset);
	SetShaderValue(RHICmdList, ComputeShaderRHI, SortParams, SortParamsValue);

	// SortKeyParams only exists in the permutation with SORT_MAX_PRECISION set.
	SetShaderValue(RHICmdList, ComputeShaderRHI, SortKeyParams, SortKeyParamsValue);
	SetShaderValue(RHICmdList, ComputeShaderRHI, CameraPosition, SortInfo.ViewOrigin);
	SetShaderValue(RHICmdList, ComputeShaderRHI, CameraDirection, SortInfo.ViewDirection);
}

void FNiagaraSortKeyGenCS::UnbindBuffers(FRHICommandList& RHICmdList)
{
	FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();
	SetUAVParameter(RHICmdList, ComputeShaderRHI, NiagaraParticleDataFloat, nullptr);
	SetUAVParameter(RHICmdList, ComputeShaderRHI, GPUParticleCountBuffer, nullptr);
	SetUAVParameter(RHICmdList, ComputeShaderRHI, OutKeys, nullptr);
	SetUAVParameter(RHICmdList, ComputeShaderRHI, OutParticleIndices, nullptr);
}


