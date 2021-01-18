// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IPerforceSourceControlWorker.h"
#include "PerforceSourceControlState.h"
#include "PerforceSourceControlChangelistState.h"

class FPerforceSourceControlRevision;

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

	/** Changelist we submitted */
	int32 OutChangelistNumber;
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
	typedef TMap<FString, TArray< TSharedRef<FPerforceSourceControlRevision, ESPMode::ThreadSafe> > > FHistoryMap;
	FHistoryMap OutHistory;

	/** Map of filenames to modified flag */
	TArray<FString> OutModifiedFiles;
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
	TArray<TArray<FPerforceSourceControlState>> OutCLShelvedFilesStates;
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
};

class FPerforceReopenWorker : public IPerforceSourceControlWorker
{
public:
	virtual ~FPerforceReopenWorker() {}
	// IPerforceSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FPerforceSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;
};

