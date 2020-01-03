// Copyright Epic Games, Inc. All Rights Reserved.

#include "BuildPatchFileConstructor.h"
#include "IBuildManifestSet.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/FileManager.h"
#include "HAL/RunnableThread.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Misc/OutputDeviceRedirector.h"
#include "BuildPatchServicesPrivate.h"
#include "Interfaces/IBuildInstaller.h"
#include "Data/ChunkData.h"
#include "Common/StatsCollector.h"
#include "Common/SpeedRecorder.h"
#include "Common/FileSystem.h"
#include "Installer/ChunkSource.h"
#include "Installer/ChunkReferenceTracker.h"
#include "Installer/InstallerError.h"
#include "Installer/InstallerAnalytics.h"
#include "BuildPatchUtil.h"

using namespace BuildPatchServices;

// This define the number of bytes on a half-finished file that we ignore from the end
// incase of previous partial write.
#define NUM_BYTES_RESUME_IGNORE     1024

// Helper functions wrapping common code.
namespace FileConstructorHelpers
{
	void WaitWhilePaused(FThreadSafeBool& bIsPaused, FThreadSafeBool& bShouldAbort)
	{
		// Wait while paused
		while (bIsPaused && !bShouldAbort)
		{
			FPlatformProcess::Sleep(0.5f);
		}
	}

	bool CheckAndReportRemainingDiskSpaceError(IInstallerError* InstallerError, const FString& InstallDirectory, uint64 RemainingBytesRequired, const TCHAR* SpaceErrorCode, uint64* OutAvailableDiskSpace = nullptr)
	{
		bool bContinueConstruction = true;
		uint64 TotalSize = 0;
		uint64 AvailableSpace = 0;
		if (FPlatformMisc::GetDiskTotalAndFreeSpace(InstallDirectory, TotalSize, AvailableSpace))
		{
			if (AvailableSpace < RemainingBytesRequired)
			{
				UE_LOG(LogBuildPatchServices, Error, TEXT("Out of HDD space. Needs %llu bytes, Free %llu bytes"), RemainingBytesRequired, AvailableSpace);
				InstallerError->SetError(
					EBuildPatchInstallError::OutOfDiskSpace,
					SpaceErrorCode,
					0,
					BuildPatchServices::GetDiskSpaceMessage(InstallDirectory, RemainingBytesRequired, AvailableSpace));
				bContinueConstruction = false;
			}
		}
		if (OutAvailableDiskSpace != nullptr)
		{
			*OutAvailableDiskSpace = AvailableSpace;
		}
		return bContinueConstruction;
	}

	uint64 CalculateRequiredDiskSpace(const FBuildPatchAppManifestPtr& CurrentManifest, const FBuildPatchAppManifestRef& BuildManifest, const EInstallMode& InstallMode, const TSet<FString>& InInstallTags)
	{
		// Make tags expected
		TSet<FString> InstallTags = InInstallTags;
		if (InstallTags.Num() == 0)
		{
			BuildManifest->GetFileTagList(InstallTags);
		}
		InstallTags.Add(TEXT(""));
		// Calculate the files that need constructing.
		FString DummyString;
		TSet<FString> FilesToConstruct;
		BuildManifest->GetOutdatedFiles(CurrentManifest, DummyString, FilesToConstruct);
		TSet<FString> TaggedFiles;
		BuildManifest->GetTaggedFileList(InstallTags, TaggedFiles);
		FilesToConstruct = FilesToConstruct.Intersect(TaggedFiles);
		// Count disk space needed by each operation.
		int64 DiskSpaceDeltaPeak = 0;
		const bool bCurrentManifestIsValid = CurrentManifest.IsValid();
		if (InstallMode == EInstallMode::DestructiveInstall && bCurrentManifestIsValid)
		{
			// The simplest method will be to run through each high level file operation, tracking peak disk usage delta.
			int64 DiskSpaceDelta = 0;

			// Loop through all files to be made next, in order.
			FilesToConstruct.Sort(TLess<FString>());
			for (const FString& FileToConstruct : FilesToConstruct)
			{
				// First we would need to make the new file.
				DiskSpaceDelta += BuildManifest->GetFileSize(FileToConstruct);
				if (DiskSpaceDeltaPeak < DiskSpaceDelta)
				{
					DiskSpaceDeltaPeak = DiskSpaceDelta;
				}
				// Then we can remove the current existing file.
				DiskSpaceDelta -= CurrentManifest->GetFileSize(FileToConstruct);
			}
		}
		else
		{
			// When not destructive, or no CurrentManifest, we always stage all new and changed files.
			DiskSpaceDeltaPeak = BuildManifest->GetFileSize(FilesToConstruct);
		}
		return FMath::Max<int64>(DiskSpaceDeltaPeak, 0);
	}
}

