// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Shader.h"
#include "ShaderCompiler.h"

struct FShaderCompilerEnvironment;
class FComputeKernelResource;

struct FComputeKernelShaderPermutationParameters : public FShaderPermutationParameters
{
	const FComputeKernelResource* KernelShader;

	FComputeKernelShaderPermutationParameters(EShaderPlatform InPlatform, const FComputeKernelResource* InKernelShader)
		: FShaderPermutationParameters(InPlatform)
		, KernelShader(InKernelShader)
	{}
};

class FComputeKernelShaderType : public FShaderType
{
public:
	typedef FShader::CompiledShaderInitializerType CompiledShaderInitializerType;

	FComputeKernelShaderType(
		FTypeLayoutDesc& InTypeLayout,
		const TCHAR* InName,
		const TCHAR* InSourceFilename,
		const TCHAR* InFunctionName,
		uint32 InFrequency,					// ugly - ignored for Niagara shaders but needed for IMPLEMENT_SHADER_TYPE macro magic
		int32 InTotalPermutationCount,
		ConstructSerializedType InConstructSerializedRef,
		ConstructCompiledType InConstructCompiledRef,
		ModifyCompilationEnvironmentType InModifyCompilationEnvironmentRef,
		ShouldCompilePermutationType InShouldCompilePermutationRef,
		ValidateCompiledResultType InValidateCompiledResultRef,
		uint32 InTypeSize,
		const FShaderParametersMetadata* InRootParametersMetadata = nullptr
		)
		: FShaderType(
			EShaderTypeForDynamicCast::ComputeKernel,
			InTypeLayout, 
			InName, 
			InSourceFilename, 
			InFunctionName, 
			SF_Compute, 
			InTotalPermutationCount,
			InConstructSerializedRef,
			InConstructCompiledRef,
			InModifyCompilationEnvironmentRef,
			InShouldCompilePermutationRef,
			InValidateCompiledResultRef,
			InTypeSize,
			InRootParametersMetadata
			)
	{
	}

	void BeginCompileShader(
		uint32 ShaderMapId,
		EShaderPlatform ShaderPlatform,
		FComputeKernelResource* KernelShader,
		TArray<FShaderCommonCompileJobPtr>& InOutNewJobs
		);

	bool ShouldCache(EShaderPlatform ShaderPlatform, const FComputeKernelResource* KernelShader) const
	{
		return ShouldCompilePermutation(FComputeKernelShaderPermutationParameters(ShaderPlatform, KernelShader));
	}

protected:
	void SetupCompileEnvironment(EShaderPlatform ShaderPlatform, const FComputeKernelResource* KernelShader, FShaderCompilerEnvironment& Environment) const
	{
		ModifyCompilationEnvironment(FComputeKernelShaderPermutationParameters(ShaderPlatform, KernelShader), Environment);
	}
};
