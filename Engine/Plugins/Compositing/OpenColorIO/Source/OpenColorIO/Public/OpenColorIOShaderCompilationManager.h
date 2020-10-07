// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShaderCompiler.h"

class FShaderType;

/** Stores all of the input and output information used to compile a single shader. */
class FOpenColorIOShaderCompileJob
{
public:
	/** Id of the shader map this shader belongs to. */
	uint32 Id;
	/** true if the results of the shader compile have been processed. */
	bool bFinalized;
	/** Output of the shader compile */
	bool bSucceeded;
	bool bOptimizeForLowLatency;
	/** Shader type that this shader belongs to, must be valid */
	FShaderType* ShaderType;
	/** Input for the shader compile */
	FShaderCompilerInput Input;
	FShaderCompilerOutput Output;

	FOpenColorIOShaderCompileJob(uint32 InId, FShaderType* InShaderType) :
		Id(InId),
		bFinalized(false),
		bSucceeded(false),
		bOptimizeForLowLatency(false),
		ShaderType(InShaderType)
	{
	}
};
using FOpenColorIOShaderCompileJobSharedRef = TSharedRef<FOpenColorIOShaderCompileJob, ESPMode::ThreadSafe>;

/** Information tracked for each shader compile worker process instance. */
struct FOpenColorIOShaderCompileWorkerInfo
{
	/** Process handle of the worker app once launched.  Invalid handle means no process. */
	FProcHandle WorkerProcess;

	/** Tracks whether tasks have been issued to the worker. */
	bool bIssuedTasksToWorker;

	/** Whether the worker has been launched for this set of tasks. */
	bool bLaunchedWorker;

	/** Tracks whether all tasks issued to the worker have been received. */
	bool bComplete;

	/** Time at which the worker started the most recent batch of tasks. */
	double StartTime;

	/** Jobs that this worker is responsible for compiling. */
	TArray<FOpenColorIOShaderCompileJobSharedRef> QueuedJobs;

	FOpenColorIOShaderCompileWorkerInfo() :
		bIssuedTasksToWorker(false),
		bLaunchedWorker(false),
		bComplete(false),
		StartTime(0)
	{
	}

	// warning: not virtual
	~FOpenColorIOShaderCompileWorkerInfo()
	{
		if (WorkerProcess.IsValid())
		{
			FPlatformProcess::TerminateProc(WorkerProcess);
			FPlatformProcess::CloseProc(WorkerProcess);
		}
	}
};


/** Results for a single compiled shader map. */
struct FOpenColorIOShaderMapCompileResults
{
	FOpenColorIOShaderMapCompileResults() :
		NumJobsQueued(0),
		bAllJobsSucceeded(true)
	{}

	int32 NumJobsQueued;
	bool bAllJobsSucceeded;
	TArray<FOpenColorIOShaderCompileJobSharedRef> FinishedJobs;
};


/** Results for a single compiled and finalized shader map. */
struct FOpenColorIOShaderMapFinalizeResults : public FOpenColorIOShaderMapCompileResults
{
	/** Tracks finalization progress on this shader map. */
	int32 FinalizeJobIndex;

	FOpenColorIOShaderMapFinalizeResults(const FOpenColorIOShaderMapCompileResults& InCompileResults) :
		FOpenColorIOShaderMapCompileResults(InCompileResults),
		FinalizeJobIndex(0)
	{}
};


// handles finished shader compile jobs, applying of the shaders to their config asset, and some error handling
//
class FOpenColorIOShaderCompilationManager
{
public:
	FOpenColorIOShaderCompilationManager();
	~FOpenColorIOShaderCompilationManager();

	OPENCOLORIO_API void Tick(float DeltaSeconds = 0.0f);
	OPENCOLORIO_API void AddJobs(TArray<FOpenColorIOShaderCompileJobSharedRef> InNewJobs);
	OPENCOLORIO_API void ProcessAsyncResults();

	void FinishCompilation(const TCHAR* InTransformName, const TArray<int32>& ShaderMapIdsToFinishCompiling);

private:
	void ProcessCompiledOpenColorIOShaderMaps(TMap<int32, FOpenColorIOShaderMapFinalizeResults>& CompiledShaderMaps, float TimeBudget);
	void RunCompileJobs();

	void InitWorkerInfo();

	TArray<FOpenColorIOShaderCompileJobSharedRef> JobQueue;

	/** Map from shader map Id to the compile results for that map, used to gather compiled results. */
	TMap<int32, FOpenColorIOShaderMapCompileResults> OpenColorIOShaderMapJobs;

	/** Map from shader map id to results being finalized.  Used to track shader finalizations over multiple frames. */
	TMap<int32, FOpenColorIOShaderMapFinalizeResults> PendingFinalizeOpenColorIOShaderMaps;

	TArray<struct FOpenColorIOShaderCompileWorkerInfo*> WorkerInfos;
};

extern OPENCOLORIO_API FOpenColorIOShaderCompilationManager GOpenColorIOShaderCompilationManager;

