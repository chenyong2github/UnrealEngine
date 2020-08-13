// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComputeFramework/ComputeKernelShaderMap.h"
#include "ShaderCompiler.h"
#include "ComputeFramework/ComputeKernelShaderType.h"
#include "ComputeFramework/ComputeKernelResource.h"
#include "ComputeFramework/ComputeKernel.h"

IMPLEMENT_TYPE_LAYOUT(FComputeKernelCompilationOutput);
IMPLEMENT_TYPE_LAYOUT(FComputeKernelShaderMapId);
IMPLEMENT_TYPE_LAYOUT(FComputeKernelShaderMapContent);

TMap<TRefCountPtr<FComputeKernelShaderMap>, TArray<const FComputeKernelResource*>> FComputeKernelShaderMap::ComputeKernelShaderMapsBeingCompiled;

static bool ShouldCacheComputeKernelShader(
	EShaderPlatform ShaderPlatform, 
	const FComputeKernelShaderType* ShaderType, 
	const FComputeKernelResource* KernelShader
	)
{
	return ShaderType->ShouldCache(ShaderPlatform, KernelShader) && KernelShader->ShouldCache(ShaderPlatform, ShaderType);
}

void FComputeKernelShaderMap::LoadFromDerivedDataCache(
	EShaderPlatform ShaderPlatform, 
	const FComputeKernelShaderMapId& ShaderMapId, 
	FComputeKernelResource* KernelShader, 
	TRefCountPtr<FComputeKernelShaderMap>& InOutGameThreadShaderMap
	)
{

}

bool FComputeKernelShaderMap::TryToAddToExistingCompilationTask(
	FComputeKernelResource* KernelShader
	)
{
	return false;
}

void FComputeKernelShaderMap::Compile(
	EShaderPlatform ShaderPlatform,
	FComputeKernelResource* KernelResource,
	const FComputeKernelShaderMapId& ShaderMapId,
	bool bSynchronousCompile
	)
{
	check(IsInGameThread());
	check(GetRefCount() > 0);
	
	if (FPlatformProperties::RequiresCookedData())
	{
		UE_LOG(ComputeKernel, Fatal, TEXT("Trying to compile FComputeKernelResource [%s] at run-time is not supported on %s!"), *KernelResource->GetFriendlyName(), FPlatformProperties::PlatformName());
		return;
	}

	TArray<const FComputeKernelResource*>* CorrespondingKernels = ComputeKernelShaderMapsBeingCompiled.Find(this);

	if (CorrespondingKernels)
	{
		// if this shader map is already doing an async, cannot do sync compile at this time.
		check(!bSynchronousCompile);

		CorrespondingKernels->AddUnique(KernelResource);
		return;
	}

	ComputeKernelShaderMapsBeingCompiled.Add(this, { KernelResource });

#if DEBUG_INFINITESHADERCOMPILE
	UE_LOG(ComputeKernel, Display, TEXT("Added FComputeKernelShaderMap 0x%08X%08X with FComputeKernelResource 0x%08X%08X to ComputeKernelShaderMapsBeingCompiled"), (int)((int64)(this) >> 32), (int)((int64)(this)), (int)((int64)(KernelShader) >> 32), (int)((int64)(KernelShader)));
#endif  

	FComputeKernelShaderMapContent* NewContent = new FComputeKernelShaderMapContent(ShaderPlatform);
#if WITH_EDITORONLY_DATA
	NewContent->FriendlyName = KernelResource->GetFriendlyName();
#endif
	NewContent->ShaderMapId = ShaderMapId;
	AssignContent(NewContent);
	
	TArray<TSharedRef<FShaderCommonCompileJob, ESPMode::ThreadSafe>> NewJobs;
	TMap<TShaderTypePermutation<const FShaderType>, TSharedRef<FShaderCommonCompileJob, ESPMode::ThreadSafe>> SharedShaderJobs;

	const TArray<FShaderType*>& ComputeKernelShaderTypeCollection = FShaderType::GetSortedTypes(FShaderType::EShaderTypeForDynamicCast::ComputeKernel);
	for (auto* ShaderType : ComputeKernelShaderTypeCollection)
	{
		FComputeKernelShaderType* ComputeKernelShaderType = ShaderType->GetComputeKernelShaderType();
		if (!ComputeKernelShaderType || !ShouldCacheComputeKernelShader(ShaderPlatform, ComputeKernelShaderType, KernelResource) || NewContent->HasShader(ShaderType, KernelResource->GetPermutationId()))
		{
			continue;
		}

		TSharedRef<FShaderCommonCompileJob, ESPMode::ThreadSafe> Job = ComputeKernelShaderType->BeginCompileShader(
			ShaderPlatform,
			KernelResource,
			NewJobs
			);

		TShaderTypePermutation<const FShaderType> ShaderTypePermutation(ComputeKernelShaderType, KernelResource->GetPermutationId());
		check(!SharedShaderJobs.Find(ShaderTypePermutation));

		SharedShaderJobs.Add(ShaderTypePermutation, Job);

		CompilationRequestId = Job->Id;
	}

	/**
	Register(ShaderPlatform);

	GComputeKernelShaderCompilationManager.AddJobs(NewJobs);

	if (bSynchronousCompile)
	{
		TArray<int32> CurrentShaderMapId;
		CurrentShaderMapId.Add(CompilationRequestId);
		GComputeKernelShaderCompilationManager->FinishCompilation(
			NewContent->FriendlyName,
			CurrentShaderMapId
			);
	}
	**/
}
