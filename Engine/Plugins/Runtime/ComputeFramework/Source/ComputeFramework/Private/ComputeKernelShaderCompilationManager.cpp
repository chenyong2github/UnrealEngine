// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComputeFramework/ComputeKernelShaderCompilationManager.h"

#include "ComputeFramework/ComputeKernelShared.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "RHIShaderFormatDefinitions.inl"
#include "ShaderCompiler.h"

#if WITH_EDITOR
#include "Interfaces/IShaderFormat.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogComputeKernelShaderCompiler, All, All);

static int32 GShowComputeKernelShaderWarnings = 0;
static FAutoConsoleVariableRef CVarShowComputeKernelShaderWarnings(
	TEXT("ComputeKernel.ShowShaderCompilerWarnings"),
	GShowComputeKernelShaderWarnings,
	TEXT("When set to 1, will display all warnings from ComputeKernel shader compiles.")
	);


#if WITH_EDITOR
FComputeKernelShaderCompilationManager GComputeKernelShaderCompilationManager;

void FComputeKernelShaderCompilationManager::Tick(float DeltaSeconds)
{
	ProcessAsyncResults();
}

void FComputeKernelShaderCompilationManager::AddJobs(TArray<FShaderCommonCompileJobPtr> InNewJobs)
{
	check(IsInGameThread());
	JobQueue.Append(InNewJobs);

	for (FShaderCommonCompileJobPtr& Job : InNewJobs)
	{
		FComputeKernelShaderMapCompileResults& ShaderMapInfo = ComputeKernelShaderMapJobs.FindOrAdd(Job->Id);
		ShaderMapInfo.NumJobsQueued++;

		FShaderCompileJob* CurrentJob = Job->GetSingleShaderJob();

		CurrentJob->Input.DumpDebugInfoRootPath = GShaderCompilingManager->GetAbsoluteShaderDebugInfoDirectory() / CurrentJob->Input.ShaderPlatformName.ToString();
		FPaths::NormalizeDirectoryName(CurrentJob->Input.DumpDebugInfoRootPath);

		CurrentJob->Input.DebugExtension.Empty();
		CurrentJob->Input.DumpDebugInfoPath.Empty();
		if (GShaderCompilingManager->GetDumpShaderDebugInfo() == FShaderCompilingManager::EDumpShaderDebugInfo::Always)
		{
			CurrentJob->Input.DumpDebugInfoRootPath = GShaderCompilingManager->CreateShaderDebugInfoPath(CurrentJob->Input);
		}
	}

	GShaderCompilingManager->SubmitJobs(InNewJobs, FString(), FString());
}

void FComputeKernelShaderCompilationManager::ProcessAsyncResults()
{
	check(IsInGameThread());

	// Process the results from the shader compile worker
	for (int32 JobIndex = JobQueue.Num() - 1; JobIndex >= 0; JobIndex--)
	{
		auto CurrentJob = JobQueue[JobIndex]->GetSingleShaderJob();

		if (!CurrentJob->bReleased)
		{
			continue;
		}

		CurrentJob->bSucceeded = CurrentJob->Output.bSucceeded;
		if (CurrentJob->Output.bSucceeded)
		{
			UE_LOG(LogComputeKernelShaderCompiler, Log, TEXT("GPU shader compile succeeded. Id %d"), CurrentJob->Id);
		}
		else
		{
			UE_LOG(LogComputeKernelShaderCompiler, Log, TEXT("ERROR: GPU shader compile failed! Id %d"), CurrentJob->Id);
		}

		FComputeKernelShaderMapCompileResults& ShaderMapResults = ComputeKernelShaderMapJobs.FindChecked(CurrentJob->Id);
		ShaderMapResults.FinishedJobs.Add(JobQueue[JobIndex]);
		ShaderMapResults.bAllJobsSucceeded = ShaderMapResults.bAllJobsSucceeded && CurrentJob->bSucceeded;
		JobQueue.RemoveAt(JobIndex);
	}

	for (TMap<int32, FComputeKernelShaderMapCompileResults>::TIterator It(ComputeKernelShaderMapJobs); It; ++It)
	{
		const FComputeKernelShaderMapCompileResults& Results = It.Value();

		if (Results.FinishedJobs.Num() == Results.NumJobsQueued)
		{
			PendingFinalizeComputeKernelShaderMaps.Add(It.Key(), FComputeKernelShaderMapCompileResults(Results));
			ComputeKernelShaderMapJobs.Remove(It.Key());
		}
	}

	if (PendingFinalizeComputeKernelShaderMaps.Num() > 0)
	{
		ProcessCompiledComputeKernelShaderMaps(PendingFinalizeComputeKernelShaderMaps, 10);
	}
}

