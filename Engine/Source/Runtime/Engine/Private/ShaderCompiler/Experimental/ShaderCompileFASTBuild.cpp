// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShaderCompiler.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/FileManager.h"
#include "Misc/ScopeLock.h"

#if PLATFORM_DESKTOP

namespace FASTBuildShaderCompilerVariables
{
	// Disabled until the ShaderAutogen dependency issue is fixed.
	int32 Enabled = 0;
	FAutoConsoleVariableRef CVarFASTBuildShaderCompile(
		TEXT("r.FASTBuildShaderCompile"),
		Enabled,
		TEXT("Enables or disables the use of FASTBuild to build shaders.\n")
		TEXT("0: Local builds only. \n")
		TEXT("1: Distribute builds using FASTBuild."),
		ECVF_Default);

	int32 SendPDB = 0;
	FAutoConsoleVariableRef CVarFASTBuildSendPDB(
		TEXT("r.FASTBuildShaderSendPDB"),
		SendPDB,
		TEXT("Enable when distributed shader compiler workers crash.\n")
		TEXT("0: Do not send along debug information in FASTBuild. \n")
		TEXT("1: Send along debug information in FASTBuild."),
		ECVF_Default);

	int32 MinBatchSize = 20;
	FAutoConsoleVariableRef CVarXGEShaderCompileXMLMinBatchSize(
        TEXT("r.FASTBuild.Shader.MinBatchSize"),
        MinBatchSize,
        TEXT("Minimum number of shaders to compile with FASTBuild.\n")
        TEXT("Smaller number of shaders will compile locally."),
        ECVF_Default);

	int32 BatchSize = 12;
	FAutoConsoleVariableRef CVarFASTBuildShaderCompileBatchSize(
		TEXT("r.FASTBuild.Shader.BatchSize"),
		BatchSize,
		TEXT("Specifies the number of shaders to batch together into a single FASTBUILD task.\n")
		TEXT("Default = 12\n"),
		ECVF_Default);

	float JobTimeout = 0.5f;
	FAutoConsoleVariableRef CVarFastBuildShaderCompileJobTimeout(
		TEXT("r.FASTBuild.Shader.JobTimeout"),
		JobTimeout,
		TEXT("The number of seconds to wait for additional shader jobs to be submitted before starting a build.\n")
		TEXT("Default = 0.5\n"),
		ECVF_Default);
}

#if PLATFORM_WINDOWS
static FString FASTBuild_ExecutablePath(TEXT("Extras\\ThirdPartyNotUE\\FASTBuild\\Win64\\FBuild.exe"));
static const FString FASTBuild_CachePath(TEXT("..\\Saved\\FASTBuildCache"));
static const FString FASTBuild_Toolchain[]
{
	TEXT("Engine\\Binaries\\Win64\\dxil.dll"),
	TEXT("Engine\\Binaries\\ThirdParty\\ShaderConductor\\Win64\\dxcompiler.dll"),
	TEXT("Engine\\Binaries\\ThirdParty\\ShaderConductor\\Win64\\ShaderConductor.dll"),
	TEXT("Engine\\Binaries\\ThirdParty\\Windows\\DirectX\\x64\\d3dcompiler_47.dll")
};
#elif PLATFORM_MAC
static FString FASTBuild_ExecutablePath(TEXT("Extras/ThirdPartyNotUE/FASTBuild/Mac/FBuild"));
static const FString FASTBuild_CachePath(TEXT("../Saved/FASTBuildCache"));
static const FString FASTBuild_Toolchain[]
{
//	TEXT("Engine/Binaries/Mac/libdxcompiler.dylib"),
	TEXT("Engine/Binaries/ThirdParty/ShaderConductor/Mac/libdxcompiler.dylib"),
	TEXT("Engine/Binaries/ThirdParty/ShaderConductor/Mac/libShaderConductor.dylib")
};
#elif PLATFORM_LINUX
static FString FASTBuild_ExecutablePath(TEXT("Extras/ThirdPartyNotUE/FASTBuild/Linux/fbuild"));
static const FString FASTBuild_CachePath(TEXT("../Saved/FASTBuildCache"));
static const FString FASTBuild_Toolchain[] = {};
#endif

static const FString FASTBuild_SuccessFileName(TEXT("Success"));
static const FString FASTBuild_ScriptFileName(TEXT("shaders.bff"));

