// Copyright Epic Games, Inc. All Rights Reserved.

/*==============================================================================
NiagaraSortingGPU.cpp: Niagara sorting shaders
==============================================================================*/

#include "NiagaraSortingGPU.h"
#include "NiagaraGPUSortInfo.h"
#include "ShaderParameterUtils.h"

int32 GNiagaraGPUSorting = 1;
static FAutoConsoleVariableRef CVarNiagaraGPUSorting(
	TEXT("Niagara.GPUSorting"),
	GNiagaraGPUSorting,
	TEXT("Whether to sort particles on the GPU"),
	ECVF_Default
);

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

float GNiagaraGPUSortingBufferSlack = 2.f;
static FAutoConsoleVariableRef CVarNiagaraGPUSortingBufferSlack(
	TEXT("Niagara.GPUSorting.BufferSlack"),
	GNiagaraGPUSortingBufferSlack,
	TEXT("Slack ratio when allocating GPU sort buffer (default=2)"),
	ECVF_Default
);

int32 GNiagaraGPUSortingMinBufferSize = 8192;
static FAutoConsoleVariableRef CVarNiagaraGPUSortingMinBufferSize(
	TEXT("Niagara.GPUSorting.MinBufferSize"),
	GNiagaraGPUSortingMinBufferSize,
	TEXT("Minimum GPU sort buffer size, in particles (default=8192)"),
	ECVF_Default
);

int32 GNiagaraGPUSortingFrameCountBeforeBufferShrinking = 100;
static FAutoConsoleVariableRef CVarNiagaraGPUSortingFrameCountBeforeBufferShrinking(
	TEXT("Niagara.GPUSorting.FrameCountBeforeShrinking"),
	GNiagaraGPUSortingFrameCountBeforeBufferShrinking,
	TEXT("Number of consecutive frames where the GPU sort buffer is considered oversized before allowing shrinking. (default=100)"),
	ECVF_Default
);

class FNiagaraSortingDummyUAV : public FRenderResource
{
public:
	FRWBuffer Buffer;

	virtual void InitRHI() override
	{
		Buffer.Initialize(sizeof(int32), 1, EPixelFormat::PF_R32_SINT, BUF_Static, TEXT("FNiagaraSortingDummyUAV"));
	}

	virtual void ReleaseRHI() override
	{
		Buffer.Release();
	}
}; 

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
	FloatDataOffset.Bind(Initializer.ParameterMap, TEXT("NiagaraFloatDataOffset"));
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

bool FNiagaraSortKeyGenCS::Serialize(FArchive& Ar)
{
	bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
	Ar << NiagaraParticleDataFloat;
	Ar << FloatDataOffset;
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
}

void FNiagaraSortKeyGenCS::SetOutput(FRHICommandList& RHICmdList, FRHIUnorderedAccessView* OutKeysUAV, FRHIUnorderedAccessView* OutIndicesUAV)
{
	FRHIComputeShader* ComputeShaderRHI = GetComputeShader();
	SetUAVParameter(RHICmdList, ComputeShaderRHI, OutKeys, OutKeysUAV);
	SetUAVParameter(RHICmdList, ComputeShaderRHI, OutParticleIndices, OutIndicesUAV);
}

