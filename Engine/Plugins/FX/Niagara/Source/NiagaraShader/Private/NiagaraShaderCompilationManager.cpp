// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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

DEFINE_LOG_CATEGORY_STATIC(LogNiagaraShaderCompiler, All, All);

static int32 GShowNiagaraShaderWarnings = 0;
static FAutoConsoleVariableRef CVarShowNiagaraShaderWarnings(
	TEXT("niagara.ShowShaderCompilerWarnings"),
	GShowNiagaraShaderWarnings,
	TEXT("When set to 1, will display all warnings from Niagara shader compiles.")
	);

//Disabled until a few issues can be resolved.
int32 GEnableNiagaraShaderCompilerAsyncHack = 0;
static FAutoConsoleVariableRef CVarEnableNiagaraShaderCompilerAsyncHack(
	TEXT("fx.EnableNiagaraShaderCompilerAsyncHack"),
	GEnableNiagaraShaderCompilerAsyncHack,
	TEXT("If > 0 GPU Niagara shaders will be compiled off the main therad. This is a temporary solution until these are offloaded to the shader compiler worker. \n"),
	ECVF_ReadOnly
);

//#define ASYNC_HACK_LOG(...)
#define ASYNC_HACK_LOG(Fmt, ...) if( IsAsyncHackEnabled() ){UE_LOG(LogNiagaraShaderCompiler, Log, Fmt, ##__VA_ARGS__);}

#if WITH_EDITOR



//ASYNC HACK: Temporary code to run this on a worker thread. This will eventually be unnecessary as we'll move this to shadercompilerworker.
//ASYNC HACK: There is a lot of code in here to do with this code being run on shader compiler worker.
//ASYNC HACK: For now I'm leaving this in as these hacks will soon be removed.
//ASYNC HACK: At that point, all this other code will make sense.
//ASYNC HACK: During this time it's going to be very confusing to read this code.

//ASYNC HACK: This async task will be kicked from the GT and will run all current work in JobQueue.
//ASYNC HACK: JobQueue can be added to in this time meaning this worker will run for longer.
//ASYNC HACK: Once this task completes and more Jobs come in, another task will be kicked off. 
//ASYNC HACK: Only one of these tasks can ever be in flight at once! The shader compilation is not thread-safe. 
//ASYNC HACK: It doesn't have to be on the game thread but it can't be parallelized.

class FNiagaraShaderCompilerWorkerAsyncTask
{
	FNiagaraShaderCompilationManager* CompilerWorker;

public:
	FNiagaraShaderCompilerWorkerAsyncTask(FNiagaraShaderCompilationManager* InCompilerWorker)
		: CompilerWorker(InCompilerWorker)
	{

	}

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FNiagaraShaderCompilerWorkerAsyncTask, STATGROUP_TaskGraphTasks);
	}

	ENamedThreads::Type GetDesiredThread()
	{
		return ENamedThreads::AnyBackgroundThreadNormalTask;
	}

	static ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::TrackSubsequents; }

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		check(CompilerWorker);
		CompilerWorker->RunCompileJobs();
	}
};

FNiagaraShaderCompilationManager& FNiagaraShaderCompilationManager::Get()
{
	static FNiagaraShaderCompilationManager Instance;
	return Instance;
}

bool FNiagaraShaderCompilationManager::IsAsyncHackEnabled()const
{
	return FApp::ShouldUseThreadingForPerformance() && GEnableNiagaraShaderCompilerAsyncHack;
}

