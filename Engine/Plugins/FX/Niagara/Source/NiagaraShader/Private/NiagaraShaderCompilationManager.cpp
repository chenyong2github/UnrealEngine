// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraShaderCompilationManager.h"
#include "NiagaraShared.h"
#if WITH_EDITOR
#include "Interfaces/IShaderFormat.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#endif
#include "ShaderCompiler.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "UObject/UObjectThreadContext.h"
#include "GlobalShader.h"

DEFINE_LOG_CATEGORY_STATIC(LogNiagaraShaderCompiler, All, All);

static int32 GShowNiagaraShaderWarnings = 0;
static FAutoConsoleVariableRef CVarShowNiagaraShaderWarnings(
	TEXT("niagara.ShowShaderCompilerWarnings"),
	GShowNiagaraShaderWarnings,
	TEXT("When set to 1, will display all warnings from Niagara shader compiles.")
	);

#if WITH_EDITOR

NIAGARASHADER_API FNiagaraShaderCompilationManager GNiagaraShaderCompilationManager;

NIAGARASHADER_API void FNiagaraShaderCompilationManager::AddJobs(TArray<FShaderCommonCompileJob*> InNewJobs)
{
	check(IsInGameThread());
	JobQueue.Append(InNewJobs);
	
	for (FShaderCommonCompileJob *Job : InNewJobs)
	{
		FNiagaraShaderMapCompileResults& ShaderMapInfo = NiagaraShaderMapJobs.FindOrAdd(Job->Id);
//		ShaderMapInfo.bApplyCompletedShaderMapForRendering = bApplyCompletedShaderMapForRendering;
//		ShaderMapInfo.bRecreateComponentRenderStateOnCompletion = bRecreateComponentRenderStateOnCompletion;
		ShaderMapInfo.NumJobsQueued++;

		FShaderCompileJob& CurrentJob = *((FShaderCompileJob*) Job);

		// Fast math breaks The ExecGrid layout script because floor(x/y) returns a bad value if x == y. Yay.
		if (IsMetalPlatform((EShaderPlatform)CurrentJob.Input.Target.Platform))
		{
			CurrentJob.Input.Environment.CompilerFlags.Add(CFLAG_NoFastMath);
		}

		UE_LOG(LogNiagaraShaderCompiler, Verbose, TEXT("Adding niagara gpu shader compile job... %s"), *CurrentJob.Input.DebugGroupName);

		static ITargetPlatformManagerModule& TPM = GetTargetPlatformManagerRef();
		const FName Format = LegacyShaderPlatformToShaderFormat(EShaderPlatform(CurrentJob.Input.Target.Platform));
		FString AbsoluteDebugInfoDirectory = IFileManager::Get().ConvertToAbsolutePathForExternalAppForWrite(*(FPaths::ProjectSavedDir() / TEXT("ShaderDebugInfo")));
		FPaths::NormalizeDirectoryName(AbsoluteDebugInfoDirectory);
		CurrentJob.Input.DumpDebugInfoPath = AbsoluteDebugInfoDirectory / Format.ToString() / CurrentJob.Input.DebugGroupName;
		if (!IFileManager::Get().DirectoryExists(*CurrentJob.Input.DumpDebugInfoPath))
		{
			verifyf(IFileManager::Get().MakeDirectory(*CurrentJob.Input.DumpDebugInfoPath, true), TEXT("Failed to create directory for shader debug info '%s'"), *CurrentJob.Input.DumpDebugInfoPath);
		}
	}
	GShaderCompilingManager->AddJobs(InNewJobs, true, false, FString(), FString(), true);
}

