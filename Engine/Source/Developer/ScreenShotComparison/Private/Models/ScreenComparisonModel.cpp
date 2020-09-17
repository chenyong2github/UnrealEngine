// Copyright Epic Games, Inc. All Rights Reserved.

#include "ScreenComparisonModel.h"
#include "ISourceControlModule.h"
#include "ISourceControlOperation.h"
#include "SourceControlOperations.h"
#include "ISourceControlProvider.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogScreenshotComparison, Log, All);

FScreenComparisonModel::FScreenComparisonModel(const FComparisonReport& InReport)
	: Report(InReport)
	, bComplete(false)
{
	const FImageComparisonResult& ComparisonResult = Report.GetComparisonResult();

	/*
		Remember that this report may have been loaded from elsewhere so we will not have access to the comparison/delta paths that
		were used at the time. We need to use the files in the report folder, though we will write to the ideal approved path since
		that is where a blessed image should be checked in to.
	*/

	FString SourceImage = FPaths::Combine(Report.GetReportPath(), Report.GetComparisonResult().ReportIncomingFilePath);
	FString OutputImage = FPaths::Combine(FPaths::ProjectDir(), Report.GetComparisonResult().IdealApprovedFolderPath, FPaths::GetCleanFilename(Report.GetComparisonResult().IncomingFilePath));

	FileImports.Add(FFileMapping(OutputImage, SourceImage));
	FileImports.Add(FFileMapping(FPaths::ChangeExtension(OutputImage, TEXT("json")), FPaths::ChangeExtension(SourceImage, TEXT("json"))));
}

bool FScreenComparisonModel::IsComplete() const
{
	return bComplete;
}

void FScreenComparisonModel::Complete(bool bWasSuccessful)
{
	if (bWasSuccessful)
	{
		// Delete report folder
		IFileManager::Get().DeleteDirectory(*Report.GetReportPath(), false, true);
	}

	bComplete = true;
	OnComplete.Broadcast();
}

TOptional<FAutomationScreenshotMetadata> FScreenComparisonModel::GetMetadata()
{
	// Load it.
	if ( !Metadata.IsSet() )
	{
		const FImageComparisonResult& Comparison = Report.GetComparisonResult();

		FString IncomingImage = Report.GetReportPath() / Comparison.ReportIncomingFilePath;
		FString IncomingMetadata = FPaths::ChangeExtension(IncomingImage, TEXT("json"));

		if ( !IncomingMetadata.IsEmpty() )
		{
			FString Json;
			if ( FFileHelper::LoadFileToString(Json, *IncomingMetadata) )
			{
				FAutomationScreenshotMetadata LoadedMetadata;
				if ( FJsonObjectConverter::JsonObjectStringToUStruct(Json, &LoadedMetadata, 0, 0) )
				{
					Metadata = LoadedMetadata;
				}
			}
		}
	}

	return Metadata;
}

bool FScreenComparisonModel::AddNew()
{
	bool bSuccess = true;

	// Copy the files from the reports location to the destination location
	TArray<FString> SourceControlFiles;
	for ( const FFileMapping& Incoming : FileImports)
	{
		if (IFileManager::Get().Copy(*Incoming.DestinationFile, *Incoming.SourceFile, true, true) != 0)
		{
			bSuccess = false;
		}

		SourceControlFiles.Add(Incoming.DestinationFile);
	}

	if (bSuccess)
	{
		// Add the files to source control
		ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
		if (SourceControlProvider.Execute(ISourceControlOperation::Create<FMarkForAdd>(), SourceControlFiles) == ECommandResult::Failed)
		{
			// TODO Error
		}
	}

	Complete(bSuccess);

	return bSuccess;
}