void FNiagaraShaderCompilationManager::Tick(float DeltaSeconds, bool bBlock)
{
	if (IsAsyncHackEnabled())
	{
		//ASYNC HACK: Temporary code to run this on a worker thread. This will eventually be unnecessary as we'll move this to shadercompilerworker.
		check(IsInGameThread());
	
		//Kick off a new task if the last one has finished processing jobs.
		if (!AsyncWork || AsyncWork->IsComplete())
		{
			if (JobQueue.Num() > 0)
			{
				ASYNC_HACK_LOG(TEXT("AsyncHack: New Compiler Async Task"));
				AsyncWork = TGraphTask<FNiagaraShaderCompilerWorkerAsyncTask>::CreateTask(nullptr, ENamedThreads::GameThread).ConstructAndDispatchWhenReady(this);
			}
		}

		if (bBlock && AsyncWork && !AsyncWork->IsComplete())
		{
			float StartTime = FPlatformTime::Seconds();
			FTaskGraphInterface::Get().WaitUntilTaskCompletes(AsyncWork, ENamedThreads::GameThread_Local);
			AsyncWork = nullptr;
			float EndTime = FPlatformTime::Seconds();
			ASYNC_HACK_LOG(TEXT("AsyncHack: Blocked on Compiler Async Task for %5.6f Seconds"), EndTime - StartTime);
		}
		//ASYNC HACK END: Temporary code to run this on a worker thread. This will eventually be unnecessary as we'll move this to shadercompilerworker.
	}
	else
	{
		RunCompileJobs();
	}
}

FNiagaraShaderCompilationManager::FNiagaraShaderCompilationManager()
{
	// Ew. Should we just use FShaderCompilingManager's workers instead? Is that safe?
	const int32 NumVirtualCores = FPlatformMisc::NumberOfCoresIncludingHyperthreads();
	const uint32 NumNiagaraShaderCompilingThreads = FMath::Min(NumVirtualCores-1, 4);		

	for (uint32 WorkerIndex = 0; WorkerIndex < NumNiagaraShaderCompilingThreads; WorkerIndex++)
	{
		WorkerInfos.Add(new FNiagaraShaderCompileWorkerInfo());
	}

	AsyncWork = nullptr;
}

