// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSourceControlReview.h"

#include "AssetRegistry/AssetData.h"
#include "HAL/FileManager.h"
#include "ClassIconFinder.h"
#include "Framework/Views/TableViewMetadata.h"
#include "ISourceControlModule.h"
#include "Misc/PackageName.h"
#include "SourceControlOperations.h"
#include "Misc/Paths.h"
#include "SSourceControlReviewEntry.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Styling/StarshipCoreStyle.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Input/SButton.h"
#include "Misc/App.h"
#include "Misc/MessageDialog.h"
#include "Styling/StyleColors.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Internationalization/Regex.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Input/SComboBox.h"

#define LOCTEXT_NAMESPACE "SourceControlReview"

using namespace SourceControlReview;

namespace ReviewHelpers
{
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

const UClass* FChangelistFileData::GetIconClass()
{
	// If we haven't cached the icon class yet, find it
	if (!CachedIconClass.IsSet())
	{
		CachedIconClass = nullptr;
		const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		FString TempPackageName;
		if (FPackageName::TryConvertFilenameToLongPackageName(ReviewFileName, TempPackageName))
		{
			TArray<FAssetData> OutAssetData;
			AssetRegistryModule.Get().GetAssetsByPackageName(*TempPackageName, OutAssetData);

			if (OutAssetData.Num() > 0)
			{
				CachedIconClass = FClassIconFinder::GetIconClassForAssetData(OutAssetData[0]);
			}
		}
	}

	return CachedIconClass.GetValue();
}

void SSourceControlReview::Construct(const FArguments& InArgs)
{
	const FString ProjectName = FApp::GetProjectName();
	ensureAlwaysMsgf((!ProjectName.IsEmpty()), TEXT("BlueprintReviewTool - Unable to get ProjectName"));

	const static FSlateRoundedBoxBrush RecessedBrush(FStyleColors::Recessed, CoreStyleConstants::InputFocusRadius);
	const static FEditableTextBoxStyle InfoWidgetStyle =
		FEditableTextBoxStyle(FAppStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox"))
		.SetBackgroundImageNormal(RecessedBrush)
		.SetBackgroundImageHovered(RecessedBrush)
		.SetBackgroundImageFocused(RecessedBrush)
		.SetBackgroundImageReadOnly(RecessedBrush);

	const static FMargin InfoWidgetMargin(4.f, 2.f, 4.f, 8.f);

	LoadCLHistory();
	
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
				SAssignNew(ChangelistInfoWidget, SGridPanel)
				.FillColumn(1, 1.f)

				// Changelist
				+SGridPanel::Slot(0,0)
				.Padding(InfoWidgetMargin)
				.HAlign(HAlign_Right)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ChangelistNumber", "Changelist"))
					.Font(FStyleFonts::Get().Normal)
				]
				+SGridPanel::Slot(1,0)
				.Padding(4.f, 0.f, 4.f, 8.f)
				.HAlign(HAlign_Left)
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.Padding(0.f, 0.f, 8.f, 0.f)
					.AutoWidth()
					[
						SAssignNew(ChangelistNumComboBox, SComboBox<TSharedPtr<FChangelistLightInfo>>)
						.OptionsSource(&CLHistory)
						.OnGenerateWidget(this, &SSourceControlReview::MakeCLComboOption)
						.OnSelectionChanged(this, &SSourceControlReview::OnCLComboSelection)
						.ContentPadding(0.f)
						.Content()
						[
							SAssignNew(ChangelistNumText, SEditableText)
                     		.Font(FStyleFonts::Get().Normal)
                     		.MinDesiredWidth(55.f)
                     		.Justification(ETextJustify::Center)
                     		.OnTextCommitted(this, &SSourceControlReview::OnChangelistNumCommitted)
                     		.OnTextChanged(this, &SSourceControlReview::OnChangelistNumChanged)
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
							.Text(LOCTEXT("LoadChangelistText", "Load"))
							.Font(FStyleFonts::Get().Normal)
						]
					]
				]

