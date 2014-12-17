// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ShaderCompiler.cpp: Platform independent shader compilations.
=============================================================================*/

#include "EnginePrivate.h"
#include "EditorSupportDelegates.h"
#include "ExceptionHandling.h"
#include "GlobalShader.h"
#include "TargetPlatform.h"
#include "DerivedDataCacheInterface.h"
#include "EngineModule.h"
#include "ShaderCompiler.h"
#include "RendererInterface.h"

DEFINE_LOG_CATEGORY(LogShaderCompilers);

// Set to 1 to debug ShaderCompilerWorker.exe. Set a breakpoint in LaunchWorker() to get the cmd-line.
#define DEBUG_SHADERCOMPILEWORKER 0
// Default value comes from bPromptToRetryFailedShaderCompiles in BaseEngine.ini
// This is set as a global variable to allow changing in the debugger even in release
// For example if there are a lot of content shader compile errors you want to skip over without relaunching
bool GRetryShaderCompilation = false;

int32 GDumpShaderDebugInfo = 0;
static FAutoConsoleVariableRef CVarDumpShaderDebugInfo(
	TEXT("r.DumpShaderDebugInfo"),
	GDumpShaderDebugInfo,
	TEXT("When set to 1, will cause any shaders that are then compiled to dump debug info to GameName/Saved/ShaderDebugInfo\n")
	TEXT("The debug info is platform dependent, but usually includes a preprocessed version of the shader source.\n")
	TEXT("On iOS, if the PowerVR graphics SDK is installed to the default path, the PowerVR shader compiler will be called and errors will be reported during the cook.")
	);

static TAutoConsoleVariable<int32> CVarKeepShaderDebugData(
	TEXT("r.Shaders.KeepDebugInfo"),
	0,
	TEXT("Whether to keep shader reflection and debug data from shader bytecode, default is to strip.  When using graphical debuggers like Nsight it can be useful to enable this on startup."),
	ECVF_ReadOnly);

static TAutoConsoleVariable<int32> CVarOptimizeShaders(
	TEXT("r.Shaders.Optimize"),
	1,
	TEXT("Whether to optimize shaders.  When using graphical debuggers like Nsight it can be useful to disable this on startup."),
	ECVF_ReadOnly);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
static TAutoConsoleVariable<FString> CVarD3DCompilerPath(TEXT("r.D3DCompilerPath"),
	TEXT(""),	// default
	TEXT("Allows to specify a HLSL compiler version that is different from the one the code was compiled.\n")
	TEXT("No path (\"\") means the default one is used.\n")
	TEXT("If the compiler cannot be found an error is reported and it will compile further with the default one.\n")
	TEXT("This console variable works with ShaderCompileWorker (with multi threading) and without multi threading.\n")
	TEXT("This variable can be set in ConsoleVariables.ini to be defined at startup.\n")
	TEXT("e.g. c:/temp/d3dcompiler_44.dll or \"\""),
	ECVF_Cheat);
#endif

namespace XGEConsoleVariables
{
	int32 Enabled = 0;
	FAutoConsoleVariableRef CVarXGEShaderCompile(
		TEXT("r.XGEShaderCompile"),
		Enabled,
		TEXT("Enables or disables the use of XGE to build shaders.\n")
		TEXT("0: Local builds only. \n")
		TEXT("1: Distribute builds using XGE (default)."),
		ECVF_Default);

	/** The maximum number of shaders to group into a single XGE task. */
	int32 BatchSize = 16;
	FAutoConsoleVariableRef CVarXGEShaderCompileBatchSize(
		TEXT("r.XGEShaderCompile.BatchSize"),
		BatchSize,
		TEXT("Specifies the number of shaders to batch together into a single XGE task.\n")
		TEXT("Default = 16\n"),
		ECVF_Default);

	/** The total number of batches to fill with shaders before creating another group of batches. */
	int32 BatchGroupSize = 128;
	FAutoConsoleVariableRef CVarXGEShaderCompileBatchGroupSize(
		TEXT("r.XGEShaderCompile.BatchGroupSize"),
		BatchGroupSize,
		TEXT("Specifies the number of batches to fill with shaders.\n")
		TEXT("Shaders are spread across this number of batches until all the batches are full.\n")
		TEXT("This allows the XGE compile to go wider when compiling a small number of shaders.\n")
		TEXT("Default = 128\n"),
		ECVF_Default);

	/**
	 * The number of seconds to wait after a job is submitted before kicking off the XGE process.
	 * This allows time for the engine to enqueue more shaders, so we get better batching.
	 */
	float JobTimeout = 0.5f;
	FAutoConsoleVariableRef CVarXGEShaderCompileJobTimeout(
		TEXT("r.XGEShaderCompile.JobTimeout"),
		JobTimeout,
		TEXT("The number of seconds to wait for additional shader jobs to be submitted before starting a build.\n")
		TEXT("Default = 0.5\n"),
		ECVF_Default);
}

// Serialize Queued Job information
static void DoWriteTasks(const TArray<FShaderCompileJob*>& QueuedJobs, FArchive& TransferFile)
{
	int32 ShaderCompileWorkerInputVersion = 2;
	TransferFile << ShaderCompileWorkerInputVersion;
	int32 NumBatches = QueuedJobs.Num();
	TransferFile << NumBatches;

	// Serialize all the batched jobs
	for (int32 JobIndex = 0; JobIndex < QueuedJobs.Num(); JobIndex++)
	{
		TransferFile << QueuedJobs[JobIndex]->Input;
	}

	TransferFile.Close();
}

// Process results from Worker Process
static void DoReadTaskResults(const TArray<FShaderCompileJob*>& QueuedJobs, FArchive& OutputFile)
{
	int32 ShaderCompileWorkerOutputVersion;
	OutputFile << ShaderCompileWorkerOutputVersion;
	check(ShaderCompileWorkerOutputVersion == 1);

	int32 ErrorCode;
	OutputFile << ErrorCode;

	int32 CallstackLength = 0;
	OutputFile << CallstackLength;

	int32 ExceptionInfoLength = 0;
	OutputFile << ExceptionInfoLength;

	// Worker crashed
	if (ErrorCode == 1)
	{
		TCHAR* Callstack = new TCHAR[CallstackLength + 1];
		OutputFile.Serialize(Callstack, CallstackLength * sizeof(TCHAR));
		Callstack[CallstackLength] = 0;

		TCHAR* ExceptionInfo = new TCHAR[ExceptionInfoLength + 1];
		OutputFile.Serialize(ExceptionInfo, ExceptionInfoLength * sizeof(TCHAR));
		ExceptionInfo[ExceptionInfoLength] = 0;

		UE_LOG(LogShaderCompilers, Fatal, TEXT("ShaderCompileWorker crashed! \n %s \n %s"), ExceptionInfo, Callstack);

		delete [] Callstack;
		delete [] ExceptionInfo;
	}

	int32 NumJobs;
	OutputFile << NumJobs;
	check(NumJobs == QueuedJobs.Num());

	for (int32 JobIndex = 0; JobIndex < NumJobs; JobIndex++)
	{
		FShaderCompileJob* CurrentJob = QueuedJobs[JobIndex];
		check(!CurrentJob->bFinalized);
		CurrentJob->bFinalized = true;

		// Deserialize the shader compilation output.
		OutputFile << CurrentJob->Output;

		// Generate a hash of the output and cache it
		// The shader processing this output will use it to search for existing FShaderResources
		CurrentJob->Output.GenerateOutputHash();
		CurrentJob->bSucceeded = CurrentJob->Output.bSucceeded;
	}
}

#if PLATFORM_SUPPORTS_NAMED_PIPES
struct FShaderPipeConfig
{
	bool	bUseNamedPipes;
	bool	bUseNamedPipesAsync;
	bool	bSingleJobPerNamedPipeProcess;
	bool	bReuseNamedPipeAndProcess;
	int32	PipeGuid;

	FShaderPipeConfig() :
		bUseNamedPipes(true),
		bUseNamedPipesAsync(true),
		bSingleJobPerNamedPipeProcess(false),
		bReuseNamedPipeAndProcess(true),
		PipeGuid(0)
	{
	}

	void ReadFromConfigIni()
	{
		verify(GConfig->GetBool( TEXT("DevOptions.Shaders"), TEXT("bUseNamedPipes"), bUseNamedPipes, GEngineIni ));
		verify(GConfig->GetBool( TEXT("DevOptions.Shaders"), TEXT("bUseNamedPipesAsync"), bUseNamedPipesAsync, GEngineIni));
		verify(GConfig->GetBool( TEXT("DevOptions.Shaders"), TEXT("bSingleJobPerNamedPipeProcess"), bSingleJobPerNamedPipeProcess, GEngineIni));
		verify(GConfig->GetBool( TEXT("DevOptions.Shaders"), TEXT("bReuseNamedPipeAndProcess"), bReuseNamedPipeAndProcess, GEngineIni));
	}
};

FShaderPipeConfig			GShaderPipeConfig;

struct FPipeWorkerInfo
{
	FString PipeName;
	FPlatformNamedPipe NamedPipe;

	// Holds the serialized data for queued jobs to send to the worker process
	TArray<uint8> WorkJobBuffer;

	enum EState
	{
		State_Idle,
		State_Connecting,
		State_SendingJobData,
		State_ReceivingResultSize,
		State_ReceivingResults,
	};

	EState State;

	// Holds the size of the response from the worker process
	int32 ResultsTransferSize;

	// Holds the response from the worker process
	TArray<uint8> ResultsBuffer;

	FPipeWorkerInfo() :
		State(State_Idle),
		ResultsTransferSize(0)
	{
	}

	// Updates the state based off async communication with the pipe
	bool UpdateResultsState()
	{
		bool bAgain = false;
		do
		{
			bAgain = false;
			if (!NamedPipe.UpdateAsyncStatus())
			{
				return false;
			}

			switch (State)
			{
				case State_Idle:
					verify(NamedPipe.OpenConnection());
					State = State_Connecting;
					bAgain = true;
					break;

				case State_Connecting:
					if (NamedPipe.IsReadyForRW())
					{
						if (NamedPipe.WriteBytes(WorkJobBuffer.Num(), WorkJobBuffer.GetData()))
						{
							State = State_SendingJobData;
						}
						else if (NamedPipe.HasFailed())
						{
							return false;
						}
						bAgain = true;
					}
					break;

				case State_SendingJobData:
					if (NamedPipe.IsReadyForRW())
					{
						// Read the total number of bytes from the response
						if (NamedPipe.ReadInt32(ResultsTransferSize))
						{
							State = State_ReceivingResultSize;
						}
						else if (NamedPipe.HasFailed())
						{
							return false;
						}
						bAgain = true;
					}
					break;

				case State_ReceivingResultSize:
					if (NamedPipe.IsReadyForRW())
					{
						// Read the response
						ResultsBuffer.Empty(ResultsTransferSize);
						ResultsBuffer.AddUninitialized(ResultsTransferSize);
						if (NamedPipe.ReadBytes(ResultsTransferSize, ResultsBuffer.GetData()))
						{
							State = State_ReceivingResults;
						}
						else if (NamedPipe.HasFailed())
						{
							return false;
						}
						bAgain = true;
					}
					break;

				case State_ReceivingResults:
					if (NamedPipe.IsReadyForRW())
					{
						State = State_Idle;
						return true;
					}
					break;

				default:
					// Unknown state!
					check(0);
					bAgain = false;
					break;
			}
		}
		while (bAgain);

		return false;
	}

	void CreatePipe(uint32 WorkerIndex, uint32 ProcessId, bool bAllocPipeName)
	{
		if (bAllocPipeName)
		{
			++GShaderPipeConfig.PipeGuid;
			PipeName = FString::Printf(TEXT("\\\\.\\Pipe\\ShaderCompiler_%d_%d_%d"), ProcessId, WorkerIndex, GShaderPipeConfig.PipeGuid);
//FPlatformMisc::LowLevelOutputDebugStringf(TEXT("*** New pipe %s\n"), *PipeName);
		}

//FPlatformMisc::LowLevelOutputDebugStringf(TEXT("*** Creating pipe %s\n"), *PipeName);
		if (!NamedPipe.Create(PipeName, true, GShaderPipeConfig.bUseNamedPipesAsync))
		{
			UE_LOG(LogShaderCompilers, Fatal, TEXT("ShaderCompileWorker %d couldn't create pipe %s (GetLastError %d)"), WorkerIndex, *PipeName, GetLastError());
		}
	}

	void DestroyPipe() 
	{
//FPlatformMisc::LowLevelOutputDebugStringf(TEXT("*** Destroying Pipe %s\n"), *NamedPipe.GetName());
		NamedPipe.Destroy();
		State = State_Idle;
	}

	void WriteTasksForPipe( TArray<FShaderCompileJob*>& QueuedJobs ) 
	{
		// Make the data we'll transfer through the pipe (but don't send it yet!)
		WorkJobBuffer.Empty(0);
		FMemoryWriter TransferWriter(WorkJobBuffer);
		{
			TArray<uint8> Buffer;
			FMemoryWriter BufferWriter(Buffer);

			DoWriteTasks(QueuedJobs, BufferWriter);

			int32 BufferSize = Buffer.Num();
			TransferWriter << BufferSize;
			TransferWriter.Serialize(Buffer.GetData(), Buffer.Num());
		}
		TransferWriter.Close();
	}
};
#endif	// PLATFORM_SUPPORTS_NAMED_PIPES

/** Information tracked for each shader compile worker process instance. */
struct FShaderCompileWorkerInfo
{
	/** Process handle of the worker app once launched.  Invalid handle means no process. */
	FProcHandle WorkerProcess;

	/** Tracks whether tasks have been issued to the worker. */
	bool bIssuedTasksToWorker;	

	/** Whether the worker has been launched for this set of tasks. */
	bool bLaunchedWorker;

	/** Tracks whether all tasks issued to the worker have been received. */
	bool bComplete;

#if PLATFORM_SUPPORTS_NAMED_PIPES
	/** Named pipe */
	FPipeWorkerInfo PipeWorker;

	bool bWorkerForPipeWasLaunched;
#endif

	/** Time at which the worker started the most recent batch of tasks. */
	double StartTime;

	/** Jobs that this worker is responsible for compiling. */
	TArray<FShaderCompileJob*> QueuedJobs;

	FShaderCompileWorkerInfo() :
		bIssuedTasksToWorker(false),		
		bLaunchedWorker(false),
		bComplete(false),
#if PLATFORM_SUPPORTS_NAMED_PIPES
		bWorkerForPipeWasLaunched(false),
#endif
		StartTime(0)
	{
	}

	// warning: not virtual
	~FShaderCompileWorkerInfo()
	{
		if(WorkerProcess.IsValid())
		{
			FPlatformProcess::TerminateProc(WorkerProcess);
			WorkerProcess.Close();
		}
	}

	void CreatePipeAndNewTask(uint32 WorkerIndex, uint32 ProcessId)
	{
#if PLATFORM_SUPPORTS_NAMED_PIPES
		check(GShaderPipeConfig.bUseNamedPipes);
		if (QueuedJobs.Num() > 0)
		{
			// Open the pipe; figure out if the worker is still listening, which means we can recycle the pipe
			bool bAllocNameForPipe = (PipeWorker.PipeName.Len() == 0);
			bAllocNameForPipe |= !GShaderPipeConfig.bReuseNamedPipeAndProcess;
			bAllocNameForPipe |= !bWorkerForPipeWasLaunched;

			if (bWorkerForPipeWasLaunched)
			{
				// Make sure it's alive
				if (!FShaderCompilingManager::IsShaderCompilerWorkerRunning(WorkerProcess))
				{
					bAllocNameForPipe = true;
					bWorkerForPipeWasLaunched = false;
				}
			}

			if (bAllocNameForPipe)
			{
				// Request a new worker
				bWorkerForPipeWasLaunched = false;
			}
			PipeWorker.CreatePipe(WorkerIndex, ProcessId, bAllocNameForPipe);
			PipeWorker.WriteTasksForPipe(QueuedJobs);
		}
#else
		check(0);
#endif	// PLATFORM_SUPPORTS_NAMED_PIPES
	}