void FNiagaraShaderCompilationManager::RunCompileJobs()
{
	// If we aren't compiling through workers, so we can just track the serial time here.
//	COOK_STAT(FScopedDurationTimer CompileTimer(NiagaraShaderCookStats::AsyncCompileTimeSec));
	int32 NumActiveThreads = 0;


	ASYNC_HACK_LOG(TEXT("AsyncHack: RunCompileJobs()"));

	for (int32 WorkerIndex = 0; WorkerIndex < WorkerInfos.Num(); WorkerIndex++)
	{
		FNiagaraShaderCompileWorkerInfo& CurrentWorkerInfo = *WorkerInfos[WorkerIndex];

		// If this worker doesn't have any queued jobs, look for more in the input queue
		if (CurrentWorkerInfo.QueuedJobs.Num() == 0)
		{
			check(!CurrentWorkerInfo.bComplete);

			//ASYNC HACK: Temporary code to run this on a worker thread. This will eventually be unnecessary as we'll move this to shadercompilerworker.
			if (IsAsyncHackEnabled())
			{
				check(!IsInGameThread());
				//Lock access to the job queue.
				JobQueueCriticalSection.Lock();
			}
			//ASYNC HACK: Temporary code to run this on a worker thread. This will eventually be unnecessary as we'll move this to shadercompilerworker.

			if (JobQueue.Num() > 0)
			{
				ASYNC_HACK_LOG(TEXT("AsyncHack: Taking %d Jobs from the Job Queue()"), JobQueue.Num());

				bool bAddedLowLatencyTask = false;
				int32 JobIndex = 0;

				// Try to grab up to MaxShaderJobBatchSize jobs
				// Don't put more than one low latency task into a batch
				for (; JobIndex < JobQueue.Num(); JobIndex++)
				{
					CurrentWorkerInfo.QueuedJobs.Add(JobQueue[JobIndex]);
				}

				// Update the worker state as having new tasks that need to be issued					
				// don't reset worker app ID, because the shadercompilerworkers don't shutdown immediately after finishing a single job queue.
				CurrentWorkerInfo.bIssuedTasksToWorker = true;
				CurrentWorkerInfo.bLaunchedWorker = true;
				CurrentWorkerInfo.StartTime = FPlatformTime::Seconds();
				JobQueue.RemoveAt(0, JobIndex);
			}

			//ASYNC HACK: Temporary code to run this on a worker thread. This will eventually be unnecessary as we'll move this to shadercompilerworker.
			if (IsAsyncHackEnabled())
			{
				JobQueueCriticalSection.Unlock();
			}
			//ASYNC HACK: Temporary code to run this on a worker thread. This will eventually be unnecessary as we'll move this to shadercompilerworker.
		}

		if (CurrentWorkerInfo.bIssuedTasksToWorker && CurrentWorkerInfo.bLaunchedWorker)
		{
			NumActiveThreads++;
		}

		if (CurrentWorkerInfo.QueuedJobs.Num() > 0)
		{
			for (int32 JobIndex = 0; JobIndex < CurrentWorkerInfo.QueuedJobs.Num(); JobIndex++)
			{
				FShaderCompileJob& CurrentJob = *((FShaderCompileJob*)(CurrentWorkerInfo.QueuedJobs[JobIndex]));

				ASYNC_HACK_LOG(TEXT("AsyncHack: Processing Compile Job %s"), *CurrentJob.Input.GenerateShaderName());
				check(!CurrentJob.bFinalized);
				CurrentJob.bFinalized = true;

				static ITargetPlatformManagerModule& TPM = GetTargetPlatformManagerRef();

				const FName Format = LegacyShaderPlatformToShaderFormat(EShaderPlatform(CurrentJob.Input.Target.Platform));
				const IShaderFormat* Compiler = TPM.FindShaderFormat(Format);

				if (!Compiler)
				{
					UE_LOG(LogNiagaraShaderCompiler, Fatal, TEXT("Can't compile shaders for format %s, couldn't load compiler dll"), *Format.ToString());
				}
				CA_ASSUME(Compiler != NULL);
				
				// Fast math breaks The ExecGrid layout script because floor(x/y) returns a bad value if x == y. Yay.
				if(IsMetalPlatform((EShaderPlatform)CurrentJob.Input.Target.Platform))
				{
					CurrentJob.Input.Environment.CompilerFlags.Add(CFLAG_NoFastMath);
				}

				UE_LOG(LogNiagaraShaderCompiler, Log, TEXT("Compile Job processing... %s"), *CurrentJob.Input.DebugGroupName);

				FString AbsoluteDebugInfoDirectory = IFileManager::Get().ConvertToAbsolutePathForExternalAppForWrite(*(FPaths::ProjectSavedDir() / TEXT("ShaderDebugInfo")));
				FPaths::NormalizeDirectoryName(AbsoluteDebugInfoDirectory);
				CurrentJob.Input.DumpDebugInfoPath = AbsoluteDebugInfoDirectory / Format.ToString() / CurrentJob.Input.DebugGroupName;
				if (!IFileManager::Get().DirectoryExists(*CurrentJob.Input.DumpDebugInfoPath))
				{
					verifyf(IFileManager::Get().MakeDirectory(*CurrentJob.Input.DumpDebugInfoPath, true), TEXT("Failed to create directory for shader debug info '%s'"), *CurrentJob.Input.DumpDebugInfoPath);
				}

				if (IsValidRef(CurrentJob.Input.SharedEnvironment))
				{
					// Merge the shared environment into the per-shader environment before calling into the compile function
					// Normally this happens in the worker
					CurrentJob.Input.Environment.Merge(*CurrentJob.Input.SharedEnvironment);
				}

				// Compile the shader directly through the platform dll (directly from the shader dir as the working directory)
				Compiler->CompileShader(Format, CurrentJob.Input, CurrentJob.Output, FString(FPlatformProcess::ShaderDir()));

				CurrentJob.bSucceeded = CurrentJob.Output.bSucceeded;

				if (CurrentJob.Output.bSucceeded)
				{
					// Generate a hash of the output and cache it
					// The shader processing this output will use it to search for existing FShaderResources
					CurrentJob.Output.GenerateOutputHash();
					UE_LOG(LogNiagaraShaderCompiler, Log, TEXT("GPU shader compile succeeded. Id %d"), CurrentJob.Id);
				}
				else
				{
					UE_LOG(LogNiagaraShaderCompiler, Warning, TEXT("GPU shader compile failed! Id %d"), CurrentJob.Id);
				}

				CurrentWorkerInfo.bComplete = true;
			}
		}
	}


	//ASYNC HACK: Temporary code to run this on a worker thread. This will eventually be unnecessary as we'll move this to shadercompilerworker.
	if (IsAsyncHackEnabled())
	{
		//Lock access to the results queue.
		ResultsQueueCriticalSection.Lock();
	}
	//ASYNC HACK: Temporary code to run this on a worker thread. This will eventually be unnecessary as we'll move this to shadercompilerworker.

	for (int32 WorkerIndex = 0; WorkerIndex < WorkerInfos.Num(); WorkerIndex++)
	{
		FNiagaraShaderCompileWorkerInfo& CurrentWorkerInfo = *WorkerInfos[WorkerIndex];
		if (CurrentWorkerInfo.bComplete)
		{
			for (int32 JobIndex = 0; JobIndex < CurrentWorkerInfo.QueuedJobs.Num(); JobIndex++)
			{
				FNiagaraShaderMapCompileResults& ShaderMapResults = NiagaraShaderMapJobs.FindChecked(CurrentWorkerInfo.QueuedJobs[JobIndex]->Id);
				ShaderMapResults.FinishedJobs.Add(CurrentWorkerInfo.QueuedJobs[JobIndex]);
				ShaderMapResults.bAllJobsSucceeded = ShaderMapResults.bAllJobsSucceeded && CurrentWorkerInfo.QueuedJobs[JobIndex]->bSucceeded;
			}
		}

		CurrentWorkerInfo.bComplete = false;
		CurrentWorkerInfo.QueuedJobs.Empty();
	}

	//ASYNC HACK: Temporary code to run this on a worker thread. This will eventually be unnecessary as we'll move this to shadercompilerworker.
	if (IsAsyncHackEnabled())
	{
		ResultsQueueCriticalSection.Unlock();
	}
	//ASYNC HACK: Temporary code to run this on a worker thread. This will eventually be unnecessary as we'll move this to shadercompilerworker.
}



