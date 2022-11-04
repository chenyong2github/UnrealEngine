// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSourceControlReview.h"

#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "SChangelistEditableText.h"
#include "SourceControlOperations.h"
#include "SSourceControlReviewEntry.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Styling/StarshipCoreStyle.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Misc/App.h"
#include "Misc/MessageDialog.h"

#define LOCTEXT_NAMESPACE "SourceControlReview"

namespace ReviewHelpers
{
	const FText EnterChangelistText(LOCTEXT("EnterChangelistText", "Enter a changelist number to diff:"));
	const FText EnterChangelistTooltip(LOCTEXT("EnterChangelistTooltip", "Enter changelist"));
	const FText LoadChangelistText(LOCTEXT("LoadChangelistText", "Load Changelist"));
	const FText LoadingText(LOCTEXT("LoadingText", "Loading..."));
	
	const FString FileDepotKey = TEXT("depotFile");
	const FString FileRevisionKey = TEXT("rev");
	const FString FileActionKey = TEXT("action");
	const FString TimeKey = TEXT("time");
	const FString AuthorKey = TEXT("user");
	const FString DescriptionKey = TEXT("desc");
	const FString ChangelistStatusKey = TEXT("status");
	const FString ChangelistPendingStatusKey = TEXT("pending");
	constexpr int32 RecordIndex = 0;
}

void SSourceControlReview::Construct(const FArguments& InArgs)
{
	const FString ProjectName = FApp::GetProjectName();
	ensureAlwaysMsgf((!ProjectName.IsEmpty()), TEXT("BlueprintReviewTool - Unable to get ProjectName"));
	
	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Panel"))
		.Padding(10.f)
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text(ReviewHelpers::EnterChangelistText)
					.Font(FStyleFonts::Get().LargeBold)
				]
				+SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.Padding(5.f,0.f)
				.AutoWidth()
				[
					SNew(SBorder)
					.ToolTipText(ReviewHelpers::EnterChangelistTooltip)
					[
						SAssignNew(ChangelistNumWidget, SChangelistEditableText)
						.Font(FStyleFonts::Get().Large)
						.MinDesiredWidth(200.f)
						.Justification(ETextJustify::Center)
						.Style(&FAppStyle::Get().GetWidgetStyle<FEditableTextStyle>("NormalEditableText"))
						.OnTextCommitted(this, &SSourceControlReview::OnChangelistNumCommitted)
					]
				]
				+SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.OnClicked(this, &SSourceControlReview::OnLoadChangelistClicked)
					[
						SNew(STextBlock)
						.Text(ReviewHelpers::LoadChangelistText)
						.Font(FStyleFonts::Get().LargeBold)
					]
				]
			]
			+SVerticalBox::Slot()
			.Padding(0.f, 10.f)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.AutoHeight()
			[
				SAssignNew(LoadingTextBlock, STextBlock)
				.Visibility(EVisibility::Collapsed)
				.Text(ReviewHelpers::LoadingText)
				.Font(FStyleFonts::Get().Large)
			]
			+SVerticalBox::Slot()
			.Padding(0.f, 15.f)
			.AutoHeight()
			[
				SAssignNew(LoadingProgressBar, SProgressBar)
				.Visibility(EVisibility::Collapsed)
				.Percent(1.f)
			]
			+SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 20.f)
			[
				GetChangelistInfoWidget()
			]
			+SVerticalBox::Slot()
			[
				SNew(SScrollBox)
				+SScrollBox::Slot()
				[
					SAssignNew(ChangelistEntriesWidget, SVerticalBox)
				]
			]
		]
	];
}

