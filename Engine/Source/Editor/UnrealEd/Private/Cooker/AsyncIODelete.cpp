// Copyright Epic Games, Inc. All Rights Reserved.

#include "AsyncIODelete.h"
#include "CoreMinimal.h"

#include "Async/Async.h"
#include "Containers/UnrealString.h"
#include "CookOnTheSide/CookOnTheFlyServer.h" // needed for DECLARE_LOG_CATEGORY_EXTERN(LogCook,...)
#include "HAL/Event.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Math/NumericLimits.h"
#include "Misc/StringBuilder.h"
#include "Misc/Paths.h"
#include "Misc/PathViews.h"
#include "Templates/UnrealTemplate.h"

#if WITH_ASYNCIODELETE_DEBUG
TArray<FString> FAsyncIODelete::AllTempRoots;
#endif

FAsyncIODelete::FAsyncIODelete(const FStringView& InOwnedTempRoot)
{
	SetTempRoot(InOwnedTempRoot);
}

FAsyncIODelete::~FAsyncIODelete()
{
	SetTempRoot(FStringView());
}

void FAsyncIODelete::SetTempRoot(const FStringView& InOwnedTempRoot)
{
	Teardown();

#if WITH_ASYNCIODELETE_DEBUG
	if (!RequestedTempRoot.IsEmpty())
	{
		RemoveTempRoot(*RequestedTempRoot);
	}
#endif

	RequestedTempRoot = InOwnedTempRoot;

#if WITH_ASYNCIODELETE_DEBUG
	if (!RequestedTempRoot.IsEmpty())
	{
		AddTempRoot(*RequestedTempRoot);
	}
#endif
}

void FAsyncIODelete::SetDeletesPaused(bool bInPaused)
{
	bPaused = bInPaused;
	if (AsyncEnabled())
	{
		if (!bPaused)
		{
			IFileManager& FileManager = IFileManager::Get();
			for (const FString& DeletePath : PausedDeletes)
			{
				const bool IsDirectory = FileManager.DirectoryExists(*DeletePath);
				const bool IsFile = !IsDirectory && FileManager.FileExists(*DeletePath);
				if (!IsDirectory && !IsFile)
				{
					continue;
				}
				CreateDeleteTask(DeletePath, IsDirectory ? EPathType::Directory : EPathType::File);
			}
			PausedDeletes.Empty();
		}
	}
}

void FAsyncIODelete::Setup()
{
	if (bInitialized)
	{
		return;
	}

	bInitialized = true;
	TempRoot = RequestedTempRoot;
	if (RequestedTempRoot.IsEmpty())
	{
		checkf(false, TEXT("DeleteDirectory called without having first set a TempRoot"));
		return;
	}

	if (AsyncEnabled())
	{
		// Delete the TempRoot directory to clear the results from any previous process using the same TempRoot that did not shut down cleanly
		FString FoundRoot;
		if (!TryPurgeTempRootFamily(&FoundRoot))
		{
			// TryPurgeTempRootFamily logged the warning
			return;
		}

		// Create the empty directory to work in
		if (!IFileManager::Get().MakeDirectory(*FoundRoot, true))
		{
			UE_LOG(LogCook, Error, TEXT("Could not create asyncdelete root directory '%s'. LastError: %i. Falling back to synchronous delete."),
				*TempRoot, FPlatformMisc::GetLastError());
			return;
		}
		TempRoot = FoundRoot;

		// Allocate the task event
		check(TasksComplete == nullptr);
		TasksComplete = FPlatformProcess::GetSynchEventFromPool(true /* IsManualReset */);
		check(ActiveTaskCount == 0);
		TasksComplete->Trigger(); // We have 0 tasks so the event should be in the Triggered state

		// Assert that all other teardown-transient variables were cleared by the constructor or by the previous teardown
		// TempRoot and bPaused are preserved across setup/teardown and may have any value
		check(PausedDeletes.Num() == 0);
		check(DeleteCounter == 0);
		bAsyncInitialized = true;
	}
}

void FAsyncIODelete::Teardown()
{
	if (!bInitialized)
	{
		return;
	}

	if (bAsyncInitialized)
	{
		// Clear task variables
		WaitForAllTasks();
		check(ActiveTaskCount == 0 && TasksComplete != nullptr && TasksComplete->Wait(0));
		FPlatformProcess::ReturnSynchEventToPool(TasksComplete);
		TasksComplete = nullptr;

		// Remove the temp directory from disk
		TryPurgeTempRootFamily(nullptr);

		// Clear delete variables; we don't need to run the tasks for the remaining pauseddeletes because synchronously deleting the temp directory above did the work they were going to do
		PausedDeletes.Empty();
		DeleteCounter = 0;
		bAsyncInitialized = false;
	}
	TempRoot.Reset();

	// We are now torn down and ready for a new setup
	bInitialized = false;
}

bool FAsyncIODelete::WaitForAllTasks(float TimeLimitSeconds)
{
	if (!bAsyncInitialized)
	{
		return true;
	}

	if (TimeLimitSeconds <= 0.f)
	{
		TasksComplete->Wait();
	}
	else
	{
		if (!TasksComplete->Wait(FTimespan::FromSeconds(TimeLimitSeconds)))
		{
			return false;
		}
	}
	check(ActiveTaskCount == 0);
	return true;
}

