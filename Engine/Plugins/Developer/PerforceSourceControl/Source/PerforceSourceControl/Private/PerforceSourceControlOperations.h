// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IPerforceSourceControlWorker.h"
#include "PerforceSourceControlState.h"
#include "PerforceSourceControlChangelistState.h"

class FPerforceSourceControlRevision;
typedef TMap<FString, TArray< TSharedRef<FPerforceSourceControlRevision, ESPMode::ThreadSafe> > > FPerforceFileHistoryMap;

class FPerforceConnectWorker : public IPerforceSourceControlWorker
{
public:
	virtual ~FPerforceConnectWorker() {}
	// IPerforceSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FPerforceSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;
};

class FPerforceCheckOutWorker : public IPerforceSourceControlWorker
{
public:
	virtual ~FPerforceCheckOutWorker() {}
	// IPerforceSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FPerforceSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;

public:
	/** Map of filenames to perforce state */
	TMap<FString, EPerforceState::Type> OutResults;
	FPerforceSourceControlChangelist InChangelist;
};

class FPerforceCheckInWorker : public IPerforceSourceControlWorker
{
public:
	virtual ~FPerforceCheckInWorker() {}
	// IPerforceSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FPerforceSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;

public:
	/** Map of filenames to perforce state */
	TMap<FString, EPerforceState::Type> OutResults;

	/** Changelist we asked to submit */
	FPerforceSourceControlChangelist InChangelist;

	/** Changelist we submitted */
	FPerforceSourceControlChangelist OutChangelist;
};

class FPerforceMarkForAddWorker : public IPerforceSourceControlWorker
{
public:
	virtual ~FPerforceMarkForAddWorker() {}
	// IPerforceSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FPerforceSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;

public:
	/** Map of filenames to perforce state */
	TMap<FString, EPerforceState::Type> OutResults;
	FPerforceSourceControlChangelist InChangelist;
};

class FPerforceDeleteWorker : public IPerforceSourceControlWorker
{
public:
	virtual ~FPerforceDeleteWorker() {}
	// IPerforceSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FPerforceSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;

public:
	/** Map of filenames to perforce state */
	TMap<FString, EPerforceState::Type> OutResults;
};

class FPerforceRevertWorker : public IPerforceSourceControlWorker
{
public:
	virtual ~FPerforceRevertWorker() {}
	// IPerforceSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FPerforceSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;

public:
	/** Map of filenames to perforce state */
	TMap<FString, EPerforceState::Type> OutResults;

	/** Changelist to be udpated */
	FPerforceSourceControlChangelist ChangelistToUpdate;
};

class FPerforceSyncWorker : public IPerforceSourceControlWorker
{
public:
	virtual ~FPerforceSyncWorker() {}
	// IPerforceSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FPerforceSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;

public:
	/** Map of filenames to perforce state */
	TMap<FString, EPerforceState::Type> OutResults;
};

class FPerforceUpdateStatusWorker : public IPerforceSourceControlWorker
{
public:
	virtual ~FPerforceUpdateStatusWorker() {}
	// IPerforceSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FPerforceSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;

public:
	/** Temporary states for results */
	TArray<FPerforceSourceControlState> OutStates;

	/** Map of filename->state */
	TMap<FString, EPerforceState::Type> OutStateMap;

	/** Map of filenames to history */
	FPerforceFileHistoryMap OutHistory;

	/** Map of filenames to modified flag */
	TArray<FString> OutModifiedFiles;

	/** Override on status update return */
	bool bForceQuiet = false;
};

class FPerforceGetWorkspacesWorker : public IPerforceSourceControlWorker
{
public:
	virtual ~FPerforceGetWorkspacesWorker() {}
	// IPerforceSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FPerforceSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;
};

class FPerforceGetPendingChangelistsWorker : public IPerforceSourceControlWorker
{
public:
	virtual ~FPerforceGetPendingChangelistsWorker() {}
	// IPerforceSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FPerforceSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;

public:
	/** Temporary states for results */
	TArray<FPerforceSourceControlChangelistState> OutChangelistsStates;
	TArray<TArray<FPerforceSourceControlState>> OutCLFilesStates;
	TArray<TMap<FString, EPerforceState::Type>> OutCLShelvedFilesStates;
	TArray<TMap<FString, FString>> OutCLShelvedFilesMap;

private:
	/** Controls whether or not we will remove changelists from the cache after a full update */
	bool bCleanupCache = false;
};

class FPerforceCopyWorker : public IPerforceSourceControlWorker
{
public:
	virtual ~FPerforceCopyWorker() {}
	// IPerforceSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FPerforceSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;

public:
	/** Map of filenames to perforce state */
	TMap<FString, EPerforceState::Type> OutResults;
};

