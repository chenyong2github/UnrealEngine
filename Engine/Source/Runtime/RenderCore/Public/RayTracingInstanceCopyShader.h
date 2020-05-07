// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShaderParameters.h"
#include "ShaderParameterUtils.h"
#include "Shader.h"
#include "GlobalShader.h"

struct RayTracingInstanceCopyCS : public FGlobalShader
{
	DECLARE_EXPORTED_SHADER_TYPE(RayTracingInstanceCopyCS, Global, RENDERCORE_API);

public:
	static constexpr uint32 ThreadGroupSize = 64;

	RayTracingInstanceCopyCS() {}
	RayTracingInstanceCopyCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		InstancesCPUCountParam.Bind(Initializer.ParameterMap, TEXT("InstancesCPUCount"), SPF_Mandatory);
		DescBufferOffsetParam.Bind(Initializer.ParameterMap, TEXT("DescBufferOffset"), SPF_Mandatory);
		InstancesTransformsParam.Bind(Initializer.ParameterMap, TEXT("InstancesTransforms"), SPF_Mandatory);
		InstancesDescriptorsParam.Bind(Initializer.ParameterMap, TEXT("InstancesDescriptors"), SPF_Mandatory);
	}

	static const TCHAR* GetSourceFilename() { return TEXT("/Engine/Private/Raytracing/RayTracingInstanceCopy.usf"); }	
	static const TCHAR* GetFunctionName() { return TEXT("RayTracingInstanceCopyShaderCS"); }

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
	static inline void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), ThreadGroupSize);
	}

	const FShaderParameter& GetInstancesCountParam() const { return InstancesCPUCountParam; }
	inline const FShaderParameter& GetInstancesDescBufferOffsetParam() const { return DescBufferOffsetParam; }
	inline const FShaderResourceParameter& GetInstancesTransformsParam() const { return InstancesTransformsParam; }	
	inline const FShaderResourceParameter& GetInstancesDescriptorsParam() const { return InstancesDescriptorsParam; }

private:
	LAYOUT_FIELD(FShaderParameter, InstancesCPUCountParam);
	LAYOUT_FIELD(FShaderParameter, DescBufferOffsetParam);
	LAYOUT_FIELD(FShaderResourceParameter, InstancesTransformsParam);
	LAYOUT_FIELD(FShaderResourceParameter, InstancesDescriptorsParam);
};

/**
 * CS can be dispatched from inside low level RHIs via FRHICommandList_RecursiveHazardous. 
 * ResourceBindCallback is provided to allow the RHI to override how the UAV resource is bound to the underlying platform context.
 */
inline void CopyRayTracingGPUInstances(FRHICommandList& RHICmdList, uint32 InstancesCount, int32 DescBufferOffset, FRHIShaderResourceView* TransformsSRV, FRHIUnorderedAccessView* InstancesDescUAV, TFunctionRef<void(FRHIComputeShader*, const FShaderResourceParameter&, const FShaderResourceParameter&, bool)> ResourceBindCallback)
{
	TShaderMapRef<RayTracingInstanceCopyCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	FRHIComputeShader* ShaderRHI = ComputeShader.GetComputeShader();
	RHICmdList.SetComputeShader(ShaderRHI);

	SetShaderValue(RHICmdList, ShaderRHI, ComputeShader->GetInstancesCountParam(), InstancesCount);
	SetShaderValue(RHICmdList, ShaderRHI, ComputeShader->GetInstancesDescBufferOffsetParam(), DescBufferOffset);

	ResourceBindCallback(ShaderRHI, ComputeShader->GetInstancesTransformsParam(), ComputeShader->GetInstancesDescriptorsParam(), true);

	uint32 NGroups = FMath::DivideAndRoundUp(InstancesCount, RayTracingInstanceCopyCS::ThreadGroupSize);
	RHICmdList.DispatchComputeShader(NGroups, 1, 1);

	ResourceBindCallback(ShaderRHI, ComputeShader->GetInstancesTransformsParam(), ComputeShader->GetInstancesDescriptorsParam(), false);
}

inline void CopyRayTracingGPUInstances(FRHICommandList& RHICmdList, int32 InstancesCount, int32 DescBufferOffset, FRHIShaderResourceView* TransformsSRV, FRHIUnorderedAccessView* InstancesDescUAV)
{
	CopyRayTracingGPUInstances(RHICmdList, InstancesCount, DescBufferOffset, TransformsSRV, InstancesDescUAV,
		[&RHICmdList, TransformsSRV, InstancesDescUAV](
			FRHIComputeShader* ShaderRHI, 
			const FShaderResourceParameter& InstancesTransformsParam,
			const FShaderResourceParameter& InstancesDescriptorsParam, 
			bool bSet)
	{
		SetSRVParameter(RHICmdList, ShaderRHI, InstancesTransformsParam, bSet ? TransformsSRV : nullptr);
		SetUAVParameter(RHICmdList, ShaderRHI, InstancesDescriptorsParam, bSet ? InstancesDescUAV : nullptr);
	}
	);
}