	void ReadAndWaitResultsFromPipe()
	{
#if PLATFORM_SUPPORTS_NAMED_PIPES
		check(GShaderPipeConfig.bUseNamedPipes);
		if (QueuedJobs.Num() == 0 || !bWorkerForPipeWasLaunched)
		{
			return;
		}

		check(PipeWorker.NamedPipe.IsCreated());

		bool bError = false;
		do
		{
			PipeWorker.NamedPipe.BlockForAsyncIO();
			if (PipeWorker.NamedPipe.HasFailed())
			{
				bError = true;
				break;
			}
		}
		while (!PipeWorker.UpdateResultsState());

		if (!bError)
		{
			FMemoryReader ResultReader(PipeWorker.ResultsBuffer);
			DoReadTaskResults(QueuedJobs, ResultReader);
			bComplete = true;
		}

		// Close pipe
		PipeWorker.DestroyPipe();
#else
		check(0);
#endif	// PLATFORM_SUPPORTS_NAMED_PIPES
	}

	void ReadResultsFromPipe()
	{
#if PLATFORM_SUPPORTS_NAMED_PIPES
		check(PipeWorker.NamedPipe.IsCreated());

		PipeWorker.NamedPipe.BlockForAsyncIO();
		if (PipeWorker.UpdateResultsState())
		{
			FMemoryReader ResultReader(PipeWorker.ResultsBuffer);
			DoReadTaskResults(QueuedJobs, ResultReader);
			bComplete = true;
		}

		// Close pipe
		PipeWorker.DestroyPipe();
#else
		check(0);
#endif	// PLATFORM_SUPPORTS_NAMED_PIPES
	}

};

//--------------------
// @LH : LUTHA - XGE Distributed Shader Compilation
//-----
FShaderCompileThreadRunnableBase::FShaderCompileThreadRunnableBase(FShaderCompilingManager* InManager)
	: Manager(InManager)
	, Thread(nullptr)
	, bTerminatedByError(false)
	, bForceFinish(false)
{
}
void FShaderCompileThreadRunnableBase::StartThread()
{
//-----
// @LH : END
//--------------------
	if (Manager->bAllowAsynchronousShaderCompiling && !FPlatformProperties::RequiresCookedData())
	{
		Thread = FRunnableThread::Create(this, TEXT("ShaderCompilingThread"), 0, TPri_Normal, FPlatformAffinity::GetPoolThreadMask());
	}
}

FShaderCompileThreadRunnable::FShaderCompileThreadRunnable(FShaderCompilingManager* InManager)
	: FShaderCompileThreadRunnableBase(InManager)
	, LastCheckForWorkersTime(0)
{
	for (uint32 WorkerIndex = 0; WorkerIndex < Manager->NumShaderCompilingThreads; WorkerIndex++)
	{
		WorkerInfos.Add(new FShaderCompileWorkerInfo());
	}
}

FShaderCompileThreadRunnable::~FShaderCompileThreadRunnable()
{
	for (int32 Index = 0; Index < WorkerInfos.Num(); Index++)
	{
		delete WorkerInfos[Index];
	}

	WorkerInfos.Empty(0);
}

/** Entry point for the shader compiling thread. */
uint32 FShaderCompileThreadRunnableBase::Run()
{
#if _MSC_VER
	if(!FPlatformMisc::IsDebuggerPresent())
	{
#if !PLATFORM_SEH_EXCEPTIONS_DISABLED
		__try
#endif
		{
			check(Manager->bAllowAsynchronousShaderCompiling);
			// Do the work
			while (!bForceFinish)
			{
				CompilingLoop();
			}
		}
#if !PLATFORM_SEH_EXCEPTIONS_DISABLED
		__except( 
#if PLATFORM_WINDOWS
			ReportCrash( GetExceptionInformation() )
#else
			EXCEPTION_EXECUTE_HANDLER
#endif
			)
		{
#if WITH_EDITORONLY_DATA
			ErrorMessage = GErrorHist;
#endif
			// Use a memory barrier to ensure that the main thread sees the write to ErrorMessage before
			// the write to bTerminatedByError.
			FPlatformMisc::MemoryBarrier();

			bTerminatedByError = true;
		}
#endif
	}
	else
#endif
	{
		check(Manager->bAllowAsynchronousShaderCompiling);
		while (!bForceFinish)
		{
			CompilingLoop();
		}
	}

	return 0;
}

/** Called by the main thread only, reports exceptions in the worker threads */
void FShaderCompileThreadRunnableBase::CheckHealth() const
{
	if (bTerminatedByError)
	{
#if WITH_EDITORONLY_DATA
		GErrorHist[0] = 0;
#endif
		GIsCriticalError = false;
		UE_LOG(LogShaderCompilers, Fatal,TEXT("Shader Compiling thread exception:\r\n%s"), *ErrorMessage);
	}
}

int32 FShaderCompileThreadRunnable::PullTasksFromQueue()
{
	int32 NumActiveThreads = 0;
	{
		// Enter the critical section so we can access the input and output queues
		FScopeLock Lock(&Manager->CompileQueueSection);

		const int32 NumWorkersToFeed = Manager->bCompilingDuringGame ? Manager->NumShaderCompilingThreadsDuringGame : WorkerInfos.Num();

		for (int32 WorkerIndex = 0; WorkerIndex < WorkerInfos.Num(); WorkerIndex++)
		{
			FShaderCompileWorkerInfo& CurrentWorkerInfo = *WorkerInfos[WorkerIndex];

			// If this worker doesn't have any queued jobs, look for more in the input queue
			if (CurrentWorkerInfo.QueuedJobs.Num() == 0 && WorkerIndex < NumWorkersToFeed)
			{
				check(!CurrentWorkerInfo.bComplete);

				if (Manager->CompileQueue.Num() > 0)
				{
					bool bAddedLowLatencyTask = false;
					int32 JobIndex = 0;

					// Try to grab up to MaxShaderJobBatchSize jobs
					// Don't put more than one low latency task into a batch
					for (; JobIndex < Manager->MaxShaderJobBatchSize && JobIndex < Manager->CompileQueue.Num() && !bAddedLowLatencyTask; JobIndex++)
					{
						bAddedLowLatencyTask |= Manager->CompileQueue[JobIndex]->bOptimizeForLowLatency;
						CurrentWorkerInfo.QueuedJobs.Add(Manager->CompileQueue[JobIndex]);
					}

					// Update the worker state as having new tasks that need to be issued					
					// don't reset worker app ID, because the shadercompilerworkers don't shutdown immediately after finishing a single job queue.
					CurrentWorkerInfo.bIssuedTasksToWorker = false;					
					CurrentWorkerInfo.bLaunchedWorker = false;
					CurrentWorkerInfo.StartTime = FPlatformTime::Seconds();
					NumActiveThreads++;
					Manager->CompileQueue.RemoveAt(0, JobIndex);
				}
			}
			else
			{
				if (CurrentWorkerInfo.QueuedJobs.Num() > 0)
				{
					NumActiveThreads++;
				}

				// Add completed jobs to the output queue, which is ShaderMapJobs
				if (CurrentWorkerInfo.bComplete)
				{
					for (int32 JobIndex = 0; JobIndex < CurrentWorkerInfo.QueuedJobs.Num(); JobIndex++)
					{
						FShaderMapCompileResults& ShaderMapResults = Manager->ShaderMapJobs.FindChecked(CurrentWorkerInfo.QueuedJobs[JobIndex]->Id);
						ShaderMapResults.FinishedJobs.Add(CurrentWorkerInfo.QueuedJobs[JobIndex]);
						ShaderMapResults.bAllJobsSucceeded = ShaderMapResults.bAllJobsSucceeded && CurrentWorkerInfo.QueuedJobs[JobIndex]->bSucceeded;
					}

					const float ElapsedTime = FPlatformTime::Seconds() - CurrentWorkerInfo.StartTime;

					Manager->WorkersBusyTime += ElapsedTime;

					// Log if requested or if there was an exceptionally slow batch, to see the offender easily
					if (Manager->bLogJobCompletionTimes || ElapsedTime > 30.0f)
					{
						FString JobNames;

						for (int32 JobIndex = 0; JobIndex < CurrentWorkerInfo.QueuedJobs.Num(); JobIndex++)
						{
							const FShaderCompileJob& Job = *CurrentWorkerInfo.QueuedJobs[JobIndex];
							JobNames += FString(Job.ShaderType->GetName()) + TEXT(" Instructions = ") + FString::FromInt(Job.Output.NumInstructions) + TEXT(", ");
						}

						UE_LOG(LogShaders, Display, TEXT("Finished batch of %u jobs in %.3fs, %s"), CurrentWorkerInfo.QueuedJobs.Num(), ElapsedTime, *JobNames);
					}

					// Using atomics to update NumOutstandingJobs since it is read outside of the critical section
					FPlatformAtomics::InterlockedAdd(&Manager->NumOutstandingJobs, -CurrentWorkerInfo.QueuedJobs.Num());

					CurrentWorkerInfo.bComplete = false;
					CurrentWorkerInfo.QueuedJobs.Empty();
				}
			}
		}
	}
	return NumActiveThreads;
}

void FShaderCompileThreadRunnable::WriteNewTasks()
{
	for (int32 WorkerIndex = 0; WorkerIndex < WorkerInfos.Num(); WorkerIndex++)
	{
		FShaderCompileWorkerInfo& CurrentWorkerInfo = *WorkerInfos[WorkerIndex];

		// Only write tasks once
		if (!CurrentWorkerInfo.bIssuedTasksToWorker && CurrentWorkerInfo.QueuedJobs.Num() > 0)
		{
			CurrentWorkerInfo.bIssuedTasksToWorker = true;

			const FString WorkingDirectory = Manager->AbsoluteShaderBaseWorkingDirectory + FString::FromInt(WorkerIndex);

#if PLATFORM_MAC || PLATFORM_LINUX
			// To make sure that the process waiting for input file won't try to read it until it's ready
			// we use a temp file name during writing.
			FString TransferFileName;
			do
			{
				FGuid Guid;
				FPlatformMisc::CreateGuid(Guid);
				TransferFileName = WorkingDirectory + Guid.ToString();
			} while (IFileManager::Get().FileSize(*TransferFileName) != INDEX_NONE);
#else
			const FString TransferFileName = WorkingDirectory / TEXT("WorkerInputOnly.in");
#endif

			// Write out the file that the worker app is waiting for, which has all the information needed to compile the shader.
			// 'Only' indicates that the worker should keep checking for more tasks after this one
			FArchive* TransferFile = NULL;

#if PLATFORM_SUPPORTS_NAMED_PIPES
			if (GShaderPipeConfig.bUseNamedPipes && !GShaderPipeConfig.bSingleJobPerNamedPipeProcess)
			{
				CurrentWorkerInfo.CreatePipeAndNewTask(WorkerIndex, GShaderCompilingManager->ProcessId);
			}
			else
#endif // PLATFORM_SUPPORTS_NAMED_PIPES
			{
				int32 RetryCount = 0;
				// Retry over the next two seconds if we can't write out the input file
				// Anti-virus and indexing applications can interfere and cause this write to fail
				//@todo - switch to shared memory or some other method without these unpredictable hazards
				while (TransferFile == NULL && RetryCount < 2000)
				{
					if (RetryCount > 0)
					{
						FPlatformProcess::Sleep(0.01f);
					}
					TransferFile = IFileManager::Get().CreateFileWriter(*TransferFileName, FILEWRITE_EvenIfReadOnly);
					RetryCount++;
				}
				if (TransferFile == NULL)
				{
					TransferFile = IFileManager::Get().CreateFileWriter(*TransferFileName, FILEWRITE_EvenIfReadOnly | FILEWRITE_NoFail);
				}
				check(TransferFile);

				DoWriteTasks(CurrentWorkerInfo.QueuedJobs, *TransferFile);
				delete TransferFile;

#if PLATFORM_MAC || PLATFORM_LINUX
				// Change the transfer file name to proper one
				FString ProperTransferFileName = WorkingDirectory / TEXT("WorkerInputOnly.in");
				IFileManager::Get().Move(*ProperTransferFileName, *TransferFileName);
#endif
			}
		}
	}
}

void FShaderCompileThreadRunnable::LaunchWorkerIfNeeded(FShaderCompileWorkerInfo& CurrentWorkerInfo, uint32 WorkerIndex)
{
#if PLATFORM_SUPPORTS_NAMED_PIPES
	if (CurrentWorkerInfo.QueuedJobs.Num() == 0)
	{
		return;
	}

	if (CurrentWorkerInfo.bWorkerForPipeWasLaunched)
	{
		// Check that the worker didn't fatal error or there was an error in the pipe
		const double CurrentTime = FPlatformTime::Seconds();

		// Limit how often we check for workers running since IsApplicationRunning eats up some CPU time on Windows
		const bool bCheckForWorkerRunning = (CurrentTime - LastCheckForWorkersTime > 0.1f);
		if (bCheckForWorkerRunning)
		{
			LastCheckForWorkersTime = CurrentTime;
		}

		if (bCheckForWorkerRunning && !FShaderCompilingManager::IsShaderCompilerWorkerRunning(CurrentWorkerInfo.WorkerProcess))
		{
			// Worker died, so clear this pipe and make a new one
			CurrentWorkerInfo.PipeWorker.DestroyPipe();
			CurrentWorkerInfo.bWorkerForPipeWasLaunched = false;

			// clean up the proc handle
			FPlatformProcess::CloseProc(CurrentWorkerInfo.WorkerProcess);
			CurrentWorkerInfo.WorkerProcess = FProcHandle();

			check(GShaderPipeConfig.bUseNamedPipes && !GShaderPipeConfig.bSingleJobPerNamedPipeProcess);
			CurrentWorkerInfo.CreatePipeAndNewTask(WorkerIndex, GShaderCompilingManager->ProcessId);
		}
	}

	if (!CurrentWorkerInfo.bWorkerForPipeWasLaunched)
	{
		const FString WorkingDirectory = Manager->ShaderBaseWorkingDirectory + FString::FromInt(WorkerIndex) + TEXT("/");

		// Store the Id with this thread so that we will know not to launch it again
		const FString& PipeName = CurrentWorkerInfo.PipeWorker.NamedPipe.GetName();
		// make sure we don't overwrite a running process
		check(!CurrentWorkerInfo.WorkerProcess.IsValid());
		CurrentWorkerInfo.WorkerProcess = Manager->LaunchWorker(WorkingDirectory, Manager->ProcessId, WorkerIndex, PipeName, PipeName, true, !GShaderPipeConfig.bReuseNamedPipeAndProcess);		
		CurrentWorkerInfo.bLaunchedWorker = true;
		CurrentWorkerInfo.bWorkerForPipeWasLaunched = true;
	}
#endif
}



