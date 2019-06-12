// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*==============================================================================
NiagaraDrawIndirect.cpp : Niagara shader to generate the draw indirect args for Niagara renderers.
==============================================================================*/

#include "NiagaraDrawIndirect.h"
#include "NiagaraGPUSortInfo.h"


IMPLEMENT_GLOBAL_SHADER(FNiagaraDrawIndirectArgsGenCS, "/Plugin/FX/Niagara/Private/NiagaraDrawIndirectArgsGen.usf", "MainCS", SF_Compute);

void FNiagaraDrawIndirectArgsGenCS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), NIAGARA_DRAW_INDIRECT_ARGS_GEN_THREAD_COUNT);
	OutEnvironment.SetDefine(TEXT("NIAGARA_DRAW_INDIRECT_ARGS_SIZE"), NIAGARA_DRAW_INDIRECT_ARGS_SIZE);
}

FNiagaraDrawIndirectArgsGenCS::FNiagaraDrawIndirectArgsGenCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	: FGlobalShader(Initializer)
{
	TaskInfosParam.Bind(Initializer.ParameterMap, TEXT("TaskInfos"));
	InstanceCountsParam.Bind(Initializer.ParameterMap, TEXT("InstanceCounts"));
	DrawIndirectArgsParam.Bind(Initializer.ParameterMap, TEXT("DrawIndirectArgs"));
	TaskCountParam.Bind(Initializer.ParameterMap, TEXT("TaskCount"));
}

bool FNiagaraDrawIndirectArgsGenCS::Serialize(FArchive& Ar)
{
	bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
	Ar << TaskInfosParam;
	Ar << InstanceCountsParam;
	Ar << DrawIndirectArgsParam;
	Ar << TaskCountParam;
	return bShaderHasOutdatedParameters;
}

void FNiagaraDrawIndirectArgsGenCS::SetOutput(FRHICommandList& RHICmdList, FRHIUnorderedAccessView* DrawIndirectArgsUAV, FRHIUnorderedAccessView* InstanceCountsUAV)
{
	FRHIComputeShader* ComputeShaderRHI = GetComputeShader();
	if (DrawIndirectArgsParam.IsBound())
	{
		RHICmdList.SetUAVParameter(ComputeShaderRHI, DrawIndirectArgsParam.GetUAVIndex(), DrawIndirectArgsUAV);
	}
	if (InstanceCountsParam.IsBound())
	{
		RHICmdList.SetUAVParameter(ComputeShaderRHI, InstanceCountsParam.GetUAVIndex(), InstanceCountsUAV);
	}
}

void FNiagaraDrawIndirectArgsGenCS::SetParameters(FRHICommandList& RHICmdList, FRHIShaderResourceView* TaskInfosBuffer, int32 NumArgGenTasks, int32 NumInstanceCountClearTasks)
{
	FRHIComputeShader* ComputeShaderRHI = GetComputeShader();

	RHICmdList.SetShaderResourceViewParameter(ComputeShaderRHI, TaskInfosParam.GetBaseIndex(), TaskInfosBuffer);

	const FUintVector4 TaskCountValue((int32)NumArgGenTasks, (int32)NumInstanceCountClearTasks, (int32)(NumArgGenTasks + NumInstanceCountClearTasks), 0);
	RHICmdList.SetShaderParameter(ComputeShaderRHI, TaskCountParam.GetBufferIndex(), TaskCountParam.GetBaseIndex(), TaskCountParam.GetNumBytes(), &TaskCountValue);
}

void FNiagaraDrawIndirectArgsGenCS::UnbindBuffers(FRHICommandList& RHICmdList)
{
	FRHIComputeShader* ComputeShaderRHI = GetComputeShader();
	if (TaskInfosParam.IsBound())
	{
		RHICmdList.SetShaderResourceViewParameter(ComputeShaderRHI, TaskInfosParam.GetBaseIndex(), nullptr);
	}
	if (DrawIndirectArgsParam.IsBound())
	{
		RHICmdList.SetUAVParameter(ComputeShaderRHI, DrawIndirectArgsParam.GetUAVIndex(), nullptr);
	}
	if (InstanceCountsParam.IsBound())
	{
		RHICmdList.SetUAVParameter(ComputeShaderRHI, InstanceCountsParam.GetUAVIndex(), nullptr);
	}
}
