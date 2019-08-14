// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeCounter.h"
#include "HAL/Runnable.h"

struct FFilesystemInfo;

/** 
 * DDC Filesystem Cache cleanup thread.
 */
class DERIVEDDATACACHE_API FDDCCleanup : public FRunnable
{
	/** Singleton instance */
	static FDDCCleanup* Runnable;
	
	/** Thread to run the cleanup FRunnable on */
	FRunnableThread* Thread;
	/** > 0 if we've been asked to abort work in progress at the next opportunity */
	FThreadSafeCounter StopTaskCounter;
	/** List of filesystems to clean up */
	TArray< TSharedPtr< struct FFilesystemInfo > > CleanupList;
	/** Synchronization object */
	FCriticalSection DataLock;
	/** If true, work without giving up time to other threads */
	bool bDontWaitBetweenDeletes;

	/** Time before any deleting starts */
	float TimeToWaitAfterInit;
	/** Time to wait before starting deleting the next DDC directory files */
	float TimeBetweenDeleteingDirectories;
	/** Time to wait before deleting the next DDC file */
	float TimeBetweenDeletingFiles;

	/** Constructor */
	FDDCCleanup();

	/** Destructor */
	virtual ~FDDCCleanup();

	//~ Begin FRunnable Interface.
	virtual bool Init();
	virtual uint32 Run();
	virtual void Stop();
	virtual void Exit();
	//~ End FRunnable Interface

	/** Checks if there's been any Stop requests */
	bool ShouldStop() const;

	/**
	 * Waits for a given amount of time periodically checking if there's been any Stop requests.
	 *
	 * @param InSeconds time in seconds to wait.
	 * @param InSleepTime interval at which to check for Stop requests.
	 */
	void Wait( const float InSeconds, const float InSleepTime = 0.1f );

	/**
	 * Performs directory cleanup.
	 *
	 * @param FilesystemInfo Filesystem to cleanup.
	 * @return true if the filesystem contained any directories to clean up, false otherwise.
	 */
	bool CleanupFilesystemDirectory( TSharedPtr< FFilesystemInfo > FilesystemInfo );

	/** Makes sure this thread has stopped properly */
	void EnsureCompletion();

public:

	/**
	 * Adds DDC filesystem to clean up.
	 *
	 * @param InCachePath Filesystem path.
	 * @param InDaysToDelete Number of days since last access time to consider a file as unused.
	 * @param InMaxContinuousFileChecks Number of files to check without pausing.
	 */
	void AddFilesystem( FString& InCachePath, int32 InDaysToDelete, int32 InMaxNumFoldersToCheck, int32 InMaxContinuousFileChecks );

	/** Gets DDC Cleanup singleton instance, crates one if it doesn't exist already */
	static FDDCCleanup* Get();

	/** Gets DDC Cleanup singleton instance */
	static FDDCCleanup* GetNoInit()
	{
		return Runnable;
	}

	/** Shuts down DDC Cleanup thread. */
	static void Shutdown();

	/** Sets whether cleanup should give up time to other threads between deletes */
	void WaitBetweenDeletes(bool bWait)
	{
		bDontWaitBetweenDeletes = !bWait;
	}

	/** Checks if the cleanup thread is done deleting files */
	bool IsFinished() const
	{
		return !CleanupList.Num();
	}
};