bool FShaderCompileThreadRunnable::LaunchWorkersIfNeeded()
{
	const double CurrentTime = FPlatformTime::Seconds();
	// Limit how often we check for workers running since IsApplicationRunning eats up some CPU time on Windows
	const bool bCheckForWorkerRunning = (CurrentTime - LastCheckForWorkersTime > .1f);
	bool bAbandonWorkers = false;

	if (bCheckForWorkerRunning)
	{
		LastCheckForWorkersTime = CurrentTime;
	}

	for (int32 WorkerIndex = 0; WorkerIndex < WorkerInfos.Num(); WorkerIndex++)
	{
		FShaderCompileWorkerInfo& CurrentWorkerInfo = *WorkerInfos[WorkerIndex];
		if (CurrentWorkerInfo.QueuedJobs.Num() == 0 )
		{
			// Skip if nothing to do
			// Also, use the opportunity to free OS resources by cleaning up handles of no more running processes
			if (CurrentWorkerInfo.WorkerProcess.IsValid() && !FShaderCompilingManager::IsShaderCompilerWorkerRunning(CurrentWorkerInfo.WorkerProcess))
			{
				FPlatformProcess::CloseProc(CurrentWorkerInfo.WorkerProcess);
				CurrentWorkerInfo.WorkerProcess = FProcHandle();
			}
			continue;
		}

#if PLATFORM_SUPPORTS_NAMED_PIPES
		if (GShaderPipeConfig.bUseNamedPipes && !GShaderPipeConfig.bSingleJobPerNamedPipeProcess)
		{
			LaunchWorkerIfNeeded(CurrentWorkerInfo, WorkerIndex);
		}
		else
#endif	// PLATFORM_SUPPORTS_NAMED_PIPES
		{
			if (!CurrentWorkerInfo.WorkerProcess.IsValid() || (bCheckForWorkerRunning && !FShaderCompilingManager::IsShaderCompilerWorkerRunning(CurrentWorkerInfo.WorkerProcess)))
			{
				// @TODO: dubious design - worker should not be launched unless we know there's more work to do.
				bool bLaunchAgain = true;

				// Detect when the worker has exited due to fatal error
				// bLaunchedWorker check here is necessary to distinguish between 'process isn't running because it crashed' and 'process isn't running because it exited cleanly and the outputfile was already consumed'
				if (CurrentWorkerInfo.WorkerProcess.IsValid())
				{
					// shader compiler exited one way or another, so clear out the stale PID.
					FPlatformProcess::CloseProc(CurrentWorkerInfo.WorkerProcess);
					CurrentWorkerInfo.WorkerProcess = FProcHandle();

					if (CurrentWorkerInfo.bLaunchedWorker)
					{
						const FString WorkingDirectory = Manager->AbsoluteShaderBaseWorkingDirectory + FString::FromInt(WorkerIndex) + TEXT("/");
						const FString OutputFileNameAndPath = WorkingDirectory + TEXT("WorkerOutputOnly.out");

						if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*OutputFileNameAndPath))
						{
							// If the worker is no longer running but it successfully wrote out the output, no need to assert
							bLaunchAgain = false;
						}
						else
						{
							UE_LOG(LogShaderCompilers, Error, TEXT("ShaderCompileWorker terminated unexpectedly!  Falling back to directly compiling which will be very slow.  Thread %u."), WorkerIndex);

							bAbandonWorkers = true;
							break;
						}
					}
				}

				if (bLaunchAgain)
				{
					const FString WorkingDirectory = Manager->ShaderBaseWorkingDirectory + FString::FromInt(WorkerIndex) + TEXT("/");
					FString InputFileName(TEXT("WorkerInputOnly.in"));
					FString OutputFileName(TEXT("WorkerOutputOnly.out"));

					// Store the handle with this thread so that we will know not to launch it again
					CurrentWorkerInfo.WorkerProcess = Manager->LaunchWorker(WorkingDirectory, Manager->ProcessId, WorkerIndex, InputFileName, OutputFileName, false, false);					
					CurrentWorkerInfo.bLaunchedWorker = true;
				}
			}
		}
	}

	return bAbandonWorkers;
}

void FShaderCompileThreadRunnable::ReadAvailableResults()
{
	for (int32 WorkerIndex = 0; WorkerIndex < WorkerInfos.Num(); WorkerIndex++)
	{
		FShaderCompileWorkerInfo& CurrentWorkerInfo = *WorkerInfos[WorkerIndex];

		// Check for available result files
		if (CurrentWorkerInfo.QueuedJobs.Num() > 0)
		{
#if PLATFORM_SUPPORTS_NAMED_PIPES
			if (GShaderPipeConfig.bUseNamedPipes && !GShaderPipeConfig.bSingleJobPerNamedPipeProcess)
			{
				if (!CurrentWorkerInfo.bWorkerForPipeWasLaunched || !CurrentWorkerInfo.PipeWorker.NamedPipe.UpdateAsyncStatus())
				{
					continue;
				}

				if (CurrentWorkerInfo.PipeWorker.UpdateResultsState())
				{
					FMemoryReader ResultReader(CurrentWorkerInfo.PipeWorker.ResultsBuffer);
					DoReadTaskResults(CurrentWorkerInfo.QueuedJobs, ResultReader);
					CurrentWorkerInfo.bComplete = true;
					CurrentWorkerInfo.PipeWorker.DestroyPipe();
				}
			}
			else
#endif // PLATFORM_SUPPORTS_NAMED_PIPES
			{
				// Distributed compiles always use the same directory
				const FString WorkingDirectory = Manager->AbsoluteShaderBaseWorkingDirectory + FString::FromInt(WorkerIndex) + TEXT("/");
				// 'Only' indicates to the worker that it should log and continue checking for the input file after the first one is processed
				const TCHAR* InputFileName = TEXT("WorkerInputOnly.in");
				const FString OutputFileNameAndPath = WorkingDirectory + TEXT("WorkerOutputOnly.out");

				// In the common case the output file will not exist, so check for existence before opening
				// This is only a win if FileExists is faster than CreateFileReader, which it is on Windows
				if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*OutputFileNameAndPath))
				{
					FArchive* OutputFilePtr = IFileManager::Get().CreateFileReader(*OutputFileNameAndPath, FILEREAD_Silent);

					if (OutputFilePtr)
					{
						FArchive& OutputFile = *OutputFilePtr;
						DoReadTaskResults(CurrentWorkerInfo.QueuedJobs, OutputFile);

						// Close the output file.
						delete OutputFilePtr;

						// Delete the output file now that we have consumed it, to avoid reading stale data on the next compile loop.
						bool bDeletedOutput = IFileManager::Get().Delete(*OutputFileNameAndPath, true, true);
						int32 RetryCount = 0;
						// Retry over the next two seconds if we couldn't delete it
						while (!bDeletedOutput && RetryCount < 200)
						{
							FPlatformProcess::Sleep(0.01f);
							bDeletedOutput = IFileManager::Get().Delete(*OutputFileNameAndPath, true, true);
							RetryCount++;
						}
						checkf(bDeletedOutput, TEXT("Failed to delete %s!"), *OutputFileNameAndPath);

						CurrentWorkerInfo.bComplete = true;
					}
				}
			}
		}
	}
}

void FShaderCompileThreadRunnable::CompileDirectlyThroughDll()
{
	for (int32 WorkerIndex = 0; WorkerIndex < WorkerInfos.Num(); WorkerIndex++)
	{
		FShaderCompileWorkerInfo& CurrentWorkerInfo = *WorkerInfos[WorkerIndex];

		if (CurrentWorkerInfo.QueuedJobs.Num() > 0)
		{
			for (int32 JobIndex = 0; JobIndex < CurrentWorkerInfo.QueuedJobs.Num(); JobIndex++)
			{
				FShaderCompileJob& CurrentJob = *CurrentWorkerInfo.QueuedJobs[JobIndex];

				check(!CurrentJob.bFinalized);
				CurrentJob.bFinalized = true;

				static ITargetPlatformManagerModule& TPM = GetTargetPlatformManagerRef();
				const FName Format = LegacyShaderPlatformToShaderFormat(EShaderPlatform(CurrentJob.Input.Target.Platform));
				const IShaderFormat* Compiler = TPM.FindShaderFormat(Format);

				if (!Compiler)
				{
					UE_LOG(LogShaderCompilers, Fatal, TEXT("Can't compile shaders for format %s, couldn't load compiler dll"), *Format.ToString());
				}
				CA_ASSUME(Compiler != NULL);

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
				}
			}

			CurrentWorkerInfo.bComplete = true;
		}
	}
}

int32 FShaderCompileThreadRunnable::CompilingLoop()
{
	// Grab more shader compile jobs from the input queue, and move completed jobs to Manager->ShaderMapJobs
	const int32 NumActiveThreads = PullTasksFromQueue();

	if (NumActiveThreads == 0 && Manager->bAllowAsynchronousShaderCompiling)
	{
		// Yield while there's nothing to do
		// Note: sleep-looping is bad threading practice, wait on an event instead!
		// The shader worker thread does it because it needs to communicate with other processes through the file system
		FPlatformProcess::Sleep(.010f);
	}

	if (Manager->bAllowCompilingThroughWorkers)
	{
#if PLATFORM_SUPPORTS_NAMED_PIPES
		if (GShaderPipeConfig.bUseNamedPipes && GShaderPipeConfig.bSingleJobPerNamedPipeProcess && GShaderCompilingManager)
		{
			for (int32 WorkerIndex = 0; WorkerIndex < WorkerInfos.Num(); WorkerIndex++)
			{
				FShaderCompileWorkerInfo& CurrentWorkerInfo = *WorkerInfos[WorkerIndex];
				CurrentWorkerInfo.CreatePipeAndNewTask(WorkerIndex, GShaderCompilingManager->ProcessId);
				LaunchWorkerIfNeeded(CurrentWorkerInfo, WorkerIndex);
				CurrentWorkerInfo.ReadAndWaitResultsFromPipe();
			}
		}
		else
#endif	// PLATFORM_SUPPORTS_NAMED_PIPES
		{
			// Write out the files which are input to the shader compile workers
			WriteNewTasks();

			// Launch shader compile workers if they are not already running
			// Workers can time out when idle so they may need to be relaunched
			bool bAbandonWorkers = LaunchWorkersIfNeeded();

			if (bAbandonWorkers)
			{
				// Fall back to local compiles if the SCW crashed.
				// This is nasty but needed to work around issues where message passing through files to SCW is unreliable on random PCs
				Manager->bAllowCompilingThroughWorkers = false;
			}
			else
			{
				// Read files which are outputs from the shader compile workers
				ReadAvailableResults();
			}
		}
	}
	else
	{
		CompileDirectlyThroughDll();
	}

	return NumActiveThreads;
}

FShaderCompilingManager* GShaderCompilingManager = NULL;

FShaderCompilingManager::FShaderCompilingManager() :
	bCompilingDuringGame(false),
	NumOutstandingJobs(0),
#if PLATFORM_MAC
	ShaderCompileWorkerName(TEXT("../../../Engine/Binaries/Mac/ShaderCompileWorker"))
#elif PLATFORM_LINUX
	ShaderCompileWorkerName(TEXT("../../../Engine/Binaries/Linux/ShaderCompileWorker"))
#else
	ShaderCompileWorkerName(TEXT("../../../Engine/Binaries/Win64/ShaderCompileWorker.exe"))
#endif
{
	WorkersBusyTime = 0;
	bFallBackToDirectCompiles = false;

	// Threads must use absolute paths on Windows in case the current directory is changed on another thread!
	ShaderCompileWorkerName = FPaths::ConvertRelativePathToFull(ShaderCompileWorkerName);

	// Read values from the engine ini
	verify(GConfig->GetBool( TEXT("DevOptions.Shaders"), TEXT("bAllowCompilingThroughWorkers"), bAllowCompilingThroughWorkers, GEngineIni ));
	verify(GConfig->GetBool( TEXT("DevOptions.Shaders"), TEXT("bAllowAsynchronousShaderCompiling"), bAllowAsynchronousShaderCompiling, GEngineIni ));

	// override the use of workers, can be helpful for debugging shader compiler code
	if (!FPlatformProcess::SupportsMultithreading() || FParse::Param(FCommandLine::Get(),TEXT("noshaderworker")))
	{
		bAllowCompilingThroughWorkers = false;
	}

	if (!FPlatformProcess::SupportsMultithreading())
	{
		bAllowAsynchronousShaderCompiling = false;
	}

	int32 NumUnusedShaderCompilingThreads;
	verify(GConfig->GetInt( TEXT("DevOptions.Shaders"), TEXT("NumUnusedShaderCompilingThreads"), NumUnusedShaderCompilingThreads, GEngineIni ));

	int32 NumUnusedShaderCompilingThreadsDuringGame;
	verify(GConfig->GetInt( TEXT("DevOptions.Shaders"), TEXT("NumUnusedShaderCompilingThreadsDuringGame"), NumUnusedShaderCompilingThreadsDuringGame, GEngineIni ));

	// Use all the cores on the build machines
	if (GIsBuildMachine || FParse::Param(FCommandLine::Get(), TEXT("USEALLAVAILABLECORES")))
	{
		NumUnusedShaderCompilingThreads = 0;
	}

	verify(GConfig->GetInt( TEXT("DevOptions.Shaders"), TEXT("MaxShaderJobBatchSize"), MaxShaderJobBatchSize, GEngineIni ));
	verify(GConfig->GetBool( TEXT("DevOptions.Shaders"), TEXT("bPromptToRetryFailedShaderCompiles"), bPromptToRetryFailedShaderCompiles, GEngineIni ));
	verify(GConfig->GetBool( TEXT("DevOptions.Shaders"), TEXT("bLogJobCompletionTimes"), bLogJobCompletionTimes, GEngineIni ));

#if PLATFORM_SUPPORTS_NAMED_PIPES
	GShaderPipeConfig.ReadFromConfigIni();
#endif

	GRetryShaderCompilation = bPromptToRetryFailedShaderCompiles;

	verify(GConfig->GetFloat( TEXT("DevOptions.Shaders"), TEXT("ProcessGameThreadTargetTime"), ProcessGameThreadTargetTime, GEngineIni ));

#if UE_BUILD_DEBUG
	// Increase budget for processing results in debug or else it takes forever to finish due to poor framerate
	ProcessGameThreadTargetTime *= 3;
#endif

	// Get the current process Id, this will be used by the worker app to shut down when it's parent is no longer running.
	ProcessId = FPlatformProcess::GetCurrentProcessId();

	// Use a working directory unique to this game, process and thread so that it will not conflict 
	// With processes from other games, processes from the same game or threads in this same process.
	// Use IFileManager to do path conversion to properly handle sandbox paths (outside of standard paths in particular).
	ShaderBaseWorkingDirectory = FPlatformProcess::ShaderWorkingDir() / FString::FromInt(ProcessId) + TEXT("/");
	FString AbsoluteBaseDirectory = IFileManager::Get().ConvertToAbsolutePathForExternalAppForWrite(*ShaderBaseWorkingDirectory);
	FPaths::NormalizeDirectoryName(AbsoluteBaseDirectory);
	AbsoluteShaderBaseWorkingDirectory = AbsoluteBaseDirectory + TEXT("/");

	FString AbsoluteDebugInfoDirectory = IFileManager::Get().ConvertToAbsolutePathForExternalAppForWrite(*(FPaths::GameSavedDir() / TEXT("ShaderDebugInfo")));
	FPaths::NormalizeDirectoryName(AbsoluteDebugInfoDirectory);
	AbsoluteShaderDebugInfoDirectory = AbsoluteDebugInfoDirectory;

	const int32 NumVirtualCores = FPlatformMisc::NumberOfCoresIncludingHyperthreads();

	NumShaderCompilingThreads = bAllowCompilingThroughWorkers ? (NumVirtualCores - NumUnusedShaderCompilingThreads) : 1;

	// Make sure there's at least one worker allowed to be active when compiling during the game
	NumShaderCompilingThreadsDuringGame = bAllowCompilingThroughWorkers ? (NumVirtualCores - NumUnusedShaderCompilingThreadsDuringGame) : 1;

	// On machines with few cores, each core will have a massive impact on compile time, so we prioritize compile latency over editor performance during the build
	if (NumVirtualCores <= 4)
	{
		NumShaderCompilingThreads = NumVirtualCores - 1;
		NumShaderCompilingThreadsDuringGame = NumVirtualCores - 1;
	}

	NumShaderCompilingThreads = FMath::Max<int32>(1, NumShaderCompilingThreads);
	NumShaderCompilingThreadsDuringGame = FMath::Max<int32>(1, NumShaderCompilingThreadsDuringGame);

	NumShaderCompilingThreadsDuringGame = FMath::Min<int32>(NumShaderCompilingThreadsDuringGame, NumShaderCompilingThreads);

	if (FShaderCompileXGEThreadRunnable::IsSupported())
	{
		UE_LOG(LogShaderCompilers, Display, TEXT("Using XGE Shader Compiler."));
		Thread = new FShaderCompileXGEThreadRunnable(this);
	}
	else
	{
		UE_LOG(LogShaderCompilers, Display, TEXT("Using Local Shader Compiler."));
		Thread = new FShaderCompileThreadRunnable(this);
	}
	Thread->StartThread();
}