void FComputeKernelShaderCompilationManager::ProcessCompiledComputeKernelShaderMaps(
	TMap<int32, FComputeKernelShaderMapFinalizeResults>& CompiledShaderMaps,
	float TimeBudget)
{
	// Keeps shader maps alive as they are passed from the shader compiler and applied to the owning kernel
	TArray<TRefCountPtr<FComputeKernelShaderMap> > LocalShaderMapReferences;
	TMap<FComputeKernelResource*, FComputeKernelShaderMap*> KernelsToUpdate;

	// Process compiled shader maps in FIFO order, in case a shader map has been enqueued multiple times,
	// Which can happen if a kernel is edited while a background compile is going on
	for (TMap<int32, FComputeKernelShaderMapFinalizeResults>::TIterator ProcessIt(CompiledShaderMaps); ProcessIt; ++ProcessIt)
	{
		TRefCountPtr<FComputeKernelShaderMap> ShaderMap = nullptr;
		TArray<FComputeKernelResource*>* Kernels = nullptr;

		for (TMap<TRefCountPtr<FComputeKernelShaderMap>, TArray<FComputeKernelResource*> >::TIterator ShaderMapIt(FComputeKernelShaderMap::GetInFlightShaderMaps()); ShaderMapIt; ++ShaderMapIt)
		{
			if (ShaderMapIt.Key()->GetCompilingId() == ProcessIt.Key())
			{
				ShaderMap = ShaderMapIt.Key();
				Kernels = &ShaderMapIt.Value();
				break;
			}
		}

		if (ShaderMap && Kernels)
		{
			TArray<FString> Errors;
			FComputeKernelShaderMapFinalizeResults& CompileResults = ProcessIt.Value();
			TArray<FShaderCommonCompileJobPtr>& ResultArray = CompileResults.FinishedJobs;

			// Make a copy of the array as this entry of FComputeKernelShaderMap::ShaderMapsBeingCompiled will be removed below
			TArray<FComputeKernelResource*> KernelsArray = *Kernels;
			bool bSuccess = true;

			for (int32 JobIndex = 0; JobIndex < ResultArray.Num(); JobIndex++)
			{
				FShaderCompileJob& CurrentJob = static_cast<FShaderCompileJob&>(*ResultArray[JobIndex].GetReference());
				bSuccess = bSuccess && CurrentJob.bSucceeded;

				if (bSuccess)
				{
					check(CurrentJob.Output.ShaderCode.GetShaderCodeSize() > 0);
				}

				if (GShowComputeKernelShaderWarnings || !CurrentJob.bSucceeded)
				{
					for (int32 ErrorIndex = 0; ErrorIndex < CurrentJob.Output.Errors.Num(); ErrorIndex++)
					{
						Errors.AddUnique(CurrentJob.Output.Errors[ErrorIndex].GetErrorString());
					}

					if (CurrentJob.Output.Errors.Num())
					{
						UE_LOG(LogComputeKernelShaderCompiler, Log, TEXT("There were errors for job \"%s\""), *CurrentJob.Input.DebugGroupName)
						for (const FShaderCompilerError& Error : CurrentJob.Output.Errors)
						{
							UE_LOG(LogComputeKernelShaderCompiler, Log, TEXT("Error: %s"), *Error.GetErrorString())
						}
					}
				}
				else
				{
					UE_LOG(LogComputeKernelShaderCompiler, Log, TEXT("There were NO errors for job \"%s\""), *CurrentJob.Input.DebugGroupName);
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
				FComputeKernelShaderMap::GetInFlightShaderMaps().Remove(ShaderMap);

				for (FComputeKernelResource* Kernel : KernelsArray)
				{
					FComputeKernelShaderMap* CompletedShaderMap = ShaderMap;

					Kernel->RemoveOutstandingCompileId(ShaderMap->GetCompilingId());

					// Only process results that still match the ID which requested a compile
					// This avoids applying shadermaps which are out of date and a newer one is in the async compiling pipeline
					if (Kernel->IsSame(CompletedShaderMap->GetShaderMapId()))
					{
						if (Errors.Num() != 0)
						{
							FString SourceCode;
							Kernel->GetHLSLSource(SourceCode);
							UE_LOG(LogComputeKernelShaderCompiler, Log, TEXT("Compile output as text:"));
							UE_LOG(LogComputeKernelShaderCompiler, Log, TEXT("==================================================================================="));
							TArray<FString> OutputByLines;
							SourceCode.ParseIntoArrayLines(OutputByLines, false);
							for (int32 i = 0; i < OutputByLines.Num(); i++)
							{
								UE_LOG(LogComputeKernelShaderCompiler, Log, TEXT("/*%04d*/\t\t%s"), i + 1, *OutputByLines[i]);
							}
							UE_LOG(LogComputeKernelShaderCompiler, Log, TEXT("==================================================================================="));
						}

						if (!bSuccess)
						{
							// Propagate error messages
							Kernel->SetCompileOutputMessages(Errors);
							KernelsToUpdate.Add(Kernel, nullptr);

							for (int32 ErrorIndex = 0; ErrorIndex < Errors.Num(); ErrorIndex++)
							{
								FString ErrorMessage = Errors[ErrorIndex];
								// Work around build machine string matching heuristics that will cause a cook to fail
								ErrorMessage.ReplaceInline(TEXT("error "), TEXT("err0r "), ESearchCase::CaseSensitive);
								UE_LOG(LogComputeKernelShaderCompiler, Warning, TEXT("	%s"), *ErrorMessage);
							}
						}
						else
						{
							// if we succeeded and our shader map is not complete this could be because the kernel was being edited quicker then the compile could be completed
							// Don't modify kernel for which the compiled shader map is no longer complete
							// This shouldn't happen since kernels are pretty much baked in the designated config file.
							if (CompletedShaderMap->IsComplete(Kernel, true))
							{
								KernelsToUpdate.Add(Kernel, CompletedShaderMap);
							}

							if (GShowComputeKernelShaderWarnings && Errors.Num() > 0)
							{
								UE_LOG(LogComputeKernelShaderCompiler, Warning, TEXT("Warnings while compiling ComputeKernel %s for platform %s:"),
									*Kernel->GetFriendlyName(),
									*LegacyShaderPlatformToShaderFormat(ShaderMap->GetShaderPlatform()).ToString());
								for (int32 ErrorIndex = 0; ErrorIndex < Errors.Num(); ErrorIndex++)
								{
									UE_LOG(LogComputeKernelShaderCompiler, Warning, TEXT("	%s"), *Errors[ErrorIndex]);
								}

								Kernel->SetCompileOutputMessages(Errors);
							}
						}
					}
					else
					{
						if (CompletedShaderMap->IsComplete(Kernel, true))
						{
							const FString ShaderFormatName = FDataDrivenShaderPlatformInfo::GetShaderFormat(ShaderMap->GetShaderPlatform()).ToString();
							if (bSuccess)
							{
								const FString SuccessMessage = FString::Printf(TEXT("%s: %s shader compilation success!"), *Kernel->GetFriendlyName(), *ShaderFormatName);
								Kernel->NotifyCompilationFinished(SuccessMessage);
							}
							else
							{
								const FString FailMessage = FString::Printf(TEXT("%s: %s shader compilation failed."), *Kernel->GetFriendlyName(), *ShaderFormatName);
								Kernel->NotifyCompilationFinished(FailMessage);
							}
						}
					}
				}

				// Cleanup shader jobs and compile tracking structures
				ResultArray.Empty();
				CompiledShaderMaps.Remove(ShaderMap->GetCompilingId());
			}

			if (TimeBudget < 0)
			{
				break;
			}
		}
	}

	if (KernelsToUpdate.Num() > 0)
	{
		for (TMap<FComputeKernelResource*, FComputeKernelShaderMap*>::TConstIterator It(KernelsToUpdate); It; ++It)
		{
			FComputeKernelResource* Kernel = It.Key();
			FComputeKernelShaderMap* ShaderMap = It.Value();

			Kernel->SetGameThreadShaderMap(It.Value());

			ENQUEUE_RENDER_COMMAND(FSetShaderMapOnComputeKernel)(
				[Kernel, ShaderMap](FRHICommandListImmediate& RHICmdList)
				{
					Kernel->SetRenderingThreadShaderMap(ShaderMap);
				});

			if (ShaderMap && ShaderMap->CompiledSuccessfully())
			{
				const FString ShaderFormatName = FDataDrivenShaderPlatformInfo::GetShaderFormat(ShaderMap->GetShaderPlatform()).ToString();
				const FString SuccessMessage = FString::Printf(TEXT("%s: %s shader compilation success!"), 
					*Kernel->GetFriendlyName(), 
					*ShaderFormatName);
				Kernel->NotifyCompilationFinished(SuccessMessage);
			}
			else
			{
				const FString FailMessage = FString::Printf(TEXT("%s: Shader compilation failed."), *Kernel->GetFriendlyName());
				Kernel->NotifyCompilationFinished(FailMessage);
			}
		}
	}
}

void FComputeKernelShaderCompilationManager::FinishCompilation(const TCHAR* InKernelName, const TArray<int32>& ShaderMapIdsToFinishCompiling)
{
	check(!FPlatformProperties::RequiresCookedData());

	GShaderCompilingManager->FinishCompilation(NULL, ShaderMapIdsToFinishCompiling);
	ProcessAsyncResults();	// grab compiled shader maps and assign them to their resources

	check(ComputeKernelShaderMapJobs.Num() == 0);
}

#endif // WITH_EDITOR