void FNiagaraSortKeyGenCS::SetParameters(FRHICommandList& RHICmdList, const FNiagaraGPUSortInfo& SortInfo, uint32 EmitterKey, int32 OutputOffset, const FUintVector4& SortKeyParamsValue)
{
	FRHIComputeShader* ComputeShaderRHI = GetComputeShader();

	SetSRVParameter(RHICmdList, ComputeShaderRHI, NiagaraParticleDataFloat, SortInfo.ParticleDataFloatSRV);
	SetShaderValue(RHICmdList, ComputeShaderRHI, FloatDataOffset, SortInfo.FloatDataOffset);
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
	FRHIComputeShader* ComputeShaderRHI = GetComputeShader();
	SetUAVParameter(RHICmdList, ComputeShaderRHI, NiagaraParticleDataFloat, nullptr);
	SetUAVParameter(RHICmdList, ComputeShaderRHI, GPUParticleCountBuffer, nullptr);
	SetUAVParameter(RHICmdList, ComputeShaderRHI, OutKeys, nullptr);
	SetUAVParameter(RHICmdList, ComputeShaderRHI, OutParticleIndices, nullptr);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_SHADER_TYPE(, FNiagaraCopyIntBufferRegionCS, TEXT("/Plugin/FX/Niagara/Private/NiagaraCopyIntBuffer.usf"), TEXT("MainCS"), SF_Compute);

FNiagaraCopyIntBufferRegionCS::FNiagaraCopyIntBufferRegionCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	: FGlobalShader(Initializer)
{
	CopyParams.Bind(Initializer.ParameterMap, TEXT("CopyParams"));
	SourceData.Bind(Initializer.ParameterMap, TEXT("SourceData"));
	DestData[0].Bind(Initializer.ParameterMap, TEXT("DestData0"));
	DestData[1].Bind(Initializer.ParameterMap, TEXT("DestData1"));
	DestData[2].Bind(Initializer.ParameterMap, TEXT("DestData2"));
}

bool FNiagaraCopyIntBufferRegionCS::Serialize(FArchive& Ar)
{
	bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
	Ar << CopyParams;
	Ar << SourceData;
	Ar << DestData[0];
	Ar << DestData[1];
	Ar << DestData[2];
	return bShaderHasOutdatedParameters;
}

void FNiagaraCopyIntBufferRegionCS::SetParameters(
	FRHICommandList& RHICmdList,
	FRHIShaderResourceView* InSourceData,
	FRHIUnorderedAccessView* const* InDestDatas,
	const int32* InUsedIndexCounts,
	int32 StartingIndex,
	int32 DestCount)
{
	static TGlobalResource<FNiagaraSortingDummyUAV> NiagaraSortingDummyUAV[NIAGARA_COPY_BUFFER_BUFFER_COUNT];
		
	FRHIComputeShader* ComputeShaderRHI = GetComputeShader();
	check(DestCount > 0 && DestCount <= NIAGARA_COPY_BUFFER_BUFFER_COUNT);

	RHICmdList.SetShaderResourceViewParameter(ComputeShaderRHI, SourceData.GetBaseIndex(), InSourceData);

	FUintVector4 CopyParamsValue(StartingIndex, 0, 0, 0);
	for (int32 Index = 0; Index < DestCount; ++Index)
	{
		SetUAVParameter(RHICmdList, ComputeShaderRHI, DestData[Index], InDestDatas[Index]);
		CopyParamsValue[Index + 1] = InUsedIndexCounts[Index];
	}

	for (int32 Index = DestCount; Index < NIAGARA_COPY_BUFFER_BUFFER_COUNT; ++Index)
	{
		RHICmdList.TransitionResource(EResourceTransitionAccess::ERWNoBarrier, EResourceTransitionPipeline::EComputeToCompute, NiagaraSortingDummyUAV[Index].Buffer.UAV);
		SetUAVParameter(RHICmdList, ComputeShaderRHI, DestData[Index], NiagaraSortingDummyUAV[Index].Buffer.UAV);
	}

	SetShaderValue(RHICmdList, ComputeShaderRHI, CopyParams, CopyParamsValue);
}

void FNiagaraCopyIntBufferRegionCS::UnbindBuffers(FRHICommandList& RHICmdList)
{
	FRHIComputeShader* ComputeShaderRHI = GetComputeShader();
	SetSRVParameter(RHICmdList, ComputeShaderRHI, SourceData, nullptr);
	for (int32 Index = 0; Index < NIAGARA_COPY_BUFFER_BUFFER_COUNT; ++Index)
	{
		SetUAVParameter(RHICmdList, ComputeShaderRHI, DestData[Index], nullptr);
	}
}