void FShaderCompilingManager::AddJobs(TArray<FShaderCompileJob*>& NewJobs, bool bApplyCompletedShaderMapForRendering, bool bOptimizeForLowLatency)
{
	check(!FPlatformProperties::RequiresCookedData());

	// Lock CompileQueueSection so we can access the input and output queues
	FScopeLock Lock(&CompileQueueSection);

	if (bOptimizeForLowLatency)
	{
		int32 InsertIndex = 0;

		for (; InsertIndex < CompileQueue.Num(); InsertIndex++)
		{
			if (!CompileQueue[InsertIndex]->bOptimizeForLowLatency)
			{
				break;
			}
		}

		// Insert after the last low latency task, but before all the normal tasks
		// This is necessary to make sure that jobs from the same material get processed in order
		// Note: this is assuming that the value of bOptimizeForLowLatency never changes for a certain material
		CompileQueue.InsertZeroed(InsertIndex, NewJobs.Num());

		for (int32 JobIndex = 0; JobIndex < NewJobs.Num(); JobIndex++)
		{
			CompileQueue[InsertIndex + JobIndex] = NewJobs[JobIndex];
		}
	}
	else
	{
		CompileQueue.Append(NewJobs);
	}

	// Using atomics to update NumOutstandingJobs since it is read outside of the critical section
	FPlatformAtomics::InterlockedAdd(&NumOutstandingJobs, NewJobs.Num());

	for (int32 JobIndex = 0; JobIndex < NewJobs.Num(); JobIndex++)
	{
		NewJobs[JobIndex]->bOptimizeForLowLatency = bOptimizeForLowLatency;
		FShaderMapCompileResults& ShaderMapInfo = ShaderMapJobs.FindOrAdd(NewJobs[JobIndex]->Id);
		ShaderMapInfo.bApplyCompletedShaderMapForRendering = bApplyCompletedShaderMapForRendering;
		ShaderMapInfo.NumJobsQueued++;
	}
}

/** Launches the worker, returns the launched process handle. */
FProcHandle FShaderCompilingManager::LaunchWorker(const FString& WorkingDirectory, uint32 InProcessId, uint32 ThreadId, const FString& WorkerInputFile, const FString& WorkerOutputFile, bool bUseNamedPipes, bool bSingleConnectionPipe)
{
	// Setup the parameters that the worker application needs
	// Surround the working directory with double quotes because it may contain a space 
	// WorkingDirectory ends with a '\', so we have to insert another to meet the Windows commandline parsing rules 
	// http://msdn.microsoft.com/en-us/library/17w5ykft.aspx 
	// Use IFileManager to do path conversion to properly handle sandbox paths (outside of standard paths in particular).
	FString WorkerAbsoluteDirectory = IFileManager::Get().ConvertToAbsolutePathForExternalAppForWrite(*WorkingDirectory);
	FPaths::NormalizeDirectoryName(WorkerAbsoluteDirectory);
	FString WorkerParameters = FString(TEXT("\"")) + WorkerAbsoluteDirectory + TEXT("/\" ") + FString::FromInt(InProcessId) + TEXT(" ") + FString::FromInt(ThreadId) + TEXT(" ") + WorkerInputFile + TEXT(" ") + WorkerOutputFile;
	if (bUseNamedPipes)
	{
		WorkerParameters += FString(bSingleConnectionPipe ? TEXT(" -communicatethroughnamedpipeonce ") : TEXT(" -communicatethroughnamedpipe "));
	}
	else
	{
		WorkerParameters += FString(TEXT(" -communicatethroughfile "));
	}
	if ( GIsBuildMachine )
	{
		WorkerParameters += FString(TEXT(" -buildmachine "));
	}
	WorkerParameters += FCommandLine::GetSubprocessCommandline();

	// Launch the worker process
	int32 PriorityModifier = -1; // below normal

#if DEBUG_SHADERCOMPILEWORKER
	// Note: Set breakpoint here and launch the shadercompileworker with WorkerParameters a cmd-line
	const TCHAR* WorkerParametersText = *WorkerParameters;
	FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Launching shader compile worker w/ WorkerParameters\n\t%s\n"), WorkerParametersText);
	return FProcHandle(17);
#else
#if UE_BUILD_DEBUG && PLATFORM_LINUX
	FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Launching shader compile worker:\n\t%s\n"), *WorkerParameters);
#endif
	uint32 WorkerId = 0;
	FProcHandle WorkerHandle = FPlatformProcess::CreateProc(*ShaderCompileWorkerName, *WorkerParameters, true, false, false, &WorkerId, PriorityModifier, NULL, NULL);
	if( !WorkerHandle.IsValid() )
	{
		// If this doesn't error, the app will hang waiting for jobs that can never be completed
		UE_LOG(LogShaderCompilers, Fatal, TEXT( "Couldn't launch %s! Make sure the file is in your binaries folder." ), *ShaderCompileWorkerName );
	}

	return WorkerHandle;
#endif
}

/** Flushes all pending jobs for the given shader maps. */
void FShaderCompilingManager::BlockOnShaderMapCompletion(const TArray<int32>& ShaderMapIdsToFinishCompiling, TMap<int32, FShaderMapFinalizeResults>& CompiledShaderMaps)
{
	if (bAllowAsynchronousShaderCompiling)
	{
		int32 NumPendingJobs = 0;

		do 
		{
			Thread->CheckHealth();
			NumPendingJobs = 0;
			{
				// Lock CompileQueueSection so we can access the input and output queues
				FScopeLock Lock(&CompileQueueSection);

				for (int32 ShaderMapIndex = 0; ShaderMapIndex < ShaderMapIdsToFinishCompiling.Num(); ShaderMapIndex++)
				{
					const FShaderMapCompileResults* ResultsPtr = ShaderMapJobs.Find(ShaderMapIdsToFinishCompiling[ShaderMapIndex]);

					if (ResultsPtr)
					{
						const FShaderMapCompileResults& Results = *ResultsPtr;

						if (Results.FinishedJobs.Num() == Results.NumJobsQueued)
						{
							CompiledShaderMaps.Add(ShaderMapIdsToFinishCompiling[ShaderMapIndex], FShaderMapFinalizeResults(Results));
							ShaderMapJobs.Remove(ShaderMapIdsToFinishCompiling[ShaderMapIndex]);
						}
						else
						{
							NumPendingJobs += Results.NumJobsQueued;
						}
					}
				}
			}

			if (NumPendingJobs > 0)
			{
				// Yield CPU time while waiting
				FPlatformProcess::Sleep(.01f);
			}
		} 
		while (NumPendingJobs > 0);
	}
	else
	{
		int32 NumActiveWorkers = 0;
		do 
		{
			NumActiveWorkers = Thread->CompilingLoop();
		} 
		while (NumActiveWorkers > 0);

		check(CompileQueue.Num() == 0);

		for (int32 ShaderMapIndex = 0; ShaderMapIndex < ShaderMapIdsToFinishCompiling.Num(); ShaderMapIndex++)
		{
			const FShaderMapCompileResults* ResultsPtr = ShaderMapJobs.Find(ShaderMapIdsToFinishCompiling[ShaderMapIndex]);

			if (ResultsPtr)
			{
				const FShaderMapCompileResults& Results = *ResultsPtr;
				check(Results.FinishedJobs.Num() == Results.NumJobsQueued);

				CompiledShaderMaps.Add(ShaderMapIdsToFinishCompiling[ShaderMapIndex], FShaderMapFinalizeResults(Results));
				ShaderMapJobs.Remove(ShaderMapIdsToFinishCompiling[ShaderMapIndex]);
			}
		}
	}
}

void FShaderCompilingManager::BlockOnAllShaderMapCompletion(TMap<int32, FShaderMapFinalizeResults>& CompiledShaderMaps)
{
	if (bAllowAsynchronousShaderCompiling)
	{
		int32 NumPendingJobs = 0;

		do 
		{
			Thread->CheckHealth();
			NumPendingJobs = 0;
			{
				// Lock CompileQueueSection so we can access the input and output queues
				FScopeLock Lock(&CompileQueueSection);

				for (TMap<int32, FShaderMapCompileResults>::TIterator It(ShaderMapJobs); It; ++It)
				{
					const FShaderMapCompileResults& Results = It.Value();

					if (Results.FinishedJobs.Num() == Results.NumJobsQueued)
					{
						CompiledShaderMaps.Add(It.Key(), FShaderMapFinalizeResults(Results));
						It.RemoveCurrent();
					}
					else
					{
						NumPendingJobs += Results.NumJobsQueued;
					}
				}
			}

			if (NumPendingJobs > 0)
			{
				// Yield CPU time while waiting
				FPlatformProcess::Sleep(.01f);
			}
		} 
		while (NumPendingJobs > 0);
	}
	else
	{
		int32 NumActiveWorkers = 0;
		do 
		{
			NumActiveWorkers = Thread->CompilingLoop();
		} 
		while (NumActiveWorkers > 0);

		check(CompileQueue.Num() == 0);

		for (TMap<int32, FShaderMapCompileResults>::TIterator It(ShaderMapJobs); It; ++It)
		{
			const FShaderMapCompileResults& Results = It.Value();

			check(Results.FinishedJobs.Num() == Results.NumJobsQueued);
			CompiledShaderMaps.Add(It.Key(), FShaderMapFinalizeResults(Results));
			It.RemoveCurrent();
		}
	}
}

void FShaderCompilingManager::ProcessCompiledShaderMaps(
	TMap<int32, FShaderMapFinalizeResults>& CompiledShaderMaps, 
	float TimeBudget)
{
	// Keeps shader maps alive as they are passed from the shader compiler and applied to the owning FMaterial
	TArray<TRefCountPtr<FMaterialShaderMap> > LocalShaderMapReferences;
	TMap<FMaterial*, FMaterialShaderMap*> MaterialsToUpdate;
	TMap<FMaterial*, FMaterialShaderMap*> MaterialsToApplyToScene;

	// Process compiled shader maps in FIFO order, in case a shader map has been enqueued multiple times,
	// Which can happen if a material is edited while a background compile is going on
	for (TMap<int32, FShaderMapFinalizeResults>::TIterator ProcessIt(CompiledShaderMaps); ProcessIt; ++ProcessIt)
	{
		TRefCountPtr<FMaterialShaderMap> ShaderMap = NULL;
		TArray<FMaterial*>* Materials = NULL;

		for (TMap<TRefCountPtr<FMaterialShaderMap>, TArray<FMaterial*> >::TIterator ShaderMapIt(FMaterialShaderMap::ShaderMapsBeingCompiled); ShaderMapIt; ++ShaderMapIt)
		{
			if (ShaderMapIt.Key()->CompilingId == ProcessIt.Key())
			{
				ShaderMap = ShaderMapIt.Key();
				Materials = &ShaderMapIt.Value();
				break;
			}
		}

		check((ShaderMap && Materials) || ProcessIt.Key() == GlobalShaderMapId);

		if (ShaderMap && Materials)
		{
			TArray<FString> Errors;
			FShaderMapFinalizeResults& CompileResults = ProcessIt.Value();
			const TArray<FShaderCompileJob*>& ResultArray = CompileResults.FinishedJobs;

			// Make a copy of the array as this entry of FMaterialShaderMap::ShaderMapsBeingCompiled will be removed below
			TArray<FMaterial*> MaterialsArray = *Materials;
			bool bSuccess = true;

			for (int32 JobIndex = 0; JobIndex < ResultArray.Num(); JobIndex++)
			{
				FShaderCompileJob& CurrentJob = *ResultArray[JobIndex];
				bSuccess = bSuccess && CurrentJob.bSucceeded;

				if (CurrentJob.bSucceeded)
				{
					check(CurrentJob.Output.Code.Num() > 0);
				}
				else 
				{
					for (int32 ErrorIndex = 0; ErrorIndex < CurrentJob.Output.Errors.Num(); ErrorIndex++)
					{
						Errors.AddUnique(CurrentJob.Output.Errors[ErrorIndex].StrippedErrorMessage);
					}
				}
			}

			bool bShaderMapComplete = true;

			if (bSuccess)
			{
				bShaderMapComplete = ShaderMap->ProcessCompilationResults(ResultArray, CompileResults.FinalizeJobIndex, TimeBudget);
			}

			for ( auto& Material : MaterialsArray )
			{
				Material->RemoveOutstandingCompileId( ShaderMap->CompilingId );
			}

			if (bShaderMapComplete)
			{
				ShaderMap->bCompiledSuccessfully = bSuccess;

				// Pass off the reference of the shader map to LocalShaderMapReferences
				LocalShaderMapReferences.Add(ShaderMap);
				FMaterialShaderMap::ShaderMapsBeingCompiled.Remove(ShaderMap);

				for (int32 MaterialIndex = 0; MaterialIndex < MaterialsArray.Num(); MaterialIndex++)
				{
					FMaterial* Material = MaterialsArray[MaterialIndex];
					FMaterialShaderMap* CompletedShaderMap = bSuccess ? ShaderMap : NULL;

					if (CompletedShaderMap 
						// Don't modify materials for which the compiled shader map is no longer complete
						// This can happen if a material being compiled is edited, or if CheckMaterialUsage changes a flag and causes a recompile
						&& CompletedShaderMap->IsComplete(Material, true))
					{
						MaterialsToUpdate.Add(Material, CompletedShaderMap);

						// Note: if !CompileResults.bApplyCompletedShaderMapForRendering, RenderingThreadShaderMap must be set elsewhere to match up with the new value of GameThreadShaderMap
						if (CompileResults.bApplyCompletedShaderMapForRendering)
						{
							MaterialsToApplyToScene.Add(Material, CompletedShaderMap);
						}
					}
				}

				if (!bSuccess)
				{				
					for (int32 MaterialIndex = 0; MaterialIndex < MaterialsArray.Num(); MaterialIndex++)
					{
						FMaterial& CurrentMaterial = *MaterialsArray[MaterialIndex];

						// Propagate error messages
						CurrentMaterial.CompileErrors = Errors;

						MaterialsToUpdate.Add( &CurrentMaterial, NULL );

						if (CurrentMaterial.IsDefaultMaterial())
						{
							// Log the errors unsuppressed before the fatal error, so it's always obvious from the log what the compile error was
							for (int32 ErrorIndex = 0; ErrorIndex < Errors.Num(); ErrorIndex++)
							{
								UE_LOG(LogShaderCompilers, Warning, TEXT("	%s"), *Errors[ErrorIndex]);
							}
							// Assert if a default material could not be compiled, since there will be nothing for other failed materials to fall back on.
							UE_LOG(LogShaderCompilers, Fatal,TEXT("Failed to compile default material %s!"), *CurrentMaterial.GetBaseMaterialPathName());
						}

						UE_LOG(LogShaderCompilers, Warning, TEXT("Failed to compile Material %s for platform %s, Default Material will be used in game."), 
							*CurrentMaterial.GetBaseMaterialPathName(), 
							*LegacyShaderPlatformToShaderFormat(ShaderMap->GetShaderPlatform()).ToString());

						for (int32 ErrorIndex = 0; ErrorIndex < Errors.Num(); ErrorIndex++)
						{
							UE_LOG(LogShaders, Warning, TEXT("	%s"), *Errors[ErrorIndex]);
						}
					}
				}

				// Cleanup shader jobs and compile tracking structures
				for (int32 JobIndex = 0; JobIndex < ResultArray.Num(); JobIndex++)
				{
					delete ResultArray[JobIndex];
				}

				CompiledShaderMaps.Remove(ShaderMap->CompilingId);
			}

			if (TimeBudget < 0)
			{
				break;
			}
		}
		else if (ProcessIt.Key() == GlobalShaderMapId)
		{
			FShaderMapFinalizeResults* GlobalShaderResults = CompiledShaderMaps.Find(GlobalShaderMapId);

			if (GlobalShaderResults)
			{
				const TArray<FShaderCompileJob*>& CompilationResults = GlobalShaderResults->FinishedJobs;

				ProcessCompiledGlobalShaders(CompilationResults);

				for (int32 ResultIndex = 0; ResultIndex < CompilationResults.Num(); ResultIndex++)
				{
					delete CompilationResults[ResultIndex];
				}

				CompiledShaderMaps.Remove(GlobalShaderMapId);
			}
		}
	}

	if (MaterialsToUpdate.Num() > 0)
	{
		for (TMap<FMaterial*, FMaterialShaderMap*>::TConstIterator It(MaterialsToUpdate); It; ++It)
		{
			FMaterial* Material = It.Key();
			FMaterialShaderMap* ShaderMap = It.Value();
			check(!ShaderMap || ShaderMap->IsValidForRendering());

			Material->SetGameThreadShaderMap(It.Value());
		}

		const TSet<FSceneInterface*>& AllocatedScenes = GetRendererModule().GetAllocatedScenes();

		for (TSet<FSceneInterface*>::TConstIterator SceneIt(AllocatedScenes); SceneIt; ++SceneIt)
		{
			(*SceneIt)->SetShaderMapsOnMaterialResources(MaterialsToApplyToScene);
		}

		for (TMap<FMaterial*, FMaterialShaderMap*>::TIterator It(MaterialsToUpdate); It; ++It)
		{
			FMaterial* Material = It.Key();

			Material->NotifyCompilationFinished();
		}

#if WITH_EDITOR
		FEditorSupportDelegates::RedrawAllViewports.Broadcast();
#endif // WITH_EDITOR
	}
}


