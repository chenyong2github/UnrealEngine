// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkeinSourceControlProvider.h"
#include "SkeinSourceControlThumbnail.h"
#include "SkeinSourceControlCommand.h"
#include "SkeinSourceControlState.h"
#include "SkeinSourceControlUtils.h"
#include "ScopedSourceControlProgress.h"
#include "SourceControlOperations.h"
#include "SourceControlHelpers.h"
#include "Logging/MessageLog.h"

#if SOURCE_CONTROL_WITH_SLATE
#include "SSkeinSourceControlSettings.h"
#endif

#define LOCTEXT_NAMESPACE "SkeinSourceControlProvider"

void FSkeinSourceControlProvider::Init(bool bForceConnection)
{
	bSkeinBinaryFound = SkeinSourceControlUtils::IsSkeinAvailable();
	bSkeinProjectFound = SkeinSourceControlUtils::IsSkeinProjectFound(FPaths::ProjectDir(), ProjectRoot, ProjectName);
	BinaryPath = SkeinSourceControlUtils::FindSkeinBinaryPath();
}

void FSkeinSourceControlProvider::Close()
{
	BinaryPath.Empty();
	ProjectName.Empty();
	ProjectRoot.Empty();
	bSkeinProjectFound = false;
	bSkeinBinaryFound = false;
}

FText FSkeinSourceControlProvider::GetStatusText() const
{
	FFormatNamedArguments Args;
	Args.Add(TEXT("IsAvailable"), IsAvailable() ? LOCTEXT("Yes", "Yes") : LOCTEXT("No", "No"));
	Args.Add(TEXT("IsEnabled"), IsEnabled() ? LOCTEXT("Yes", "Yes") : LOCTEXT("No", "No"));
	Args.Add(TEXT("ProjectName"), FText::FromString(ProjectName));
	Args.Add(TEXT("ProjectRoot"), FText::FromString(ProjectRoot));

	return FText::Format(LOCTEXT("SkeinStatusText", "Available: {IsAvailable}\nEnabled: {IsEnabled}\nProjectName: {ProjectName}\nProjectRoot: {ProjectRoot}"), Args);
}

bool FSkeinSourceControlProvider::IsAvailable() const
{
	return bSkeinBinaryFound;
}

bool FSkeinSourceControlProvider::IsEnabled() const
{
	return bSkeinBinaryFound && bSkeinProjectFound;
}

const FName& FSkeinSourceControlProvider::GetName(void) const
{
	static FName ProviderName("Skein");
	return ProviderName;
}

ECommandResult::Type FSkeinSourceControlProvider::GetState(const TArray<FString>& InFiles, TArray<TSharedRef<ISourceControlState, ESPMode::ThreadSafe>>& OutState, EStateCacheUsage::Type InStateCacheUsage)
{
	if (!IsEnabled())
	{
		return ECommandResult::Failed;
	}

	TArray<FString> AbsoluteFiles = SourceControlHelpers::AbsoluteFilenames(InFiles);

	if (InStateCacheUsage == EStateCacheUsage::ForceUpdate)
	{
		Execute(ISourceControlOperation::Create<FUpdateStatus>(), AbsoluteFiles);
	}

	for (TArray<FString>::TConstIterator It(AbsoluteFiles); It; It++)
	{
		OutState.Add(GetStateInternal(*It));
	}

	return ECommandResult::Succeeded;
}

ECommandResult::Type FSkeinSourceControlProvider::GetState(const TArray<FSourceControlChangelistRef>& InChangelists, TArray<FSourceControlChangelistStateRef>& OutState, EStateCacheUsage::Type InStateCacheUsage)
{
	return ECommandResult::Failed;
}

TArray<FSourceControlStateRef> FSkeinSourceControlProvider::GetCachedStateByPredicate(TFunctionRef<bool(const FSourceControlStateRef&)> Predicate) const
{
	TArray<FSourceControlStateRef> Result;
	for (const auto& CacheItem : StateCache)
	{
		FSourceControlStateRef State = CacheItem.Value;
		if (Predicate(State))
		{
			Result.Add(State);
		}
	}
	return Result;
}

bool FSkeinSourceControlProvider::RemoveFileFromCache(const FString& Filename)
{
	return StateCache.Remove(Filename) > 0;
}