#if PLATFORM_MAC
static FString GetMetalCompilerFolder()
{
	FString Result;
	if (FPlatformProcess::ExecProcess(TEXT("/usr/bin/xcrun"), TEXT("--sdk macosx metal -v"), nullptr, &Result, &Result))
	{
		const TCHAR InstalledDirText[] = TEXT("InstalledDir:");
		int32 InstalledDirOffset = Result.Find(InstalledDirText, ESearchCase::CaseSensitive);
		if (InstalledDirOffset != INDEX_NONE)
		{
			InstalledDirOffset += UE_ARRAY_COUNT(InstalledDirText);
			const int32 MacOSBinOffset = Result.Find(TEXT("/macos/bin\n"), ESearchCase::CaseSensitive, ESearchDir::FromStart, InstalledDirOffset);
			if (MacOSBinOffset != INDEX_NONE)
			{
				FString Substring = Result.Mid(InstalledDirOffset, MacOSBinOffset - InstalledDirOffset);
				return Result.Mid(InstalledDirOffset, MacOSBinOffset - InstalledDirOffset);
			}
		}
	}

	return FString();
}
#endif

bool FShaderCompileFASTBuildThreadRunnable::IsSupported()
{
	if (FASTBuildShaderCompilerVariables::Enabled == 1)
	{
		// Only try to use FASTBuild if either the brokerahe path or the coordinator env variable is set up
		FString CoordinatorAddress = FPlatformMisc::GetEnvironmentVariable(TEXT("FASTBUILD_COORDINATOR"));
		if (CoordinatorAddress.IsEmpty())
		{
			FString BrokeragePath = FPlatformMisc::GetEnvironmentVariable(TEXT("FASTBUILD_BROKERAGE_PATH"));
			if (BrokeragePath.IsEmpty())
			{
				FASTBuildShaderCompilerVariables::Enabled = 0;
				return false;
			}
		}

		// Check to see if the FASTBuild exe exists
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

		FASTBuild_ExecutablePath = FPaths::EngineDir() / FASTBuild_ExecutablePath;
		if (!PlatformFile.FileExists(*FASTBuild_ExecutablePath))
		{
			UE_LOG(LogShaderCompilers, Warning, TEXT("Cannot use FASTBuild Shader Compiler as FASTBuild is not found: %s"),
				*FPaths::ConvertRelativePathToFull(FASTBuild_ExecutablePath));
			FASTBuildShaderCompilerVariables::Enabled = 0;
			return false;
		}

#if PLATFORM_MAC
		static bool bCopyMetalCompilerToIntermediateDir = true;
		if (bCopyMetalCompilerToIntermediateDir)
		{
			SCOPED_AUTORELEASE_POOL;

			// Make a copy of all the Metal shader compiler files in the intermediate folder, so that they are in the same directory tree as SharedCompileWorker.
			// This is required for FASTBuild to preserve the directory structure when it copies these files to the worker
			const FString SrcDir = GetMetalCompilerFolder();
			if (SrcDir.Len() == 0)
			{
				UE_LOG(LogShaderCompilers, Warning, TEXT("Cannot use FASTBuild Shader Compiler as Metal shader compiler could not be found"));
				FASTBuildShaderCompilerVariables::Enabled = 0;
			}
			else
			{
				const FString IntermediateShadersDir = FPaths::EngineIntermediateDir() / TEXT("Shaders");
				const FString DestDir = IntermediateShadersDir / TEXT("metal");
				if (PlatformFile.DirectoryExists(*DestDir))
				{
					PlatformFile.DeleteDirectoryRecursively(*DestDir);
				}

				if (!PlatformFile.DirectoryExists(*IntermediateShadersDir))
				{
					PlatformFile.CreateDirectoryTree(*IntermediateShadersDir);
				}

				NSFileManager* FileManager = [NSFileManager defaultManager]; // Use NSFileManager as PlatformFile's CopyDirectoryTree does not preserve file modification times
				const bool bCopied = [FileManager copyItemAtPath:SrcDir.GetNSString() toPath:DestDir.GetNSString() error:nil];
				if (!bCopied)
				{
					UE_LOG(LogShaderCompilers, Warning, TEXT("Cannot use FASTBuild Shader Compiler as Metal shader compiler could not be copied to the intermediate folder: %s -> %s"),
						*SrcDir, *DestDir);
					FASTBuildShaderCompilerVariables::Enabled = 0;
				}
			}

			bCopyMetalCompilerToIntermediateDir = false;
		}
#endif
	}

	return FASTBuildShaderCompilerVariables::Enabled == 1;
}