/**
 * Shutdown the shader compile manager
 * this function should be used when ending the game to shutdown shader compile threads
 * will not complete current pending shader compilation
 */
void FShaderCompilingManager::Shutdown()
{
	Thread->Stop();
	Thread->WaitForCompletion();
}


bool FShaderCompilingManager::HandlePotentialRetryOnError(TMap<int32, FShaderMapFinalizeResults>& CompletedShaderMaps)
{
	bool bRetryCompile = false;

	for (TMap<int32, FShaderMapFinalizeResults>::TIterator It(CompletedShaderMaps); It; ++It)
	{
		FShaderMapFinalizeResults& Results = It.Value();

		if (!Results.bAllJobsSucceeded)
		{
			bool bSpecialEngineMaterial = false;
			const FMaterialShaderMap* ShaderMap = NULL;

			for (TMap<TRefCountPtr<FMaterialShaderMap>, TArray<FMaterial*> >::TConstIterator ShaderMapIt(FMaterialShaderMap::ShaderMapsBeingCompiled); ShaderMapIt; ++ShaderMapIt)
			{
				const FMaterialShaderMap* TestShaderMap = ShaderMapIt.Key();

				if (TestShaderMap->CompilingId == It.Key())
				{
					ShaderMap = TestShaderMap;

					for (int32 MaterialIndex = 0; MaterialIndex < ShaderMapIt.Value().Num(); MaterialIndex++)
					{
						FMaterial* Material = ShaderMapIt.Value()[MaterialIndex];
						bSpecialEngineMaterial = bSpecialEngineMaterial || Material->IsSpecialEngineMaterial();
					}
					break;
				}
			}

			check(ShaderMap || It.Key() == GlobalShaderMapId);

#if WITH_EDITORONLY_DATA

			if (UE_LOG_ACTIVE(LogShaders, Log) 
				// Always log detailed errors when a special engine material or global shader fails to compile, as those will be fatal errors
				|| bSpecialEngineMaterial 
				|| It.Key() == GlobalShaderMapId)
			{
				const TArray<FShaderCompileJob*>& CompleteJobs = Results.FinishedJobs;
				TArray<const FShaderCompileJob*> ErrorJobs;
				TArray<FString> UniqueErrors;

				// Gather unique errors
				for (int32 JobIndex = 0; JobIndex < CompleteJobs.Num(); JobIndex++)
				{
					const FShaderCompileJob& CurrentJob = *CompleteJobs[JobIndex];

					if (!CurrentJob.bSucceeded)
					{
						for (int32 ErrorIndex = 0; ErrorIndex < CurrentJob.Output.Errors.Num(); ErrorIndex++)
						{
							const FShaderCompilerError& CurrentError = CurrentJob.Output.Errors[ErrorIndex];

							// Include warnings if LogShaders is unsuppressed, otherwise only include errors
							if (UE_LOG_ACTIVE(LogShaders, Log) || CurrentError.StrippedErrorMessage.Contains(TEXT("error")) )
							{
								UniqueErrors.AddUnique(CurrentJob.Output.Errors[ErrorIndex].GetErrorString());
								ErrorJobs.AddUnique(&CurrentJob);
							}
						}
					}
				}

				// Assuming all the jobs are for the same platform
				const EShaderPlatform TargetShaderPlatform = (EShaderPlatform)CompleteJobs[0]->Input.Target.Platform;
				const TCHAR* MaterialName = ShaderMap ? *ShaderMap->GetFriendlyName() : TEXT("global shaders");
				FString ErrorString = FString::Printf(TEXT("%i Shader compiler errors compiling %s for platform %s:"), UniqueErrors.Num(), MaterialName, *LegacyShaderPlatformToShaderFormat(TargetShaderPlatform).ToString());
				UE_LOG(LogShaderCompilers, Warning, TEXT("%s"), *ErrorString);
				ErrorString += TEXT("\n");

				for (int32 JobIndex = 0; JobIndex < CompleteJobs.Num(); JobIndex++)
				{
					const FShaderCompileJob& CurrentJob = *CompleteJobs[JobIndex];

					if (!CurrentJob.bSucceeded)
					{
						for (int32 ErrorIndex = 0; ErrorIndex < CurrentJob.Output.Errors.Num(); ErrorIndex++)
						{
							FShaderCompilerError CurrentError = CurrentJob.Output.Errors[ErrorIndex];
							int32 UniqueError = INDEX_NONE;

							if (UniqueErrors.Find(CurrentError.GetErrorString(), UniqueError))
							{
								// This unique error is being processed, remove it from the array
								UniqueErrors.RemoveAt(UniqueError);

								// Remap filenames
								if (CurrentError.ErrorFile == TEXT("Material.usf"))
								{
									// MaterialTemplate.usf is dynamically included as Material.usf
									// Currently the material translator does not add new lines when filling out MaterialTemplate.usf,
									// So we don't need the actual filled out version to find the line of a code bug.
									CurrentError.ErrorFile = TEXT("MaterialTemplate.usf");
								}
								else if (CurrentError.ErrorFile.Contains(TEXT("memory")) )
								{
									// Files passed to the shader compiler through memory will be named memory
									// Only the shader's main file is passed through memory without a filename
									CurrentError.ErrorFile = FString(CurrentJob.ShaderType->GetShaderFilename()) + TEXT(".usf");
								}
								else if (CurrentError.ErrorFile == TEXT("VertexFactory.usf"))
								{
									// VertexFactory.usf is dynamically included from whichever vertex factory the shader was compiled with.
									check(CurrentJob.VFType);
									CurrentError.ErrorFile = FString(CurrentJob.VFType->GetShaderFilename()) + TEXT(".usf");
								}
								else if (CurrentError.ErrorFile == TEXT("") && CurrentJob.ShaderType)
								{
									// Some shader compiler errors won't have a file and line number, so we just assume the error happened in file containing the entrypoint function.
									CurrentError.ErrorFile = FString(CurrentJob.ShaderType->GetShaderFilename()) + TEXT(".usf");
								}

								FString UniqueErrorString;

								if ( CurrentJob.ShaderType )
								{
									// Construct a path that will enable VS.NET to find the shader file, relative to the solution
									const FString SolutionPath = FPaths::RootDir();
									FString ShaderPath = FPlatformProcess::ShaderDir();
									FPaths::MakePathRelativeTo(ShaderPath, *SolutionPath);
									CurrentError.ErrorFile = ShaderPath / CurrentError.ErrorFile;
									UniqueErrorString = FString::Printf(TEXT("%s(%s): Shader %s, VF %s:\n\t%s\n"), 
										*CurrentError.ErrorFile, 
										*CurrentError.ErrorLineString, 
										CurrentJob.ShaderType->GetName(), 
										CurrentJob.VFType ? CurrentJob.VFType->GetName() : TEXT("None"), 
										*CurrentError.StrippedErrorMessage);
								}
								else
								{
									UniqueErrorString = FString::Printf(TEXT("%s(0): %s\n"), 
										*CurrentJob.Input.SourceFilename, 
										*CurrentError.StrippedErrorMessage);
								}

								if (FPlatformMisc::IsDebuggerPresent() && !GIsBuildMachine)
								{
									// Using OutputDebugString to avoid any text getting added before the filename,
									// Which will throw off VS.NET's ability to take you directly to the file and line of the error when double clicking it in the output window.
									FPlatformMisc::LowLevelOutputDebugStringf(*UniqueErrorString);
								}
								else
								{
									UE_LOG(LogShaderCompilers, Warning, TEXT("%s"), *UniqueErrorString);
								}

								ErrorString += UniqueErrorString;
							}
						}
					}
				}

				if (UE_LOG_ACTIVE(LogShaders, Log) && bPromptToRetryFailedShaderCompiles)
				{
#if UE_BUILD_DEBUG
					// Use debug break in debug with the debugger attached, otherwise message box
					if (FPlatformMisc::IsDebuggerPresent())
					{
						// A shader compile error has occurred, see the debug output for information.
						// Double click the errors in the VS.NET output window and the IDE will take you directly to the file and line of the error.
						// Check ErrorJobs for more state on the failed shaders, for example in-memory includes like Material.usf
						FPlatformMisc::DebugBreak();
						// Set GRetryShaderCompilation to true in the debugger to enable retries in debug
						// NOTE: MaterialTemplate.usf will not be reloaded when retrying!
						bRetryCompile = GRetryShaderCompilation;
					}
					else 
#endif	//UE_BUILD_DEBUG
						if (FPlatformMisc::MessageBoxExt( EAppMsgType::YesNo, *FText::Format(NSLOCTEXT("UnrealEd", "Error_RetryShaderCompilation", "{0}\r\n\r\nRetry compilation?"),
							FText::FromString(ErrorString)).ToString(), TEXT("Error")))
						{
							bRetryCompile = true;
						}
				}

				if (bRetryCompile)
				{
					break;
				}
			}
#endif	//WITH_EDITORONLY_DATA
		}
	}

	if (bRetryCompile)
	{
		// Flush the shader file cache so that any changes will be propagated.
		FlushShaderFileCache();

		TArray<int32> MapsToRemove;

		for (TMap<int32, FShaderMapFinalizeResults>::TIterator It(CompletedShaderMaps); It; ++It)
		{
			FShaderMapFinalizeResults& Results = It.Value();

			if (!Results.bAllJobsSucceeded)
			{
				MapsToRemove.Add(It.Key());

				// Reset outputs
				for (int32 JobIndex = 0; JobIndex < Results.FinishedJobs.Num(); JobIndex++)
				{
					FShaderCompileJob& CurrentJob = *Results.FinishedJobs[JobIndex];

					// NOTE: Changes to MaterialTemplate.usf before retrying won't work, because the entry for Material.usf in CurrentJob.Environment.IncludeFileNameToContentsMap isn't reset
					CurrentJob.Output = FShaderCompilerOutput();
					CurrentJob.bFinalized = false;
				}

				// Send all the shaders from this shader map through the compiler again
				AddJobs(Results.FinishedJobs, Results.bApplyCompletedShaderMapForRendering, true);
			}
		}

		const int32 OriginalNumShaderMaps = CompletedShaderMaps.Num();

		// Remove the failed shader maps
		for (int32 RemoveIndex = 0; RemoveIndex < MapsToRemove.Num(); RemoveIndex++)
		{
			CompletedShaderMaps.Remove(MapsToRemove[RemoveIndex]);
		}

		check(CompletedShaderMaps.Num() == OriginalNumShaderMaps - MapsToRemove.Num());

		// Block until the failed shader maps have been compiled again
		BlockOnShaderMapCompletion(MapsToRemove, CompletedShaderMaps);

		check(CompletedShaderMaps.Num() == OriginalNumShaderMaps);
	}

	return bRetryCompile;
}

void FShaderCompilingManager::CancelCompilation(const TCHAR* MaterialName, const TArray<int32>& ShaderMapIdsToCancel)
{
	check(!FPlatformProperties::RequiresCookedData());
	UE_LOG(LogShaders, Log, TEXT("CancelCompilation %s "), MaterialName ? MaterialName : TEXT(""));

	// Lock CompileQueueSection so we can access the input and output queues
	FScopeLock Lock(&CompileQueueSection);

	int32 TotalNumJobsRemoved = 0;
	for (int32 IdIndex = 0; IdIndex < ShaderMapIdsToCancel.Num(); ++IdIndex)
	{
		int32 MapIdx = ShaderMapIdsToCancel[IdIndex];
		if (FShaderMapCompileResults* ShaderMapJob = ShaderMapJobs.Find(MapIdx))
		{
			int32 NumJobsRemoved = 0;

			int32 JobIndex = CompileQueue.Num();
			while ( --JobIndex >= 0 )
			{
				if (FShaderCompileJob* Job = CompileQueue[JobIndex])
				{
					if (Job->Id == MapIdx)
					{
						++TotalNumJobsRemoved;
						++NumJobsRemoved;
						CompileQueue.RemoveAt(JobIndex, 1, false);
					}
				}
			}

			ShaderMapJob->NumJobsQueued -= NumJobsRemoved;

			if (ShaderMapJob->NumJobsQueued == 0)
			{
				//We've removed all the jobs for this shader map so remove it.
				ShaderMapJobs.Remove(MapIdx);
			}
		}
	}
	CompileQueue.Shrink();

	// Using atomics to update NumOutstandingJobs since it is read outside of the critical section
	FPlatformAtomics::InterlockedAdd(&NumOutstandingJobs, -TotalNumJobsRemoved);
}