				// Author
				+SGridPanel::Slot(0,1)
				.Padding(InfoWidgetMargin)
				.HAlign(HAlign_Right)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ChangelistAuthor", "Author"))
					.Font(FStyleFonts::Get().Normal)
				]
				+SGridPanel::Slot(1,1)
				.Padding(4.f, 0.f, 4.f, 8.f)
				.HAlign(HAlign_Fill)
				[
					SNew(SEditableTextBox)
					.Text_Lambda([this]{return CurrentChangelistInfo.Author;})
					.IsReadOnly(true)
					.Style(&InfoWidgetStyle)
				]

				// Path
				+SGridPanel::Slot(0,2)
				.Padding(InfoWidgetMargin)
				.HAlign(HAlign_Right)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ChangelistPath", "Path"))
					.Font(FStyleFonts::Get().Normal)
				]
				+SGridPanel::Slot(1,2)
				.Padding(4.f, 0.f, 4.f, 8.f)
				.HAlign(HAlign_Fill)
				[
					SNew(SEditableTextBox)
					.Text_Lambda([this]{return CurrentChangelistInfo.SharedPath;})
					.IsReadOnly(true)
					.Style(&InfoWidgetStyle)
				]

				// Status
				+SGridPanel::Slot(0,3)
				.Padding(InfoWidgetMargin)
				.HAlign(HAlign_Right)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ChangelistStatus", "Status"))
					.Font(FStyleFonts::Get().Normal)
				]
				+SGridPanel::Slot(1,3)
				.Padding(4.f, 0.f, 4.f, 8.f)
				.HAlign(HAlign_Fill)
				[
					SNew(SEditableTextBox)
					.Text_Lambda([this]{return CurrentChangelistInfo.Status;})
					.IsReadOnly(true)
					.Style(&InfoWidgetStyle)
				]

				// Description
				+SGridPanel::Slot(0,4)
				.Padding(InfoWidgetMargin)
				.HAlign(HAlign_Right)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ChangelistDescription", "Description"))
					.Font(FStyleFonts::Get().Normal)
				]
				+SGridPanel::Slot(1,4)
				.Padding(InfoWidgetMargin)
				.HAlign(HAlign_Fill)
				[
					SNew(SBox)
					.MaxDesiredHeight(147.f)
					.MinDesiredHeight(147.f)
					[
						SNew(SMultiLineEditableTextBox)
						.Text_Lambda([this]{return CurrentChangelistInfo.Description;})
						.AutoWrapText(true)
						.IsReadOnly(true)
						.Style(&InfoWidgetStyle)
					]
				]
			]
			
			+SVerticalBox::Slot()
			.VAlign(VAlign_Top)
			[
				SAssignNew(ChangelistEntriesWidget, SListView<TSharedPtr<FChangelistFileData>>)
				.ListItemsSource(&ChangelistFiles)
				.OnGenerateRow(this, &SSourceControlReview::OnGenerateFileRow)
				.HeaderRow(
					SNew(SHeaderRow)
					+HeaderColumn(ColumnIds::Status)
					+HeaderColumn(ColumnIds::File)
					+HeaderColumn(ColumnIds::Tools)
				)
			]
			
			+SVerticalBox::Slot()
			.Padding(0.f, 0.f, 0.f, 98.f)
			.HAlign(HAlign_Center)
			.AutoHeight()
			[
				SAssignNew(EnterChangelistTextBlock, STextBlock)
				.Visibility(EVisibility::Visible)
				.ColorAndOpacity(FStyleColors::AccentGray)
				.Text(LOCTEXT("EnterChangelistText", "Enter a Changelist number above to search"))
			]
			
			+SVerticalBox::Slot()
			.Padding(0.f, 10.f)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Bottom)
			.AutoHeight()
			[
				SAssignNew(LoadingTextBlock, STextBlock)
				.Visibility(EVisibility::Collapsed)
				.Text(LOCTEXT("LoadingText", "Loading..."))
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
		]
	];
}

SSourceControlReview::~SSourceControlReview()
{
	if (GetChangelistDetailsCommand)
	{
		ISourceControlModule::Get().GetProvider().CancelOperation(GetChangelistDetailsCommand.ToSharedRef());
		GetChangelistDetailsCommand.Reset();
	}
}

void SSourceControlReview::LoadChangelist(const FString& Changelist)
{
	if (!IsLoading())
	{
		SetLoading(true);
	}
	else
	{
		FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("ReviewChangelistTool", "ChangelistError", "Changelist is already loading"));
		return;
	}

	for(int32 I = 1; I < CLHistory.Num(); ++I)
	{
		if (CLHistory[I]->Number.EqualTo(CLHistory[0]->Number))
		{
			CLHistory.RemoveAt(I);
			break;
		}
	}
	bUncommittedChangelistNum = false;
	ChangelistFiles.Empty();
	RedirectorsFound.Empty();

	//This command runs p4 -describe (or similar for other version controls) to retrieve changelist record information
	GetChangelistDetailsCommand = ISourceControlOperation::Create<FGetChangelistDetails>();
	GetChangelistDetailsCommand->SetChangelistNumber(Changelist);

	ISourceControlModule::Get().GetProvider().Execute(
		GetChangelistDetailsCommand.ToSharedRef(),
		EConcurrency::Asynchronous,
		FSourceControlOperationComplete::CreateRaw(this, &SSourceControlReview::OnChangelistLoadComplete, Changelist));
}