bool FAsyncIODelete::Delete(const FStringView& PathToDelete, EPathType ExpectedType)
{
	IFileManager& FileManager = IFileManager::Get();
	FString PathToDeleteStr(PathToDelete);

	const bool IsDirectory = FileManager.DirectoryExists(*PathToDeleteStr);
	const bool IsFile = !IsDirectory && FileManager.FileExists(*PathToDeleteStr);
	if (!IsDirectory && !IsFile)
	{
		return true;
	}
	if (ExpectedType == EPathType::Directory && !IsDirectory)
	{
		checkf(false, TEXT("DeleteDirectory called on \"%.*s\" which is not a directory."), PathToDelete.Len(), PathToDelete.GetData());
		return false;
	}
	if (ExpectedType == EPathType::File && !IsFile)
	{
		checkf(false, TEXT("DeleteFile called on \"%.*s\" which is not a file."), PathToDelete.Len(), PathToDelete.GetData());
		return false;
	}

	if (bAsyncInitialized)
	{
		if (DeleteCounter == UINT32_MAX)
		{
			Teardown();
		}
	}
	Setup();
	// Prevent the user from trying to delete our temproot or anything inside it
	if (FPaths::IsUnderDirectory(PathToDeleteStr, TempRoot) || FPaths::IsUnderDirectory(TempRoot, PathToDeleteStr))
	{
		return false;
	}
	if (bAsyncInitialized)
	{
		const FString TempPath = FPaths::Combine(TempRoot, FString::Printf(TEXT("%u"), DeleteCounter));
		DeleteCounter++;

		const bool bReplace = true;
		const bool bEvenIfReadOnly = true;
		const bool bMoveAttributes = false;
		const bool bDoNotRetryOnError = true;
		if (!IFileManager::Get().Move(*TempPath, *PathToDeleteStr, bReplace, bEvenIfReadOnly, bMoveAttributes, bDoNotRetryOnError)) // IFileManager::Move works on either files or directories
		{
			// The move failed; try a synchronous delete as backup
			UE_LOG(LogCook, Warning, TEXT("Failed to move path '%.*s' for async delete (LastError == %i); falling back to synchronous delete."), PathToDelete.Len(), PathToDelete.GetData(), FPlatformMisc::GetLastError());
			return SynchronousDelete(*PathToDeleteStr, ExpectedType);
		}

		if (bPaused)
		{
			PausedDeletes.Add(TempPath);
		}
		else
		{
			CreateDeleteTask(TempPath, ExpectedType);
		}
		return true;
	}
	else
	{
		return SynchronousDelete(*PathToDeleteStr, ExpectedType);
	}
}

void FAsyncIODelete::CreateDeleteTask(const FStringView& InDeletePath, EPathType PathType)
{
	{
		FScopeLock Lock(&CriticalSection);
		TasksComplete->Reset();
		ActiveTaskCount++;
	}

	AsyncThread(
		[this, DeletePath = FString(InDeletePath), PathType]() { SynchronousDelete(*DeletePath, PathType); },
		0, TPri_Normal,
		[this]() { OnTaskComplete(); });
}

void FAsyncIODelete::OnTaskComplete()
{
	FScopeLock Lock(&CriticalSection);
	check(ActiveTaskCount > 0);
	ActiveTaskCount--;
	if (ActiveTaskCount == 0)
	{
		TasksComplete->Trigger();
	}
}

bool FAsyncIODelete::SynchronousDelete(const TCHAR* InDeletePath, EPathType PathType)
{
	bool Result;
	const bool bRequireExists = false;
	if (PathType == EPathType::Directory)
	{
		const bool bTree = true;
		Result = IFileManager::Get().DeleteDirectory(InDeletePath, bRequireExists, bTree);
	}
	else
	{
		const bool bEvenIfReadOnly = true;
		Result = IFileManager::Get().Delete(InDeletePath, bRequireExists, bEvenIfReadOnly);
	}

	if (!Result)
	{
		UE_LOG(LogCook, Warning, TEXT("Failed to asyncdelete %s '%s'. LastError == %i."), PathType == EPathType::Directory ? TEXT("directory") : TEXT("file"), InDeletePath, FPlatformMisc::GetLastError());
	}
	return Result;
}