/**
 * This struct handles loading and saving of simple resume information, that will allow us to decide which
 * files should be resumed from. It will also check that we are creating the same version and app as we expect to be.
 */
struct FResumeData
{
public:
	// File system dependency
	const IFileSystem* const FileSystem;

	// The manifests for the app we are installing
	const IBuildManifestSet* const ManifestSet;

	// Save the staging directory
	const FString StagingDir;

	// The filename to the resume data information
	const FString ResumeDataFile;

	// The resume ids that we loaded from disk
	TSet<FString> LoadedResumeIds;

	// The set of files that were started
	TSet<FString> FilesStarted;

	// The set of files that were completed, determined by expected file size
	TSet<FString> FilesCompleted;

	// The set of files that exist but are not able to assume resumable
	TSet<FString> FilesIncompatible;

	// Whether we have any resume data for this install
	bool bHasResumeData;

public:

	FResumeData(IFileSystem* InFileSystem, IBuildManifestSet* InManifestSet, const FString& InStagingDir)
		: FileSystem(InFileSystem)
		, ManifestSet(InManifestSet)
		, StagingDir(InStagingDir)
		, ResumeDataFile(InStagingDir / TEXT("$resumeData"))
		, bHasResumeData(false)
	{
		// Load data from previous resume file
		bHasResumeData = FileSystem->FileExists(*ResumeDataFile);
		GLog->Logf(TEXT("BuildPatchResumeData file found: %s"), bHasResumeData ? TEXT("true") : TEXT("false"));
		if (bHasResumeData)
		{
			// Grab existing resume metadata.
			const bool bCullEmptyLines = true;
			FString PrevResumeData;
			TArray<FString> PrevResumeDataLines;
			FileSystem->LoadFileToString(*ResumeDataFile, PrevResumeData);
			PrevResumeData.ParseIntoArrayLines(PrevResumeDataLines, bCullEmptyLines);
			// Grab current resume ids
			const bool bCheckLegacyIds = true;
			TSet<FString> NewResumeIds;
			ManifestSet->GetInstallResumeIds(NewResumeIds, bCheckLegacyIds);
			LoadedResumeIds.Reserve(PrevResumeDataLines.Num());
			// Check if any builds we are installing are a resume from previous run.
			for (FString& PrevResumeDataLine : PrevResumeDataLines)
			{
				PrevResumeDataLine.TrimStartAndEndInline();
				LoadedResumeIds.Add(PrevResumeDataLine);
				if (NewResumeIds.Contains(PrevResumeDataLine))
				{
					bHasResumeData = true;
					GLog->Logf(TEXT("BuildPatchResumeData version matched %s"), *PrevResumeDataLine);
				}
			}
		}
	}

	/**
	 * Saves out the resume data
	 */
	void SaveOut(const TSet<FString>& ResumeIds)
	{
		// Save out the patch versions
		FileSystem->SaveStringToFile(*ResumeDataFile, FString::Join(ResumeIds, TEXT("\n")));
	}

	/**
	 * Checks whether the file was completed during last install attempt and adds it to FilesCompleted if so
	 * @param Filename    The filename to check
	 */
	void CheckFile(const FString& Filename)
	{
		// If we had resume data, check if this file might have been resumable
		if (bHasResumeData)
		{
			int64 DiskFileSize;
			const FString FullFilename = StagingDir / Filename;
			const bool bFileExists = FileSystem->GetFileSize(*FullFilename, DiskFileSize);
			const bool bCheckLegacyIds = true;
			TSet<FString> FileResumeIds;
			ManifestSet->GetInstallResumeIdsForFile(Filename, FileResumeIds, bCheckLegacyIds);
			if (LoadedResumeIds.Intersect(FileResumeIds).Num() > 0)
			{
				const FFileManifest* NewFileManifest = ManifestSet->GetNewFileManifest(Filename);
				if (NewFileManifest && bFileExists)
				{
					const uint64 UnsignedDiskFileSize = DiskFileSize;
					if (UnsignedDiskFileSize > 0 && UnsignedDiskFileSize <= NewFileManifest->FileSize)
					{
						FilesStarted.Add(Filename);
					}
					if (UnsignedDiskFileSize == NewFileManifest->FileSize)
					{
						FilesCompleted.Add(Filename);
					}
					if (UnsignedDiskFileSize > NewFileManifest->FileSize)
					{
						FilesIncompatible.Add(Filename);
					}
				}
			}
			else if (bFileExists)
			{
				FilesIncompatible.Add(Filename);
			}
		}
	}
};