void SSourceControlReview::OnChangelistLoadComplete(const FSourceControlOperationRef& InOperation, ECommandResult::Type InResult, FString Changelist)
{
	// this command is cancelled when this widget is destroyed. Exit immediately to avoid touching invalid data
	if (InResult == ECommandResult::Cancelled)
	{
		return;
	}
	
	const TSharedRef<FGetChangelistDetails, ESPMode::ThreadSafe> Operation = StaticCastSharedRef<FGetChangelistDetails>(InOperation);
	const TArray<TMap<FString, FString>>& Record = Operation->GetChangelistDetails();
	
	if (!IsChangelistRecordValid(Record))
	{
		CLHistory.RemoveAt(0); // don't save history of invalid changelists
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
	if (CLHistory[0]->Author.IsEmpty())
	{
		// annoyingly the options list doesn't visually update unless the shared pointer is different
		CLHistory[0] = MakeShared<FChangelistLightInfo>(CLHistory[0]->Number, CurrentChangelistInfo.Author, CurrentChangelistInfo.Description);
	}
	ChangelistNumComboBox->RefreshOptions();
	SaveCLHistory();
	
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
	LoadChangelist(ChangelistNumText->GetText().ToString());
	return FReply::Handled();
}

void SSourceControlReview::OnChangelistNumChanged(const FText& Text)
{
	const FString& Data = Text.ToString();

	// Use the longest substring that consists of only valid characters.
	// For example if someone enters:
	// "john.doe2/CL_123456789/version_13", we'll use "123456789" because it's longer than "2" and "13"
	int32 BestMatchBegin = 0;
	int32 BestMatchLen = 0;
	
	FRegexMatcher RegexMatcher(FRegexPattern(TEXT("\\d+")), Data);
	while(RegexMatcher.FindNext())
	{
		const int32 MatchBegin = RegexMatcher.GetMatchBeginning();
		const int32 MatchLen = RegexMatcher.GetMatchEnding() - MatchBegin;
		
		if (MatchLen > BestMatchLen)
		{
			BestMatchBegin = MatchBegin;
			BestMatchLen = MatchLen;
		}
	}
	
	const FText ValidText = FText::FromString(Data.Mid(BestMatchBegin, BestMatchLen));
	ChangelistNumText->SetText(ValidText);

	const TSharedPtr<FChangelistLightInfo> ChangelistLightInfo = MakeShared<FChangelistLightInfo>(ValidText);
	if (!bUncommittedChangelistNum || CLHistory.IsEmpty())
	{
		bUncommittedChangelistNum = true;
		if (CLHistory.Num() > 5)
		{
			CLHistory.Pop();
		}
		CLHistory.Insert(ChangelistLightInfo, 0);
	}
	else
	{
		CLHistory[0] = ChangelistLightInfo;
	}
	ChangelistNumComboBox->RefreshOptions();
	if (ChangelistNumComboBox->GetSelectedItem() != CLHistory[0])
	{
		ChangelistNumComboBox->SetSelectedItem(CLHistory[0]);
	}
}

void SSourceControlReview::OnChangelistNumCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter)
	{
		LoadChangelist(Text.ToString());
	}
}

TSharedRef<SWidget> SSourceControlReview::MakeCLComboOption(TSharedPtr<FChangelistLightInfo> Item) const
{
	FText Text;
	if (!Item->Author.IsEmpty())
	{
		Text = FText::Format(LOCTEXT("CLComboOption", "{0} - {1}"), Item->Number, Item->Author);
	}
	else
	{
		Text =Item->Number;
	}
	
	
	return SNew(STextBlock)
		.Text(Text)
		.ToolTipText(Item->Description);
}

void SSourceControlReview::OnCLComboSelection(TSharedPtr<FChangelistLightInfo> Item, ESelectInfo::Type SelectInfo) const
{
	if (Item.IsValid() && SelectInfo != ESelectInfo::Direct)
	{
		ChangelistNumText->SetText(Item->Number);
	}
}