NIAGARASHADER_API void FNiagaraShaderCompilationManager::AddJobs(TArray<FShaderCommonCompileJob*> InNewJobs)
{
	//ASYNC HACK: Temporary code to run this on a worker thread. This will eventually be unnecessary as we'll move this to shadercompilerworker.
	if (IsAsyncHackEnabled())
	{
		JobQueueCriticalSection.Lock();
		ResultsQueueCriticalSection.Lock();
		ASYNC_HACK_LOG(TEXT("AsyncHack: Adding %d jobs"), InNewJobs.Num());
	}
	//ASYNC HACK: Temporary code to run this on a worker thread. This will eventually be unnecessary as we'll move this to shadercompilerworker.


	for (FShaderCommonCompileJob *Job : InNewJobs)
	{
		FNiagaraShaderMapCompileResults& ShaderMapInfo = NiagaraShaderMapJobs.FindOrAdd(Job->Id);
//		ShaderMapInfo.bApplyCompletedShaderMapForRendering = bApplyCompletedShaderMapForRendering;
//		ShaderMapInfo.bRecreateComponentRenderStateOnCompletion = bRecreateComponentRenderStateOnCompletion;
		ShaderMapInfo.NumJobsQueued++;
	}

	JobQueue.Append(InNewJobs);

	//ASYNC HACK: Temporary code to run this on a worker thread. This will eventually be unnecessary as we'll move this to shadercompilerworker.
	if (IsAsyncHackEnabled())
	{
		ResultsQueueCriticalSection.Unlock();
		JobQueueCriticalSection.Unlock();
	}
	//ASYNC HACK: Temporary code to run this on a worker thread. This will eventually be unnecessary as we'll move this to shadercompilerworker.
}