void FNiagaraShaderCompilationManager::ProcessAsyncResults()
{
	check(IsInGameThread());

	TArray<int32> FinalizedShaderMapIDs;
	for (int32 JobIndex = JobQueue.Num() - 1; JobIndex >= 0; JobIndex--)
	{
		FShaderCompileJob* Job = (FShaderCompileJob*)(JobQueue[JobIndex]);
		if (Job->bFinalized)
		{
			FinalizedShaderMapIDs.Add(Job->Id);
		}
	}

	// We do this because the finalization flag is set by another thread, so the manager might not have had a chance to fully process the result.
	GShaderCompilingManager->FinishCompilation(NULL, FinalizedShaderMapIDs);

	// Process the results from the shader compile worker
	for (int32 JobIndex = JobQueue.Num() - 1; JobIndex >= 0; JobIndex--)
	{
		FShaderCompileJob& CurrentJob = *((FShaderCompileJob*)(JobQueue[JobIndex]));

		if (!CurrentJob.bFinalized)
		{
			continue;
		}

		CurrentJob.bSucceeded = CurrentJob.Output.bSucceeded;
		if (CurrentJob.Output.bSucceeded)
		{
			UE_LOG(LogNiagaraShaderCompiler, Verbose, TEXT("GPU shader compile succeeded. Id %d"), CurrentJob.Id);
		}
		else
		{
			UE_LOG(LogNiagaraShaderCompiler, Warning, TEXT("GPU shader compile failed! Id %d"), CurrentJob.Id);
		}

		FNiagaraShaderMapCompileResults& ShaderMapResults = NiagaraShaderMapJobs.FindChecked(CurrentJob.Id);
		ShaderMapResults.FinishedJobs.Add(&CurrentJob);
		ShaderMapResults.bAllJobsSucceeded = ShaderMapResults.bAllJobsSucceeded && CurrentJob.bSucceeded;
		JobQueue.RemoveAt(JobIndex);
	}

	// Get all Niagara shader maps to finalize
	//
	for (TMap<int32, FNiagaraShaderMapCompileResults>::TIterator It(NiagaraShaderMapJobs); It; ++It)
	{
		const FNiagaraShaderMapCompileResults& Results = It.Value();

		if (Results.FinishedJobs.Num() == Results.NumJobsQueued)
		{
			PendingFinalizeNiagaraShaderMaps.Add(It.Key(), FNiagaraShaderMapFinalizeResults(Results));
			NiagaraShaderMapJobs.Remove(It.Key());
		}
	}

	if (PendingFinalizeNiagaraShaderMaps.Num() > 0)
	{
		ProcessCompiledNiagaraShaderMaps(PendingFinalizeNiagaraShaderMaps, 10);
	}
}