TSharedRef<SWidget> SSourceControlReview::GetChangelistInfoWidget()
{
	auto GenerateRow = [](FText Key, const TAttribute<FText>& Value)->TSharedRef<SWidget>
	{
		const TSharedRef<SWidget> KeyWidget = SNew(STextBlock)
			.Text(FText::Format(LOCTEXT("ChangelistInfoKey", "{0}:"), Key))
			.Font(FStyleFonts::Get().Large);

		const TSharedRef<SWidget> ValueWidget = SNew(SBox)
			.MaxDesiredHeight(125.f)
			[
				SNew(SScrollBox)
				+SScrollBox::Slot()
				[
					SNew(SMultiLineEditableTextBox)
					.Text(Value)
					.IsReadOnly(true)
					.Font(FStyleFonts::Get().Normal)
					.BackgroundColor(FLinearColor(0,0,0,0))
				]
			];

		return SNew(SWidgetSwitcher)
		.WidgetIndex_Lambda(
			[Value]{
				const FString ResolvedValue = Value.Get().ToString();
				int32 Index;
				return ResolvedValue.FindChar('\n', Index);
			}
		)
		+SWidgetSwitcher::Slot()
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				KeyWidget
			]
			+SHorizontalBox::Slot()
			[
				ValueWidget
			]
		]
		+SWidgetSwitcher::Slot()
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			[
				KeyWidget
			]
			+SHorizontalBox::Slot()
			[
				ValueWidget
			]
		];
	};
	
	return SAssignNew(ChangelistInfoWidget, SVerticalBox)
	.Visibility(EVisibility::Collapsed)
	+SVerticalBox::Slot()
	.AutoHeight()
	[
		GenerateRow(LOCTEXT("ChangelistAuthor", "Author"), TAttribute<FText>::CreateLambda(
			[this]{return CurrentChangelistInfo.Author;}))
	]
	+SVerticalBox::Slot()
	.AutoHeight()
	[
		GenerateRow(LOCTEXT("ChangelistPath", "Path"), TAttribute<FText>::CreateLambda(
			[this]{return CurrentChangelistInfo.SharedPath;}))
	]
	+SVerticalBox::Slot()
	.AutoHeight()
	[
		GenerateRow(LOCTEXT("ChangelistStatus", "Status"), TAttribute<FText>::CreateLambda(
			[this]{return CurrentChangelistInfo.Status;}))
	]
	+SVerticalBox::Slot()
	.AutoHeight()
	[
		GenerateRow(LOCTEXT("ChangelistDescription", "Description"), TAttribute<FText>::CreateLambda(
			[this]{return CurrentChangelistInfo.Description;}))
	];
}

void SSourceControlReview::LoadChangelist(const FString& Changelist)
{
	ChangelistEntriesWidget->ClearChildren();
	
	if (!IsLoading())
	{
		SetLoading(true);
	}
	else
	{
		FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("ReviewChangelistTool", "ChangelistError", "Changelist is already loading"));
		return;
	}

	ChangelistFileDataMap.Empty();

	//This command runs p4 -describe (or similar for other version controls) to retrieve changelist record information
	const TSharedRef<FGetChangelistDetails> GetChangelistDetailsCommand = ISourceControlOperation::Create<FGetChangelistDetails>();
	GetChangelistDetailsCommand->SetChangelistNumber(Changelist);

	ISourceControlModule::Get().GetProvider().Execute(
		GetChangelistDetailsCommand,
		EConcurrency::Asynchronous,
		FSourceControlOperationComplete::CreateRaw(this, &SSourceControlReview::OnChangelistLoadComplete, Changelist));
}