FDelegateHandle FSkeinSourceControlProvider::RegisterSourceControlStateChanged_Handle(const FSourceControlStateChanged::FDelegate& SourceControlStateChanged)
{
	return OnSourceControlStateChanged.Add(SourceControlStateChanged);
}

void FSkeinSourceControlProvider::UnregisterSourceControlStateChanged_Handle(FDelegateHandle Handle)
{
	OnSourceControlStateChanged.Remove(Handle);
}

ECommandResult::Type FSkeinSourceControlProvider::Execute(const FSourceControlOperationRef& InOperation, FSourceControlChangelistPtr InChangelist, const TArray<FString>& InFiles, EConcurrency::Type InConcurrency, const FSourceControlOperationComplete& InOperationCompleteDelegate)
{
	// Query to see if the we allow this operation
	TSharedPtr<ISkeinSourceControlWorker, ESPMode::ThreadSafe> Worker = CreateWorker(InOperation->GetName());
	if (!Worker.IsValid())
	{
		// This operation is unsupported by this source control provider
		FFormatNamedArguments Arguments;
		Arguments.Add(TEXT("OperationName"), FText::FromName(InOperation->GetName()));
		Arguments.Add(TEXT("ProviderName"), FText::FromName(GetName()));
		FText Message = FText::Format(LOCTEXT("UnsupportedOperation", "Operation '{OperationName}' not supported by source control provider '{ProviderName}'"), Arguments);

		FMessageLog("SourceControl").Error(Message);
		InOperation->AddErrorMessge(Message);

		InOperationCompleteDelegate.ExecuteIfBound(InOperation, ECommandResult::Failed);
		return ECommandResult::Failed;
	}

	TArray<FString> AbsoluteFiles = SourceControlHelpers::AbsoluteFilenames(InFiles);

	// Fire off operation
	if (InConcurrency == EConcurrency::Synchronous)
	{
		FSkeinSourceControlCommand* Command = new FSkeinSourceControlCommand(InOperation, Worker.ToSharedRef());
		Command->bAutoDelete = false;
		Command->Files = AbsoluteFiles;
		Command->OperationCompleteDelegate = InOperationCompleteDelegate;
		return ExecuteSynchronousCommand(*Command, InOperation->GetInProgressString());
	}
	else
	{
		FSkeinSourceControlCommand* Command = new FSkeinSourceControlCommand(InOperation, Worker.ToSharedRef());
		Command->bAutoDelete = true;
		Command->Files = AbsoluteFiles;
		Command->OperationCompleteDelegate = InOperationCompleteDelegate;
		return IssueCommand(*Command);
	}

	return ECommandResult::Failed;
}

bool FSkeinSourceControlProvider::CanCancelOperation(const FSourceControlOperationRef& InOperation) const
{
	return false;
}

void FSkeinSourceControlProvider::CancelOperation(const FSourceControlOperationRef& InOperation)
{
}

bool FSkeinSourceControlProvider::UsesLocalReadOnlyState() const
{
	return false;
}

bool FSkeinSourceControlProvider::UsesChangelists() const
{
	return false;
}

bool FSkeinSourceControlProvider::UsesCheckout() const
{
	return false;
}

void FSkeinSourceControlProvider::Tick()
{
	check(IsInGameThread());

	bool bStatesUpdated = false;
	for (int32 CommandIndex = 0; CommandIndex < CommandQueue.Num(); ++CommandIndex)
	{
		FSkeinSourceControlCommand& Command = *CommandQueue[CommandIndex];
		if (Command.bExecuteProcessed)
		{
			// Remove command from the queue
			CommandQueue.RemoveAt(CommandIndex);

			// Let command update the states of any files
			bStatesUpdated |= Command.Worker->UpdateStates();

			// Dump any messages to output log
			FMessageLog SourceControlLog("SourceControl");
			for (int32 ErrorIndex = 0; ErrorIndex < Command.ErrorMessages.Num(); ++ErrorIndex)
			{
				SourceControlLog.Error(FText::FromString(Command.ErrorMessages[ErrorIndex]));
			}
			for (int32 InfoIndex = 0; InfoIndex < Command.InfoMessages.Num(); ++InfoIndex)
			{
				SourceControlLog.Info(FText::FromString(Command.InfoMessages[InfoIndex]));
			}

			Command.ReturnResults();

			// Commands that are left in the array during a tick need to be deleted
			if (Command.bAutoDelete)
			{
				// Only delete commands that are not running 'synchronously'
				delete &Command;
			}

			// Only do one command per tick loop, as we dont want concurrent modification
			// of the command queue (which can happen in the completion delegate)
			break;
		}
	}

	if (bStatesUpdated)
	{
		OnSourceControlStateChanged.Broadcast();
	}
}