void SSourceControlReview::SaveCLHistory()
{
	TArray<FString> Numbers;
	TArray<FString> Authors;
	TArray<FString> Descriptions;
	for(const TSharedPtr<FChangelistLightInfo>& Item : CLHistory)
	{
		Numbers.Add(Item->Number.ToString());
		Authors.Add(Item->Author.ToString());
		Descriptions.Add(Item->Description.ToString());
	}
	GConfig->SetArray(TEXT("SourceControlReview"), TEXT("CLHistory.Numbers"), Numbers, GEngineIni);
	GConfig->SetArray(TEXT("SourceControlReview"), TEXT("CLHistory.Authors"), Authors, GEngineIni);
	GConfig->SetArray(TEXT("SourceControlReview"), TEXT("CLHistory.Descriptions"), Descriptions, GEngineIni);
}

void SSourceControlReview::LoadCLHistory()
{
	TArray<FString> Numbers;
	TArray<FString> Authors;
	TArray<FString> Descriptions;
	GConfig->GetArray(TEXT("SourceControlReview"), TEXT("CLHistory.Numbers"), Numbers, GEngineIni);
	GConfig->GetArray(TEXT("SourceControlReview"), TEXT("CLHistory.Authors"), Authors, GEngineIni);
	GConfig->GetArray(TEXT("SourceControlReview"), TEXT("CLHistory.Descriptions"), Descriptions, GEngineIni);
	CLHistory.Empty();
	for(int32 I = 0; I < Numbers.Num(); ++I)
	{
		CLHistory.Add(MakeShared<FChangelistLightInfo>(
			FText::FromString(Numbers[I]),
			FText::FromString(Authors[I]),
			FText::FromString(Descriptions[I])
		));
	}
}

void SSourceControlReview::OnGetFileFromSourceControl(TSharedPtr<FChangelistFileData> ChangelistFileData)
{
	if (ChangelistFileData && ChangelistFileData->IsDataValidForEntry())
	{
		ChangelistFiles.Add(ChangelistFileData);
	}

	FilesLoaded++;

	LoadingProgressBar->SetPercent(FilesToLoad ? static_cast<float>(FilesLoaded) / static_cast<float>(FilesToLoad) : 1.f);

	if (UPackage* ReviewFilePkg = LoadPackage(nullptr, *ChangelistFileData->ReviewFileName, LOAD_ForDiff | LOAD_DisableCompileOnLoad | LOAD_DisableEngineVersionChecks))
	{
		const UObject* ReviewAsset = FindObject<UObject>(ReviewFilePkg, *ChangelistFileData->AssetName);
		if(ReviewAsset && ReviewAsset->IsA<UObjectRedirector>())
		{
			const UPackage* RedirectedPackage = Cast<UObjectRedirector>(ReviewAsset)->DestinationObject->GetPackage();
			const FString RedirectPath = IFileManager::Get().ConvertToRelativePath(*RedirectedPackage->GetLoadedPath().GetLocalFullPath());
			RedirectorsFound.Add(RedirectPath, ChangelistFileData);
		}
	}

	if (FilesToLoad == FilesLoaded)
	{
		const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>( TEXT( "AssetRegistry" ) );

		// Patch up redirectors such that the renamed assets only use a single diff entry
		ChangelistFiles.RemoveAllSwap([&RedirectorsFound = RedirectorsFound](const TSharedPtr<FChangelistFileData> &ChangelistFile)
		{
			if (ChangelistFile->FileSourceControlAction == ESourceControlAction::Add)
			{
				const FString RelativePath = IFileManager::Get().ConvertToRelativePath(*ChangelistFile->AssetFilePath);
				if (const TWeakPtr<FChangelistFileData>* Found = RedirectorsFound.Find(RelativePath))
				{
					if (const TSharedPtr<FChangelistFileData>& MergedRenameEntry = Found->Pin())
					{
						MergedRenameEntry->ReviewFileName = ChangelistFile->ReviewFileName;
						MergedRenameEntry->PreviousAssetName = MergedRenameEntry->AssetName;
						MergedRenameEntry->AssetName = ChangelistFile->AssetName;
						MergedRenameEntry->RelativeFilePath = ChangelistFile->RelativeFilePath;
						MergedRenameEntry->AssetFilePath = ChangelistFile->AssetFilePath;
						MergedRenameEntry->PreviousFileRevisionNum = MergedRenameEntry->ReviewFileRevisionNum;
						MergedRenameEntry->ReviewFileRevisionNum = ChangelistFile->ReviewFileRevisionNum;
						return true;
					}
				}
			}
			return false;
		});
		
		Algo::Sort(ChangelistFiles, [](const TSharedPtr<FChangelistFileData> &A, const TSharedPtr<FChangelistFileData> &B)
		{
			return A->RelativeFilePath < B->RelativeFilePath;
		});
		
		TArray<FString> ChangelistFilePaths;
		for (const TSharedPtr<FChangelistFileData>& FileData : ChangelistFiles)
		{
			if(FileData)
			{
				ChangelistFilePaths.Add(FileData->ReviewFileName);
			}
		}
		
		AssetRegistryModule.Get().ScanFilesSynchronous(ChangelistFilePaths);

		// now that the files are in the asset registry, cache their associated class so their class icons can be created quickly
		for (const TSharedPtr<FChangelistFileData>& FileData : ChangelistFiles)
		{
			if(FileData)
			{
				FileData->GetIconClass();
			}
		}
		
		ChangelistEntriesWidget->RebuildList();
		SetLoading(false);
	}
}