/* FBuildPatchFileConstructor implementation
 *****************************************************************************/
FBuildPatchFileConstructor::FBuildPatchFileConstructor(FFileConstructorConfig InConfiguration, IFileSystem* InFileSystem, IChunkSource* InChunkSource, IChunkReferenceTracker* InChunkReferenceTracker, IInstallerError* InInstallerError, IInstallerAnalytics* InInstallerAnalytics, IFileConstructorStat* InFileConstructorStat)
	: Configuration(MoveTemp(InConfiguration))
	, Thread(nullptr)
	, bIsRunning(false)
	, bIsInited(false)
	, bInitFailed(false)
	, bIsDownloadStarted(false)
	, bInitialDiskSizeCheck(false)
	, bIsPaused(false)
	, bShouldAbort(false)
	, ThreadLock()
	, ConstructionStack()
	, FileSystem(InFileSystem)
	, ChunkSource(InChunkSource)
	, ChunkReferenceTracker(InChunkReferenceTracker)
	, InstallerError(InInstallerError)
	, InstallerAnalytics(InInstallerAnalytics)
	, FileConstructorStat(InFileConstructorStat)
	, TotalJobSize(0)
	, ByteProcessed(0)
	, RequiredDiskSpace(0)
	, AvailableDiskSpace(0)
{
	// Count initial job size
	const int32 ConstructListNum = Configuration.ConstructList.Num();
	ConstructionStack.Reserve(ConstructListNum);
	ConstructionStack.AddDefaulted(ConstructListNum);
	for (int32 ConstructListIdx = 0; ConstructListIdx < ConstructListNum ; ++ConstructListIdx)
	{
		const FString& ConstructListElem = Configuration.ConstructList[ConstructListIdx];
		const FFileManifest* FileManifest = Configuration.ManifestSet->GetNewFileManifest(ConstructListElem);
		if (FileManifest)
		{
			TotalJobSize += FileManifest->FileSize;
		}
		ConstructionStack[(ConstructListNum - 1) - ConstructListIdx] = ConstructListElem;
	}
	// Start thread!
	const TCHAR* ThreadName = TEXT("FileConstructorThread");
	Thread = FRunnableThread::Create(this, ThreadName);
}

FBuildPatchFileConstructor::~FBuildPatchFileConstructor()
{
	// Wait for and deallocate the thread
	if( Thread != nullptr )
	{
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
	}
}

bool FBuildPatchFileConstructor::Init()
{
	// We are ready to go if our delegates are bound and directories successfully created
	bool bStageDirExists = IFileManager::Get().DirectoryExists(*Configuration.StagingDirectory);
	if (!bStageDirExists)
	{
		UE_LOG(LogBuildPatchServices, Error, TEXT("FBuildPatchFileConstructor: Stage directory missing %s"), *Configuration.StagingDirectory);
		InstallerError->SetError(EBuildPatchInstallError::InitializationError, InitializationErrorCodes::MissingStageDirectory);
	}
	SetInitFailed(!bStageDirExists);
	return bStageDirExists;
}

