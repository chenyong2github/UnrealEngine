// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComputeFramework/ComputeKernelShaderType.h"
#include "ShaderCompiler.h"
#include "RHI.h"
#include "ComputeFramework/ComputeKernelResource.h"

TSharedRef<FShaderCommonCompileJob, ESPMode::ThreadSafe> FComputeKernelShaderType::BeginCompileShader(
	EShaderPlatform ShaderPlatform,
	FComputeKernelResource* KernelShader,
	TArray<TSharedRef<FShaderCommonCompileJob, ESPMode::ThreadSafe>>& InOutNewJobs
	)
{
	FShaderCompileJob* NewJob = new FShaderCompileJob(FShaderCommonCompileJob::GetNextJobId(), nullptr, this, KernelShader->GetPermutationId());
	{
		NewJob->Input.Target = FShaderTarget(SF_Compute, ShaderPlatform);
		NewJob->Input.ShaderFormat = LegacyShaderPlatformToShaderFormat(ShaderPlatform);

		NewJob->Input.VirtualSourceFilePath = KernelShader->GetSourceFileName();
		NewJob->Input.EntryPointName = KernelShader->GetEntryPointName();

		NewJob->Input.SharedEnvironment = KernelShader->CreateShaderCompilationEnvironment(ShaderPlatform);

		//NewJob->Input.Environment.SetDefine(TEXT("GPU_SIMULATION"), 1);
		//NewJob->Input.Environment.IncludeVirtualPathToContentsMap.Add(TEXT("/Engine/Generated/NiagaraEmitterInstance.ush"), Script->HlslOutput);

		SetupCompileEnvironment(ShaderPlatform, KernelShader, NewJob->Input.Environment);
	}

	TSharedRef<FShaderCommonCompileJob, ESPMode::ThreadSafe> SharedJob(NewJob);
	
	::GlobalBeginCompileShader(
		KernelShader->GetFriendlyName(),
		nullptr,
		this,
		nullptr,
		KernelShader->GetSourceFileName(),
		KernelShader->GetEntryPointName(),
		FShaderTarget(SF_Compute, ShaderPlatform),
		SharedJob,
		InOutNewJobs
		);

	return SharedJob;
}