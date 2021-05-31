// Copyright Epic Games, Inc. All Rights Reserved.

#include "UncontrolledChangelistState.h"

#include "Algo/Transform.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "SourceControlOperations.h"
#include "UncontrolledChangelist.h"

#define LOCTEXT_NAMESPACE "UncontrolledChangelists"

FUncontrolledChangelistState::FUncontrolledChangelistState(const FUncontrolledChangelist& InUncontrolledChangelist)
	: Changelist(InUncontrolledChangelist)
{
}

FName FUncontrolledChangelistState::GetIconName() const
{
	return FName("SourceControl.UncontrolledChangelist");
}

FName FUncontrolledChangelistState::GetSmallIconName() const
{
	return FName("SourceControl.UncontrolledChangelist_Small");
}

FText FUncontrolledChangelistState::GetDisplayText() const
{
	return FText::FromString(Changelist.ToString());
}

FText FUncontrolledChangelistState::GetDescriptionText() const
{
	return FText::FromString(Description);
}

FText FUncontrolledChangelistState::GetDisplayTooltip() const
{
	return LOCTEXT("Tooltip", "Tooltip");
}

const FDateTime& FUncontrolledChangelistState::GetTimeStamp() const
{
	return TimeStamp;
}

const TSet<FSourceControlStateRef>& FUncontrolledChangelistState::GetFilesStates() const
{
	return Files;
}

void FUncontrolledChangelistState::Serialize(TSharedRef<FJsonObject> OutJsonObject) const
{
	TArray<TSharedPtr<FJsonValue>> FileValues;

	for (const FSourceControlStateRef& File : Files)
	{
		FileValues.Add(MakeShareable(new FJsonValueString(File->GetFilename())));
	}

	OutJsonObject->SetArrayField(FILES_NAME, MoveTemp(FileValues));
}

bool FUncontrolledChangelistState::Deserialize(const TSharedRef<FJsonObject> InJsonValue)
{
	const TArray<TSharedPtr<FJsonValue>>* FileValues = nullptr;
	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();

	if ((!InJsonValue->TryGetArrayField(FILES_NAME, FileValues)) || (FileValues == nullptr))
	{
		UE_LOG(LogSourceControl, Error, TEXT("Cannot get field %s."), FILES_NAME);
		return false;
	}

	TArray<FString> Filenames;

	Algo::Transform(*FileValues, Filenames, [](const TSharedPtr<FJsonValue>& File)
	{
		return File->AsString();
	});

	AddFiles(Filenames, ECheckFlags::Modified | ECheckFlags::NotCheckedOut);

	return true;
}

bool FUncontrolledChangelistState::AddFiles(const TArray<FString>& InFilenames, const ECheckFlags InCheckFlags)
{
	TArray<FSourceControlStateRef> FileStates;
	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
	bool bCheckStatus = (InCheckFlags & ECheckFlags::Modified) != ECheckFlags::None;
	bool bCheckCheckout = (InCheckFlags & ECheckFlags::NotCheckedOut) != ECheckFlags::None;
	bool bOutChanged = false;

	if (InFilenames.IsEmpty())
	{
		return bOutChanged;
	}

	if (bCheckStatus)
	{
		auto UpdateStatusOperation = ISourceControlOperation::Create<FUpdateStatus>();
		UpdateStatusOperation->SetUpdateModifiedState(true);
		SourceControlProvider.Execute(UpdateStatusOperation, InFilenames);
	}

	bool GetStateSucceeded = SourceControlProvider.GetState(InFilenames, FileStates, EStateCacheUsage::Use) == ECommandResult::Succeeded;

	if (GetStateSucceeded && (!FileStates.IsEmpty()))
	{
		for (FSourceControlStateRef FileState : FileStates)
		{
			bool bIsCheckoutCompliant = (!bCheckCheckout) || (!FileState->IsCheckedOut());
			bool bIsStatusCompliant = (!bCheckStatus) || FileState->IsModified() || (!FileState->IsSourceControlled());

			if (bIsCheckoutCompliant && bIsStatusCompliant)
			{
				Files.Add(FileState);
				bOutChanged = true;
			}
		}
	}

	return bOutChanged;
}

bool FUncontrolledChangelistState::RemoveFiles(const TArray<FSourceControlStateRef>& InFileStates)
{
	bool bOutChanged = false;

	for (const FSourceControlStateRef& FileState : InFileStates)
	{
		int32 OldSize = Files.Num();

		Files.Remove(FileState);

		bOutChanged |= OldSize != Files.Num();
	}

	return bOutChanged;
}

bool FUncontrolledChangelistState::UpdateStatus()
{
	TArray<FString> FilesToUpdate;
	bool bOutChanged = false;

	Algo::Transform(Files, FilesToUpdate, [](const FSourceControlStateRef& State) { return State->GetFilename(); });

	if (FilesToUpdate.Num() == 0)
	{
		return bOutChanged;
	}

	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
	auto UpdateStatusOperation = ISourceControlOperation::Create<FUpdateStatus>();
	UpdateStatusOperation->SetUpdateModifiedState(true);

	SourceControlProvider.Execute(UpdateStatusOperation, FilesToUpdate);

	for (auto It = Files.CreateIterator(); It; ++It)
	{
		FSourceControlStateRef& State = *It;

		if (State->IsCheckedOut() || (!State->IsModified()))
		{
			It.RemoveCurrent();
			bOutChanged = true;
		}
	}

	return bOutChanged;
}

void FUncontrolledChangelistState::RemoveDuplicates(TSet<FString>& InOutLoadedFiles, TSet<FString>& InOutModifiedFiles)
{
	for (const FSourceControlStateRef& FileState : Files)
	{
		const FString& Filename = FileState->GetFilename();
		
		InOutLoadedFiles.Remove(Filename);
		InOutModifiedFiles.Remove(Filename);
	}
}

#undef LOCTEXT_NAMESPACE
