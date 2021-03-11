// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShaderCompiler.h"
#include "HAL/FileManager.h"
#include "DistributedBuildInterface/Public/DistributedBuildControllerInterface.h"

namespace DistributedShaderCompilerVariables
{
	//TODO: Remove XGE from the name after ensuring that we are not breaking existing configs by doing so.
	int32 MinBatchSize = 20;
	FAutoConsoleVariableRef CVarXGEShaderCompileMinBatchSize(
        TEXT("r.XGEShaderCompile.MinBatchSize"),
        MinBatchSize,
        TEXT("Minimum number of shaders to compile with XGE.\n")
        TEXT("Smaller number of shaders will compile locally."),
        ECVF_Default);
}

bool FShaderCompileDistributedThreadRunnable_Interface::IsSupported()
{
	//TODO Handle Generic response
	return true;
}

class FDistributedShaderCompilerTask
{
public:
	TFuture<FDistributedBuildTaskResult> Future;
	TArray<FShaderCommonCompileJobPtr> ShaderJobs;
	FString InputFilePath;
	FString OutputFilePath;

	FDistributedShaderCompilerTask(TFuture<FDistributedBuildTaskResult>&& Future,TArray<FShaderCommonCompileJobPtr>&& ShaderJobs, FString&& InputFilePath, FString&& OutputFilePath)
		: Future(MoveTemp(Future))
		, ShaderJobs(MoveTemp(ShaderJobs))
		, InputFilePath(MoveTemp(InputFilePath))
		, OutputFilePath(MoveTemp(OutputFilePath))
	{}
};

/** Initialization constructor. */
FShaderCompileDistributedThreadRunnable_Interface::FShaderCompileDistributedThreadRunnable_Interface(class FShaderCompilingManager* InManager, IDistributedBuildController& InController)
	: FShaderCompileThreadRunnableBase(InManager)
	, NumDispatchedJobs(0)
	, CachedController(InController)
{
}

FShaderCompileDistributedThreadRunnable_Interface::~FShaderCompileDistributedThreadRunnable_Interface()
{
}

void FShaderCompileDistributedThreadRunnable_Interface::DispatchShaderCompileJobsBatch(TArray<FShaderCommonCompileJobPtr>& JobsToSerialize)
{
	FString InputFilePath = CachedController.CreateUniqueFilePath();
	FString OutputFilePath = CachedController.CreateUniqueFilePath();

	const FString WorkingDirectory = FPaths::GetPath(InputFilePath);
	const FString InputFileName = FPaths::GetCleanFilename(InputFilePath);
	const FString OutputFileName = FPaths::GetCleanFilename(OutputFilePath);

	const FString WorkerParameters = FString::Printf(TEXT("\"%s/\" %d 0 \"%s\" \"%s\" -xge_int %s%s"),
		*WorkingDirectory,
		Manager->ProcessId,
		*InputFileName,
		*OutputFileName,
		*FCommandLine::GetSubprocessCommandline(),
		GIsBuildMachine ? TEXT(" -buildmachine") : TEXT("")
	);

	// Serialize the jobs to the input file
	FArchive* InputFileAr = IFileManager::Get().CreateFileWriter(*InputFilePath, FILEWRITE_EvenIfReadOnly | FILEWRITE_NoFail);
	FShaderCompileUtilities::DoWriteTasks(JobsToSerialize, *InputFileAr);
	delete InputFileAr;

	// Kick off the job
	NumDispatchedJobs += JobsToSerialize.Num();

	FTaskCommandData TaskCommandData;
	TaskCommandData.Command = Manager->ShaderCompileWorkerName;
	TaskCommandData.CommandArgs = WorkerParameters;
	TaskCommandData.InputFileName = InputFilePath;
	TaskCommandData.Dependencies = GetDependencyFilesForJobs(JobsToSerialize);
	
	DispatchedTasks.Add(
		new FDistributedShaderCompilerTask(
			CachedController.EnqueueTask(TaskCommandData),
			MoveTemp(JobsToSerialize),
			MoveTemp(InputFilePath),
			MoveTemp(OutputFilePath)
		)
	);
}

