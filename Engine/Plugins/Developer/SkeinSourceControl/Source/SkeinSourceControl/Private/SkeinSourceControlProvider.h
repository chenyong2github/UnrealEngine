// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ISourceControlProvider.h"
#include "ISkeinSourceControlWorker.h"

class FSkeinSourceControlCommand;
class FSkeinSourceControlState;

DECLARE_DELEGATE_RetVal(FSkeinSourceControlWorkerRef, FGetSkeinSourceControlWorker)

class FSkeinSourceControlProvider : public ISourceControlProvider
{
public:
	/** Constructor */
	FSkeinSourceControlProvider()
		: bSkeinBinaryFound(false)
		, bSkeinProjectFound(false)
	{
	}

	/** ISourceControlProvider implementation */
	virtual void Init(bool bForceConnection = true) override;
	virtual void Close() override;
	virtual FText GetStatusText() const override;
	virtual bool IsAvailable() const override;
	virtual bool IsEnabled() const override;
	virtual const FName& GetName(void) const override;
	virtual bool QueryStateBranchConfig(const FString& ConfigSrc, const FString& ConfigDest) override { return false; }
	virtual void RegisterStateBranches(const TArray<FString>& BranchNames, const FString& ContentRoot) override {}
	virtual int32 GetStateBranchIndex(const FString& BranchName) const override { return INDEX_NONE; }
	virtual ECommandResult::Type GetState(const TArray<FString>& InFiles, TArray<FSourceControlStateRef>& OutState, EStateCacheUsage::Type InStateCacheUsage) override;
	virtual ECommandResult::Type GetState(const TArray<FSourceControlChangelistRef>& InChangelists, TArray<FSourceControlChangelistStateRef>& OutState, EStateCacheUsage::Type InStateCacheUsage) override;
	virtual TArray<FSourceControlStateRef> GetCachedStateByPredicate(TFunctionRef<bool(const FSourceControlStateRef&)> Predicate) const override;
	virtual FDelegateHandle RegisterSourceControlStateChanged_Handle(const FSourceControlStateChanged::FDelegate& SourceControlStateChanged) override;
	virtual void UnregisterSourceControlStateChanged_Handle(FDelegateHandle Handle) override;
	virtual ECommandResult::Type Execute(const FSourceControlOperationRef& InOperation, FSourceControlChangelistPtr InChangelist, const TArray<FString>& InFiles, EConcurrency::Type InConcurrency = EConcurrency::Synchronous, const FSourceControlOperationComplete& InOperationCompleteDelegate = FSourceControlOperationComplete()) override;
	virtual bool CanCancelOperation(const FSourceControlOperationRef& InOperation) const override;
	virtual void CancelOperation(const FSourceControlOperationRef& InOperation) override;
	virtual bool UsesLocalReadOnlyState() const override;
	virtual bool UsesChangelists() const override;
	virtual bool UsesCheckout() const override;
	virtual void Tick() override;
	virtual TArray< TSharedRef<class ISourceControlLabel> > GetLabels(const FString& InMatchingSpec) const override;
	virtual TArray<FSourceControlChangelistRef> GetChangelists(EStateCacheUsage::Type InStateCacheUsage) override;
#if SOURCE_CONTROL_WITH_SLATE
	virtual TSharedRef<class SWidget> MakeSettingsWidget() const override;
#endif // SOURCE_CONTROL_WITH_SLATE

	/** Allow calls to all ISourceControlProvider::Execute variants */
	using ISourceControlProvider::Execute;

	/** Get the path to the Skein CLI binary */
	inline const FString& GetSkeinBinaryPath() const
	{
		return BinaryPath;
	}

	/** Get the path to the root of the Skein project: can be the ProjectDir itself, or any parent directory */
	inline const FString& GetSkeinProjectRoot() const
	{
		return ProjectRoot;
	}

	/** Remove a named file from the state cache */
	bool RemoveFileFromCache(const FString& Filename);

	/** Helper function used to update state cache */
	TSharedRef<FSkeinSourceControlState, ESPMode::ThreadSafe> GetStateInternal(const FString& Filename);

	/**
	 * Register a worker with the provider.
	 * This is used internally so the provider can maintain a map of all available operations.
	 */
	void RegisterWorker(const FName& InName, const FGetSkeinSourceControlWorker& InDelegate);

private:

	/** Helper function for Execute() */
	TSharedPtr<class ISkeinSourceControlWorker, ESPMode::ThreadSafe> CreateWorker(const FName& InOperationName) const;

	/**
	 * Helper function for running command 'synchronously'.
	 * This really doesn't execute synchronously; rather it adds the command to the queue & does not return until
	 * the command is completed.
	 */
	ECommandResult::Type ExecuteSynchronousCommand(FSkeinSourceControlCommand& InCommand, const FText& Task);

	/** Issue a command asynchronously if possible. */
	ECommandResult::Type IssueCommand(FSkeinSourceControlCommand& InCommand);

private:

	/** Is Skein service available */
	mutable bool bSkeinBinaryFound;

	/** Is Skein project found */
	mutable bool bSkeinProjectFound;

	/** Skein project root */
	mutable FString ProjectRoot;

	/** Skein project name */
	mutable FString ProjectName;

	/** Skein binary path */
	FString BinaryPath;

	/** State cache */
	TMap<FString, TSharedRef<FSkeinSourceControlState, ESPMode::ThreadSafe>> StateCache;

	/** The currently registered source control operations */
	TMap<FName, FGetSkeinSourceControlWorker> WorkersMap;

	/** Queue for commands given by the main thread */
	TArray<FSkeinSourceControlCommand*> CommandQueue;

	/** For notifying when the source control states in the cache have changed */
	FSourceControlStateChanged OnSourceControlStateChanged;
};