class FPerforceResolveWorker : public IPerforceSourceControlWorker
{
public:
	virtual ~FPerforceResolveWorker() {}
	// IPerforceSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FPerforceSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;
private:
	TArray< FString > UpdatedFiles;
};

class FPerforceChangeStatusWorker : public IPerforceSourceControlWorker
{
public:
	virtual ~FPerforceChangeStatusWorker() {}
	// IPerforceSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FPerforceSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;
};

class FPerforceNewChangelistWorker : public IPerforceSourceControlWorker
{
public:
	FPerforceNewChangelistWorker();
	virtual ~FPerforceNewChangelistWorker() {}
	// IPerforceSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FPerforceSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;

public:
	/** New changelist information */
	FPerforceSourceControlChangelist NewChangelist;
	FPerforceSourceControlChangelistState NewChangelistState;

	/** Files that were moved */
	TArray<FString> MovedFiles;
};

class FPerforceDeleteChangelistWorker : public IPerforceSourceControlWorker
{
public:
	virtual ~FPerforceDeleteChangelistWorker() {}
	// IPerforceSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FPerforceSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;

public:
	FPerforceSourceControlChangelist DeletedChangelist;
};

class FPerforceEditChangelistWorker : public IPerforceSourceControlWorker
{
public:
	virtual ~FPerforceEditChangelistWorker() {}
	// IPerforceSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FPerforceSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;

public:
	FPerforceSourceControlChangelist EditedChangelist;
	FText EditedDescription;
};

class FPerforceRevertUnchangedWorker : public IPerforceSourceControlWorker
{
public:
	virtual ~FPerforceRevertUnchangedWorker() {}
	// IPerforceSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FPerforceSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;
protected:
	/** Map of filenames to perforce state */
	TMap<FString, EPerforceState::Type> OutResults;

	/** Changelist to be updated */
	FPerforceSourceControlChangelist ChangelistToUpdate;
};

class FPerforceReopenWorker : public IPerforceSourceControlWorker
{
public:
	virtual ~FPerforceReopenWorker() {}
	// IPerforceSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FPerforceSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;

protected:
	/** Reopened files */
	TArray<FString> ReopenedFiles;
	
	/** Destination changelist */
	FPerforceSourceControlChangelist DestinationChangelist;
};

class FPerforceShelveWorker : public IPerforceSourceControlWorker
{
public:
	virtual ~FPerforceShelveWorker() {}
	// IPerforceSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FPerforceSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;

protected:
	/** Map of filenames to perforce state */
	TMap<FString, EPerforceState::Type> OutResults;

	/** Map depot filenames to local file */
	TMap<FString, FString> OutFileMap;

	/** Reopened files */
	TArray<FString> MovedFiles;

	/** Changelist description if needed */
	FString ChangelistDescription;

	/** Changelist(s) to be updated */
	FPerforceSourceControlChangelist InChangelistToUpdate;
	FPerforceSourceControlChangelist OutChangelistToUpdate;
};

class FPerforceDeleteShelveWorker : public IPerforceSourceControlWorker
{
public:
	virtual ~FPerforceDeleteShelveWorker() {}
	// IPerforceSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FPerforceSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;

protected:
	/** List of files to remove from shelved files in changelist state */
	TArray<FString> FilesToRemove;

	/** Changelist to be updated */
	FPerforceSourceControlChangelist ChangelistToUpdate;
};

class FPerforceUnshelveWorker : public IPerforceSourceControlWorker
{
public:
	virtual ~FPerforceUnshelveWorker() {}
	// IPerforceSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FPerforceSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;

protected:
	/** Changelist to be updated */
	FPerforceSourceControlChangelist ChangelistToUpdate;

	/** List of files states after update */
	TArray<FPerforceSourceControlState> ChangelistFilesStates;
};

class FPerforceDownloadFileWorker : public IPerforceSourceControlWorker
{
public:
	virtual ~FPerforceDownloadFileWorker() = default;

	// IPerforceSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FPerforceSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;
};

class FPerforceCreateWorkspaceWorker : public IPerforceSourceControlWorker
{
public:
	virtual ~FPerforceCreateWorkspaceWorker() = default;

	// IPerforceSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FPerforceSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;

private:
	void AddType(const class FCreateWorkspace& Operation, FStringBuilderBase& ClientDesc);
};

class FPerforceDeleteWorkspaceWorker : public IPerforceSourceControlWorker
{
public:
	virtual ~FPerforceDeleteWorkspaceWorker() = default;

	// IPerforceSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FPerforceSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;
};