TArray<FString> FShaderCompileDistributedThreadRunnable_Interface::GetDependencyFilesForJobs(
	TArray<FShaderCommonCompileJobPtr>& Jobs)
{
	TArray<FString> Dependencies;
	uint64 ShaderPlatformMask = 0;
	static_assert(EShaderPlatform::SP_NumPlatforms <= sizeof(ShaderPlatformMask) * 8, "Insufficient bits in ShaderPlatformMask.");
	for (const FShaderCommonCompileJobPtr& Job : Jobs)
	{
		EShaderPlatform ShaderPlatform = EShaderPlatform::SP_PCD3D_SM5;
		const FShaderCompileJob* ShaderJob = Job->GetSingleShaderJob();
		if (ShaderJob)
		{
			ShaderPlatform = ShaderJob->Input.Target.GetPlatform();
			// Add the source shader file and its dependencies.
			AddShaderSourceFileEntry(Dependencies, ShaderJob->Input.VirtualSourceFilePath, ShaderPlatform);
		}
		else
		{
			const FShaderPipelineCompileJob* PipelineJob = Job->GetShaderPipelineJob();
			if (PipelineJob)
			{
				for (const TRefCountPtr<FShaderCompileJob>& CommonCompileJob : PipelineJob->StageJobs)
				{
					if (const FShaderCompileJob* SingleShaderJob = CommonCompileJob->GetSingleShaderJob())
					{
						ShaderPlatform = SingleShaderJob->Input.Target.GetPlatform();
						// Add the source shader file and its dependencies.
						AddShaderSourceFileEntry(Dependencies, SingleShaderJob->Input.VirtualSourceFilePath, ShaderPlatform);
					}
				}
			}
			else
			{
				UE_LOG(LogShaderCompilers, Fatal, TEXT("Unknown shader compilation job type."));
			}
		}
		// Add base dependencies for the platform only once.
		if (!(ShaderPlatformMask & (static_cast<uint64>(1) << ShaderPlatform)))
		{
			ShaderPlatformMask |= (static_cast<uint64>(1) << ShaderPlatform);
			TArray<FString>& ShaderPlatformCacheEntry = PlatformShaderInputFilesCache.FindOrAdd(ShaderPlatform);
			if (!ShaderPlatformCacheEntry.Num())
			{
				GetAllVirtualShaderSourcePaths(ShaderPlatformCacheEntry, ShaderPlatform);
			}
			if (Dependencies.Num())
			{
				for (const FString& Filename : ShaderPlatformCacheEntry)
				{
					Dependencies.AddUnique(Filename);
				}
			}
			else
			{
				Dependencies = ShaderPlatformCacheEntry;
			}
		}
	}

	return Dependencies;
}

