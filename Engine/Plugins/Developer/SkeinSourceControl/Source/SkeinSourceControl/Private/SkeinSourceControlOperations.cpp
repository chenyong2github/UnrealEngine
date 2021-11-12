// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkeinSourceControlOperations.h"
#include "SkeinSourceControlMetadata.h"
#include "SkeinSourceControlProvider.h"
#include "SkeinSourceControlCommand.h"
#include "SkeinSourceControlModule.h"
#include "SkeinSourceControlUtils.h"
#include "SkeinSourceControlState.h"
#include "Modules/ModuleManager.h"
#include "SourceControlOperations.h"

#define LOCTEXT_NAMESPACE "SkeinSourceControl"



static bool UpdateCachedStates(const TArray<FSkeinSourceControlState>& InStates)
{
	FSkeinSourceControlModule& SkeinSourceControl = FModuleManager::LoadModuleChecked<FSkeinSourceControlModule>("SkeinSourceControl");
	FSkeinSourceControlProvider& Provider = SkeinSourceControl.GetProvider();

	int NbStatesUpdated = 0;

	for (const auto& InState : InStates)
	{
		TSharedRef<FSkeinSourceControlState, ESPMode::ThreadSafe> State = Provider.GetStateInternal(InState.Filename);
		if (State->State != InState.State)
		{
			State->State = InState.State;
			State->TimeStamp = FDateTime::Now();
			NbStatesUpdated++;
		}
	}

	return (NbStatesUpdated > 0);
}



FName FSkeinConnectWorker::GetName() const
{
	return "Connect";
}

bool FSkeinConnectWorker::Execute(FSkeinSourceControlCommand& InCommand)
{
	check(InCommand.Operation->GetName() == GetName());

	if (InCommand.SkeinBinaryPath.IsEmpty())
	{
		StaticCastSharedRef<FConnect>(InCommand.Operation)->SetErrorText(LOCTEXT("SkeinNotAvailable", "The Skein Command Line application could not be found."));
		InCommand.bCommandSuccessful = false;
		return InCommand.bCommandSuccessful;
	}

	if (InCommand.SkeinProjectRoot.IsEmpty())
	{
		StaticCastSharedRef<FConnect>(InCommand.Operation)->SetErrorText(LOCTEXT("SkeinNotEnabled", "There is no Skein project initialized for this location."));
		InCommand.bCommandSuccessful = false;
		return InCommand.bCommandSuccessful;
	}

	InCommand.bCommandSuccessful = SkeinSourceControlUtils::RunCommand(TEXT("auth login"), InCommand.SkeinBinaryPath, InCommand.SkeinProjectRoot, TArray<FString>(), InCommand.Files, InCommand.InfoMessages, InCommand.ErrorMessages);
	return InCommand.bCommandSuccessful;
}

bool FSkeinConnectWorker::UpdateStates() const
{
	check(IsInGameThread());

	return UpdateCachedStates(States);
}



FName FSkeinCheckInWorker::GetName() const
{
	return "CheckIn";
}

bool FSkeinCheckInWorker::Execute(FSkeinSourceControlCommand& InCommand)
{
	check(InCommand.Operation->GetName() == GetName());

	TSharedRef<FCheckIn, ESPMode::ThreadSafe> Operation = StaticCastSharedRef<FCheckIn>(InCommand.Operation);

	// Gather all files under our control - they will all end up in the snapshot
	TArray<FString> ProjectRoots;
	ProjectRoots.Add(InCommand.SkeinProjectRoot);

	SkeinSourceControlUtils::RunUpdateStatus(InCommand.SkeinBinaryPath, InCommand.SkeinProjectRoot, ProjectRoots, InCommand.ErrorMessages, States);

	// Export properties for each of them (metadata + thumbnail)
	ParallelFor(States.Num(),
		[&] (int32 ElemIdx)
		{
			FSkeinSourceControlState& FileState = States[ElemIdx];
			if (FileState.IsSourceControlled())
			{
				FString& File = FileState.Filename;

				const FString FileMetadata = SkeinSourceControlUtils::GetIntermediateMetadataPath(File);
				const FString FileThumbnail = SkeinSourceControlUtils::GetIntermediateThumbnailPath(File);

				SkeinSourceControlMetadata::ExtractMetadata(File, FileMetadata, FileThumbnail);
			}
		}
	);

	// Grab the message for this snapshot
	TArray<FString> Parameters;
	Parameters.Add("--message=\"" + Operation->GetDescription().ToString() + "\"");

	// Create the snapshot
	InCommand.bCommandSuccessful = SkeinSourceControlUtils::RunCommand(TEXT("projects snapshots create"), InCommand.SkeinBinaryPath, InCommand.SkeinProjectRoot, Parameters, TArray<FString>() /* InCommand.Files */, InCommand.InfoMessages, InCommand.ErrorMessages);

	// Cache the new file states
	SkeinSourceControlUtils::RunUpdateStatus(InCommand.SkeinBinaryPath, InCommand.SkeinProjectRoot, ProjectRoots, InCommand.ErrorMessages, States);

	return InCommand.bCommandSuccessful;
}