void FNiagaraShaderCompilationManager::ProcessCompiledNiagaraShaderMaps(TMap<int32, FNiagaraShaderMapFinalizeResults>& CompiledShaderMaps, float TimeBudget)
{
	check(IsInGameThread());

	// Keeps shader maps alive as they are passed from the shader compiler and applied to the owning Script
	TArray<TRefCountPtr<FNiagaraShaderMap> > LocalShaderMapReferences;
	TMap<FNiagaraShaderScript*, FNiagaraShaderMap*> ScriptsToUpdate;

	// Process compiled shader maps in FIFO order, in case a shader map has been enqueued multiple times,
	// Which can happen if a script is edited while a background compile is going on
	for (TMap<int32, FNiagaraShaderMapFinalizeResults>::TIterator ProcessIt(CompiledShaderMaps); ProcessIt; ++ProcessIt)
	{
		TRefCountPtr<FNiagaraShaderMap> ShaderMap = NULL;
		TArray<FNiagaraShaderScript*>* Scripts = NULL;

		for (TMap<TRefCountPtr<FNiagaraShaderMap>, TArray<FNiagaraShaderScript*> >::TIterator ShaderMapIt(FNiagaraShaderMap::GetInFlightShaderMaps()); ShaderMapIt; ++ShaderMapIt)
		{
			if (ShaderMapIt.Key()->GetCompilingId() == ProcessIt.Key())
			{
				ShaderMap = ShaderMapIt.Key();
				Scripts = &ShaderMapIt.Value();
				break;
			}
		}

		if (ShaderMap && Scripts)
		{
			TArray<FString> Errors;
			FNiagaraShaderMapFinalizeResults& CompileResults = ProcessIt.Value();
			const TArray<FShaderCommonCompileJob*>& ResultArray = CompileResults.FinishedJobs;

			// Make a copy of the array as this entry of FNiagaraShaderMap::ShaderMapsBeingCompiled will be removed below
			TArray<FNiagaraShaderScript*> ScriptArray = *Scripts;
			bool bSuccess = true;

			for (int32 JobIndex = 0; JobIndex < ResultArray.Num(); JobIndex++)
			{
				FShaderCompileJob& CurrentJob = *((FShaderCompileJob*)(ResultArray[JobIndex]));
				bSuccess = bSuccess && CurrentJob.bSucceeded;

				if (bSuccess)
				{
					check(CurrentJob.Output.ShaderCode.GetShaderCodeSize() > 0);
				}

				if (GShowNiagaraShaderWarnings || !CurrentJob.bSucceeded)
				{
					for (int32 ErrorIndex = 0; ErrorIndex < CurrentJob.Output.Errors.Num(); ErrorIndex++)
					{
						Errors.AddUnique(CurrentJob.Output.Errors[ErrorIndex].GetErrorString().Replace(TEXT("Error"), TEXT("Err0r")));
					}

					if (CurrentJob.Output.Errors.Num())
					{
						UE_LOG(LogNiagaraShaderCompiler, Display, TEXT("There were issues for job \"%s\""), *CurrentJob.Input.DebugGroupName);
						for (const FShaderCompilerError& Error : CurrentJob.Output.Errors)
						{
							UE_LOG(LogNiagaraShaderCompiler, Warning, TEXT("%s"), *Error.GetErrorString())
						}
					}
				}
				else
				{
					UE_LOG(LogNiagaraShaderCompiler, Verbose, TEXT("Shader compile job \"%s\" completed."), *CurrentJob.Input.DebugGroupName);
				}
			}

			bool bShaderMapComplete = true;
			if (bSuccess)
			{
				bShaderMapComplete = ShaderMap->ProcessCompilationResults(ResultArray, CompileResults.FinalizeJobIndex, TimeBudget);
			}

			if (bShaderMapComplete)
			{
				ShaderMap->SetCompiledSuccessfully(bSuccess);

				// Pass off the reference of the shader map to LocalShaderMapReferences
				LocalShaderMapReferences.Add(ShaderMap);
				FNiagaraShaderMap::GetInFlightShaderMaps().Remove(ShaderMap);

				for (FNiagaraShaderScript* Script : ScriptArray)
				{
					FNiagaraShaderMap* CompletedShaderMap = ShaderMap;

					Script->RemoveOutstandingCompileId(ShaderMap->GetCompilingId());

					// Only process results that still match the ID which requested a compile
					// This avoids applying shadermaps which are out of date and a newer one is in the async compiling pipeline
					if (Script->IsSame(CompletedShaderMap->GetShaderMapId()))
					{
						if (Errors.Num() != 0)
						{
							FString SourceCode;
							Script->GetScriptHLSLSource(SourceCode);
							UE_LOG(LogNiagaraShaderCompiler, Log, TEXT("Compile output as text:"));
							UE_LOG(LogNiagaraShaderCompiler, Log, TEXT("==================================================================================="));
							TArray<FString> OutputByLines;
							SourceCode.ParseIntoArrayLines(OutputByLines, false);
							for (int32 i = 0; i < OutputByLines.Num(); i++)
							{
								UE_LOG(LogNiagaraShaderCompiler, Log, TEXT("/*%04d*/\t\t%s"), i + 1, *OutputByLines[i]);
							}
							UE_LOG(LogNiagaraShaderCompiler, Log, TEXT("==================================================================================="));
						}

						if (!bSuccess)
						{
							// Propagate error messages
							Script->SetCompileErrors(Errors);
							ScriptsToUpdate.Add(Script, NULL);

							for (int32 ErrorIndex = 0; ErrorIndex < Errors.Num(); ErrorIndex++)
							{
								FString ErrorMessage = Errors[ErrorIndex];
								// Work around build machine string matching heuristics that will cause a cook to fail
								ErrorMessage.ReplaceInline(TEXT("error "), TEXT("err0r "), ESearchCase::CaseSensitive);
								UE_LOG(LogNiagaraShaderCompiler, Warning, TEXT("	%s"), *ErrorMessage);
							}
						}
						else
						{
							// if we succeeded and our shader map is not complete this could be because the script was being edited quicker then the compile could be completed
							// Don't modify scripts for which the compiled shader map is no longer complete
							// This can happen if a script being compiled is edited
							if (CompletedShaderMap->IsComplete(Script, true))
							{
								ScriptsToUpdate.Add(Script, CompletedShaderMap);
							}

							if (GShowNiagaraShaderWarnings && Errors.Num() > 0)
							{
								UE_LOG(LogNiagaraShaderCompiler, Warning, TEXT("Warnings while compiling Niagara Script %s for platform %s:"),
									*Script->GetFriendlyName(),
									*LegacyShaderPlatformToShaderFormat(ShaderMap->GetShaderPlatform()).ToString());
								for (int32 ErrorIndex = 0; ErrorIndex < Errors.Num(); ErrorIndex++)
								{
									UE_LOG(LogNiagaraShaderCompiler, Warning, TEXT("	%s"), *Errors[ErrorIndex]);
								}
							}
						}
					}
					else
					{
						// Can't call NotifyCompilationFinished() when post-loading. 
						// This normally happens when compiled in-sync for which the callback is not required.
						if (CompletedShaderMap->IsComplete(Script, true) && !FUObjectThreadContext::Get().IsRoutingPostLoad)
						{
							Script->NotifyCompilationFinished();
						}
					}
				}

				// Cleanup shader jobs and compile tracking structures
				for (int32 JobIndex = 0; JobIndex < ResultArray.Num(); JobIndex++)
				{
					delete ResultArray[JobIndex];
				}

				CompiledShaderMaps.Remove(ShaderMap->GetCompilingId());
			}

			if (TimeBudget < 0)
			{
				break;
			}
		}
	}

	if (ScriptsToUpdate.Num() > 0)
	{
		for (TMap<FNiagaraShaderScript*, FNiagaraShaderMap*>::TConstIterator It(ScriptsToUpdate); It; ++It)
		{
			FNiagaraShaderScript* Script = It.Key();
			FNiagaraShaderMap* ShaderMap = It.Value();
			//check(!ShaderMap || ShaderMap->IsValidForRendering());

			Script->SetGameThreadShaderMap(It.Value());

			ENQUEUE_RENDER_COMMAND(FSetShaderMapOnScriptResources)(
				[Script, ShaderMap](FRHICommandListImmediate& RHICmdList)
				{
					Script->SetRenderingThreadShaderMap(ShaderMap);
				});

			// Can't call NotifyCompilationFinished() when post-loading. 
			// This normally happens when compiled in-sync for which the callback is not required.
			if (!FUObjectThreadContext::Get().IsRoutingPostLoad)
			{
				Script->NotifyCompilationFinished();
			}
		}
	}
}


void FNiagaraShaderCompilationManager::FinishCompilation(const TCHAR* ScriptName, const TArray<int32>& ShaderMapIdsToFinishCompiling)
{
	check(!FPlatformProperties::RequiresCookedData());
	GShaderCompilingManager->FinishCompilation(NULL, ShaderMapIdsToFinishCompiling);
	ProcessAsyncResults();
}

#endif // WITH_EDITOR