/** Initialization constructor. */
FShaderCompileFASTBuildThreadRunnable::FShaderCompileFASTBuildThreadRunnable(FShaderCompilingManager* InManager)
	: FShaderCompileThreadRunnableBase(InManager)
	, BuildProcessID(INDEX_NONE)
	, ShaderBatchesInFlightCompleted(0)
	, FASTBuildWorkingDirectory(InManager->AbsoluteShaderBaseWorkingDirectory / TEXT("FASTBuild"))
	, FASTBuildDirectoryIndex(0)
	, LastAddTime(0)
	, StartTime(0)
	, BatchIndexToCreate(0)
	, BatchIndexToFill(0)
{
}

FShaderCompileFASTBuildThreadRunnable::~FShaderCompileFASTBuildThreadRunnable()
{
	if (BuildProcessHandle.IsValid())
	{
		// We still have a build in progress, so we need to terminate it.
		FPlatformProcess::TerminateProc(BuildProcessHandle);
		FPlatformProcess::CloseProc(BuildProcessHandle);

		FPlatformProcess::ClosePipe(PipeRead, PipeWrite);
	}

	// Clean up any intermediate files/directories we've got left over.
	IFileManager::Get().DeleteDirectory(*FASTBuildWorkingDirectory, false, true);

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

void FShaderCompileFASTBuildThreadRunnable::PostCompletedJobsForBatch(FShaderBatch* Batch)
{
	// Enter the critical section so we can access the input and output queues
	FScopeLock Lock(&Manager->CompileQueueSection);
	for (const auto& Job : Batch->GetJobs())
	{
		Manager->ProcessFinishedJob(Job);
	}
}

void FShaderCompileFASTBuildThreadRunnable::FShaderBatch::AddJob(FShaderCommonCompileJobPtr Job)
{
	// We can only add jobs to a batch which hasn't been written out yet.
	if (bTransferFileWritten)
	{
		UE_LOG(LogShaderCompilers, Fatal, TEXT("Attempt to add shader compile jobs to a FASTBuild shader batch which has already been written to disk."));
	}
	else
	{
		Jobs.Add(Job);
	}
}

void FShaderCompileFASTBuildThreadRunnable::FShaderBatch::WriteTransferFile()
{
	// Write out the file that the worker app is waiting for, which has all the information needed to compile the shader.
	FArchive* TransferFile = FShaderCompileUtilities::CreateFileHelper(InputFileNameAndPath);
	FShaderCompileUtilities::DoWriteTasks(Jobs, *TransferFile, true);
	delete TransferFile;

	bTransferFileWritten = true;
}

void FShaderCompileFASTBuildThreadRunnable::FShaderBatch::SetIndices(int32 InDirectoryIndex, int32 InBatchIndex)
{
	DirectoryIndex = InDirectoryIndex;
	BatchIndex = InBatchIndex;

	WorkingDirectory = FString::Printf(TEXT("%s/%d/%d"), *DirectoryBase, DirectoryIndex, BatchIndex);

	InputFileNameAndPath = WorkingDirectory / InputFileName;
	OutputFileNameAndPath = WorkingDirectory / OutputFileName;
	SuccessFileNameAndPath = WorkingDirectory / SuccessFileName;
}

void FShaderCompileFASTBuildThreadRunnable::FShaderBatch::CleanUpFiles(bool bKeepInputFile)
{
	if (!bKeepInputFile)
	{
		FShaderCompileUtilities::DeleteFileHelper(InputFileNameAndPath);
	}

	FShaderCompileUtilities::DeleteFileHelper(OutputFileNameAndPath);
	FShaderCompileUtilities::DeleteFileHelper(SuccessFileNameAndPath);
}

static void FASTBuildWriteScriptFileHeader(FArchive& ScriptFile, const FString& WorkerName)
{
	static const TCHAR HeaderTemplate[] =
		TEXT("Settings\r\n")
		TEXT("{\r\n")
		TEXT("\t.CachePath = '%s'\r\n")
		TEXT("}\r\n")
		TEXT("\r\n")
		TEXT("Compiler('ShaderCompiler')\r\n")
		TEXT("{\r\n")
		TEXT("\t.CompilerFamily = 'custom'\r\n")
		TEXT("\t.Executable = '%s'\r\n")
		TEXT("\t.ExecutableRootPath = '%s'\r\n")
		TEXT("\t.SimpleDistributionMode = true\r\n")
		TEXT("\t.ExtraFiles = \r\n")
		TEXT("\t{\r\n");

	const FString HeaderString = FString::Printf(HeaderTemplate, *FASTBuild_CachePath, *WorkerName,
			*IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*FPaths::RootDir()));
	ScriptFile.Serialize((void*)StringCast<ANSICHAR>(*HeaderString, HeaderString.Len()).Get(),
			sizeof(ANSICHAR) * HeaderString.Len());

	for (const FString& ExtraFilePartialPath : FASTBuild_Toolchain)
	{
		const FString ExtraFile = TEXT("\t\t'") + IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*(FPaths::RootDir() / ExtraFilePartialPath)) + TEXT("',\r\n");
		ScriptFile.Serialize((void*)StringCast<ANSICHAR>(*ExtraFile, ExtraFile.Len()).Get(), sizeof(ANSICHAR) * ExtraFile.Len());
	}

	class FDependencyEnumerator : public IPlatformFile::FDirectoryVisitor
	{
	public:
		FDependencyEnumerator(FArchive& InScriptFile, const TCHAR* InPrefix, const TCHAR* InExtension)
			: ScriptFile(InScriptFile)
			, Prefix(InPrefix)
			, Extension(InExtension)
		{
		}

		virtual bool Visit(const TCHAR* FilenameChar, bool bIsDirectory) override
		{
			if (!bIsDirectory)
			{
				const FString Filename = FString(FilenameChar);

				if ((!Prefix || Filename.Contains(Prefix)) && (!Extension || Filename.EndsWith(Extension)))
				{
					const FString ExtraFile = TEXT("\t\t'") + IFileManager::Get().ConvertToAbsolutePathForExternalAppForWrite(*Filename) + TEXT("',\r\n");
					ScriptFile.Serialize((void*)StringCast<ANSICHAR>(*ExtraFile, ExtraFile.Len()).Get(), sizeof(ANSICHAR) * ExtraFile.Len());
				}
			}

			return true;
		}

		FArchive& ScriptFile;
		const TCHAR* Prefix;
		const TCHAR* Extension;
	};

	FDependencyEnumerator DllDeps = FDependencyEnumerator(ScriptFile, TEXT("ShaderCompileWorker-"), PLATFORM_WINDOWS == 1 ? TEXT(".dll") : (PLATFORM_MAC == 1 ? TEXT(".dylib") : TEXT(".so")));
	IFileManager::Get().IterateDirectoryRecursively(*FPlatformProcess::GetModulesDirectory(), DllDeps);
	FDependencyEnumerator ModulesDeps = FDependencyEnumerator(ScriptFile, TEXT("ShaderCompileWorker"), TEXT(".modules"));
	IFileManager::Get().IterateDirectoryRecursively(*FPlatformProcess::GetModulesDirectory(), ModulesDeps);