void FShaderCompilingManager::FinishCompilation(const TCHAR* MaterialName, const TArray<int32>& ShaderMapIdsToFinishCompiling)
{
	check(!FPlatformProperties::RequiresCookedData());
	const double StartTime = FPlatformTime::Seconds();

	FText StatusUpdate;
	if ( MaterialName != NULL )
	{
		FFormatNamedArguments Args;
		Args.Add( TEXT("MaterialName"), FText::FromString( MaterialName ) );
		StatusUpdate = FText::Format( NSLOCTEXT("ShaderCompilingManager", "CompilingShadersForMaterialStatus", "Compiling shaders: {MaterialName}..."), Args );
	}
	else
	{
		StatusUpdate = NSLOCTEXT("ShaderCompilingManager", "CompilingShadersStatus", "Compiling shaders...");
	}

	FScopedSlowTask SlowTask(0, StatusUpdate, GIsEditor && !IsRunningCommandlet());

	TMap<int32, FShaderMapFinalizeResults> CompiledShaderMaps;
	BlockOnShaderMapCompletion(ShaderMapIdsToFinishCompiling, CompiledShaderMaps);

	bool bRetry = false;
	do 
	{
		bRetry = HandlePotentialRetryOnError(CompiledShaderMaps);
	} 
	while (bRetry);

	ProcessCompiledShaderMaps(CompiledShaderMaps, FLT_MAX);
	check(CompiledShaderMaps.Num() == 0);

	const double EndTime = FPlatformTime::Seconds();

	UE_LOG(LogShaders, Log, TEXT("FinishCompilation %s %.3fs"), MaterialName ? MaterialName : TEXT(""), (float)(EndTime - StartTime));
}

void FShaderCompilingManager::FinishAllCompilation()
{
	check(!FPlatformProperties::RequiresCookedData());
	const double StartTime = FPlatformTime::Seconds();

	TMap<int32, FShaderMapFinalizeResults> CompiledShaderMaps;
	BlockOnAllShaderMapCompletion(CompiledShaderMaps);

	bool bRetry = false;
	do 
	{
		bRetry = HandlePotentialRetryOnError(CompiledShaderMaps);
	} 
	while (bRetry);

	ProcessCompiledShaderMaps(CompiledShaderMaps, FLT_MAX);
	check(CompiledShaderMaps.Num() == 0);

	const double EndTime = FPlatformTime::Seconds();

	UE_LOG(LogShaders, Log, TEXT("FinishAllCompilation %.3fs"), (float)(EndTime - StartTime));
}

void FShaderCompilingManager::ProcessAsyncResults(bool bLimitExecutionTime, bool bBlockOnGlobalShaderCompletion)
{
	if (bAllowAsynchronousShaderCompiling)
	{
		Thread->CheckHealth();
		{
			const double StartTime = FPlatformTime::Seconds();

			// Block on global shaders before checking for shader maps to finalize
			// So if we block on global shaders for a long time, we will get a chance to finalize all the non-global shader maps completed during that time.
			if (bBlockOnGlobalShaderCompletion)
			{
				TArray<int32> ShaderMapId;
				ShaderMapId.Add(GlobalShaderMapId);

				// Block until the global shader map jobs are complete
				GShaderCompilingManager->BlockOnShaderMapCompletion(ShaderMapId, PendingFinalizeShaderMaps);
			}

			int32 NumCompilingShaderMaps = 0;
			{
				// Lock CompileQueueSection so we can access the input and output queues
				FScopeLock Lock(&CompileQueueSection);

				if (!bBlockOnGlobalShaderCompletion)
				{
					bCompilingDuringGame = true;
				}

				TArray<int32> ShaderMapsToRemove;

				for (TMap<int32, FShaderMapCompileResults>::TIterator It(ShaderMapJobs); It; ++It)
				{
					const FShaderMapCompileResults& Results = It.Value();

					if (Results.FinishedJobs.Num() == Results.NumJobsQueued)
					{
						PendingFinalizeShaderMaps.Add(It.Key(), FShaderMapFinalizeResults(Results));
						ShaderMapsToRemove.Add(It.Key());
					}
				}

				for (int32 RemoveIndex = 0; RemoveIndex < ShaderMapsToRemove.Num(); RemoveIndex++)
				{
					ShaderMapJobs.Remove(ShaderMapsToRemove[RemoveIndex]);
				}

				NumCompilingShaderMaps = ShaderMapJobs.Num();
			}

			int32 NumPendingShaderMaps = PendingFinalizeShaderMaps.Num();

			if (PendingFinalizeShaderMaps.Num() > 0)
			{
				bool bRetry = false;
				do 
				{
					bRetry = HandlePotentialRetryOnError(PendingFinalizeShaderMaps);
				} 
				while (bRetry);

				const float TimeBudget = bLimitExecutionTime ? ProcessGameThreadTargetTime : FLT_MAX;
				ProcessCompiledShaderMaps(PendingFinalizeShaderMaps, TimeBudget);
				check(bLimitExecutionTime || PendingFinalizeShaderMaps.Num() == 0);
			}

			if (bBlockOnGlobalShaderCompletion)
			{
				check(PendingFinalizeShaderMaps.Num() == 0);

				if (NumPendingShaderMaps - PendingFinalizeShaderMaps.Num() > 0)
				{
					UE_LOG(LogShaders, Warning, TEXT("Blocking ProcessAsyncResults for %.1fs, processed %u shader maps, %u being compiled"), 
						(float)(FPlatformTime::Seconds() - StartTime),
						NumPendingShaderMaps - PendingFinalizeShaderMaps.Num(), 
						NumCompilingShaderMaps);
				}
			}
			else if (NumPendingShaderMaps - PendingFinalizeShaderMaps.Num() > 0)
			{
				UE_LOG(LogShaders, Log, TEXT("Completed %u async shader maps, %u more pending, %u being compiled"), 
					NumPendingShaderMaps - PendingFinalizeShaderMaps.Num(), 
					PendingFinalizeShaderMaps.Num(),
					NumCompilingShaderMaps);
			}
		}
	}
	else
	{
		check(CompileQueue.Num() == 0);
	}
}

bool FShaderCompilingManager::IsShaderCompilerWorkerRunning(FProcHandle & WorkerHandle)
{
	return FPlatformProcess::IsProcRunning(WorkerHandle);
}

/** Enqueues a shader compile job with GShaderCompilingManager. */
void GlobalBeginCompileShader(
	const FString& DebugGroupName,
	FVertexFactoryType* VFType,
	FShaderType* ShaderType,
	const TCHAR* SourceFilename,
	const TCHAR* FunctionName,
	FShaderTarget Target,
	FShaderCompileJob* NewJob,
	TArray<FShaderCompileJob*>& NewJobs,
	bool bAllowDevelopmentShaderCompile
	)
{
	FShaderCompilerInput& Input = NewJob->Input;
	Input.Target = Target;
	Input.ShaderFormat = LegacyShaderPlatformToShaderFormat(EShaderPlatform(Target.Platform));
	Input.SourceFilename = SourceFilename;
	Input.EntryPointName = FunctionName;
	Input.DumpDebugInfoRootPath = GShaderCompilingManager->GetAbsoluteShaderDebugInfoDirectory() / Input.ShaderFormat.ToString();

	if (GDumpShaderDebugInfo != 0)
	{
		Input.DumpDebugInfoPath = Input.DumpDebugInfoRootPath / DebugGroupName;

		if (VFType)
		{
			Input.DumpDebugInfoPath = Input.DumpDebugInfoPath / VFType->GetName();
		}

		Input.DumpDebugInfoPath = Input.DumpDebugInfoPath / ShaderType->GetName();
		// Sanitize the name to be used as a path
		// List mostly comes from set of characters not allowed by windows in a path.  Just try to rename a file and type one of these for the list.
		Input.DumpDebugInfoPath.ReplaceInline(TEXT("<"), TEXT("("));
		Input.DumpDebugInfoPath.ReplaceInline(TEXT(">"), TEXT(")"));
		Input.DumpDebugInfoPath.ReplaceInline(TEXT("::"), TEXT("=="));		
		Input.DumpDebugInfoPath.ReplaceInline(TEXT("|"), TEXT("_"));		
		Input.DumpDebugInfoPath.ReplaceInline(TEXT("*"), TEXT("-"));
		Input.DumpDebugInfoPath.ReplaceInline(TEXT("?"), TEXT("!"));
		Input.DumpDebugInfoPath.ReplaceInline(TEXT("\""), TEXT("\'"));

		if (!IFileManager::Get().DirectoryExists(*Input.DumpDebugInfoPath))
		{
			verifyf(IFileManager::Get().MakeDirectory(*Input.DumpDebugInfoPath, true), TEXT("Failed to create directory for shader debug info '%s'"), *Input.DumpDebugInfoPath);
		}
	}

	// Add the appropriate definitions for the shader frequency.
	{
		Input.Environment.SetDefine(TEXT("PIXELSHADER"),	Target.Frequency == SF_Pixel);
		Input.Environment.SetDefine(TEXT("DOMAINSHADER"),	Target.Frequency == SF_Domain);
		Input.Environment.SetDefine(TEXT("HULLSHADER"),		Target.Frequency == SF_Hull);
		Input.Environment.SetDefine(TEXT("VERTEXSHADER"),	Target.Frequency == SF_Vertex);
		Input.Environment.SetDefine(TEXT("GEOMETRYSHADER"),	Target.Frequency == SF_Geometry);
		Input.Environment.SetDefine(TEXT("COMPUTESHADER"),	Target.Frequency == SF_Compute);
	}

	ShaderType->AddReferencedUniformBufferIncludes(Input.Environment, Input.SourceFilePrefix, (EShaderPlatform)Target.Platform);

	if (VFType)
	{
		VFType->AddReferencedUniformBufferIncludes(Input.Environment, Input.SourceFilePrefix, (EShaderPlatform)Target.Platform);
	}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	{
		FString Path = CVarD3DCompilerPath.GetValueOnAnyThread();
		if(!Path.IsEmpty())
		{
			Input.Environment.SetDefine(TEXT("D3DCOMPILER_PATH"), *Path);
		}
	}
#endif

	{
		static const auto CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shaders.Optimize"));

		if (CVar->GetInt() == 0)
		{
			Input.Environment.CompilerFlags.Add(CFLAG_Debug);
		}
	}
	
	{
		static const auto CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shaders.KeepDebugInfo"));

		if (CVar->GetInt() != 0)
		{
			Input.Environment.CompilerFlags.Add(CFLAG_KeepDebugInfo);
		}
	}

	{
		FString ShaderPDBRoot;
		GConfig->GetString(TEXT("DevOptions.Shaders"), TEXT("ShaderPDBRoot"), ShaderPDBRoot, GEngineIni);
		if ( !ShaderPDBRoot.IsEmpty() )
		{
			Input.Environment.SetDefine(TEXT("SHADER_PDB_ROOT"), *ShaderPDBRoot);
		}
	}

	{
		static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("Compat.UseDXT5NormalMaps"));
		Input.Environment.SetDefine(TEXT("DXT5_NORMALMAPS"), CVar ? (CVar->GetValueOnGameThread() != 0) : 0);
	}

	if(bAllowDevelopmentShaderCompile)
	{
		static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.CompileShadersForDevelopment"));
		Input.Environment.SetDefine(TEXT("COMPILE_SHADERS_FOR_DEVELOPMENT"), CVar ? (CVar->GetValueOnGameThread() != 0) : 0);
	}

	{
		static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.AllowStaticLighting"));
		Input.Environment.SetDefine(TEXT("ALLOW_STATIC_LIGHTING"), CVar ? (CVar->GetValueOnGameThread() != 0) : 1);
	}

	{
		static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.GBuffer"));
		Input.Environment.SetDefine(TEXT("NO_GBUFFER"), CVar ? (CVar->GetValueOnGameThread() == 0) : 0);
	}

	{
		static IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DBuffer"));
		Input.Environment.SetDefine(TEXT("USE_DBUFFER"), CVar ? CVar->GetInt() : 0);
	}
	
	{
		int32 UseFrameBufferSRGB = 1;
#if PLATFORM_MAC // @todo: remove once Apple fixes radr://16754329 AMD Cards don't always perform FRAMEBUFFER_SRGB if the draw FBO has mixed sRGB & non-SRGB colour attachments
		static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mac.UseFrameBufferSRGB"));
		UseFrameBufferSRGB = CVar ? (CVar->GetValueOnGameThread() != 0) : 0;
#endif
		Input.Environment.SetDefine(TEXT("USE_FRAMEBUFFER_SRGB"), UseFrameBufferSRGB);
	}

	NewJobs.Add(NewJob);
}


/** Timer class used to report information on the 'recompileshaders' console command. */
class FRecompileShadersTimer
{
public:
	FRecompileShadersTimer(const TCHAR* InInfoStr=TEXT("Test")) :
		InfoStr(InInfoStr),
		bAlreadyStopped(false)
	{
		StartTime = FPlatformTime::Seconds();
	}

	FRecompileShadersTimer(const FString& InInfoStr) :
		InfoStr(InInfoStr),
		bAlreadyStopped(false)
	{
		StartTime = FPlatformTime::Seconds();
	}

	void Stop(bool DisplayLog = true)
	{
		if (!bAlreadyStopped)
		{
			bAlreadyStopped = true;
			EndTime = FPlatformTime::Seconds();
			TimeElapsed = EndTime-StartTime;
			if (DisplayLog)
			{
				UE_LOG(LogShaderCompilers, Warning, TEXT("		[%s] took [%.4f] s"),*InfoStr,TimeElapsed);
			}
		}
	}

	~FRecompileShadersTimer()
	{
		Stop(true);
	}

protected:
	double StartTime,EndTime;
	double TimeElapsed;
	FString InfoStr;
	bool bAlreadyStopped;
};

class FRecompileShaderMessageHandler : public IPlatformFile::IFileServerMessageHandler
{
public:
	FRecompileShaderMessageHandler( const TCHAR* InCmd ) :
		Cmd( InCmd )
	{
	}

	/** Subclass fills out an archive to send to the server */
	virtual void FillPayload(FArchive& Payload) override
	{
		bool bCompileChangedShaders = true;

		const TCHAR* CmdString = *Cmd;
		FString CmdName = FParse::Token(CmdString, 0);

		if( !CmdName.IsEmpty() && FCString::Stricmp(*CmdName,TEXT("Material"))==0 )
		{
			bCompileChangedShaders = false;

			// tell other side the material to load, by pathname
			FString RequestedMaterialName( FParse::Token( CmdString, 0 ) );

			for( TObjectIterator<UMaterialInterface> It; It; ++It )
			{
				UMaterial* Material = It->GetMaterial();

				if( Material && Material->GetName() == RequestedMaterialName)
				{
					MaterialsToLoad.Add( It->GetPathName() );
					break;
				}
			}
		}
		else
		{
			// tell other side all the materials to load, by pathname
			for( TObjectIterator<UMaterialInterface> It; It; ++It )
			{
				MaterialsToLoad.Add( It->GetPathName() );
			}
		}

		Payload << MaterialsToLoad;
		uint32 ShaderPlatform = ( uint32 )GMaxRHIShaderPlatform;
		Payload << ShaderPlatform;
		// tell the other side the Ids we have so it doesn't send back duplicates
		// (need to serialize this into a TArray since FShaderResourceId isn't known in the file server)
		TArray<FShaderResourceId> AllIds;
		FShaderResource::GetAllShaderResourceId( AllIds );

		TArray<uint8> SerializedBytes;
		FMemoryWriter Ar( SerializedBytes );
		Ar << AllIds;
		Payload << SerializedBytes;
		Payload << bCompileChangedShaders;
	}

