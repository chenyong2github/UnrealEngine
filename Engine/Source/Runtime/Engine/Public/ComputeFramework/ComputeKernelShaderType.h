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
	TSharedRef<FShaderCommonCompileJob, ESPMode::ThreadSafe> BeginCompileShader(
		EShaderPlatform ShaderPlatform,
		FComputeKernelResource* KernelShader,
		TArray<TSharedRef<FShaderCommonCompileJob, ESPMode::ThreadSafe>>& InOutNewJobs
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