#if PLATFORM_WINDOWS
	if (FASTBuildShaderCompilerVariables::SendPDB)
	{
		FDependencyEnumerator PdbDeps = FDependencyEnumerator(ScriptFile, TEXT("ShaderCompileWorker"), TEXT(".pdb"));
		IFileManager::Get().IterateDirectoryRecursively(*FPlatformProcess::GetModulesDirectory(), PdbDeps);
	}
#endif

	FDependencyEnumerator IniDeps = FDependencyEnumerator(ScriptFile, nullptr, TEXT(".ini"));
	TArray<FString> EngineConfigDirs = FPaths::GetExtensionDirs(FPaths::EngineDir(), TEXT("Config"));
	for (const FString& ConfigDir : EngineConfigDirs)
	{
		IFileManager::Get().IterateDirectoryRecursively(*ConfigDir, IniDeps);
	}

	FDependencyEnumerator ShaderUsfDeps = FDependencyEnumerator(ScriptFile, nullptr, TEXT(".usf"));
	FDependencyEnumerator ShaderUshDeps = FDependencyEnumerator(ScriptFile, nullptr, TEXT(".ush"));
	FDependencyEnumerator ShaderHeaderDeps = FDependencyEnumerator(ScriptFile, nullptr, TEXT(".h"));
	const TMap<FString, FString> ShaderSourceDirectoryMappings = AllShaderSourceDirectoryMappings();
	for (auto& ShaderDirectoryMapping : ShaderSourceDirectoryMappings)
	{
		IFileManager::Get().IterateDirectoryRecursively(*ShaderDirectoryMapping.Value, ShaderUsfDeps);
		IFileManager::Get().IterateDirectoryRecursively(*ShaderDirectoryMapping.Value, ShaderUshDeps);
		IFileManager::Get().IterateDirectoryRecursively(*ShaderDirectoryMapping.Value, ShaderHeaderDeps);
	}