uint32 FBuildPatchFileConstructor::Run()
{
	// TODO: Remove this check, or assert on construction
	if (Configuration.ManifestSet == nullptr)
	{
		UE_LOG(LogBuildPatchServices, Error, TEXT("FBuildPatchFileConstructor: ManifestSet is null."));
		return 0;
	}

	SetRunning(true);
	SetInited(true);
	FileConstructorStat->OnTotalRequiredUpdated(TotalJobSize);

	// Check for resume data
	FResumeData ResumeData(FileSystem, Configuration.ManifestSet, Configuration.StagingDirectory);

	// Remove incompatible files
	if (ResumeData.bHasResumeData)
	{
		for (const FString& FileToConstruct : Configuration.ConstructList)
		{
			ResumeData.CheckFile(FileToConstruct);
			const bool bFileIncompatible = ResumeData.FilesIncompatible.Contains(FileToConstruct);
			if (bFileIncompatible)
			{
				GLog->Logf(TEXT("FBuildPatchFileConstructor: Deleting incompatible stage file %s"), *FileToConstruct);
				FileSystem->DeleteFile(*(Configuration.StagingDirectory / FileToConstruct));
			}
		}
	}

	// Save for started versions
	TSet<FString> ResumeIds;
	const bool bCheckLegacyIds = false;

	Configuration.ManifestSet->GetInstallResumeIds(ResumeIds, bCheckLegacyIds);
	ResumeData.SaveOut(ResumeIds);

	// Start resume progress at zero or one.
	FileConstructorStat->OnResumeStarted();

	// While we have files to construct, run.
	FString FileToConstruct;
	while (GetFileToConstruct(FileToConstruct) && !bShouldAbort)
	{
		// Get the file manifest.
		const FFileManifest* FileManifest = Configuration.ManifestSet->GetNewFileManifest(FileToConstruct);
		bool bFileSuccess = FileManifest != nullptr;
		if (bFileSuccess)
		{
			const int64 FileSize = FileManifest->FileSize;
			FileConstructorStat->OnFileStarted(FileToConstruct, FileSize);

			// Check resume status for this file.
			const bool bFilePreviouslyComplete = ResumeData.FilesCompleted.Contains(FileToConstruct);
			const bool bFilePreviouslyStarted = ResumeData.FilesStarted.Contains(FileToConstruct);

			// Construct or skip the file.
			if (bFilePreviouslyComplete)
			{
				bFileSuccess = true;
				CountBytesProcessed(FileSize);
				GLog->Logf(TEXT("FBuildPatchFileConstructor: Skipping completed file %s"), *FileToConstruct);
				// Go through each chunk part, and dereference it from the reference tracker.
				for (const FChunkPart& ChunkPart : FileManifest->ChunkParts)
				{
					bFileSuccess = ChunkReferenceTracker->PopReference(ChunkPart.Guid) && bFileSuccess;
				}
			}
			else
			{
				bFileSuccess = ConstructFileFromChunks(FileToConstruct, bFilePreviouslyStarted);
			}
		}
		else
		{
			// Only report or log if the first error
			if (InstallerError->HasError() == false)
			{
				InstallerAnalytics->RecordConstructionError(FileToConstruct, INDEX_NONE, TEXT("Missing File Manifest"));
				UE_LOG(LogBuildPatchServices, Error, TEXT("FBuildPatchFileConstructor: Missing file manifest for %s"), *FileToConstruct);
			}
			// Always set
			InstallerError->SetError(EBuildPatchInstallError::FileConstructionFail, ConstructionErrorCodes::MissingFileInfo);
		}

		if (bFileSuccess)
		{
			// If we are destructive, remove the old file.
			if (Configuration.InstallMode == EInstallMode::DestructiveInstall)
			{
				const bool bRequireExists = false;
				const bool bEvenReadOnly = true;
				FString FileToDelete = Configuration.InstallDirectory / FileToConstruct;
				FPaths::NormalizeFilename(FileToDelete);
				FPaths::CollapseRelativeDirectories(FileToDelete);
				if (FileSystem->FileExists(*FileToDelete))
				{
					OnBeforeDeleteFile().Broadcast(FileToDelete);
					IFileManager::Get().Delete(*FileToDelete, bRequireExists, bEvenReadOnly);
				}
			}
		}
		else
		{
			// This will only record and log if a failure was not already registered.
			bShouldAbort = true;
			InstallerError->SetError(EBuildPatchInstallError::FileConstructionFail, ConstructionErrorCodes::UnknownFail);
			UE_LOG(LogBuildPatchServices, Error, TEXT("FBuildPatchFileConstructor: Failed to build %s "), *FileToConstruct);
		}
		FileConstructorStat->OnFileCompleted(FileToConstruct, bFileSuccess);

		// Wait while paused.
		FileConstructorHelpers::WaitWhilePaused(bIsPaused, bShouldAbort);
	}

	// Mark resume complete if we didn't have work to do.
	if (!bIsDownloadStarted)
	{
		FileConstructorStat->OnResumeCompleted();
	}
	FileConstructorStat->OnConstructionCompleted();

	SetRunning(false);
	return 0;
}