	/** Subclass pulls data response from the server */
	virtual void ProcessResponse(FArchive& Response) override
	{
		// pull back the compiled mesh material data (if any)
		TArray<uint8> MeshMaterialMaps;
		Response << MeshMaterialMaps;

		// now we need to refresh the RHI resources
		FlushRenderingCommands();

		// reload the global shaders
		GetGlobalShaderMap(GMaxRHIShaderPlatform, true);

		//invalidate global bound shader states so they will be created with the new shaders the next time they are set (in SetGlobalBoundShaderState)
		for(TLinkedList<FGlobalBoundShaderStateResource*>::TIterator It(FGlobalBoundShaderStateResource::GetGlobalBoundShaderStateList());It;It.Next())
		{
			BeginUpdateResourceRHI(*It);
		}

		// load all the mesh material shaders if any were sent back
		if (MeshMaterialMaps.Num() > 0)
		{
			// this will stop the rendering thread, and reattach components, in the destructor
			FMaterialUpdateContext UpdateContext;

			// parse the shaders
			FMemoryReader MemoryReader(MeshMaterialMaps, true);
			FNameAsStringProxyArchive Ar(MemoryReader);
			FMaterialShaderMap::LoadForRemoteRecompile(Ar, GMaxRHIShaderPlatform, MaterialsToLoad);

			// gather the shader maps to reattach
			for (TObjectIterator<UMaterial> It; It; ++It)
			{
				UpdateContext.AddMaterial(*It);
			}

			// fixup uniform expressions
			UMaterialInterface::RecacheAllMaterialUniformExpressions();
		}
	}

private:
	/** The materials we send over the network and expect maps for on the return */
	TArray<FString> MaterialsToLoad;

	/** The recompileshader console command to parse */
	FString Cmd;
};

bool RecompileShaders(const TCHAR* Cmd, FOutputDevice& Ar)
{
	// if this platform can't compile shaders, then we try to send a message to a file/cooker server
	if (FPlatformProperties::RequiresCookedData())
	{
		FRecompileShaderMessageHandler Handler( Cmd );

		// send the info, the handler will process the response (and update shaders, etc)
		IFileManager::Get().SendMessageToServer(TEXT("RecompileShaders"), &Handler);

		return true;
	}

	FString FlagStr(FParse::Token(Cmd, 0));
	if( FlagStr.Len() > 0 )
	{
		GWarn->BeginSlowTask( NSLOCTEXT("ShaderCompilingManager", "BeginRecompilingShadersTask", "Recompiling shaders"), true );

		// Flush the shader file cache so that any changes to shader source files will be detected
		FlushShaderFileCache();
		FlushRenderingCommands();

		if( FCString::Stricmp(*FlagStr,TEXT("Changed"))==0)
		{
			TArray<FShaderType*> OutdatedShaderTypes;
			TArray<const FVertexFactoryType*> OutdatedFactoryTypes;
			{
				FRecompileShadersTimer SearchTimer(TEXT("Searching for changed files"));
				FShaderType::GetOutdatedTypes(OutdatedShaderTypes, OutdatedFactoryTypes);
			}

			if (OutdatedShaderTypes.Num() > 0 || OutdatedFactoryTypes.Num() > 0)
			{
				FRecompileShadersTimer TestTimer(TEXT("RecompileShaders Changed"));

				// Kick off global shader recompiles
				UMaterialInterface::IterateOverActiveFeatureLevels([&](ERHIFeatureLevel::Type InFeatureLevel) {
					auto ShaderPlatform = GShaderPlatformForFeatureLevel[InFeatureLevel];
					BeginRecompileGlobalShaders(OutdatedShaderTypes, ShaderPlatform);
					UMaterial::UpdateMaterialShaders(OutdatedShaderTypes, OutdatedFactoryTypes, ShaderPlatform);
				});

				GWarn->StatusUpdate(0, 1, NSLOCTEXT("ShaderCompilingManager", "CompilingGlobalShaderStatus", "Compiling global shaders..."));

				// Block on global shaders
				FinishRecompileGlobalShaders();
			}
			else
			{
				UE_LOG(LogShaderCompilers, Warning, TEXT("No Shader changes found."));
			}
		}
		else if( FCString::Stricmp(*FlagStr,TEXT("Global"))==0)
		{
			FRecompileShadersTimer TestTimer(TEXT("RecompileShaders Global"));
			RecompileGlobalShaders();
		}
		else if( FCString::Stricmp(*FlagStr,TEXT("Material"))==0)
		{
			FString RequestedMaterialName(FParse::Token(Cmd, 0));
			FRecompileShadersTimer TestTimer(FString::Printf(TEXT("Recompile Material %s"), *RequestedMaterialName));
			bool bMaterialFound = false;
			for( TObjectIterator<UMaterial> It; It; ++It )
			{
				UMaterial* Material = *It;
				if( Material && Material->GetName() == RequestedMaterialName)
				{
					bMaterialFound = true;
#if WITH_EDITOR
					// <Pre/Post>EditChange will force a re-creation of the resource,
					// in turn recompiling the shader.
					Material->PreEditChange(NULL);
					Material->PostEditChange();
#endif // WITH_EDITOR
					break;
				}
			}

			if (!bMaterialFound)
			{
				TestTimer.Stop(false);
				UE_LOG(LogShaderCompilers, Warning, TEXT("Couldn't find Material %s!"), *RequestedMaterialName);
			}
		}
		else if( FCString::Stricmp(*FlagStr,TEXT("All"))==0)
		{
			FRecompileShadersTimer TestTimer(TEXT("RecompileShaders"));
			RecompileGlobalShaders();
			for( TObjectIterator<UMaterial> It; It; ++It )
			{
				UMaterial* Material = *It;
				if( Material )
				{
					UE_LOG(LogShaderCompilers, Log, TEXT("recompiling [%s]"),*Material->GetFullName());

#if WITH_EDITOR
					// <Pre/Post>EditChange will force a re-creation of the resource,
					// in turn recompiling the shader.
					Material->PreEditChange(NULL);
					Material->PostEditChange();
#endif // WITH_EDITOR
				}
			}
		}
		else
		{
			TArray<FShaderType*> ShaderTypes = FShaderType::GetShaderTypesByFilename(*FlagStr);
			if (ShaderTypes.Num() > 0)
			{
				FRecompileShadersTimer TestTimer(TEXT("RecompileShaders SingleShader"));
				
				TArray<const FVertexFactoryType*> FactoryTypes;

				UMaterialInterface::IterateOverActiveFeatureLevels([&](ERHIFeatureLevel::Type InFeatureLevel) {
					auto ShaderPlatform = GShaderPlatformForFeatureLevel[InFeatureLevel];
					BeginRecompileGlobalShaders(ShaderTypes, ShaderPlatform);
					UMaterial::UpdateMaterialShaders(ShaderTypes, FactoryTypes, ShaderPlatform);
					FinishRecompileGlobalShaders();
				});
			}
		}

		GWarn->EndSlowTask();

		return 1;
	}

	UE_LOG(LogShaderCompilers, Warning, TEXT("Invalid parameter. Options are: \n'Changed', 'Global', 'Material [name]', 'All' 'Platform [name]'\nNote: Platform implies Changed, and requires the proper target platform modules to be compiled."));
	return 1;
}

static FString XGE_ConsolePath;
static const FString XGE_ScriptFileName(TEXT("xgscript.xml"));
static const FString XGE_InputFileName (TEXT("WorkerInput.in"));
static const FString XGE_OutputFileName(TEXT("WorkerOutput.out"));
static const FString XGE_SuccessFileName(TEXT("Success"));

bool FShaderCompileXGEThreadRunnable::IsSupported()
{
	// List of possible paths to xgconsole.exe
	static const TCHAR* Paths[] =
	{
		TEXT("C:\\Program Files\\Xoreax\\IncrediBuild\\xgConsole.exe"),
		TEXT("C:\\Program Files (x86)\\Xoreax\\IncrediBuild\\xgConsole.exe")
	};

	// Check the command line to see if XGE shader compilation has been enabled/disabled.
	// This overrides the value of the console variable.
	if (FParse::Param(FCommandLine::Get(), TEXT("xgeshadercompile")))
	{
		XGEConsoleVariables::Enabled = 1;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("noxgeshadercompile")))
	{
		XGEConsoleVariables::Enabled = 0;
	}

	// Check for a valid installation of Incredibuild by seeing if xgconsole.exe exists.
	if (XGEConsoleVariables::Enabled == 1)
	{
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

		bool bFound = false;
		for (int PathIndex = 0; PathIndex < ARRAY_COUNT(Paths); PathIndex++)
		{
			if (PlatformFile.FileExists(Paths[PathIndex]))
			{
				bFound = true;
				XGE_ConsolePath = Paths[PathIndex];
				break;
			}
		}

		if (!bFound)
		{
			UE_LOG(LogShaderCompilers, Warning, TEXT("Cannot use XGE Shader Compiler as Incredibuild is not installed on this machine."));
			XGEConsoleVariables::Enabled = 0;
		}
	}

	return XGEConsoleVariables::Enabled == 1;
}

/** Initialization constructor. */
FShaderCompileXGEThreadRunnable::FShaderCompileXGEThreadRunnable(class FShaderCompilingManager* InManager)
	: FShaderCompileThreadRunnableBase(InManager)
	, BuildProcessID(INDEX_NONE)
	, XGEWorkingDirectory(InManager->AbsoluteShaderBaseWorkingDirectory / TEXT("XGE"))
	, XGEDirectoryIndex(0)
	, LastAddTime(0)
	, StartTime(0)
	, BatchIndexToCreate(0)
	, BatchIndexToFill(0)
{
}

FShaderCompileXGEThreadRunnable::~FShaderCompileXGEThreadRunnable()
{
	if (BuildProcessHandle.IsValid())
	{
		// We still have a build in progress.
		// Kill it...
		FPlatformProcess::TerminateProc(BuildProcessHandle);
		BuildProcessHandle.Close();
	}

	// Clean up any intermediate files/directories we've got left over.
	IFileManager::Get().DeleteDirectory(*XGEWorkingDirectory, false, true);

	// Delete all the shader batch instances we have.
	for (FShaderBatch* Batch : ShaderBatchesIncomplete)
		delete Batch;

	for (FShaderBatch* Batch : ShaderBatchesInFlight)
		delete Batch;

	for (FShaderBatch* Batch : ShaderBatchesFull)
		delete Batch;

	ShaderBatchesIncomplete.Empty();
	ShaderBatchesInFlight.Empty();
	ShaderBatchesFull.Empty();
}

static FArchive* CreateFileHelper(const FString& Filename)
{
	// TODO: This logic came from FShaderCompileThreadRunnable::WriteNewTasks().
	// We can't avoid code duplication unless we refactored the local worker too.

	FArchive* File = nullptr;
	int32 RetryCount = 0;
	// Retry over the next two seconds if we can't write out the file.
	// Anti-virus and indexing applications can interfere and cause this to fail.
	while (File == nullptr && RetryCount < 200)
	{
		if (RetryCount > 0)
		{
			FPlatformProcess::Sleep(0.01f);
		}
		File = IFileManager::Get().CreateFileWriter(*Filename, FILEWRITE_EvenIfReadOnly);
		RetryCount++;
	}
	if (File == nullptr)
	{
		File = IFileManager::Get().CreateFileWriter(*Filename, FILEWRITE_EvenIfReadOnly | FILEWRITE_NoFail);
	}
	checkf(File, TEXT("Failed to create file %s!"), *Filename);
	return File;
}

static void MoveFileHelper(const FString& To, const FString& From)
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	if (PlatformFile.FileExists(*From))
	{
		FString DirectoryName;
		int32 LastSlashIndex;
		if (To.FindLastChar('/', LastSlashIndex))
		{
			DirectoryName = To.Left(LastSlashIndex);
		}
		else
		{
			DirectoryName = To;
		}

		// TODO: This logic came from FShaderCompileThreadRunnable::WriteNewTasks().
		// We can't avoid code duplication unless we refactored the local worker too.

		bool Success = false;
		int32 RetryCount = 0;
		// Retry over the next two seconds if we can't move the file.
		// Anti-virus and indexing applications can interfere and cause this to fail.
		while (!Success && RetryCount < 200)
		{
			if (RetryCount > 0)
			{
				FPlatformProcess::Sleep(0.01f);
			}

			// MoveFile does not create the directory tree, so try to do that now...
			Success = PlatformFile.CreateDirectoryTree(*DirectoryName);
			if (Success)
			{
				Success = PlatformFile.MoveFile(*To, *From);
			}
			RetryCount++;
		}
		checkf(Success, TEXT("Failed to move file %s to %s!"), *From, *To);
	}
}

static void DeleteFileHelper(const FString& Filename)
{
	// TODO: This logic came from FShaderCompileThreadRunnable::WriteNewTasks().
	// We can't avoid code duplication unless we refactored the local worker too.

	if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*Filename))
	{
		bool bDeletedOutput = IFileManager::Get().Delete(*Filename, true, true);

		// Retry over the next two seconds if we couldn't delete it
		int32 RetryCount = 0;
		while (!bDeletedOutput && RetryCount < 200)
		{
			FPlatformProcess::Sleep(0.01f);
			bDeletedOutput = IFileManager::Get().Delete(*Filename, true, true);
			RetryCount++;
		}
		checkf(bDeletedOutput, TEXT("Failed to delete %s!"), *Filename);
	}
}

void FShaderCompileXGEThreadRunnable::PostCompletedJobsForBatch(FShaderBatch* Batch)
{
	// Enter the critical section so we can access the input and output queues
	FScopeLock Lock(&Manager->CompileQueueSection);
	for (FShaderCompileJob* Job : Batch->GetJobs())
	{
		FShaderMapCompileResults& ShaderMapResults = Manager->ShaderMapJobs.FindChecked(Job->Id);
		ShaderMapResults.FinishedJobs.Add(Job);
		ShaderMapResults.bAllJobsSucceeded = ShaderMapResults.bAllJobsSucceeded && Job->bSucceeded;
	}

	// Using atomics to update NumOutstandingJobs since it is read outside of the critical section
	FPlatformAtomics::InterlockedAdd(&Manager->NumOutstandingJobs, -Batch->NumJobs());
}

void FShaderCompileXGEThreadRunnable::FShaderBatch::AddJob(FShaderCompileJob* Job)
{
	// We can only add jobs to a batch which hasn't been written out yet.
	if (bTransferFileWritten)
	{
		UE_LOG(LogShaderCompilers, Fatal, TEXT("Attempt to add shader compile jobs to an XGE shader batch which has already been written to disk."));
	}
	else
	{
		Jobs.Add(Job);
	}
}
		
void FShaderCompileXGEThreadRunnable::FShaderBatch::WriteTransferFile()
{
	// Write out the file that the worker app is waiting for, which has all the information needed to compile the shader.
	FArchive* TransferFile = CreateFileHelper(InputFileNameAndPath);
	DoWriteTasks(Jobs, *TransferFile);
	delete TransferFile;

	bTransferFileWritten = true;
}

