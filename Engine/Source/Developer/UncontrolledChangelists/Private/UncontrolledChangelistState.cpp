// Copyright Epic Games, Inc. All Rights Reserved.

#include "UncontrolledChangelistState.h"

#include "Algo/Copy.h"
#include "Algo/Transform.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "SourceControlHelpers.h"
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

const TSet<FString>& FUncontrolledChangelistState::GetOfflineFiles() const
{
	return OfflineFiles;
}

void FUncontrolledChangelistState::Serialize(TSharedRef<FJsonObject> OutJsonObject) const
{
	TArray<TSharedPtr<FJsonValue>> FileValues;

	Algo::Transform(Files, FileValues, [](const FSourceControlStateRef& File) { return MakeShareable(new FJsonValueString(File->GetFilename())); });
	Algo::Transform(OfflineFiles, FileValues, [](const FString& OfflineFile) { return MakeShareable(new FJsonValueString(OfflineFile)); });

	OutJsonObject->SetArrayField(FILES_NAME, MoveTemp(FileValues));
}

bool FUncontrolledChangelistState::Deserialize(const TSharedRef<FJsonObject> InJsonValue)
{
	const TArray<TSharedPtr<FJsonValue>>* FileValues = nullptr;

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

	// No source control is available, add files to the offline file set.
	if (!SourceControlProvider.IsAvailable())
	{
		int32 OldSize = OfflineFiles.Num();
		Algo::Copy(SourceControlHelpers::AbsoluteFilenames(InFilenames), OfflineFiles);
		return OldSize != OfflineFiles.Num();
	}

	if (bCheckStatus)
	{
		auto UpdateStatusOperation = ISourceControlOperation::Create<FUpdateStatus>();
		UpdateStatusOperation->SetUpdateModifiedState(true);
		UpdateStatusOperation->SetQuiet(true);
		UpdateStatusOperation->SetForceUpdate(true);
		SourceControlProvider.Execute(UpdateStatusOperation, InFilenames);
	}

	const bool GetStateSucceeded = SourceControlProvider.GetState(InFilenames, FileStates, EStateCacheUsage::Use) == ECommandResult::Succeeded;

	if (GetStateSucceeded && (!FileStates.IsEmpty()))
	{
		for (FSourceControlStateRef FileState : FileStates)
		{
			const bool bIsSourceControlled = (!FileState->IsUnknown()) && FileState->IsSourceControlled();
			const bool bFileExists = IFileManager::Get().FileExists(*FileState->GetFilename());

			const bool bIsUncontrolled = (!bIsSourceControlled) && bFileExists;
			// File doesn't exist and is not marked for delete
			const bool bIsDeleted = bIsSourceControlled && (!bFileExists) && (!FileState->IsDeleted());
			const bool bIsModified = FileState->IsModified() && (!FileState->IsDeleted());

			const bool bIsCheckoutCompliant = (!bCheckCheckout) || (!FileState->IsCheckedOut());
			const bool bIsStatusCompliant = (!bCheckStatus) || bIsModified || bIsUncontrolled || bIsDeleted;

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
		bOutChanged |= (Files.Remove(FileState) > 0);
	}

	return bOutChanged;
}

bool FUncontrolledChangelistState::UpdateStatus()
{
	TArray<FString> FilesToUpdate;
	bool bOutChanged = false;
	int32 InitialFileNumber = Files.Num();
	int32 InitialOfflineFileNumber = OfflineFiles.Num();

	Algo::Transform(Files, FilesToUpdate, [](const FSourceControlStateRef& State) { return State->GetFilename(); });
	Algo::Copy(OfflineFiles, FilesToUpdate);

	Files.Empty();
	OfflineFiles.Empty();

	if (FilesToUpdate.Num() == 0)
	{
		return bOutChanged;
	}

	bOutChanged |= AddFiles(FilesToUpdate, ECheckFlags::All);

	bool bFileNumberChanged = InitialFileNumber == Files.Num();
	bool bOfflineFileNumberChanged = InitialOfflineFileNumber == OfflineFiles.Num();

	bOutChanged |= bFileNumberChanged || bOfflineFileNumberChanged;

	return bOutChanged;
}

void FUncontrolledChangelistState::RemoveDuplicates(TSet<FString>& InOutAddedAssets)
{
	for (const FSourceControlStateRef& FileState : Files)
	{
		const FString& Filename = FileState->GetFilename();
		
		InOutAddedAssets.Remove(Filename);
	}
}

#undef LOCTEXT_NAMESPACE