void FBuildPatchFileConstructor::Wait()
{
	if( Thread != nullptr )
	{
		Thread->WaitForCompletion();
	}
}

bool FBuildPatchFileConstructor::IsComplete()
{
	FScopeLock Lock( &ThreadLock );
	return ( !bIsRunning && bIsInited ) || bInitFailed;
}

uint64 FBuildPatchFileConstructor::GetRequiredDiskSpace()
{
	FScopeLock Lock(&ThreadLock);
	return RequiredDiskSpace;
}

uint64 FBuildPatchFileConstructor::GetAvailableDiskSpace()
{
	FScopeLock Lock(&ThreadLock);
	return AvailableDiskSpace;
}

FBuildPatchFileConstructor::FOnBeforeDeleteFile& FBuildPatchFileConstructor::OnBeforeDeleteFile()
{
	return BeforeDeleteFileEvent;
}

void FBuildPatchFileConstructor::SetRunning( bool bRunning )
{
	FScopeLock Lock( &ThreadLock );
	bIsRunning = bRunning;
}

void FBuildPatchFileConstructor::SetInited( bool bInited )
{
	FScopeLock Lock( &ThreadLock );
	bIsInited = bInited;
}

void FBuildPatchFileConstructor::SetInitFailed( bool bFailed )
{
	FScopeLock Lock( &ThreadLock );
	bInitFailed = bFailed;
}

void FBuildPatchFileConstructor::CountBytesProcessed( const int64& ByteCount )
{
	ByteProcessed += ByteCount;
	FileConstructorStat->OnProcessedDataUpdated(ByteProcessed);
}

bool FBuildPatchFileConstructor::GetFileToConstruct(FString& Filename)
{
	FScopeLock Lock(&ThreadLock);
	const bool bFileAvailable = ConstructionStack.Num() > 0;
	if (bFileAvailable)
	{
		const bool bAllowShrinking = false;
		Filename = ConstructionStack.Pop(bAllowShrinking);
	}
	return bFileAvailable;
}

int64 FBuildPatchFileConstructor::GetRemainingBytes()
{
	FScopeLock Lock(&ThreadLock);
	return Configuration.ManifestSet->GetTotalNewFileSize(ConstructionStack);
}

uint64 FBuildPatchFileConstructor::CalculateRequiredDiskSpace(const FFileManifest& InProgressFileManifest, uint64 InProgressFileSize)
{
	int64 DiskSpaceDeltaPeak = InProgressFileSize;
	if (Configuration.InstallMode == EInstallMode::DestructiveInstall)
	{
		// The simplest method will be to run through each high level file operation, tracking peak disk usage delta.
		int64 DiskSpaceDelta = InProgressFileSize;

		// Can remove old in progress file.
		DiskSpaceDelta -= InProgressFileManifest.FileSize;

		// Loop through all files to be made next, in order.
		for (int32 ConstructionStackIdx = ConstructionStack.Num() - 1; ConstructionStackIdx >= 0; --ConstructionStackIdx)
		{
			const FString& FileToConstruct = ConstructionStack[ConstructionStackIdx];
			const FFileManifest* NewFileManifest = Configuration.ManifestSet->GetNewFileManifest(FileToConstruct);
			const FFileManifest* OldFileManifest = Configuration.ManifestSet->GetCurrentFileManifest(FileToConstruct);
			// First we would need to make the new file.
			DiskSpaceDelta += NewFileManifest->FileSize;
			if (DiskSpaceDeltaPeak < DiskSpaceDelta)
			{
				DiskSpaceDeltaPeak = DiskSpaceDelta;
			}
			// Then we can remove the current existing file.
			if (OldFileManifest)
			{
				DiskSpaceDelta -= OldFileManifest->FileSize;
			}
		}
	}
	else
	{
		// When not destructive, we always stage all new and changed files.
		DiskSpaceDeltaPeak += Configuration.ManifestSet->GetTotalNewFileSize(ConstructionStack);
	}
	return FMath::Max<int64>(DiskSpaceDeltaPeak, 0);
}

