// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComputeFramework/ComputeKernelResource.h"
#include "ComputeFramework/ComputeKernelShaderMap.h"
#include "ComputeFramework/ComputeKernel.h"
#include "ShaderCompiler.h"

#if WITH_EDITOR
void FComputeKernelResource::CreateShaderMapId( 
	FComputeKernelShaderMapId* OutShaderMapId 
	) const
{
}

void FComputeKernelResource::BeginCompileShaderMap(
	EShaderPlatform ShaderPlatform,
	uint32 CompilationFlags,
	const FComputeKernelShaderMapId& ShaderMapId,
	TRefCountPtr<FComputeKernelShaderMap>& OutShaderMap
	)
{
	check(IsInGameThread());

#if WITH_EDITORONLY_DATA
	TRefCountPtr<FComputeKernelShaderMap> NewShaderMap = new FComputeKernelShaderMap();

	const bool bSynchronousCompile = 
		(CompilationFlags & uint32(EComputeKernelCompilationFlags::Synchronous)) || 
		(CompilationFlags & uint32(EComputeKernelFlags::IsDefaultKernel)) || 
		!GShaderCompilingManager->AllowAsynchronousShaderCompiling();

	NewShaderMap->Compile(ShaderPlatform, this, ShaderMapId, bSynchronousCompile);

	if (bSynchronousCompile && NewShaderMap->CompiledSuccessfully())
	{
		OutShaderMap = NewShaderMap;
	}
	else
	{
#if DEBUG_INFINITESHADERCOMPILE
		UE_LOG(ComputeKernel, Display, TEXT("Kicking off shader compilation for FComputeKernelResource [%s], ShaderMap_GT 0x%08X%08X"), TEXT("GetFriendlyName()"), (int)((int64)(NewShaderMap.GetReference()) >> 32), (int)((int64)(NewShaderMap.GetReference())));
#endif
		InFlightCompilationIds.AddUnique(NewShaderMap->GetCompilingId());

		// Async compile, engine will need to utilize fallback while compiling.
		OutShaderMap = nullptr;
	}
#else
	UE_LOG(ComputeKernel, Fatal, TEXT("Shader compilation outside the editor is not supported."));
#endif
}

void FComputeKernelResource::CacheShaders(
	EShaderPlatform ShaderPlatform,
	uint32 CompilationFlags
	)
{
	check(IsInGameThread());

	FComputeKernelShaderMapId ShaderMapId;
	CreateShaderMapId(&ShaderMapId);

	ShaderMap_GT = nullptr;
	{
		ShaderMap_GT = FComputeKernelShaderMap::Find(ShaderPlatform, ShaderMapId);
	}

	const bool bForceRecompile = (CompilationFlags & uint32(EComputeKernelCompilationFlags::Force)) != 0;

	// Attempt to load from the derived data cache uncooked
	if (!bForceRecompile && !ShaderMap_GT && !FPlatformProperties::RequiresCookedData())
	{
		FComputeKernelShaderMap::LoadFromDerivedDataCache(ShaderPlatform, ShaderMapId, this, ShaderMap_GT);
		if (ShaderMap_GT && ShaderMap_GT->IsValid())
		{
			UE_LOG(ComputeKernel, Verbose, TEXT("Loaded FShaderMap [%s] for FComputeKernelResource [%s] from DDC"), *ShaderMapId.GetFriendlyName(), TEXT("GetFriendlyName()"));
		}
		else
		{
			UE_LOG(ComputeKernel, Display, TEXT("Loading FShaderMap for FComputeKernelResource [%s] from DDC failed. Proceeding to compilation"), TEXT("GetFriendlyName()"));
		}
	}

	bool bAssumeShaderMapIsComplete = false;
#if UE_BUILD_SHIPPING || UE_BUILD_TEST
	bAssumeShaderMapIsComplete = FPlatformProperties::RequiresCookedData();
#endif

	if (ShaderMap_GT && ShaderMap_GT->TryToAddToExistingCompilationTask(this))
	{
#if DEBUG_INFINITESHADERCOMPILE
		UE_LOG(ComputeKernel, Display, TEXT("Found in flight compilation task for FComputeKernel %s, linking to other FShaderMap 0x%08X%08X"), TEXT("GetFriendlyName()"), (int)((int64)(GameThreadShaderMap.GetReference()) >> 32), (int)((int64)(GameThreadShaderMap.GetReference())));
#endif

		InFlightCompilationIds.AddUnique(ShaderMap_GT->GetCompilingId());
		UE_LOG(ComputeKernel, Log, TEXT("FComputeKernelResource [%s] found exisiting in flight compilation id [%d]"), TEXT("GetFriendlyName()"), ShaderMap_GT->GetCompilingId());

		// Reset the shader map so fall back rendering continues untill compilation is complete.
		ShaderMap_GT = nullptr;
	}
	else if (bForceRecompile || !ShaderMap_GT || !(bAssumeShaderMapIsComplete || ShaderMap_GT->IsComplete()))
	{
		if (FPlatformProperties::RequiresCookedData())
		{
			UE_LOG(ComputeKernel, Fatal, TEXT("FComputeKernelResource [%s] cannot compile on the fly on cooked target!"), TEXT("GetFriendlyName()"));
			ShaderMap_GT = nullptr;
		}
		else
		{
			if (ShaderMap_GT)
			{
				UE_LOG(ComputeKernel, Log, TEXT("FComputeKernelResource's [%s] FShaderMap [%s] is compiling. Waiting for compilation."), TEXT("GetFriendlyName()"), *ShaderMapId.GetFriendlyName());
			}
			else
			{
				UE_LOG(ComputeKernel, Log, TEXT("FComputeKernelResource's [%s] FShaderMap is missing. Proceeding to compilation."), TEXT("GetFriendlyName()"));
			}

			// Compilation is async unless EComputeKernelCompilationFlags::Synchronous flag is used.
			BeginCompileShaderMap(ShaderPlatform, CompilationFlags, ShaderMapId, ShaderMap_GT);
		}
	}

	FComputeKernelShaderMap* LoadedShaderMap = ShaderMap_RT;
	ENQUEUE_RENDER_COMMAND(FSetRenderThreadShaderMap)(
		[this, LoadedShaderMap](FRHICommandListImmediate& RHICmdList)
	{
		ShaderMap_RT = LoadedShaderMap;
	});
}

#endif