void SSourceControlReview::OnChangelistLoadComplete(const FSourceControlOperationRef& InOperation, ECommandResult::Type InResult, FString Changelist)
{
	const TSharedRef<FGetChangelistDetails, ESPMode::ThreadSafe> Operation = StaticCastSharedRef<FGetChangelistDetails>(InOperation);
	const TArray<TMap<FString, FString>>& Record = Operation->GetChangelistDetails();
	
	if (!IsChangelistRecordValid(Record))
	{
		SetLoading(false);
		return;
	}

	const TMap<FString, FString>& ChangelistRecord = Record[ReviewHelpers::RecordIndex];
	
	//Num files we are going to expect retrieved from source control
	FilesToLoad = 0;
	//Num files we've loaded so far
	FilesLoaded = 0;

	// TODO: @jordan.hoffmann revise for other version controls
	//Each file in p4 has an index "depotFile0" this is the index that is updated by the while loop to find all the files "depotFile1", "depotFile2" etc...
	uint32  RecordFileIndex = 0;
	//String representation of the current file index
	FString RecordFileIndexStr = LexToString(RecordFileIndex);
	//The p4 records is the map a file key starts with "depotFile" and is followed by file index 
	FString RecordFileMapKey = ReviewHelpers::FileDepotKey + RecordFileIndexStr;
	//The p4 records is the map a revision key starts with "rev" and is followed by file index 
	FString RecordRevisionMapKey = ReviewHelpers::FileRevisionKey + RecordFileIndexStr;
	//The p4 records is the map a revision key starts with "action" and is followed by file index 
	FString RecordActionMapKey = ReviewHelpers::FileActionKey + RecordFileIndexStr;

	SetChangelistInfo(ChangelistRecord);
	
	//the loop checks if we have a valid record "depotFile(Index)" in the records to add a file entry
	while (ChangelistRecord.Contains(RecordFileMapKey) && ChangelistRecord.Contains(RecordRevisionMapKey))
	{
		const FString &FileDepotPath = ChangelistRecord[RecordFileMapKey];
		const FString AssetName = FPaths::GetBaseFilename(FileDepotPath, true);
		
		//For each 1 file we are loading 2 revisions so files to load is always incremented by two per file
		FilesToLoad++; 

		const bool bIsShelved = ChangelistRecord[ReviewHelpers::ChangelistStatusKey] == ReviewHelpers::ChangelistPendingStatusKey;
		const int32 AssetRevision = FCString::Atoi(*ChangelistRecord[RecordRevisionMapKey]);
	
		TSharedPtr<FChangelistFileData> ChangelistFileData = MakeShared<FChangelistFileData>(AssetName, TEXT(""), ChangelistRecord[RecordRevisionMapKey], TEXT(""), TEXT(""));

		ChangelistFileData->ReviewFileDateTime = FDateTime(1970, 1, 1, 0, 0, 0, 0) + FTimespan::FromSeconds(FCString::Atoi(*ChangelistRecord[ReviewHelpers::TimeKey]));
		ChangelistFileData->ChangelistNum = FCString::Atoi(*Changelist);

		//Determine if we are dealing with submitted or pending changelist
		ChangelistFileData->ChangelistState = bIsShelved ? EChangelistState::Pending : EChangelistState::Submitted;

		//Building absolute local path is needed to use local file to retrieve file history information from p4 to then show file revision data
		ChangelistFileData->AssetFilePath = AsAssetPath(ChangelistRecord[RecordFileMapKey]);
		ChangelistFileData->RelativeFilePath = TrimSharedPath(ChangelistRecord[RecordFileMapKey]);

		SetFileSourceControlAction(ChangelistFileData, ChangelistRecord[RecordActionMapKey]);

		const int32 PreviousAssetRevision = bIsShelved && (ChangelistFileData->FileSourceControlAction == ESourceControlAction::Delete || ChangelistFileData->FileSourceControlAction == ESourceControlAction::Edit) ? AssetRevision : AssetRevision - 1;
		const FString PreviousAssetRevisionStr = FString::FromInt(PreviousAssetRevision);
		
		// retrieve files directly from source control into a temp location
		TSharedRef<FGetFile> GetFileToReviewCommand = ISourceControlOperation::Create<FGetFile>(Changelist, ChangelistRecord[RecordRevisionMapKey], ChangelistRecord[RecordFileMapKey], bIsShelved);

		TWeakPtr<SSourceControlReview> WeakReviewWidget = SharedThis(this);
		const auto GetFileCommandResponse = [WeakReviewWidget, ChangelistFileData](const FSourceControlOperationRef& InOperation, ECommandResult::Type InResult, FString* OutFileName)
		{
			if (WeakReviewWidget.IsValid())
			{
				const TSharedRef<FGetFile, ESPMode::ThreadSafe> Operation = StaticCastSharedRef<FGetFile>(InOperation);
				*OutFileName = Operation->GetOutPackageFilename();
				WeakReviewWidget.Pin()->OnGetFileFromSourceControl(ChangelistFileData);
			}
		};
		
		ISourceControlModule::Get().GetProvider().Execute(GetFileToReviewCommand, EConcurrency::Asynchronous, FSourceControlOperationComplete::CreateLambda(GetFileCommandResponse, &ChangelistFileData->ReviewFileName));

		if (ChangelistFileData->FileSourceControlAction != ESourceControlAction::Add)
		{
			FilesToLoad++;

			TSharedRef<FGetFile> GetPreviousFileCommand = ISourceControlOperation::Create<FGetFile>(Changelist, PreviousAssetRevisionStr, ChangelistRecord[RecordFileMapKey], false);

			ISourceControlModule::Get().GetProvider().Execute(GetPreviousFileCommand, EConcurrency::Asynchronous, FSourceControlOperationComplete::CreateLambda(GetFileCommandResponse, &ChangelistFileData->PreviousFileName));
		}

		//Update map keys with the next file index
		//When we are doing looking at for example "depotFile0" we build our next key "depotFile1"
		RecordFileIndex++;
		RecordFileIndexStr = LexToString(RecordFileIndex);
		RecordFileMapKey = ReviewHelpers::FileDepotKey + RecordFileIndexStr;
		RecordRevisionMapKey = ReviewHelpers::FileRevisionKey + RecordFileIndexStr;
		RecordActionMapKey = ReviewHelpers::FileActionKey + RecordFileIndexStr;
	}

	//If we have no files to load flip loading bar visibility
	if (FilesToLoad == 0)
	{
		SetLoading(false);
	}
}