bool FBuildPatchFileConstructor::ConstructFileFromChunks( const FString& Filename, bool bResumeExisting )
{
	bool bSuccess = true;
	FString NewFilename = Configuration.StagingDirectory / Filename;

	// Calculate the hash as we write the data
	FSHA1 HashState;
	FSHAHash HashValue;

	// First make sure we can get the file manifest
	const FFileManifest* FileManifest = Configuration.ManifestSet->GetNewFileManifest(Filename);
	bSuccess = FileManifest != nullptr;
	if( bSuccess )
	{
		if( !FileManifest->SymlinkTarget.IsEmpty() )
		{
#if PLATFORM_MAC
			bSuccess = symlink(TCHAR_TO_UTF8(*FileManifest->SymlinkTarget), TCHAR_TO_UTF8(*NewFilename)) == 0;
#else
			const bool bSymlinkNotImplemented = false;
			check(bSymlinkNotImplemented);
			bSuccess = false;
#endif
			return bSuccess;
		}

		// Check for resuming of existing file
		int64 StartPosition = 0;
		int32 StartChunkPart = 0;
		if (bResumeExisting)
		{
			// We have to read in the existing file so that the hash check can still be done.
			TUniquePtr<FArchive> NewFileReader(IFileManager::Get().CreateFileReader(*NewFilename));
			if (NewFileReader.IsValid())
			{
				// Start with a sensible buffer size for reading. 4 MiB.
				const int32 ReadBufferSize = 4 * 1024 * 1024;
				// Read buffer
				TArray<uint8> ReadBuffer;
				ReadBuffer.Empty(ReadBufferSize);
				ReadBuffer.SetNumUninitialized(ReadBufferSize);
				// Reuse a certain amount of the file
				StartPosition = FMath::Max<int64>(0, NewFileReader->TotalSize() - NUM_BYTES_RESUME_IGNORE);
				// We'll also find the correct chunkpart to start writing from
				int64 ByteCounter = 0;
				for (int32 ChunkPartIdx = StartChunkPart; ChunkPartIdx < FileManifest->ChunkParts.Num() && !bShouldAbort; ++ChunkPartIdx)
				{
					const FChunkPart& ChunkPart = FileManifest->ChunkParts[ChunkPartIdx];
					const int64 NextBytePosition = ByteCounter + ChunkPart.Size;
					if (NextBytePosition <= StartPosition)
					{
						// Ensure buffer is large enough
						ReadBuffer.SetNumUninitialized(ChunkPart.Size, false);
						ISpeedRecorder::FRecord ActivityRecord;
						// Read data for hash check
						FileConstructorStat->OnBeforeRead();
						ActivityRecord.CyclesStart = FStatsCollector::GetCycles();
						NewFileReader->Serialize(ReadBuffer.GetData(), ChunkPart.Size);
						ActivityRecord.CyclesEnd = FStatsCollector::GetCycles();
						ActivityRecord.Size = ChunkPart.Size;
						HashState.Update(ReadBuffer.GetData(), ChunkPart.Size);
						FileConstructorStat->OnAfterRead(ActivityRecord);
						// Count bytes read from file
						ByteCounter = NextBytePosition;
						// Set to resume from next chunk part
						StartChunkPart = ChunkPartIdx + 1;
						// Inform the reference tracker of the chunk part skip
						bSuccess = ChunkReferenceTracker->PopReference(ChunkPart.Guid) && bSuccess;
						CountBytesProcessed(ChunkPart.Size);
						FileConstructorStat->OnFileProgress(Filename, NewFileReader->Tell());
						// Wait if paused
						FileConstructorHelpers::WaitWhilePaused(bIsPaused, bShouldAbort);
					}
					else
					{
						// No more parts on disk
						break;
					}
				}
				// Set start position to the byte we got up to
				StartPosition = ByteCounter;
				// Close file
				NewFileReader->Close();
			}
		}

		// If we haven't done so yet, make the initial disk space check
		if (!bInitialDiskSizeCheck)
		{
			bInitialDiskSizeCheck = true;
			const uint64 RequiredSpace = CalculateRequiredDiskSpace(*FileManifest, FileManifest->FileSize - StartPosition);
			// ThreadLock protects access to member RequiredDiskSpace and AvailableDiskSpace;
			FScopeLock Lock(&ThreadLock);
			RequiredDiskSpace = RequiredSpace;
			if (!FileConstructorHelpers::CheckAndReportRemainingDiskSpaceError(InstallerError, Configuration.InstallDirectory, RequiredSpace, DiskSpaceErrorCodes::InitialSpaceCheck, &AvailableDiskSpace))
			{
				return false;
			}
		}

		// Now we can make sure the chunk cache knows to start downloading chunks
		if (!bIsDownloadStarted)
		{
			bIsDownloadStarted = true;
			FileConstructorStat->OnResumeCompleted();
		}

		// Attempt to create the file
		ISpeedRecorder::FRecord ActivityRecord;
		FileConstructorStat->OnBeforeAdminister();
		ActivityRecord.CyclesStart = FStatsCollector::GetCycles();
		TUniquePtr<FArchive> NewFile = FileSystem->CreateFileWriter( *NewFilename, bResumeExisting ? EWriteFlags::Append : EWriteFlags::None );
		uint32 LastError = FPlatformMisc::GetLastError();
		ActivityRecord.CyclesEnd = FStatsCollector::GetCycles();
		ActivityRecord.Size = 0;
		FileConstructorStat->OnAfterAdminister(ActivityRecord);
		bSuccess = NewFile != nullptr;
		if (bSuccess)
		{
			// Seek to file write position
			if (NewFile->Tell() != StartPosition)
			{
				FileConstructorStat->OnBeforeAdminister();
				ActivityRecord.CyclesStart = FStatsCollector::GetCycles();
				NewFile->Seek(StartPosition);
				ActivityRecord.CyclesEnd = FStatsCollector::GetCycles();
				ActivityRecord.Size = 0;
				FileConstructorStat->OnAfterAdminister(ActivityRecord);
			}

			// For each chunk, load it, and place it's data into the file
			for (int32 ChunkPartIdx = StartChunkPart; ChunkPartIdx < FileManifest->ChunkParts.Num() && bSuccess && !bShouldAbort; ++ChunkPartIdx)
			{
				const FChunkPart& ChunkPart = FileManifest->ChunkParts[ChunkPartIdx];
				bSuccess = InsertChunkData(ChunkPart, *NewFile, HashState);
				FileConstructorStat->OnFileProgress(Filename, NewFile->Tell());
				if (bSuccess)
				{
					CountBytesProcessed(ChunkPart.Size);
					// Wait while paused
					FileConstructorHelpers::WaitWhilePaused(bIsPaused, bShouldAbort);
				}
				else
				{
					// Only report or log if the first error
					if (InstallerError->HasError() == false)
					{
						InstallerAnalytics->RecordConstructionError(Filename, INDEX_NONE, TEXT("Missing Chunk"));
						UE_LOG(LogBuildPatchServices, Error, TEXT("FBuildPatchFileConstructor: Failed %s due to chunk %s"), *Filename, *ChunkPart.Guid.ToString());
					}
					InstallerError->SetError(EBuildPatchInstallError::FileConstructionFail, ConstructionErrorCodes::MissingChunkData);
				}
			}

			// Close the file writer
			FileConstructorStat->OnBeforeAdminister();
			ActivityRecord.CyclesStart = FStatsCollector::GetCycles();
			NewFile->Close();
			NewFile.Reset();
			ActivityRecord.CyclesEnd = FStatsCollector::GetCycles();
			ActivityRecord.Size = 0;
			FileConstructorStat->OnAfterAdminister(ActivityRecord);
		}
		else
		{
			// Check if drive space was the issue here
			const uint64 RequiredSpace = CalculateRequiredDiskSpace(*FileManifest, FileManifest->FileSize);
			bool bError = !FileConstructorHelpers::CheckAndReportRemainingDiskSpaceError(InstallerError, Configuration.InstallDirectory, RequiredSpace, DiskSpaceErrorCodes::DuringInstallation);

			// Otherwise we just couldn't make the file
			if (!bError)
			{
				// Only report or log if the first error
				if (InstallerError->HasError() == false)
				{
					InstallerAnalytics->RecordConstructionError(Filename, LastError, TEXT("Could Not Create File"));
					UE_LOG(LogBuildPatchServices, Error, TEXT("FBuildPatchFileConstructor: Could not create %s"), *Filename);
				}
				// Always set
				InstallerError->SetError(EBuildPatchInstallError::FileConstructionFail, ConstructionErrorCodes::FileCreateFail, LastError);
			}
		}
	}
	else
	{
		// Only report or log if the first error
		if (InstallerError->HasError() == false)
		{
			InstallerAnalytics->RecordConstructionError(Filename, INDEX_NONE, TEXT("Missing File Manifest"));
			UE_LOG(LogBuildPatchServices, Error, TEXT("FBuildPatchFileConstructor: Missing file manifest for %s"), *Filename);
		}
		// Always set
		InstallerError->SetError(EBuildPatchInstallError::FileConstructionFail, ConstructionErrorCodes::MissingFileInfo);
	}

	// Verify the hash for the file that we created
	if( bSuccess )
	{
		HashState.Final();
		HashState.GetHash( HashValue.Hash );
		bSuccess = HashValue == FileManifest->FileHash;
		if( !bSuccess )
		{
			// Only report or log if the first error
			if (InstallerError->HasError() == false)
			{
				InstallerAnalytics->RecordConstructionError(Filename, INDEX_NONE, TEXT("Serialised Verify Fail"));
				UE_LOG(LogBuildPatchServices, Error, TEXT("FBuildPatchFileConstructor: Verify failed after constructing %s"), *Filename);
			}
			// Always set
			InstallerError->SetError(EBuildPatchInstallError::FileConstructionFail, ConstructionErrorCodes::OutboundCorrupt);
		}
	}

#if PLATFORM_MAC
	if (bSuccess && EnumHasAllFlags(FileManifest->FileMetaFlags, EFileMetaFlags::UnixExecutable))
	{
		// Enable executable permission bit
		struct stat FileInfo;
		if (stat(TCHAR_TO_UTF8(*NewFilename), &FileInfo) == 0)
		{
			bSuccess = chmod(TCHAR_TO_UTF8(*NewFilename), FileInfo.st_mode | S_IXUSR | S_IXGRP | S_IXOTH) == 0;
		}
	}
#endif
	
#if PLATFORM_ANDROID || PLATFORM_ANDROIDESDEFERRED
	if (bSuccess)
	{
		IFileManager::Get().SetTimeStamp(*NewFilename, FDateTime::UtcNow());
	}
#endif

	// Delete the staging file if unsuccessful by means of construction fail (i.e. keep if canceled or download issue)
	if( !bSuccess && InstallerError->GetErrorType() == EBuildPatchInstallError::FileConstructionFail )
	{
		if (!FileSystem->DeleteFile(*NewFilename))
		{
			UE_LOG(LogBuildPatchServices, Warning, TEXT("FBuildPatchFileConstructor: Error deleting file: %s (Error Code %i)"), *NewFilename, FPlatformMisc::GetLastError());
		}
	}

	return bSuccess;
}