int32 FShaderCompileDistributedThreadRunnable_Interface::CompilingLoop()
{
	TArray<FShaderCommonCompileJobPtr> PendingJobs;
	for (int32 PriorityIndex = MaxPriorityIndex; PriorityIndex >= MinPriorityIndex; --PriorityIndex)
	{
		// Grab as many jobs from the job queue as we can
		const EShaderCompileJobPriority Priority = (EShaderCompileJobPriority)PriorityIndex;
		const int32 MinBatchSize = (Priority == EShaderCompileJobPriority::Low) ? 1 : DistributedShaderCompilerVariables::MinBatchSize;
		const int32 NumJobs = Manager->AllJobs.GetPendingJobs(EShaderCompilerWorkerType::XGE, Priority, MinBatchSize, INT32_MAX, PendingJobs);
		if (NumJobs > 0)
		{
			UE_LOG(LogShaderCompilers, Display, TEXT("Started %d 'XGE' shader compile jobs with '%s' priority"),
				NumJobs,
				ShaderCompileJobPriorityToString((EShaderCompileJobPriority)PriorityIndex));
		}
		if (PendingJobs.Num() >= DistributedShaderCompilerVariables::MinBatchSize)
		{
			break;
		}
	}

	if (PendingJobs.Num() > 0)
	{
		// Increase the batch size when more jobs are queued/in flight.
		const uint32 JobsPerBatch = FMath::Max(1, FMath::FloorToInt(FMath::LogX(2, PendingJobs.Num() + NumDispatchedJobs)));
		UE_LOG(LogShaderCompilers, Verbose, TEXT("Current jobs: %d, Batch size: %d, Num Already Dispatched: %d"), PendingJobs.Num(), JobsPerBatch, NumDispatchedJobs);


		struct FJobBatch
		{
			TArray<FShaderCommonCompileJobPtr> Jobs;
			TSet<const FShaderType*> UniquePointers;

			bool operator == (const FJobBatch& B) const
			{
				return Jobs == B.Jobs;
			}
		};


		// Different batches.
		TArray<FJobBatch> JobBatches;


		for (int32 i = 0; i < PendingJobs.Num(); i++)
		{
			// Randomize the shader compile jobs a little.
			{
				int32 PickedUpIndex = FMath::RandRange(i, PendingJobs.Num() - 1);
				if (i != PickedUpIndex)
				{
					Swap(PendingJobs[i], PendingJobs[PickedUpIndex]);
				}
			}

			// Avoid to have multiple of permutation of same global shader in same batch, to avoid pending on long shader compilation
			// of batches that tries to compile permutation of a global shader type that is giving a hard time to the shader compiler.
			const FShaderType* OptionalUniqueShaderType = nullptr;
			if (FShaderCompileJob* ShaderCompileJob = PendingJobs[i]->GetSingleShaderJob())
			{
				if (ShaderCompileJob->Key.ShaderType->GetGlobalShaderType())
				{
					OptionalUniqueShaderType = ShaderCompileJob->Key.ShaderType;
				}
			}

			// Find a batch this compile job can be packed with.
			FJobBatch* SelectedJobBatch = nullptr;
			{
				if (JobBatches.Num() == 0)
				{
					SelectedJobBatch = &JobBatches[JobBatches.Emplace()];
				}
				else if (OptionalUniqueShaderType)
				{
					for (FJobBatch& PendingJobBatch : JobBatches)
					{
						if (!PendingJobBatch.UniquePointers.Contains(OptionalUniqueShaderType))
						{
							SelectedJobBatch = &PendingJobBatch;
							break;
						}
					}

					if (!SelectedJobBatch)
					{
						SelectedJobBatch = &JobBatches[JobBatches.Emplace()];
					}
				}
				else
				{
					SelectedJobBatch = &JobBatches[0];
				}
			}

			// Assign compile job to job batch.
			{
				SelectedJobBatch->Jobs.Add(PendingJobs[i]);
				if (OptionalUniqueShaderType)
				{
					SelectedJobBatch->UniquePointers.Add(OptionalUniqueShaderType);
				}
			}

			// Kick off compile job batch.
			if (SelectedJobBatch->Jobs.Num() == JobsPerBatch)
			{
				DispatchShaderCompileJobsBatch(SelectedJobBatch->Jobs);
				JobBatches.RemoveSingleSwap(*SelectedJobBatch);
			}
		}

		// Kick off remaining compile job batches.
		for (FJobBatch& PendingJobBatch : JobBatches)
		{
			DispatchShaderCompileJobsBatch(PendingJobBatch.Jobs);
		}
	}

	for (auto Iter = DispatchedTasks.CreateIterator(); Iter; ++Iter)
	{
		bool bOutputFileReadFailed = true;

		FDistributedShaderCompilerTask* Task = *Iter;
		if (!Task->Future.IsReady())
		{
			continue;
		}

		FDistributedBuildTaskResult Result = Task->Future.Get();
		NumDispatchedJobs -= Task->ShaderJobs.Num();

		if (Result.ReturnCode != 0)
		{
			UE_LOG(LogShaderCompilers, Error, TEXT("Shader compiler returned a non-zero error code (%d)."), Result.ReturnCode);
		}

		if (Result.bCompleted)
		{
			// Check the output file exists. If it does, attempt to open it and serialize in the completed jobs.
			if (IFileManager::Get().FileExists(*Task->OutputFilePath))
			{
				FArchive* OutputFileAr = IFileManager::Get().CreateFileReader(*Task->OutputFilePath, FILEREAD_Silent);
				if (OutputFileAr)
				{
					bOutputFileReadFailed = false;
					FShaderCompileUtilities::DoReadTaskResults(Task->ShaderJobs, *OutputFileAr);
					delete OutputFileAr;
				}
			}

			if (bOutputFileReadFailed)
			{
				// Reading result from XGE job failed, so recompile shaders in current job batch locally
				UE_LOG(LogShaderCompilers, Log, TEXT("Rescheduling shader compilation to run locally after XGE job failed: %s"), *Task->OutputFilePath);

				for (FShaderCommonCompileJobPtr Job : Task->ShaderJobs)
				{
					FShaderCompileUtilities::ExecuteShaderCompileJob(*Job);
				}
			}

			// Enter the critical section so we can access the input and output queues
			{
				FScopeLock Lock(&Manager->CompileQueueSection);
				for (const auto& Job : Task->ShaderJobs)
				{
					Manager->ProcessFinishedJob(Job);
				}
			}
		}
		else
		{
			// The compile job was canceled. Return the jobs to the manager's compile queue.
			Manager->AllJobs.SubmitJobs(Task->ShaderJobs);
		}

		// Delete input and output files, if they exist.
		while (!IFileManager::Get().Delete(*Task->InputFilePath, false, true, true))
		{
			FPlatformProcess::Sleep(0.01f);
		}

		if (!bOutputFileReadFailed)
		{
			while (!IFileManager::Get().Delete(*Task->OutputFilePath, false, true, true))
			{
				FPlatformProcess::Sleep(0.01f);
			}
		}

		Iter.RemoveCurrent();
		delete Task;
	}

	// Yield for a short while to stop this thread continuously polling the disk.
	FPlatformProcess::Sleep(0.01f);

	// Return true if there is more work to be done.
	return Manager->AllJobs.GetNumOutstandingJobs() > 0;
}