bool FSkeinCheckInWorker::UpdateStates() const
{
	check(IsInGameThread());

	return UpdateCachedStates(States);
}



FName FSkeinMarkForAddWorker::GetName() const
{
	return "MarkForAdd";
}

bool FSkeinMarkForAddWorker::Execute(FSkeinSourceControlCommand& InCommand)
{
	check(InCommand.Operation->GetName() == GetName());

	InCommand.bCommandSuccessful = SkeinSourceControlUtils::RunCommand(TEXT("assets track"), InCommand.SkeinBinaryPath, InCommand.SkeinProjectRoot, TArray<FString>(), InCommand.Files, InCommand.InfoMessages, InCommand.ErrorMessages);

	SkeinSourceControlUtils::RunUpdateStatus(InCommand.SkeinBinaryPath, InCommand.SkeinProjectRoot, InCommand.Files, InCommand.ErrorMessages, States);

	return InCommand.bCommandSuccessful;
}

bool FSkeinMarkForAddWorker::UpdateStates() const
{
	check(IsInGameThread());

	return UpdateCachedStates(States);
}



FName FSkeinDeleteWorker::GetName() const
{
	return "Delete";
}

bool FSkeinDeleteWorker::Execute(FSkeinSourceControlCommand& InCommand)
{
	check(InCommand.Operation->GetName() == GetName());

	InCommand.bCommandSuccessful = SkeinSourceControlUtils::RunCommand(TEXT("assets untrack"), InCommand.SkeinBinaryPath, InCommand.SkeinProjectRoot, TArray<FString>(), InCommand.Files, InCommand.InfoMessages, InCommand.ErrorMessages);

	SkeinSourceControlUtils::RunUpdateStatus(InCommand.SkeinBinaryPath, InCommand.SkeinProjectRoot, InCommand.Files, InCommand.ErrorMessages, States);

	return InCommand.bCommandSuccessful;
}

bool FSkeinDeleteWorker::UpdateStates() const
{
	check(IsInGameThread());

	return UpdateCachedStates(States);
}



FName FSkeinRevertWorker::GetName() const
{
	return "Revert";
}

bool FSkeinRevertWorker::Execute(FSkeinSourceControlCommand& InCommand)
{
	check(InCommand.Operation->GetName() == GetName());

	InCommand.bCommandSuccessful = SkeinSourceControlUtils::RunCommand(TEXT("assets revert"), InCommand.SkeinBinaryPath, InCommand.SkeinProjectRoot, TArray<FString>(), InCommand.Files, InCommand.InfoMessages, InCommand.ErrorMessages);

	SkeinSourceControlUtils::RunUpdateStatus(InCommand.SkeinBinaryPath, InCommand.SkeinProjectRoot, InCommand.Files, InCommand.ErrorMessages, States);

	return InCommand.bCommandSuccessful;
}

bool FSkeinRevertWorker::UpdateStates() const
{
	return UpdateCachedStates(States);
}



FName FSkeinSyncWorker::GetName() const
{
	return "Sync";
}

bool FSkeinSyncWorker::Execute(FSkeinSourceControlCommand& InCommand)
{
	check(InCommand.Operation->GetName() == GetName());

	InCommand.bCommandSuccessful = SkeinSourceControlUtils::RunCommand(TEXT("projects snapshots get"), InCommand.SkeinBinaryPath, InCommand.SkeinProjectRoot, TArray<FString>(), TArray<FString>(), InCommand.InfoMessages, InCommand.ErrorMessages);

	SkeinSourceControlUtils::RunUpdateStatus(InCommand.SkeinBinaryPath, InCommand.SkeinProjectRoot, InCommand.Files, InCommand.ErrorMessages, States);

	return InCommand.bCommandSuccessful;
}

bool FSkeinSyncWorker::UpdateStates() const
{
	check(IsInGameThread());

	return UpdateCachedStates(States);
}



FName FSkeinUpdateStatusWorker::GetName() const
{
	return "UpdateStatus";
}

bool FSkeinUpdateStatusWorker::Execute(FSkeinSourceControlCommand& InCommand)
{
	check(InCommand.Operation->GetName() == GetName());

	InCommand.bCommandSuccessful = SkeinSourceControlUtils::RunUpdateStatus(InCommand.SkeinBinaryPath, InCommand.SkeinProjectRoot, InCommand.Files, InCommand.ErrorMessages, States);

	return InCommand.bCommandSuccessful;
}

bool FSkeinUpdateStatusWorker::UpdateStates() const
{
	check(IsInGameThread());

	return UpdateCachedStates(States);
}

#undef LOCTEXT_NAMESPACE