bool FBuildPatchFileConstructor::InsertChunkData(const FChunkPart& ChunkPart, FArchive& DestinationFile, FSHA1& HashState)
{
	uint8* Data;
	uint8* DataStart;
	ISpeedRecorder::FRecord ActivityRecord;
	FileConstructorStat->OnChunkGet(ChunkPart.Guid);
	IChunkDataAccess* ChunkDataAccess = ChunkSource->Get(ChunkPart.Guid);
	bool bSuccess = ChunkDataAccess != nullptr && !bShouldAbort;
	if (bSuccess)
	{
		ChunkDataAccess->GetDataLock(&Data, nullptr);
		FileConstructorStat->OnBeforeWrite();
		ActivityRecord.CyclesStart = FStatsCollector::GetCycles();
		DataStart = &Data[ChunkPart.Offset];
		HashState.Update(DataStart, ChunkPart.Size);
		DestinationFile.Serialize(DataStart, ChunkPart.Size);
		ActivityRecord.Size = ChunkPart.Size;
		ActivityRecord.CyclesEnd = FStatsCollector::GetCycles();
		FileConstructorStat->OnAfterWrite(ActivityRecord);
		ChunkDataAccess->ReleaseDataLock();
		bSuccess = ChunkReferenceTracker->PopReference(ChunkPart.Guid);
	}
	return bSuccess;
}

void FBuildPatchFileConstructor::DeleteDirectoryContents(const FString& RootDirectory)
{
	TArray<FString> SubDirNames;
	IFileManager::Get().FindFiles(SubDirNames, *(RootDirectory / TEXT("*")), false, true);
	for (const FString& DirName : SubDirNames)
	{
		IFileManager::Get().DeleteDirectory(*(RootDirectory / DirName), false, true);
	}

	TArray<FString> SubFileNames;
	IFileManager::Get().FindFiles(SubFileNames, *(RootDirectory / TEXT("*")), true, false);
	for (const FString& FileName : SubFileNames)
	{
		IFileManager::Get().Delete(*(RootDirectory / FileName), false, true);
	}
}