FReply SSourceControlReview::OnLoadChangelistClicked()
{
	LoadChangelist(ChangelistNumWidget->GetText().ToString());
	return FReply::Handled();
}

void SSourceControlReview::OnChangelistNumCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter)
	{
		LoadChangelist(Text.ToString());
	}
}

void SSourceControlReview::OnGetFileFromSourceControl(TSharedPtr<FChangelistFileData> ChangelistFileData)
{
	if (ChangelistFileData && ChangelistFileData->IsDataValidForEntry())
	{
		ChangelistFileDataMap.Add(ChangelistFileData->ReviewFileName, ChangelistFileData);
	}

	FilesLoaded++;

	LoadingProgressBar->SetPercent(FilesToLoad ? static_cast<float>(FilesLoaded) / static_cast<float>(FilesToLoad) : 1.f);

	if (FilesToLoad == FilesLoaded)
	{
		const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>( TEXT( "AssetRegistry" ) );

		TArray<TSharedPtr<FChangelistFileData>> ChangelistFilesData;
		ChangelistFileDataMap.GenerateValueArray(ChangelistFilesData);
		Algo::Sort(ChangelistFilesData, [](const TSharedPtr<FChangelistFileData> &A, const TSharedPtr<FChangelistFileData> &B)
		{
			return A->RelativeFilePath < B->RelativeFilePath;
		});
		
		TArray<FString> ChangelistFilePaths;
		for (const TSharedPtr<FChangelistFileData>& FileData : ChangelistFilesData)
		{
			if(FileData)
			{
				ChangelistFilePaths.Add(FileData->ReviewFileName);
			}
		}
		
		AssetRegistryModule.Get().ScanFilesSynchronous(ChangelistFilePaths);
		
		for(const TSharedPtr<FChangelistFileData>& FileData : ChangelistFilesData)
		{
			AddDiffFile(*FileData);
		}
		SetLoading(false);
	}
}

void SSourceControlReview::SetLoading(bool bInLoading)
{
	bChangelistLoading = bInLoading;
	
	// show loading bar and text if we're loading
	LoadingProgressBar->SetVisibility(bInLoading? EVisibility::Visible : EVisibility::Collapsed);
	LoadingTextBlock->SetVisibility(bInLoading? EVisibility::Visible : EVisibility::Collapsed);

	// hide CL info if we're loading or there's no changelist
	ChangelistInfoWidget->SetVisibility((ChangelistNumWidget->GetText().IsEmpty() || bInLoading)? EVisibility::Collapsed : EVisibility::Visible);
}

bool SSourceControlReview::IsLoading() const
{
	return bChangelistLoading;
}

bool SSourceControlReview::IsChangelistRecordValid(const TArray<TMap<FString, FString>>& InRecord) const
{
	if (InRecord.IsEmpty())
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("ChangelistNotFoundError", "No record found for this changelist"));
		return false;
	}
	if (InRecord.Num() > 1)
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("ChangelistInvalidResponseFormat", "Invalid API response from Source Control"));
		return false;
	}

	const TMap<FString, FString>& RecordMap = InRecord[ReviewHelpers::RecordIndex];
	if (!RecordMap.Contains(ReviewHelpers::ChangelistStatusKey))
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("ChangelistMissingStatus", "Changelist is missing status information"));
		return false;
	}
	if (!RecordMap.Contains(ReviewHelpers::AuthorKey))
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("ChangelistMissingAuthor", "Changelist is missing author information"));
		return false;
	}
	if (!RecordMap.Contains(ReviewHelpers::DescriptionKey))
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("ChangelistMissingDescription", "Changelist is missing description information"));
		return false;
	}
	if (!RecordMap.Contains(ReviewHelpers::TimeKey))
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("ChangelistMissingDate", "Changelist is missing date information"));
		return false;
	}

	return true;
}