#if PLATFORM_MAC
	const FString MetalIntermediateDir = FPaths::EngineIntermediateDir() + TEXT("/Shaders/metal");
	FDependencyEnumerator MetalCompilerDeps = FDependencyEnumerator(ScriptFile, nullptr, nullptr);
	IFileManager::Get().IterateDirectoryRecursively(*MetalIntermediateDir, MetalCompilerDeps);
#endif

	const FString ExtraFilesFooter =
		TEXT("\t}\r\n")
		TEXT("}\r\n");
	ScriptFile.Serialize((void*)StringCast<ANSICHAR>(*ExtraFilesFooter, ExtraFilesFooter.Len()).Get(), sizeof(ANSICHAR) * ExtraFilesFooter.Len());
}

void FShaderCompileFASTBuildThreadRunnable::GatherResultsFromFASTBuild()
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	IFileManager& FileManager = IFileManager::Get();

	// Reverse iterate so we can remove batches that have completed as we go.
	for (int32 Index = ShaderBatchesInFlight.Num() - 1; Index >= 0; Index--)
	{
		FShaderBatch* Batch = ShaderBatchesInFlight[Index];

		// If this batch is completed already, skip checks.
		if (Batch->bSuccessfullyCompleted)
		{
			continue;
		}

		constexpr uint64 VersionAndFileSizeSize = sizeof(uint32) + sizeof(uint64);
		if (PlatformFile.FileExists(*Batch->OutputFileNameAndPath) &&
		    PlatformFile.GetTimeStamp(*Batch->OutputFileNameAndPath) >= ScriptFileCreationTime &&
		    FileManager.FileSize(*Batch->OutputFileNameAndPath) > VersionAndFileSizeSize)
		{
			TUniquePtr<FArchive> OutputFilePtr(FileManager.CreateFileReader(*Batch->OutputFileNameAndPath, FILEREAD_Silent));
			if (OutputFilePtr)
			{
				FArchive& OutputFile = *OutputFilePtr;
				int32 OutputVersion;
				OutputFile << OutputVersion; // NOTE (SB): Do not care right now about the version.
				int64 FileSize = 0;
				OutputFile << FileSize;

				// NOTE (SB): Check if we received the full file yet.
				if (OutputFile.TotalSize() >= FileSize)
				{
					OutputFile.Seek(0);
					FShaderCompileUtilities::DoReadTaskResults(Batch->GetJobs(), OutputFile);

					// Cleanup the worker files
					// Do NOT clean up files until the whole batch is done, so we can clean them all up once the fastbuild process exits. Otherwise there is a race condition between FastBuild checking the output files, and us deleting them here.
					//Batch->CleanUpFiles(false);			// (false = don't keep the input file)
					Batch->bSuccessfullyCompleted = true;
					PostCompletedJobsForBatch(Batch);
					//ShaderBatchesInFlight.RemoveAt(Index);
					ShaderBatchesInFlightCompleted++;
					//delete Batch;
				}
			}
		}
	}
}

enum EFASTBuild_ReturnCodes
{
	FBUILD_OK = 0,
	FBUILD_BUILD_FAILED = -1,
	FBUILD_ERROR_LOADING_BFF = -2,
	FBUILD_BAD_ARGS = -3,
	FBUILD_ALREADY_RUNNING = -4,
	FBUILD_FAILED_TO_SPAWN_WRAPPER = -5,
	FBUILD_FAILED_TO_SPAWN_WRAPPER_FINAL = -6,
	FBUILD_WRAPPER_CRASHED = -7,
};

