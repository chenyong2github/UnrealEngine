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
static FAutoConsoleVariableRef CVarNiagaraGPUSortingCPUToGPUThreshold(
	TEXT("Niagara.GPUSorting.CPUToGPUThreshold"),
	GNiagaraGPUSortingCPUToGPUThreshold,
	TEXT("Particle count to move from a CPU sort to a GPU sort. -1 disables. (default=-1)"),
	ECVF_Default
);

int32 GNiagaraGPUCullingCPUToGPUThreshold = 0;
static FAutoConsoleVariableRef CVarNiagaraGPUCullingCPUToGPUThreshold(
	TEXT("Niagara.GPUCulling.CPUToGPUThreshold"),
	GNiagaraGPUCullingCPUToGPUThreshold,
	TEXT("Particle count to move from a CPU sort to a GPU cull. -1 disables. (default=0)"),
	ECVF_Default
);

IMPLEMENT_GLOBAL_SHADER(FNiagaraSortKeyGenCS, "/Plugin/FX/Niagara/Private/NiagaraSortKeyGen.usf", "GenerateParticleSortKeys", SF_Compute);

void FNiagaraSortKeyGenCS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), NIAGARA_KEY_GEN_THREAD_COUNT);
	OutEnvironment.SetDefine(TEXT("SORT_NONE"), (uint8)ENiagaraSortMode::None);
	OutEnvironment.SetDefine(TEXT("SORT_VIEW_DEPTH"), (uint8)ENiagaraSortMode::ViewDepth);
	OutEnvironment.SetDefine(TEXT("SORT_VIEW_DISTANCE"), (uint8)ENiagaraSortMode::ViewDistance);
	OutEnvironment.SetDefine(TEXT("SORT_CUSTOM_ASCENDING"), (uint8)ENiagaraSortMode::CustomAscending);
	OutEnvironment.SetDefine(TEXT("SORT_CUSTOM_DESCENDING"), (uint8)ENiagaraSortMode::CustomDecending);

	bool bUseWaveIntrinsics = FDataDrivenShaderPlatformInfo::GetSupportsWaveOperations(Parameters.Platform);
	OutEnvironment.SetDefine(TEXT("USE_WAVE_INTRINSICS"), bUseWaveIntrinsics);
}

