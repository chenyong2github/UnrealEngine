// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComputeFramework/ComputeKernelShaderType.h"
#include "ShaderCompiler.h"
#include "RHI.h"
#include "ComputeFramework/ComputeKernelResource.h"

void FComputeKernelShaderType::BeginCompileShader(
	uint32 ShaderMapId,
	EShaderPlatform ShaderPlatform,
	FComputeKernelResource* KernelShader,
	TArray<FShaderCommonCompileJobPtr>& InOutNewJobs
	)
{
	FShaderCompileJob* NewJob = GShaderCompilingManager->PrepareShaderCompileJob(ShaderMapId, FShaderCompileJobKey(this, nullptr, KernelShader->GetPermutationId()), EShaderCompileJobPriority::Low);
	if (NewJob)
	{
		NewJob->Input.Target = FShaderTarget(SF_Compute, ShaderPlatform);
		NewJob->Input.ShaderFormat = LegacyShaderPlatformToShaderFormat(ShaderPlatform);

		NewJob->Input.VirtualSourceFilePath = KernelShader->GetSourceFileName();
		NewJob->Input.EntryPointName = KernelShader->GetEntryPointName();

		NewJob->Input.SharedEnvironment = KernelShader->CreateShaderCompilationEnvironment(ShaderPlatform);

		//NewJob->Input.Environment.SetDefine(TEXT("GPU_SIMULATION"), 1);
		//NewJob->Input.Environment.IncludeVirtualPathToContentsMap.Add(TEXT("/Engine/Generated/NiagaraEmitterInstance.ush"), Script->HlslOutput);

		SetupCompileEnvironment(ShaderPlatform, KernelShader, NewJob->Input.Environment);

		::GlobalBeginCompileShader(
			KernelShader->GetFriendlyName(),
			nullptr,
			this,
			nullptr,
			KernelShader->GetPermutationId(),
			KernelShader->GetSourceFileName(),
			KernelShader->GetEntryPointName(),
			FShaderTarget(SF_Compute, ShaderPlatform),
			NewJob->Input
		);
		InOutNewJobs.Add(FShaderCommonCompileJobPtr(NewJob));
	}
}