void SSourceControlReview::SetLoading(bool bInLoading)
{
	bChangelistLoading = bInLoading;
	
	// show loading bar and text if we're loading
	LoadingProgressBar->SetVisibility(bInLoading? EVisibility::Visible : EVisibility::Collapsed);
	LoadingTextBlock->SetVisibility(bInLoading? EVisibility::Visible : EVisibility::Collapsed);
	if (bInLoading)
	{
		EnterChangelistTextBlock->SetVisibility(EVisibility::Collapsed);
	}
	else if(ChangelistFiles.IsEmpty())
	{
		EnterChangelistTextBlock->SetVisibility(EVisibility::Visible);
	}
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
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("ChangelistInvalidResponseFormat", "Invalid API response from Revision Control"));
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
		UE_LOG(LogSourceControl, Error, TEXT("Unable to set revision control action information. No changelist file data"));
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
	else if (SourceControlAction == FString(TEXT("move/delete")))
	{
		ChangelistFileData->FileSourceControlAction = ESourceControlAction::Delete;
	}
	else if (SourceControlAction == FString(TEXT("move/add")))
	{
		ChangelistFileData->FileSourceControlAction = ESourceControlAction::Add;
	}
	else
	{
		UE_LOG(LogSourceControl, Error, TEXT("Unable to parse revision control action information. '%s' diff will not be shown"), *ChangelistFileData->AssetName);
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

TSharedRef<ITableRow> SSourceControlReview::OnGenerateFileRow(TSharedPtr<FChangelistFileData> FileData, const TSharedRef<STableViewBase>& Table) const
{
	return SNew(SSourceControlReviewEntry, Table)
	.FileData(*FileData);
}

FString SSourceControlReview::TrimSharedPath(FString FullCLPath) const
{
	FullCLPath.RemoveFromStart(*CurrentChangelistInfo.SharedPath.ToString());
	return FullCLPath;
}

SHeaderRow::FColumn::FArguments SSourceControlReview::HeaderColumn(FName HeaderName)
{
	FText ColumnLabel;
	TOptional<float> ColumnWidth;
	if (HeaderName == ColumnIds::Status)
	{
		ColumnLabel = LOCTEXT("StatusColumnHeader", "Status");
		ColumnWidth = 60;
	}
	else if (HeaderName == ColumnIds::File)
	{
		ColumnLabel = LOCTEXT("FileColumnHeader", "File");
	}
	else if (HeaderName == ColumnIds::Tools)
	{
		ColumnLabel = LOCTEXT("ToolsColumnHeader", "Tools");
		ColumnWidth = 88;
	}

	return SHeaderRow::Column(HeaderName)
		.FixedWidth(ColumnWidth)
		.HAlignHeader(HeaderName == ColumnIds::File? HAlign_Fill : HAlign_Center)
		.HAlignCell(HAlign_Fill)
		.VAlignCell(VAlign_Fill)
		.VAlignHeader(VAlign_Fill)
		.HeaderContentPadding(FMargin(10.f, 6.f))
		[
			SNew(STextBlock)
			.Text(ColumnLabel)
			.Font(FStyleFonts::Get().Normal)
		];
}

FString SSourceControlReview::AsAssetPath(const FString& FullCLPath)
{
	const FString ProjectName = FString::Format(TEXT("/{0}/"), {FApp::GetProjectName()});
	return UKismetSystemLibrary::GetProjectDirectory() / FullCLPath.RightChop(FullCLPath.Find(ProjectName) + ProjectName.Len());
}

#undef LOCTEXT_NAMESPACE