void FNiagaraShaderCompilationManager::ProcessAsyncResults()
{
	check(IsInGameThread());

	int32 NumCompilingNiagaraShaderMaps = 0;
	TArray<int32> ShaderMapsToRemove;

	//ASYNC HACK: Temporary code to run this on a worker thread. This will eventually be unnecessary as we'll move this to shadercompilerworker.
	if (IsAsyncHackEnabled())
	{
		ResultsQueueCriticalSection.Lock();
	}
	//ASYNC HACK: Temporary code to run this on a worker thread. This will eventually be unnecessary as we'll move this to shadercompilerworker.

	// Get all Niagara shader maps to finalize
	//
	for (TMap<int32, FNiagaraShaderMapCompileResults>::TIterator It(NiagaraShaderMapJobs); It; ++It)
	{
		const FNiagaraShaderMapCompileResults& Results = It.Value();

		if (Results.FinishedJobs.Num() == Results.NumJobsQueued)
		{
			ShaderMapsToRemove.Add(It.Key());
			PendingFinalizeNiagaraShaderMaps.Add(It.Key(), FNiagaraShaderMapFinalizeResults(Results));

			//ASYNC_HACK_LOG(TEXT("AsyncHack: Grabbing compiled shader map to process."));
		}
	}

	for (int32 RemoveIndex = 0; RemoveIndex < ShaderMapsToRemove.Num(); RemoveIndex++)
	{
		NiagaraShaderMapJobs.Remove(ShaderMapsToRemove[RemoveIndex]);
	}

	NumCompilingNiagaraShaderMaps = NiagaraShaderMapJobs.Num();

	//ASYNC HACK: Temporary code to run this on a worker thread. This will eventually be unnecessary as we'll move this to shadercompilerworker.
	if (IsAsyncHackEnabled())
	{
		ResultsQueueCriticalSection.Unlock();
	}
	//ASYNC HACK: Temporary code to run this on a worker thread. This will eventually be unnecessary as we'll move this to shadercompilerworker.

	if (PendingFinalizeNiagaraShaderMaps.Num() > 0)
	{
		ProcessCompiledNiagaraShaderMaps(PendingFinalizeNiagaraShaderMaps, 0.1f);
	}
}


void FNiagaraShaderCompilationManager::ProcessCompiledNiagaraShaderMaps(
	TMap<int32, FNiagaraShaderMapFinalizeResults>& CompiledShaderMaps,
	float TimeBudget)
{
	check(IsInGameThread());

	ASYNC_HACK_LOG(TEXT("AsyncHack: Processing %d Compiled Shader Maps"), CompiledShaderMaps.Num());

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

			ASYNC_HACK_LOG(TEXT("AsyncHack: Processing %d Finished Jobs"), ResultArray.Num());
			for (int32 JobIndex = 0; JobIndex < ResultArray.Num(); JobIndex++)
			{
				FShaderCompileJob& CurrentJob = *((FShaderCompileJob*)(ResultArray[JobIndex]));
				bSuccess = bSuccess && CurrentJob.bSucceeded;

				ASYNC_HACK_LOG(TEXT("AsyncHack: Processing Finished Job %s"), *CurrentJob.Input.GenerateShaderName());
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
						
						UE_LOG(LogNiagaraShaderCompiler, Display, TEXT("There were issues for job \"%s\""), *CurrentJob.Input.DebugGroupName)
						
						for (const FShaderCompilerError& Error : CurrentJob.Output.Errors)
						{
							UE_LOG(LogNiagaraShaderCompiler, Warning, TEXT("%s"), *Error.GetErrorString())
						}
						
					}
				}
				else
				{
					UE_LOG(LogNiagaraShaderCompiler, Log, TEXT("There were NO errors for job \"%s\""), *CurrentJob.Input.DebugGroupName);
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

	//ASYNC HACK: Temporary code to run this on a worker thread. This will eventually be unnecessary as we'll move this to shadercompilerworker.
	if (IsAsyncHackEnabled())
	{
		Tick(0.0f, true);//Kick current jobs and block.
		//ASYNC HACK: Temporary code to run this on a worker thread. This will eventually be unnecessary as we'll move this to shadercompilerworker.
	}
	else
	{
		RunCompileJobs();	// since we don't async compile through another process, this will run all oustanding jobs
	}
	
	ProcessAsyncResults();	// grab compiled shader maps and assign them to their resources

	check(NiagaraShaderMapJobs.Num() == 0);
}



#endif // WITH_EDITOR