void SSourceControlReview::SetFileSourceControlAction(TSharedPtr<FChangelistFileData> ChangelistFileData, const FString& SourceControlAction)
{
	if (!ChangelistFileData.IsValid())
	{
		UE_LOG(LogSourceControl, Error, TEXT("Unable to set source control action information. No changelist file data"));
		return;
	}

	if (SourceControlAction == FString(TEXT("add")))
	{
		ChangelistFileData->FileSourceControlAction = ESourceControlAction::Add;
	}
	else if (SourceControlAction == FString(TEXT("edit")))
	{
		ChangelistFileData->FileSourceControlAction = ESourceControlAction::Edit;
	}
	else if (SourceControlAction == FString(TEXT("delete")))
	{
		ChangelistFileData->FileSourceControlAction = ESourceControlAction::Delete;
	}
	else if (SourceControlAction == FString(TEXT("branch")))
	{
		ChangelistFileData->FileSourceControlAction = ESourceControlAction::Branch;
	}
	else if (SourceControlAction == FString(TEXT("integrate")))
	{
		ChangelistFileData->FileSourceControlAction = ESourceControlAction::Integrate;
	}
	else
	{
		UE_LOG(LogSourceControl, Error, TEXT("Unable to parse source control action information. '%s' diff will not be shown"), *ChangelistFileData->AssetName);
		ChangelistFileData->FileSourceControlAction = ESourceControlAction::Unset;
	}
}

static FString GetSharedBranchPath(const TMap<FString, FString>& InChangelistRecord)
{
	FString SharedBranchPath;
	if (const FString* Found = InChangelistRecord.Find(ReviewHelpers::FileDepotKey + "0"))
	{
		SharedBranchPath = *Found;
	}
	else
	{
		return FString();
	}

	// TODO: @jordan.hoffmann: Revise for other source controls
	//Each file in p4 has an index "depotFile0" this is the index that is updated by the while loop to find all the files "depotFile1", "depotFile2" etc...
	uint32  RecordFileIndex = 1;
	//The p4 records is the map a file key starts with "depotFile" and is followed by file index 
	FString RecordFileMapKey = ReviewHelpers::FileDepotKey + LexToString(RecordFileIndex);
	while (const FString* Found = InChangelistRecord.Find(RecordFileMapKey))
	{
		// find starting from the left, find the portion that's shared between both strings
		int32 TrimIndex = 0;
		const int32 MaxTrimIndex = FMath::Min(Found->Len(), SharedBranchPath.Len());
		for(; TrimIndex < MaxTrimIndex; ++TrimIndex)
		{
			if (SharedBranchPath[TrimIndex] != (*Found)[TrimIndex])
			{
				break;
			}
		}
		// shorten SharedBranchPath to only contain the shared portion
		SharedBranchPath = SharedBranchPath.Left(TrimIndex);

		// increment to next file path
		RecordFileMapKey = ReviewHelpers::FileDepotKey + LexToString(++RecordFileIndex);
	}
	return SharedBranchPath;
}

void SSourceControlReview::SetChangelistInfo(const TMap<FString, FString>& InChangelistRecord)
{
	CurrentChangelistInfo.Author = FText::FromString(InChangelistRecord[ReviewHelpers::AuthorKey]);
	CurrentChangelistInfo.Description = FText::FromString(InChangelistRecord[ReviewHelpers::DescriptionKey]);
	CurrentChangelistInfo.Status = FText::FromString(InChangelistRecord[ReviewHelpers::ChangelistStatusKey]);
	CurrentChangelistInfo.SharedPath = FText::FromString(GetSharedBranchPath(InChangelistRecord));
}

void SSourceControlReview::AddDiffFile(const FChangelistFileData& FileData) const
{
	ChangelistEntriesWidget->AddSlot()
	.Padding(0.f, 5.f)
	[
		SNew(SSourceControlReviewEntry)
		.FileData(FileData)
	];
}

FString SSourceControlReview::TrimSharedPath(FString FullCLPath) const
{
	FullCLPath.RemoveFromStart(*CurrentChangelistInfo.SharedPath.ToString());
	return FullCLPath;
}

FString SSourceControlReview::AsAssetPath(const FString& FullCLPath)
{
	const FString ProjectName = FApp::GetProjectName();
	return UKismetSystemLibrary::GetProjectDirectory() / FullCLPath.RightChop(FullCLPath.Find(ProjectName) + ProjectName.Len());
}

#undef LOCTEXT_NAMESPACE
