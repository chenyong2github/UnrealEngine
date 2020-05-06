// Copyright Epic Games, Inc. All Rights Reserved.

/*==============================================================================
NiagaraGPURayTracingTransformsShader.cpp : Niagara shader to generate the ray tracing instances information.
==============================================================================*/

#include "NiagaraGPURayTracingTransformsShader.h"
#include "NiagaraGPUSortInfo.h"
#include "ShaderParameterUtils.h"

IMPLEMENT_GLOBAL_SHADER(FNiagaraGPURayTracingTransformsCS, "/Plugin/FX/Niagara/Private/NiagaraGPURayTracingTransforms.usf", "NiagaraGPURayTracingTransformsCS", SF_Compute);

bool FNiagaraGPURayTracingTransformsCS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return (ShouldCompileRayTracingShadersForProject(Parameters.Platform) && RHISupportsComputeShaders(Parameters.Platform) && !(Parameters.Platform == EShaderPlatform::SP_METAL || Parameters.Platform == EShaderPlatform::SP_METAL_TVOS || IsMobilePlatform(Parameters.Platform)));
}

void FNiagaraGPURayTracingTransformsCS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), ThreadGroupSize);
}

FNiagaraGPURayTracingTransformsCS::FNiagaraGPURayTracingTransformsCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	: FGlobalShader(Initializer)
{
	NiagaraOffsetsParam.Bind(Initializer.ParameterMap, TEXT("NiagaraOffsets"));
	LocalToWorldParam.Bind(Initializer.ParameterMap, TEXT("LocalTransform"));
	TLASTransformsParam.Bind(Initializer.ParameterMap, TEXT("TLASTransforms"));

	NiagaraParticleDataFloat.Bind(Initializer.ParameterMap, TEXT("NiagaraParticleDataFloat"));
	FloatDataOffset.Bind(Initializer.ParameterMap, TEXT("NiagaraFloatDataOffset"));
	FloatDataStride.Bind(Initializer.ParameterMap, TEXT("NiagaraFloatDataStride"));
	GPUInstanceCountParams.Bind(Initializer.ParameterMap, TEXT("GPUInstanceCountParams"));
	GPUInstanceCountInputBuffer.Bind(Initializer.ParameterMap, TEXT("GPUInstanceCountInputBuffer"));
}

void FNiagaraGPURayTracingTransformsCS::SetParameters(
	FRHICommandList& RHICmdList, 
	uint32 CPUInstancesCount,
	FRHIShaderResourceView*	NiagaraFloatBuffer,
	uint32 FloatDataOffsetValue,
	uint32 FloatDataStrideValue,
	uint32 GPUInstanceCountOffset,
	FRHIShaderResourceView* GPUInstanceCountInputSRV,
	const FUintVector4& NiagaraOffsets,
	const FMatrix& PrimitiveLocalToWorld,
	FRHIUnorderedAccessView* GPUInstancesTransformsUAV)
{
	FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();

	RHICmdList.SetShaderParameter(ComputeShaderRHI, NiagaraOffsetsParam.GetBufferIndex(), NiagaraOffsetsParam.GetBaseIndex(), NiagaraOffsetsParam.GetNumBytes(), &NiagaraOffsets);

	SetShaderValue(RHICmdList, ComputeShaderRHI, LocalToWorldParam, PrimitiveLocalToWorld);

	RHICmdList.SetShaderResourceViewParameter(ComputeShaderRHI, NiagaraParticleDataFloat.GetBaseIndex(), NiagaraFloatBuffer);
	RHICmdList.SetShaderParameter(ComputeShaderRHI, FloatDataOffset.GetBufferIndex(), FloatDataOffset.GetBaseIndex(), FloatDataOffset.GetNumBytes(), &FloatDataOffsetValue);
	RHICmdList.SetShaderParameter(ComputeShaderRHI, FloatDataStride.GetBufferIndex(), FloatDataStride.GetBaseIndex(), FloatDataStride.GetNumBytes(), &FloatDataStrideValue);

	const FUintVector4 GPUInstanceCountParamsValue(CPUInstancesCount, GPUInstanceCountOffset, 0, 0);
	RHICmdList.SetShaderParameter(ComputeShaderRHI, GPUInstanceCountParams.GetBufferIndex(), GPUInstanceCountParams.GetBaseIndex(), GPUInstanceCountParams.GetNumBytes(), &GPUInstanceCountParamsValue);
	RHICmdList.SetShaderResourceViewParameter(ComputeShaderRHI, GPUInstanceCountInputBuffer.GetBaseIndex(), GPUInstanceCountInputSRV);

	SetUAVParameter(RHICmdList, ComputeShaderRHI, TLASTransformsParam, GPUInstancesTransformsUAV);
}

void FNiagaraGPURayTracingTransformsCS::UnbindBuffers(FRHICommandList& RHICmdList)
{
	FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();
	if (NiagaraParticleDataFloat.IsBound())
	{
		RHICmdList.SetShaderResourceViewParameter(ComputeShaderRHI, NiagaraParticleDataFloat.GetBaseIndex(), nullptr);
	}

	if (GPUInstanceCountInputBuffer.IsBound())
	{
		RHICmdList.SetShaderResourceViewParameter(ComputeShaderRHI, GPUInstanceCountInputBuffer.GetBaseIndex(), nullptr);
	}
}