void FShaderCompileXGEThreadRunnable::FShaderBatch::SetIndices(int32 InDirectoryIndex, int32 InBatchIndex)
{
	DirectoryIndex = InDirectoryIndex;
	BatchIndex = InBatchIndex;

	WorkingDirectory = FString::Printf(TEXT("%s/%d/%d"), *DirectoryBase, DirectoryIndex, BatchIndex);

	InputFileNameAndPath = WorkingDirectory / InputFileName;
	OutputFileNameAndPath = WorkingDirectory / OutputFileName;
	SuccessFileNameAndPath = WorkingDirectory / SuccessFileName;
}

void FShaderCompileXGEThreadRunnable::FShaderBatch::CleanUpFiles(bool keepInputFile)
{
	if (!keepInputFile)
	{
		DeleteFileHelper(InputFileNameAndPath);
	}

	DeleteFileHelper(OutputFileNameAndPath);
	DeleteFileHelper(SuccessFileNameAndPath);
}

static void WriteScriptFileHeader(FArchive* ScriptFile, const FString& WorkerName)
{
	static const TCHAR HeaderTemplate[] =
		TEXT("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\r\n"																						)
		TEXT("<BuildSet FormatVersion=\"1\">\r\n"																													)
		TEXT(	"\t<Environments>\r\n"																																)
		TEXT(		"\t\t<Environment Name=\"Default\">\r\n"																										)
		TEXT(			"\t\t\t<Tools>\r\n"																															)
		TEXT(				"\t\t\t\t<Tool Name=\"ShaderCompiler\" Path=\"%s\" OutputFileMasks=\"%s,%s\" AllowRemote=\"true\" AllowRestartOnLocal=\"true\" />\r\n"	)
		TEXT(			"\t\t\t</Tools>\r\n"																														)
		TEXT(		"\t\t</Environment>\r\n"																														)
		TEXT(	"\t</Environments>\r\n"																																)
		TEXT(	"\t<Project Env=\"Default\" Name=\"Shader Compilation Project\">\r\n"																				)
		TEXT(		"\t\t<TaskGroup Name=\"Compiling Shaders\" Tool=\"ShaderCompiler\">\r\n"																		);

	FString HeaderXML = FString::Printf(HeaderTemplate, *WorkerName, *XGE_OutputFileName, *XGE_SuccessFileName);
	ScriptFile->Serialize((void*)StringCast<ANSICHAR>(*HeaderXML, HeaderXML.Len()).Get(), sizeof(ANSICHAR) * HeaderXML.Len());
}

static void WriteScriptFileFooter(FArchive* ScriptFile)
{
	static const ANSICHAR HeaderFooter[] =
				"\t\t</TaskGroup>\r\n"
			"\t</Project>\r\n"
		"</BuildSet>\r\n";

	ScriptFile->Serialize((void*)HeaderFooter, sizeof(ANSICHAR) * (ARRAY_COUNT(HeaderFooter) - 1));
}

void FShaderCompileXGEThreadRunnable::GatherResultsFromXGE()
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	IFileManager& FileManager = IFileManager::Get();

	// Reverse iterate so we can remove batches that have completed as we go.
	for (int32 Index = ShaderBatchesInFlight.Num() - 1; Index >= 0; Index--)
	{
		FShaderBatch* Batch = ShaderBatchesInFlight[Index];

		// Check to see if the shader compile worker has finished for this batch. This will be indicated by a zero length file placed in the working directory.
		// We also check the timestamp of the success file to determine if it came from the current build and is not simply a leftover from a previous build.
		if (PlatformFile.FileExists(*Batch->SuccessFileNameAndPath) && 
			PlatformFile.GetTimeStamp(*Batch->SuccessFileNameAndPath) >= ScriptFileCreationTime)
		{
			// Perform the same checks on the worker output file to verify it came from this build.
			if (PlatformFile.FileExists(*Batch->OutputFileNameAndPath) && 
				PlatformFile.GetTimeStamp(*Batch->OutputFileNameAndPath) >= ScriptFileCreationTime)
			{
				FArchive* OutputFilePtr = FileManager.CreateFileReader(*Batch->OutputFileNameAndPath, FILEREAD_Silent);
				if (OutputFilePtr)
				{
					FArchive& OutputFile = *OutputFilePtr;
					DoReadTaskResults(Batch->GetJobs(), OutputFile);

					// Close the output file.
					delete OutputFilePtr;

					// Cleanup the worker files
					Batch->CleanUpFiles(false);			// (false = don't keep the input file)
					PostCompletedJobsForBatch(Batch);
					ShaderBatchesInFlight.RemoveAt(Index);
					delete Batch;
				}
			}
		}
	}
}
		
int32 FShaderCompileXGEThreadRunnable::CompilingLoop()
{
	bool bWorkRemaining = false;

	// We can only run one XGE build at a time.
	// Check if a build is currently in progress.
	if (BuildProcessHandle.IsValid())
	{
		// Read back results from the current batches in progress.
		GatherResultsFromXGE();

		bool bDoExitCheck = false;
		if (FPlatformProcess::IsProcRunning(BuildProcessHandle))
		{
			if (ShaderBatchesInFlight.Num() == 0)
			{
				// We've processed all batches.
				// Wait for the XGE console process to exit
				FPlatformProcess::WaitForProc(BuildProcessHandle);
				bDoExitCheck = true;
			}
		}
		else
		{
			bDoExitCheck = true;
		}

		if (bDoExitCheck)
		{
			if (ShaderBatchesInFlight.Num() > 0)
			{
				// The build process has stopped.
				// Do one final pass over the output files to gather any remaining results.
				GatherResultsFromXGE();
			}

			// The build process is no longer running.
			// We need to check the return code for possible failure
			int32 ReturnCode = 0;
			FPlatformProcess::GetProcReturnCode(BuildProcessHandle, &ReturnCode);

			switch (ReturnCode)
			{
			case 0:
				// No error
				break;

			case 1:
				// One or more of the shader compile worker processes crashed.
				UE_LOG(LogShaderCompilers, Fatal, TEXT("An error occurred during an XGE shader compilation job. One or more of the shader compile worker processes exited unexpectedly (Code 1)."));
				break;

			case 2:
				// Fatal IncrediBuild error
				UE_LOG(LogShaderCompilers, Fatal, TEXT("An error occurred during an XGE shader compilation job. XGConsole.exe returned a fatal Incredibuild error (Code 2)."));
				break;

			case 3:
				// User canceled the build
				UE_LOG(LogShaderCompilers, Display, TEXT("The user terminated an XGE shader compilation job. Incomplete shader jobs will be redispatched in another XGE build."));
				break;

			default:
				UE_LOG(LogShaderCompilers, Fatal, TEXT("An unknown error occurred during an XGE shader compilation job (Code %d)."), ReturnCode);
				break;
			}

			// Reclaim jobs from the workers which did not succeed (if any).
			for (FShaderBatch* Batch : ShaderBatchesInFlight)
			{
				// Delete any output/success files, but keep the input file so we don't have to write it out again.
				Batch->CleanUpFiles(true);

				// We can't add any jobs to a shader batch which has already been written out to disk,
				// so put the batch back into the full batches list, even if the batch isn't full.
				ShaderBatchesFull.Add(Batch);

				// Reset the batch/directory indices and move the input file to the correct place.
				FString OldInputFilename = Batch->InputFileNameAndPath;
				Batch->SetIndices(XGEDirectoryIndex, BatchIndexToCreate++);
				MoveFileHelper(Batch->InputFileNameAndPath, OldInputFilename);
			}

			ShaderBatchesInFlight.Empty();
			BuildProcessHandle.Close();
		}

		bWorkRemaining |= ShaderBatchesInFlight.Num() > 0;
	}
	// No build process running. Check if we can kick one off now.
	else
	{
		// Determine if enough time has passed to allow a build to kick off.
		// Since shader jobs are added to the shader compile manager asynchronously by the engine, 
		// we want to give the engine enough time to queue up a large number of shaders.
		// Otherwise we will only be kicking off a small number of shader jobs at once.
		bool BuildDelayElapsed = (((FPlatformTime::Cycles() - LastAddTime) * FPlatformTime::GetSecondsPerCycle()) >= XGEConsoleVariables::JobTimeout);
		bool HasJobsToRun = (ShaderBatchesIncomplete.Num() > 0 || ShaderBatchesFull.Num() > 0);

		if (BuildDelayElapsed && HasJobsToRun && ShaderBatchesInFlight.Num() == 0)
		{
			// Move all the pending shader batches into the in-flight list.
			ShaderBatchesInFlight.Reserve(ShaderBatchesIncomplete.Num() + ShaderBatchesFull.Num());

			for (FShaderBatch* Batch : ShaderBatchesIncomplete)
			{
				// Check we've actually got jobs for this batch.
				check(Batch->NumJobs() > 0);

				// Make sure we've written out the worker files for any incomplete batches.
				Batch->WriteTransferFile();
				ShaderBatchesInFlight.Add(Batch);
			}

			for (FShaderBatch* Batch : ShaderBatchesFull)
			{
				// Check we've actually got jobs for this batch.
				check(Batch->NumJobs() > 0);

				ShaderBatchesInFlight.Add(Batch);
			}
			
			ShaderBatchesFull.Empty();
			ShaderBatchesIncomplete.Empty(XGEConsoleVariables::BatchGroupSize);
			
			FString ScriptFilename = XGEWorkingDirectory / FString::FromInt(XGEDirectoryIndex) / XGE_ScriptFileName;

			// Create the XGE script file.
			FArchive* ScriptFile = CreateFileHelper(ScriptFilename);
			WriteScriptFileHeader(ScriptFile, Manager->ShaderCompileWorkerName);

			// Write the XML task line for each shader batch
			for (FShaderBatch* Batch : ShaderBatchesInFlight)
			{
				FString WorkerAbsoluteDirectory = IFileManager::Get().ConvertToAbsolutePathForExternalAppForWrite(*Batch->WorkingDirectory);
				FPaths::NormalizeDirectoryName(WorkerAbsoluteDirectory);

				FString WorkerParameters = FString::Printf(TEXT("&quot;%s/&quot; %d %d &quot;%s&quot; &quot;%s&quot; -xge %s"),
					*WorkerAbsoluteDirectory,
					Manager->ProcessId,
					Batch->BatchIndex,
					*XGE_InputFileName,
					*XGE_OutputFileName,
					*FCommandLine::GetSubprocessCommandline());
				FString TaskXML = FString::Printf(TEXT("\t\t\t<Task Caption=\"Compiling %d Shaders (Batch %d)\" Params=\"%s\" />\r\n"), Batch->NumJobs(), Batch->BatchIndex, *WorkerParameters);
	
				ScriptFile->Serialize((void*)StringCast<ANSICHAR>(*TaskXML, TaskXML.Len()).Get(), sizeof(ANSICHAR) * TaskXML.Len());
			}

			// End the XML script file and close it.
			WriteScriptFileFooter(ScriptFile);
			delete ScriptFile;
			ScriptFile = nullptr;

			// Grab the timestamp from the script file.
			// We use this to ignore any left over files from previous builds by only accepting files created after the script file.
			ScriptFileCreationTime = IFileManager::Get().GetTimeStamp(*ScriptFilename);

			StartTime = FPlatformTime::Cycles();

			// Use stop on errors so we can respond to shader compile worker crashes immediately.
			// Regular shader compilation errors are not returned as worker errors.
			FString XGConsoleArgs = TEXT("/STOPONERRORS /BUILD \"") + ScriptFilename + TEXT("\"");

			// Kick off the XGE process...
			BuildProcessHandle = FPlatformProcess::CreateProc(*XGE_ConsolePath, *XGConsoleArgs, false, false, true, &BuildProcessID, 0, nullptr, nullptr);
			if(!BuildProcessHandle.IsValid())
			{
				UE_LOG(LogShaderCompilers, Fatal, TEXT("Failed to launch %s during shader compilation."), *XGE_ConsolePath);
			}

			// If the engine crashes, we don't get a chance to kill the build process.
			// Start up the build monitor process to monitor for engine crashes.
			uint32 BuildMonitorProcessID;
			FProcHandle BuildMonitorHandle = FPlatformProcess::CreateProc(*Manager->ShaderCompileWorkerName, *FString::Printf(TEXT("-xgemonitor %d %d"), Manager->ProcessId, BuildProcessID), true, false, false, &BuildMonitorProcessID, 0, nullptr, nullptr);
			BuildMonitorHandle.Close();

			// Reset batch counters and switch directories
			BatchIndexToFill = 0;
			BatchIndexToCreate = 0;
			XGEDirectoryIndex = 1 - XGEDirectoryIndex;

			bWorkRemaining = true;
		}
	}

	// Try to prepare more shader jobs (even if a build is in flight).
	TArray<FShaderCompileJob*> JobQueue;
	{
		// Enter the critical section so we can access the input and output queues
		FScopeLock Lock(&Manager->CompileQueueSection);

		// Grab as many jobs from the job queue as we can.
		int32 NumNewJobs = Manager->CompileQueue.Num();
		if (NumNewJobs > 0)
		{
			int32 DestJobIndex = JobQueue.AddUninitialized(NumNewJobs);
			for (int32 SrcJobIndex = 0; SrcJobIndex < NumNewJobs; SrcJobIndex++, DestJobIndex++)
			{
				JobQueue[DestJobIndex] = Manager->CompileQueue[SrcJobIndex];
			}
		
			Manager->CompileQueue.RemoveAt(0, NumNewJobs);
		}
	}

	if (JobQueue.Num() > 0)
	{
		// We have new jobs in the queue.
		// Group the jobs into batches and create the worker input files.
		for (int32 JobIndex = 0; JobIndex < JobQueue.Num(); JobIndex++)
		{
			if (BatchIndexToFill >= ShaderBatchesIncomplete.GetMaxIndex() || !ShaderBatchesIncomplete.IsAllocated(BatchIndexToFill))
			{
				// There are no more incomplete shader batches available.
				// Create another one...
				ShaderBatchesIncomplete.Insert(BatchIndexToFill, new FShaderBatch(
					XGEWorkingDirectory,
					XGE_InputFileName,
					XGE_SuccessFileName,
					XGE_OutputFileName,
					XGEDirectoryIndex,
					BatchIndexToCreate));

				BatchIndexToCreate++;
			}

			// Add a single job to this batch
			FShaderBatch* CurrentBatch = ShaderBatchesIncomplete[BatchIndexToFill];
			CurrentBatch->AddJob(JobQueue[JobIndex]);

			// If the batch is now full...
			if (CurrentBatch->NumJobs() == XGEConsoleVariables::BatchSize)
			{
				CurrentBatch->WriteTransferFile();

				// Move the batch to the full list.
				ShaderBatchesFull.Add(CurrentBatch);
				ShaderBatchesIncomplete.RemoveAt(BatchIndexToFill);
			}

			BatchIndexToFill++;
			BatchIndexToFill %= XGEConsoleVariables::BatchGroupSize;
		}

		// Keep track of the last time we added jobs.
		LastAddTime = FPlatformTime::Cycles();

		bWorkRemaining = true;
	}

	if (Manager->bAllowAsynchronousShaderCompiling)
	{
		// Yield for a short while to stop this thread continuously polling the disk.
		FPlatformProcess::Sleep(0.01f);
	}

	return bWorkRemaining ? 1 : 0;
}