int32 FShaderCompileFASTBuildThreadRunnable::CompilingLoop()
{
	bool bWorkRemaining = false;

	// We can only run one XGE build at a time.
	// Check if a build is currently in progress.
	if (BuildProcessHandle.IsValid())
	{
		// Read back results from the current batches in progress.
		GatherResultsFromFASTBuild();

		bool bDoExitCheck = false;
		if (FPlatformProcess::IsProcRunning(BuildProcessHandle))
		{
			const FString STDOutput = FPlatformProcess::ReadPipe(PipeRead);
			if (STDOutput.Len() > 0)
			{
				TArray<FString> Lines;
				STDOutput.ParseIntoArrayLines(Lines);
				for (const FString& Line : Lines)
				{
					UE_LOG(LogShaderCompilers, Display, TEXT("%s"), *Line);
				}
			}

			if (ShaderBatchesInFlight.Num() == ShaderBatchesInFlightCompleted)
			{
				// We've processed all batches.
				// Wait for the FASTBuild console process to exit
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
			if (ShaderBatchesInFlight.Num() > ShaderBatchesInFlightCompleted)
			{
				// The build process has stopped.
				// Do one final pass over the output files to gather any remaining results.
				GatherResultsFromFASTBuild();
			}

			// The build process is no longer running.
			// We need to check the return code for possible failure
			int32 ReturnCode = 0;
			FPlatformProcess::GetProcReturnCode(BuildProcessHandle, &ReturnCode);

			switch (ReturnCode)
			{
			case FBUILD_OK:
				// No error
				break;
			case FBUILD_BUILD_FAILED:
			case FBUILD_ERROR_LOADING_BFF:
			case FBUILD_BAD_ARGS:
			case FBUILD_FAILED_TO_SPAWN_WRAPPER:
			case FBUILD_FAILED_TO_SPAWN_WRAPPER_FINAL:
			case FBUILD_WRAPPER_CRASHED:
				// One or more of the shader compile worker processes crashed.
				UE_LOG(LogShaderCompilers, Fatal, TEXT("An error occurred during an FASTBuild shader compilation job. One or more of the shader compile worker processes exited unexpectedly (Code %d)."), ReturnCode);
				break;
			default:
				UE_LOG(LogShaderCompilers, Display, TEXT("An unknown error occurred during an FASTBuild shader compilation job (Code %d). Incomplete shader jobs will be redispatched in another FASTBuild build."), ReturnCode);
				break;
			case FBUILD_ALREADY_RUNNING:
				UE_LOG(LogShaderCompilers, Display, TEXT("FASTBuild is already running. Incomplete shader jobs will be redispatched in another FASTBuild build."));
				break;
			}

			// Reclaim jobs from the workers which did not succeed (if any).
			for (int i = 0; i < ShaderBatchesInFlight.Num(); ++i)
			{
				FShaderBatch* Batch = ShaderBatchesInFlight[i];

				if (Batch->bSuccessfullyCompleted)
				{
					// If we completed successfully, clean up.
					//PostCompletedJobsForBatch(Batch);
					Batch->CleanUpFiles(false);

					// This will be a dangling pointer until we clear the array at the end of this for loop
					delete Batch;
				}
				else
				{

					// Delete any output/success files, but keep the input file so we don't have to write it out again.
					Batch->CleanUpFiles(true);

					// We can't add any jobs to a shader batch which has already been written out to disk,
					// so put the batch back into the full batches list, even if the batch isn't full.
					ShaderBatchesFull.Add(Batch);

					// Reset the batch/directory indices and move the input file to the correct place.
					FString OldInputFilename = Batch->InputFileNameAndPath;
					Batch->SetIndices(FASTBuildDirectoryIndex, BatchIndexToCreate++);
					FShaderCompileUtilities::MoveFileHelper(Batch->InputFileNameAndPath, OldInputFilename);
				}
			}
			ShaderBatchesInFlightCompleted = 0;
			ShaderBatchesInFlight.Empty();
			FPlatformProcess::CloseProc(BuildProcessHandle);
			FPlatformProcess::ClosePipe(PipeRead, PipeWrite);
			PipeRead = nullptr;
			PipeWrite = nullptr;
		}

		bWorkRemaining |= ShaderBatchesInFlight.Num() > ShaderBatchesInFlightCompleted;
	}
	// No build process running. Check if we can kick one off now.
	else
	{
		// Determine if enough time has passed to allow a build to kick off.
		// Since shader jobs are added to the shader compile manager asynchronously by the engine, 
		// we want to give the engine enough time to queue up a large number of shaders.
		// Otherwise we will only be kicking off a small number of shader jobs at once.
		const bool BuildDelayElapsed = (((FPlatformTime::Cycles() - LastAddTime) * FPlatformTime::GetSecondsPerCycle()) >= FASTBuildShaderCompilerVariables::JobTimeout);
		const bool HasJobsToRun = (ShaderBatchesIncomplete.Num() > 0 || ShaderBatchesFull.Num() > 0);

		if (BuildDelayElapsed && HasJobsToRun && ShaderBatchesInFlight.Num() == ShaderBatchesInFlightCompleted)
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
			ShaderBatchesIncomplete.Empty();

			const FString ScriptFilename = FASTBuildWorkingDirectory / FString::FromInt(FASTBuildDirectoryIndex) / FASTBuild_ScriptFileName;

			// Create the FASTBuild script file.
			TUniquePtr<FArchive> ScriptFile(FShaderCompileUtilities::CreateFileHelper(ScriptFilename));
			check(ScriptFile.IsValid());
			FASTBuildWriteScriptFileHeader(*ScriptFile, Manager->ShaderCompileWorkerName);

			FString AdditionalCompilerOptions;
#if PLATFORM_MAC
			AdditionalCompilerOptions = FString::Printf(TEXT(" -MetalToolchainOverride=../../Intermediate/Shaders/metal"));
#endif

			// Write the task line for each shader batch
			for (const FShaderBatch* Batch : ShaderBatchesInFlight)
			{
				FString WorkerAbsoluteDirectory = IFileManager::Get().ConvertToAbsolutePathForExternalAppForWrite(*Batch->WorkingDirectory);
				FPaths::NormalizeDirectoryName(WorkerAbsoluteDirectory);

				const FString ExecFunction = FString::Printf(
					TEXT("ObjectList('ShaderBatch-%d')\r\n")
					TEXT("{\r\n")
					TEXT("\t.Compiler = 'ShaderCompiler'\r\n")
					TEXT("\t.CompilerOptions = '\"\" %d %d \"%%1\" \"%%2\"%s'\r\n")
					TEXT("\t.CompilerOutputExtension = '.out'\r\n")
					TEXT("\t.CompilerInputFiles = { '%s' }\r\n")
					TEXT("\t.CompilerOutputPath = '%s'\r\n")
					TEXT("}\r\n\r\n"),
					Batch->BatchIndex,
					Manager->ProcessId,
					Batch->BatchIndex,
					*AdditionalCompilerOptions,
					*Batch->InputFileNameAndPath,
					*WorkerAbsoluteDirectory);

				ScriptFile->Serialize((void*)StringCast<ANSICHAR>(*ExecFunction, ExecFunction.Len()).Get(), sizeof(ANSICHAR) * ExecFunction.Len());
			}

			const FString AliasBuildTargetOpen = FString(
				TEXT("Alias('all')\r\n")
				TEXT("{\r\n")
				TEXT("\t.Targets = { \r\n")
			);
			ScriptFile->Serialize((void*)StringCast<ANSICHAR>(*AliasBuildTargetOpen, AliasBuildTargetOpen.Len()).Get(), sizeof(ANSICHAR) * AliasBuildTargetOpen.Len());

			// Write write the "All" target
			for (int32 Idx = ShaderBatchesInFlight.Num() - 1; Idx >=0; --Idx)
			{
				const FShaderBatch* Batch = ShaderBatchesInFlight[Idx];
				const FString TargetExport = FString::Printf(TEXT("'ShaderBatch-%d', "), Batch->BatchIndex);

				ScriptFile->Serialize((void*)StringCast<ANSICHAR>(*TargetExport, TargetExport.Len()).Get(), sizeof(ANSICHAR) * TargetExport.Len());
			}

			FString AliasBuildTargetClose = FString(TEXT(" }\r\n}\r\n"));
			ScriptFile->Serialize((void*)StringCast<ANSICHAR>(*AliasBuildTargetClose, AliasBuildTargetClose.Len()).Get(), sizeof(ANSICHAR) * AliasBuildTargetClose.Len());

			ScriptFile = nullptr;

			// Grab the timestamp from the script file.
			// We use this to ignore any left over files from previous builds by only accepting files created after the script file.
			ScriptFileCreationTime = IFileManager::Get().GetTimeStamp(*ScriptFilename);

			StartTime = FPlatformTime::Cycles();

			const FString FASTBuildConsoleArgs = TEXT("-config \"") + ScriptFilename + TEXT("\" -dist -clean -monitor");

			// Kick off the FASTBuild process...
			verify(FPlatformProcess::CreatePipe(PipeRead, PipeWrite));
			BuildProcessHandle = FPlatformProcess::CreateProc(*FASTBuild_ExecutablePath, *FASTBuildConsoleArgs, false, false, true, &BuildProcessID, 0, nullptr, PipeWrite);
			if (!BuildProcessHandle.IsValid())
			{
				UE_LOG(LogShaderCompilers, Fatal, TEXT("Failed to launch %s during shader compilation."), *FASTBuild_ExecutablePath);
			}

			// If the engine crashes, we don't get a chance to kill the build process.
			// Start up the build monitor process to monitor for engine crashes.
			uint32 BuildMonitorProcessID;
			FProcHandle BuildMonitorHandle = FPlatformProcess::CreateProc(*Manager->ShaderCompileWorkerName, *FString::Printf(TEXT("-xgemonitor %d %d"), Manager->ProcessId, BuildProcessID), true, false, false, &BuildMonitorProcessID, 0, nullptr, nullptr);
			FPlatformProcess::CloseProc(BuildMonitorHandle);

			// Reset batch counters and switch directories
			BatchIndexToFill = 0;
			BatchIndexToCreate = 0;
			FASTBuildDirectoryIndex = 1 - FASTBuildDirectoryIndex;

			bWorkRemaining = true;
		}
	}

	// Try to prepare more shader jobs (even if a build is in flight).
	TArray<FShaderCommonCompileJobPtr> JobQueue;
	{
		// Grab as many jobs from the job queue as we can.
		for (int32 PriorityIndex = MaxPriorityIndex; PriorityIndex >= MinPriorityIndex; --PriorityIndex)
		{
			const EShaderCompileJobPriority Priority = (EShaderCompileJobPriority)PriorityIndex;
			const int32 MinBatchSize = (Priority == EShaderCompileJobPriority::Low) ? 1 : FASTBuildShaderCompilerVariables::MinBatchSize;
			const int32 NumJobs = Manager->AllJobs.GetPendingJobs(EShaderCompilerWorkerType::XGE, Priority, MinBatchSize, INT32_MAX, JobQueue);
			if (NumJobs > 0)
			{
				UE_LOG(LogShaderCompilers, Display, TEXT("Started %d 'FASTBuild' shader compile jobs with '%s' priority"),
					NumJobs,
					ShaderCompileJobPriorityToString((EShaderCompileJobPriority)PriorityIndex));
			}
			if (JobQueue.Num() >= FASTBuildShaderCompilerVariables::MinBatchSize)
			{
				// Kick a batch with just the higher priority jobs, if it's large enough
				break;
			}
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
				const uint32 ProcessId = FPlatformProcess::GetCurrentProcessId();
				const FString FASTBuild_OutputFileName = FString::Printf(TEXT("Shader-Batch-%d-%d.out"), ProcessId, BatchIndexToCreate);
				const FString FASTBuild_InputFileName = FString::Printf(TEXT("Shader-Batch-%d-%d.in"), ProcessId, BatchIndexToCreate);
				ShaderBatchesIncomplete.Insert(BatchIndexToFill, new FShaderBatch(
					FASTBuildWorkingDirectory,
					FASTBuild_InputFileName,
					FASTBuild_SuccessFileName,
					FASTBuild_OutputFileName,
					FASTBuildDirectoryIndex,
					BatchIndexToCreate));

				BatchIndexToCreate++;
			}

			// Add a single job to this batch
			FShaderBatch* CurrentBatch = ShaderBatchesIncomplete[BatchIndexToFill];
			CurrentBatch->AddJob(JobQueue[JobIndex]);

			// If the batch is now full...
			if (CurrentBatch->NumJobs() == FASTBuildShaderCompilerVariables::BatchSize)
			{
				CurrentBatch->WriteTransferFile();

				// Move the batch to the full list.
				ShaderBatchesFull.Add(CurrentBatch);
				ShaderBatchesIncomplete.RemoveAt(BatchIndexToFill);

				BatchIndexToFill++;
			}
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

#endif // PLATFORM_DESKTOP