bool FScreenComparisonModel::Replace()
{
	// @todo(agrant): test this

	// Delete all the existing files in this area
	RemoveExistingApproved();

	// Copy files to the approved
	const FString ImportIncomingRoot = Report.GetReportPath();

	TArray<FString> SourceControlFiles;

	for ( const FFileMapping& Incoming : FileImports)
	{
		SourceControlFiles.Add(Incoming.DestinationFile);
	}

	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
	if ( SourceControlProvider.Execute(ISourceControlOperation::Create<FRevert>(), SourceControlFiles) == ECommandResult::Failed )
	{
		//TODO Error
	}

	SourceControlFiles.Reset();

	// Copy the files from the reports location to the destination location
	for (const FFileMapping& Incoming : FileImports)
	{
		IFileManager::Get().Copy(*Incoming.DestinationFile, *Incoming.SourceFile, true, true);
		SourceControlFiles.Add(Incoming.DestinationFile);
	}

	if ( SourceControlProvider.Execute(ISourceControlOperation::Create<FMarkForAdd>(), SourceControlFiles) == ECommandResult::Failed )
	{
		//TODO Error
	}

	if ( SourceControlProvider.Execute(ISourceControlOperation::Create<FCheckOut>(), SourceControlFiles) == ECommandResult::Failed )
	{
		//TODO Error
	}

	Complete(true);

	return true;
}

bool FScreenComparisonModel::RemoveExistingApproved()
{
	FString ApprovedFolder = FPaths::Combine(FPaths::ProjectDir(), Report.GetComparisonResult().ApprovedFilePath);

	TArray<FString> SourceControlFiles;

	bool bSuccess = true;

	IFileManager::Get().FindFilesRecursive(SourceControlFiles, *FPaths::GetPath(ApprovedFolder), TEXT("*.*"), true, false, false);

	// Remove files from source control.
	if (SourceControlFiles.Num())
	{
		ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();

		TArray<FSourceControlStateRef> SourceControlStates;
		ECommandResult::Type Result = SourceControlProvider.GetState(SourceControlFiles, SourceControlStates, EStateCacheUsage::ForceUpdate);
		if (Result == ECommandResult::Succeeded)
		{
			TArray<FString> FilesToRevert;
			TArray<FString> FilesToDelete;

			for (const FSourceControlStateRef& SourceControlState : SourceControlStates)
			{
				// Added files must be reverted.
				if (SourceControlState->IsAdded())
				{
					FilesToRevert.Add(SourceControlState->GetFilename());
				}
				// Edited files must be reverted then deleted.
				else if (SourceControlState->IsCheckedOut())
				{
					FilesToRevert.Add(SourceControlState->GetFilename());
					FilesToDelete.Add(SourceControlState->GetFilename());
				}
				// Other source controlled files must be deleted.
				else if (SourceControlState->IsSourceControlled() && !SourceControlState->IsDeleted())
				{
					FilesToDelete.Add(SourceControlState->GetFilename());
				}
			}

			const bool WasSuccessfullyReverted = FilesToRevert.Num() == 0 || SourceControlProvider.Execute(ISourceControlOperation::Create<FRevert>(), FilesToRevert) == ECommandResult::Succeeded;
			const bool WasSuccessfullyDeleted = FilesToDelete.Num() == 0 || SourceControlProvider.Execute(ISourceControlOperation::Create<FDelete>(), FilesToDelete) == ECommandResult::Succeeded;
			bSuccess = WasSuccessfullyReverted && WasSuccessfullyDeleted;
		}
	}

	return bSuccess;
}

bool FScreenComparisonModel::AddAlternative()
{
	// @todo(agrant): test this

	// Copy files to the approved
	const FString ImportIncomingRoot = Report.GetReportPath();

	TArray<FString> SourceControlFiles;

	for ( const FFileMapping& Import : FileImports )
	{
		SourceControlFiles.Add(Import.DestinationFile);
	}

	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
	if ( SourceControlProvider.Execute(ISourceControlOperation::Create<FRevert>(), SourceControlFiles) == ECommandResult::Failed )
	{
		//TODO Error
	}

	for ( const FFileMapping& Import : FileImports )
	{
		if ( IFileManager::Get().Copy(*Import.DestinationFile, *Import.SourceFile, false, true) == COPY_OK )
		{
			SourceControlFiles.Add(Import.DestinationFile);
		}
		else
		{
			// TODO Error
		}
	}

	if ( SourceControlProvider.Execute(ISourceControlOperation::Create<FMarkForAdd>(), SourceControlFiles) == ECommandResult::Failed )
	{
		//TODO Error
	}
	if ( SourceControlProvider.Execute(ISourceControlOperation::Create<FCheckOut>(), SourceControlFiles) == ECommandResult::Failed )
	{
		//TODO Error
	}

	Complete(true);

	return true;
}