TArray< TSharedRef<ISourceControlLabel> > FSkeinSourceControlProvider::GetLabels(const FString& InMatchingSpec) const
{
	return TArray< TSharedRef<ISourceControlLabel> >();
}

TArray<FSourceControlChangelistRef> FSkeinSourceControlProvider::GetChangelists(EStateCacheUsage::Type InStateCacheUsage)
{
	return TArray<FSourceControlChangelistRef>();
}

#if SOURCE_CONTROL_WITH_SLATE
TSharedRef<class SWidget> FSkeinSourceControlProvider::MakeSettingsWidget() const
{
	return SNew(SSkeinSourceControlSettings);
}
#endif // SOURCE_CONTROL_WITH_SLATE

void FSkeinSourceControlProvider::RegisterWorker(const FName& InName, const FGetSkeinSourceControlWorker& InDelegate)
{
	WorkersMap.Add(InName, InDelegate);
}

TSharedPtr<ISkeinSourceControlWorker, ESPMode::ThreadSafe> FSkeinSourceControlProvider::CreateWorker(const FName& InOperationName) const
{
	const FGetSkeinSourceControlWorker* Operation = WorkersMap.Find(InOperationName);
	if (Operation != nullptr)
	{
		return Operation->Execute(); // Creates the ISkeinSourceControlWorker.
	}

	return nullptr;
}

ECommandResult::Type FSkeinSourceControlProvider::ExecuteSynchronousCommand(FSkeinSourceControlCommand& InCommand, const FText& Task)
{
	ECommandResult::Type Result = ECommandResult::Failed;

	// Display the progress dialog if a string was provided
	{
		FScopedSourceControlProgress Progress(Task);

		// Issue the command asynchronously...
		IssueCommand(InCommand);

		// ... then wait for its completion (thus making it synchronous)
		while (!InCommand.bExecuteProcessed)
		{
			// Tick the command queue and update progress.
			Tick();

			Progress.Tick();

			// Sleep for a bit so we don't busy-wait so much.
			FPlatformProcess::Sleep(0.01f);
		}

		// Always do one more Tick() to make sure the command queue is cleaned up.
		Tick();

		if (InCommand.bCommandSuccessful)
		{
			Result = ECommandResult::Succeeded;
		}
	}

	// Delete the command now (asynchronous commands are deleted in the Tick() method)
	check(!InCommand.bAutoDelete);

	// Ensure commands that are not auto deleted do not end up in the command queue
	if (CommandQueue.Contains(&InCommand))
	{
		CommandQueue.Remove(&InCommand);
	}
	delete &InCommand;

	return Result;
}

ECommandResult::Type FSkeinSourceControlProvider::IssueCommand(FSkeinSourceControlCommand& InCommand)
{
	if (GThreadPool != nullptr)
	{
		// Queue this to our worker thread(s) for resolving
		GThreadPool->AddQueuedWork(&InCommand);
		CommandQueue.Add(&InCommand);
		return ECommandResult::Succeeded;
	}
	else
	{
		FText Message(LOCTEXT("NoSCCThreads", "There are no threads available to process the source control command."));

		FMessageLog("SourceControl").Error(Message);
		InCommand.bCommandSuccessful = false;
		InCommand.Operation->AddErrorMessge(Message);

		return InCommand.ReturnResults();
	}
}

TSharedRef<FSkeinSourceControlState, ESPMode::ThreadSafe> FSkeinSourceControlProvider::GetStateInternal(const FString& Filename)
{
	TSharedRef<FSkeinSourceControlState, ESPMode::ThreadSafe>* State = StateCache.Find(Filename);
	if (State != NULL)
	{
		// Found cached item
		return (*State);
	}
	else
	{
		// Cache an unknown state for this item
		TSharedRef<FSkeinSourceControlState, ESPMode::ThreadSafe> NewState = MakeShareable(new FSkeinSourceControlState(Filename));
		StateCache.Add(Filename, NewState);
		return NewState;
	}
}

#undef LOCTEXT_NAMESPACE