bool FAsyncIODelete::TryPurgeTempRootFamily(FString* OutNewTempRoot)
{
	IFileManager& FileManager = IFileManager::Get();
	FString RequestedLeaf = FPaths::GetPathLeaf(RequestedTempRoot);
	FString ParentDir = FPaths::GetPath(RequestedTempRoot);

	TArray<uint32, TInlineAllocator<2>> ExistingRoots;
	FileManager.IterateDirectory(*ParentDir,
		[&ExistingRoots, &RequestedLeaf](const TCHAR* FilenameOrDirectory, bool bIsDirectory)
		{
			// Compare by PathLeaf instead of full path because absolute vs relative paths and junctions
			// may change the name of the parent directory
			FStringView ExistingPath(FilenameOrDirectory);
			FStringView ExistingLeaf = FPathViews::GetPathLeaf(ExistingPath);
			if (bIsDirectory && ExistingLeaf.StartsWith(FStringView(RequestedLeaf), ESearchCase::IgnoreCase))
			{
				FStringView Suffix = ExistingLeaf.RightChop(RequestedLeaf.Len());
				if (Suffix.Len() == 0)
				{
					ExistingRoots.Add(0);
				}
				else
				{
					// Suffix is null-terminated because it came from FilenameOrDirectory
					const TCHAR* SuffixPtr = Suffix.GetData();
					uint32 IntSuffix = 0;
					LexFromString(IntSuffix, SuffixPtr + 1);
					if (IntSuffix > 0)
					{
						ExistingRoots.Add(IntSuffix);
					}
				}
			}
			return true;
		});


	auto GetRootPath = [this](uint32 Suffix)
	{
		return Suffix == 0 ? RequestedTempRoot : FString::Printf(TEXT("%s_%d"), *RequestedTempRoot, Suffix);
	};

	uint32 LastError = 0;
	TArray<uint32> Undeletables;
	for (uint32 Suffix : ExistingRoots)
	{
		FString ExistingRoot = GetRootPath(Suffix);

		// Since we sometimes will be creating the directory again immediately, we need to take precautions against the delayed delete of directories that
		// occurs on Windows platforms; creating a new file/directory in one that was just deleted can fail.  So we need to move-delete our TempRoot
		// in addition to move-delete our clients' directories.  Since we don't have a TempRoot to move-delete into, we create a unique sibling directory name.
		FString UniqueDirectory = FPaths::CreateTempFilename(*ParentDir, TEXT("DeleteTemp"), TEXT(""));

		const bool bReplace = false;
		const bool bEvenIfReadOnly = true;
		const bool bMoveAttributes = false;
		const bool bDoNotRetryOnError = true;
		const TCHAR* DirectoryToDelete = *UniqueDirectory;
		const bool bMoveSucceeded = FileManager.Move(DirectoryToDelete, *ExistingRoot, bReplace, bEvenIfReadOnly,
			bMoveAttributes, bDoNotRetryOnError);
		if (!bMoveSucceeded)
		{
			// Move failed; fallback to inplace delete
			DirectoryToDelete = *ExistingRoot;
		}

		const bool bRequireExists = false;
		const bool bTree = true;
		const bool bDeleteSucceeded = FileManager.DeleteDirectory(DirectoryToDelete, bRequireExists, bTree);
		if (!bDeleteSucceeded)
		{
			LastError = FPlatformMisc::GetLastError();
			if (bMoveSucceeded && !bDeleteSucceeded)
			{
				// Try to move the directory back so that we can try again to delete it next time.
				FileManager.Move(*ExistingRoot, DirectoryToDelete, bReplace, bEvenIfReadOnly,
					bMoveAttributes, bDoNotRetryOnError);
			}
			Undeletables.Add(Suffix);
		}
	}

	if (OutNewTempRoot)
	{
		OutNewTempRoot->Reset();
		uint32 NewSuffix = 0;
		Undeletables.Sort();
		if (Undeletables.Num() > 0 && Undeletables[0] == 0)
		{
			NewSuffix = 1;
			for (uint32 Suffix : Undeletables)
			{
				if (Suffix == 0)
				{
					continue;
				}
				if (Suffix != NewSuffix)
				{
					break;
				}
				++NewSuffix;
			}
		}

		const uint32 MaxHangingTempRoots = 20;
		if (NewSuffix > MaxHangingTempRoots)
		{
			UE_LOG(LogCook, Error, TEXT("Could not clear %d old asyncdelete root directories '%s'_*.  LastError: %i.")
				TEXT("\n\tFalling back to synchronous delete. Delete the directories manually to silence this message."),
				Undeletables.Num(), *RequestedTempRoot, LastError);
			return false;
		}
		*OutNewTempRoot = GetRootPath(NewSuffix);
	}
	return true;
}

#if WITH_ASYNCIODELETE_DEBUG
void FAsyncIODelete::AddTempRoot(const FStringView& InTempRoot)
{
	FString TempRoot(InTempRoot);
	for (FString& Existing : AllTempRoots)
	{
		checkf(!FPaths::IsUnderDirectory(Existing, TempRoot), TEXT("New FAsyncIODelete has TempRoot \"%s\" that is a subdirectory of existing TempRoot \"%s\"."), *TempRoot, *Existing);
		checkf(!FPaths::IsUnderDirectory(TempRoot, Existing), TEXT("New FAsyncIODelete has TempRoot \"%s\" that is a parent directory of existing TempRoot \"%s\"."), *TempRoot, *Existing);
	}
	AllTempRoots.Add(MoveTemp(TempRoot));
}

void FAsyncIODelete::RemoveTempRoot(const FStringView& InTempRoot)
{
	AllTempRoots.Remove(FString(InTempRoot));
}
#endif
