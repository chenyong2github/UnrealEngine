#pragma once

#include "CoreMinimal.h"
#include "ISkeinSourceControlWorker.h"
#include "SkeinSourceControlState.h"

/** Connect to the source control. */
class FSkeinConnectWorker : public ISkeinSourceControlWorker
{
public:
	virtual ~FSkeinConnectWorker() {}
	/** ISkeinSourceControlWorker interface */
	virtual FName GetName() const override;
	virtual bool Execute(class FSkeinSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;

public:
	/** Temporary states for results */
	TArray<FSkeinSourceControlState> States;
};

/** Commit (check-in) a set of files. */
class FSkeinCheckInWorker : public ISkeinSourceControlWorker
{
public:
	virtual ~FSkeinCheckInWorker() {}
	/** ISkeinSourceControlWorker interface */
	virtual FName GetName() const override;
	virtual bool Execute(class FSkeinSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;

public:
	/** Temporary states for results */
	TArray<FSkeinSourceControlState> States;
};

/** Add an untracked file to source control. */
class FSkeinMarkForAddWorker : public ISkeinSourceControlWorker
{
public:
	virtual ~FSkeinMarkForAddWorker() {}
	/** ISkeinSourceControlWorker interface */
	virtual FName GetName() const override;
	virtual bool Execute(class FSkeinSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;

public:
	/** Temporary states for results */
	TArray<FSkeinSourceControlState> States;
};

/** Delete a file and remove it from source control. */
class FSkeinDeleteWorker : public ISkeinSourceControlWorker
{
public:
	virtual ~FSkeinDeleteWorker() {}
	/** ISkeinSourceControlWorker interface */
	virtual FName GetName() const override;
	virtual bool Execute(class FSkeinSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;

public:
	/** Temporary states for results */
	TArray<FSkeinSourceControlState> States;
};

/** Skein synchronize to the active snapshot set in WebUI. */
class FSkeinSyncWorker : public ISkeinSourceControlWorker
{
public:
	virtual ~FSkeinSyncWorker() {}
	/** ISkeinSourceControlWorker interface */
	virtual FName GetName() const override;
	virtual bool Execute(class FSkeinSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;

public:
	/** Temporary states for results */
	TArray<FSkeinSourceControlState> States;
};

/** Get source control status of files. */
class FSkeinUpdateStatusWorker : public ISkeinSourceControlWorker
{
public:
	virtual ~FSkeinUpdateStatusWorker() {}
	/** ISkeinSourceControlWorker interface */
	virtual FName GetName() const override;
	virtual bool Execute(class FSkeinSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;

public:
	/** Temporary states for results */
	TArray<FSkeinSourceControlState> States;
};