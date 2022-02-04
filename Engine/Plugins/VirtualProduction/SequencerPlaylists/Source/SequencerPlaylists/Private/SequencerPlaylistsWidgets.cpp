// Copyright Epic Games, Inc. All Rights Reserved.

#include "SequencerPlaylistsWidgets.h"
#include "SequencerPlaylist.h"
#include "SequencerPlaylistItem.h"
#include "SequencerPlaylistItem_Sequence.h"
#include "SequencerPlaylistPlayer.h"
#include "SequencerPlaylistsModule.h"
#include "SequencerPlaylistsStyle.h"

#include "AssetData.h"
#include "AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "ContentBrowserModule.h"
#include "EditorFontGlyphs.h"
#include "FileHelpers.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IContentBrowserSingleton.h"
#include "ISinglePropertyView.h"
#include "LevelEditor.h"
#include "Misc/FileHelper.h"
#include "Misc/TextFilter.h"
#include "ScopedTransaction.h"
#include "SlateOptMacros.h"
#include "SPositiveActionButton.h"
#include "Styling/SlateIconFinder.h"
#include "Styling/StyleColors.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"


#define LOCTEXT_NAMESPACE "SequencerPlaylists"


const float SSequencerPlaylistPanel::DefaultWidth = 300.0f;
const FName SSequencerPlaylistPanel::ColumnName_HoverTransport(TEXT("HoverTransport"));
const FName SSequencerPlaylistPanel::ColumnName_Items(TEXT("Items"));
const FName SSequencerPlaylistPanel::ColumnName_Offset(TEXT("Offset"));
const FName SSequencerPlaylistPanel::ColumnName_Hold(TEXT("Hold"));
const FName SSequencerPlaylistPanel::ColumnName_Loop(TEXT("Loop"));
const FName SSequencerPlaylistPanel::ColumnName_HoverDetails(TEXT("HoverDetails"));


BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SSequencerPlaylistPanel::Construct(const FArguments& InArgs, USequencerPlaylistPlayer* InPlayer)
{
	check(InPlayer);
	WeakPlayer = InPlayer;

	SearchTextFilter = MakeShared<TTextFilter<const FSequencerPlaylistRowData&>>(
		TTextFilter<const FSequencerPlaylistRowData&>::FItemToStringArray::CreateSP(this, &SSequencerPlaylistPanel::GetSearchStrings));
	SearchTextFilter->OnChanged().AddSP(this, &SSequencerPlaylistPanel::RegenerateRows);

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SImage)
			.Image(FSequencerPlaylistsStyle::Get().GetBrush("SequencerPlaylists.Panel.Background"))
		]
		+ SOverlay::Slot()
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					Construct_LeftToolbar()
				]
				+ SHorizontalBox::Slot()
				[
					SNew(SSpacer)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					Construct_RightToolbar()
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(8.0f, 3.0f)
			[
				SNew(STextBlock)
				.Font(FSequencerPlaylistsStyle::Get().GetFontStyle("SequencerPlaylists.TitleFont"))
				.Text(TAttribute<FText>::CreateLambda([This = SharedThis(this)]()
					{
						if (USequencerPlaylist* Playlist = This->GetCheckedPlaylist())
						{
							return FText::FromString(Playlist->GetName());
						}

						return FText::GetEmpty();
					}))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.MaxHeight(150.0f)
			.Padding(8.0f, 3.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					.Padding(0.0f, 0.0f, 8.0f, 0.0f)
					[
						SNew(SMultiLineEditableTextBox)
						.Padding(0)
						.Margin(0)
						.AutoWrapText(true)
						.Style(FSequencerPlaylistsStyle::Get(), "SequencerPlaylists.EditableTextBox")
						.Font(FSequencerPlaylistsStyle::Get().GetFontStyle("SequencerPlaylists.DescriptionFont"))
						.HintText(LOCTEXT("PlaylistDescriptionHint", "<playlist description>"))
						.Text(TAttribute<FText>::CreateLambda([This = SharedThis(this)]()
							{
								if (USequencerPlaylist* Playlist = This->GetCheckedPlaylist())
								{
									return Playlist->Description;
								}

								return FText::GetEmpty();
							}))
						.OnTextCommitted_Lambda([This = SharedThis(this)](const FText& NewText, ETextCommit::Type CommitType)
							{
								if (USequencerPlaylist* Playlist = This->GetCheckedPlaylist())
								{
									Playlist->Description = NewText;
								}
							})
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Bottom)
				[
					Construct_Transport()
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(8.0f, 6.0f, 8.0f, 3.0f)
			[
				Construct_AddSearchRow()
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				Construct_ItemListView()
			]
		]
	];

	RegenerateRows();
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION


BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedRef<SWidget> SSequencerPlaylistPanel::Construct_LeftToolbar()
{
	FSequencerPlaylistsModule& Module = static_cast<FSequencerPlaylistsModule&>(ISequencerPlaylistsModule::Get());
	FSlimHorizontalToolBarBuilder ToolBarBuilder(Module.GetCommandList(), FMultiBoxCustomization::None, nullptr, true);

	ToolBarBuilder.BeginSection("Playlists");
	{
		ToolBarBuilder.AddToolBarButton(
			FUIAction(FExecuteAction::CreateSP(this, &SSequencerPlaylistPanel::OnNewPlaylist)),
			NAME_None,
			LOCTEXT("NewPlaylistOptions", "New Playlist"),
			LOCTEXT("NewPlaylistOptionsToolTip", "New Playlist"),
			FSlateIcon(FSequencerPlaylistsStyle::Get().GetStyleSetName(), "SequencerPlaylists.NewPlaylist.Background", NAME_None, "SequencerPlaylists.NewPlaylist.Overlay"));

		ToolBarBuilder.AddToolBarButton(
			FUIAction(FExecuteAction::CreateSP(this, &SSequencerPlaylistPanel::OnSavePlaylistAs)),
			NAME_None,
			LOCTEXT("SavePlaylist", "Save Playlist As..."),
			LOCTEXT("SavePlaylistTooltip", "Save Playlist As..."),
			FSlateIcon(FSequencerPlaylistsStyle::Get().GetStyleSetName(), "SequencerPlaylists.SavePlaylistAs"));

		ToolBarBuilder.AddComboButton(
			FUIAction(),
			FOnGetContent::CreateSP(SharedThis(this), &SSequencerPlaylistPanel::BuildOpenPlaylistMenu),
			LOCTEXT("NewPlaylistOptions", "Open Playlist"),
			LOCTEXT("NewPlaylistOptionsToolTip", "Open Playlist"),
			FSlateIcon(FSequencerPlaylistsStyle::Get().GetStyleSetName(), "SequencerPlaylists.OpenPlaylist"));
	}
	ToolBarBuilder.EndSection();

	return ToolBarBuilder.MakeWidget();
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION


BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedRef<SWidget> SSequencerPlaylistPanel::Construct_RightToolbar()
{
	TSharedRef<SSequencerPlaylistPanel> This = SharedThis(this);

	FSequencerPlaylistsModule& Module = static_cast<FSequencerPlaylistsModule&>(ISequencerPlaylistsModule::Get());
	FSlimHorizontalToolBarBuilder ToolBarBuilder(Module.GetCommandList(), FMultiBoxCustomization::None, nullptr, true);

	ToolBarBuilder.BeginSection("TriggerMode");
	{
		TSharedRef<SWidget> TriggerToggle = SNew(SCheckBox)
			.Padding(FMargin(10.0f, 4.0f))
			.ToolTipText(LOCTEXT("ToggleTriggerToolTip", "Enables and disables Trigger Mode"))
			.Style(FEditorStyle::Get(), "ToggleButtonCheckbox")
			.IsChecked_Lambda([This]() { return This->InTriggerMode() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([This](ECheckBoxState InState) { This->bTriggerMode = (InState == ECheckBoxState::Checked); })
			[
				SNew(STextBlock)
				.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.16"))
				.Text(FText::FromString(FString(TEXT("\xf0e7"))))
			];

		ToolBarBuilder.AddWidget(TriggerToggle);
	}
	ToolBarBuilder.EndSection();

	return ToolBarBuilder.MakeWidget();
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION


BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedRef<SWidget> SSequencerPlaylistPanel::Construct_Transport()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SButton)
			.ButtonStyle(FSequencerPlaylistsStyle::Get(), "SequencerPlaylists.TransportButton.Play")
			.ContentPadding(0)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked(this, &SSequencerPlaylistPanel::HandleClicked_PlayAll)
			.ToolTipText(LOCTEXT("PlayAllButtonToolTip", "Play all items"))
			[
				SNew(SImage)
				.Image(FSequencerPlaylistsStyle::Get().GetBrush("SequencerPlaylists.Play"))
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SButton)
			.ButtonStyle(FSequencerPlaylistsStyle::Get(), "SequencerPlaylists.TransportButton.Stop")
			.ContentPadding(0)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked(this, &SSequencerPlaylistPanel::HandleClicked_StopAll)
			.ToolTipText(LOCTEXT("StopAllButtonToolTip", "Stop all items"))
			[
				SNew(SImage)
				.Image(FSequencerPlaylistsStyle::Get().GetBrush("SequencerPlaylists.Stop"))
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SButton)
			.ButtonStyle(FSequencerPlaylistsStyle::Get(), "SequencerPlaylists.TransportButton.Reset")
			.ContentPadding(FMargin(0.0f, 1.0f, 0.0f, 0.0f))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked(this, &SSequencerPlaylistPanel::HandleClicked_ResetAll)
			.ToolTipText(LOCTEXT("ResetAllButtonToolTip", "Reset all items"))
			[
				SNew(STextBlock)
				.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.14"))
				.Text(FEditorFontGlyphs::Undo)
			]
		];
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION


BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedRef<SWidget> SSequencerPlaylistPanel::Construct_AddSearchRow()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SPositiveActionButton)
			.OnClicked(this, &SSequencerPlaylistPanel::HandleClicked_AddSequence)
			.Icon(FAppStyle::Get().GetBrush("Icons.Plus"))
			.Text(LOCTEXT("AddItemButton", "Item"))
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.Padding(8.0f, 0.0f)
		[
			SAssignNew(SearchBox, SSearchBox)
			.HintText(LOCTEXT("SearchHint", "Search Playlist Items"))
			.OnTextChanged(this, &SSequencerPlaylistPanel::OnSearchTextChanged)
			.DelayChangeNotificationsWhileTyping(true)
		];
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION


BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedRef<SWidget> SSequencerPlaylistPanel::Construct_ItemListView()
{
	return SAssignNew(ItemListView, SListView<TSharedPtr<FSequencerPlaylistRowData>>)
		.SelectionMode(ESelectionMode::Single) // See TODO in HandleAcceptDrop
		.ListItemsSource(&ItemRows)
		.OnGenerateRow_Lambda([This = SharedThis(this)](TSharedPtr<FSequencerPlaylistRowData> InData, const TSharedRef<STableViewBase>& OwnerTableView)
			{
				return SNew(SSequencerPlaylistItemWidget, InData, OwnerTableView)
					.TriggerMode(TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateSP(This, &SSequencerPlaylistPanel::InTriggerMode)))
					.IsPlaying_Lambda([This, WeakItem = InData->WeakItem]() {
						USequencerPlaylistItem* Item = WeakItem.Get();
						USequencerPlaylistPlayer* Player = This->WeakPlayer.Get();
						if (Item && Player)
						{
							return Player->IsPlaying(Item);
						}
						else
						{
							return false;
						}
					})
					.OnPlayClicked(This, &SSequencerPlaylistPanel::HandleClicked_Item_Play)
					.OnStopClicked(This, &SSequencerPlaylistPanel::HandleClicked_Item_Stop)
					.OnResetClicked(This, &SSequencerPlaylistPanel::HandleClicked_Item_Reset)
					.OnRemoveClicked(This, &SSequencerPlaylistPanel::HandleClicked_Item_Remove)
					.OnIsPropertyVisible(This, &SSequencerPlaylistPanel::HandleItemDetailsIsPropertyVisible)
					.OnCanAcceptDrop(This, &SSequencerPlaylistPanel::HandleCanAcceptDrop)
					.OnAcceptDrop(This, &SSequencerPlaylistPanel::HandleAcceptDrop);
			})
		.HeaderRow(
			SNew(SHeaderRow)
			+ SHeaderRow::Column(ColumnName_HoverTransport)
				.DefaultLabel(FText::GetEmpty())
				.FillSized(20.0f)
			+ SHeaderRow::Column(ColumnName_Items)
				.DefaultLabel(LOCTEXT("ColumnLabelItems", "Playlist Items"))
				.FillWidth(1.0f)
			+ SHeaderRow::Column(ColumnName_Offset)
				.DefaultLabel(LOCTEXT("ColumnLabelOffset", "Offset"))
				.FillSized(45.0f)
			+ SHeaderRow::Column(ColumnName_Hold)
				.DefaultLabel(LOCTEXT("ColumnLabelHold", "Hold"))
				.FillSized(35.0f)
				.HAlignCell(HAlign_Center)
			+ SHeaderRow::Column(ColumnName_Loop)
				.DefaultLabel(LOCTEXT("ColumnLabelLoop", "Loop"))
				.FillSized(40.0f)
			+ SHeaderRow::Column(ColumnName_HoverDetails)
				.DefaultLabel(FText::GetEmpty())
				.FillSized(20.0f)
				.HAlignCell(HAlign_Center)
				.VAlignCell(VAlign_Center)
		);
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION


USequencerPlaylist* SSequencerPlaylistPanel::GetCheckedPlaylist()
{
	USequencerPlaylistPlayer* Player = WeakPlayer.Get();
	if (!ensure(Player))
	{
		return nullptr;
	}

	USequencerPlaylist* Playlist = Player->GetPlaylist();
	ensure(Playlist);
	return Playlist;
}


void SSequencerPlaylistPanel::RegenerateRows()
{
	USequencerPlaylist* Playlist = GetCheckedPlaylist();
	if (!Playlist)
	{
		return;
	}

	const int32 ItemCount = Playlist->Items.Num();
	ItemRows.Empty(ItemCount);
	for (int32 ItemIndex = 0; ItemIndex < ItemCount; ++ItemIndex)
	{
		USequencerPlaylistItem* Item = Playlist->Items[ItemIndex];
		FSequencerPlaylistRowData Row(ItemIndex, Item);
		if (SearchTextFilter->PassesFilter(Row))
		{
			ItemRows.Emplace(MakeShared<FSequencerPlaylistRowData>(Row));
		}
	}

	ItemListView->RequestListRefresh();
}


TSharedRef<SWidget> SSequencerPlaylistPanel::BuildOpenPlaylistMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	IContentBrowserSingleton& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get();

	FAssetPickerConfig AssetPickerConfig;
	{
		AssetPickerConfig.SelectionMode = ESelectionMode::Single;
		AssetPickerConfig.InitialAssetViewType = EAssetViewType::Column;
		AssetPickerConfig.bFocusSearchBoxWhenOpened = true;
		AssetPickerConfig.bAllowNullSelection = false;
		AssetPickerConfig.bShowBottomToolbar = true;
		AssetPickerConfig.bAutohideSearchBar = false;
		AssetPickerConfig.bAllowDragging = false;
		AssetPickerConfig.bCanShowClasses = false;
		AssetPickerConfig.bShowPathInColumnView = true;
		AssetPickerConfig.bShowTypeInColumnView = false;
		AssetPickerConfig.bSortByPathInColumnView = false;

		AssetPickerConfig.AssetShowWarningText = LOCTEXT("OpenPlaylistNoAssetsWarning", "No Playlists Found");
		AssetPickerConfig.Filter.ClassNames.Add(USequencerPlaylist::StaticClass()->GetFName());
		AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateSP(this, &SSequencerPlaylistPanel::OnLoadPlaylist);
	}

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("OpenPlaylistMenuSection", "Load Playlist"));
	{
		TSharedRef<SWidget> PresetPicker = SNew(SBox)
			.MinDesiredWidth(400.f)
			.MinDesiredHeight(400.f)
			[
				ContentBrowser.CreateAssetPicker(AssetPickerConfig)
			];

		MenuBuilder.AddWidget(PresetPicker, FText(), true, false);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}


static bool OpenSaveDialog(const FString& InDefaultPath, const FString& InNewNameSuggestion, FString& OutPackageName)
{
	FSaveAssetDialogConfig SaveAssetDialogConfig;
	{
		SaveAssetDialogConfig.DefaultPath = InDefaultPath;
		SaveAssetDialogConfig.DefaultAssetName = InNewNameSuggestion;
		SaveAssetDialogConfig.AssetClassNames.Add(USequencerPlaylist::StaticClass()->GetFName());
		SaveAssetDialogConfig.ExistingAssetPolicy = ESaveAssetDialogExistingAssetPolicy::AllowButWarn;
		SaveAssetDialogConfig.DialogTitleOverride = LOCTEXT("SavePlaylistDialogTitle", "Save Sequencer Playlist");
	}

	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	FString SaveObjectPath = ContentBrowserModule.Get().CreateModalSaveAssetDialog(SaveAssetDialogConfig);

	if (!SaveObjectPath.IsEmpty())
	{
		OutPackageName = FPackageName::ObjectPathToPackageName(SaveObjectPath);
		return true;
	}

	return false;
}


bool GetSavePlaylistPackageName(FString& OutName)
{
	// TODO
	//USequencerPlaylistsSettings* ConfigSettings = GetMutableDefault<USequencerPlaylistsSettings>();

	FDateTime Today = FDateTime::Now();

	TMap<FString, FStringFormatArg> FormatArgs;
	FormatArgs.Add(TEXT("date"), Today.ToString());

	// determine default package path
	const FString DefaultSaveDirectory;// = FString::Format(*ConfigSettings->GetPresetSaveDir().Path, FormatArgs);

	FString DialogStartPath;
	FPackageName::TryConvertFilenameToLongPackageName(DefaultSaveDirectory, DialogStartPath);
	if (DialogStartPath.IsEmpty())
	{
		DialogStartPath = TEXT("/Game");
	}

	// determine default asset name
	FString DefaultName = LOCTEXT("NewPlaylistDefaultName", "NewSequencerPlaylist").ToString();

	FString UniquePackageName;
	FString UniqueAssetName;

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	AssetToolsModule.Get().CreateUniqueAssetName(DialogStartPath / DefaultName, TEXT(""), UniquePackageName, UniqueAssetName);

	FString DialogStartName = FPaths::GetCleanFilename(UniqueAssetName);

	FString UserPackageName;
	FString NewPackageName;

	// get destination for asset
	bool bFilenameValid = false;
	while (!bFilenameValid)
	{
		if (!OpenSaveDialog(DialogStartPath, DialogStartName, UserPackageName))
		{
			return false;
		}

		NewPackageName = FString::Format(*UserPackageName, FormatArgs);

		FText OutError;
		bFilenameValid = FFileHelper::IsFilenameValidForSaving(NewPackageName, OutError);
	}

	//ConfigSettings->PresetSaveDir.Path = FPackageName::GetLongPackagePath(UserPackageName);
	//ConfigSettings->SaveConfig();
	OutName = MoveTemp(NewPackageName);
	return true;
}


void SSequencerPlaylistPanel::OnSavePlaylistAs()
{
	USequencerPlaylist* Playlist = GetCheckedPlaylist();
	if (!Playlist)
	{
		return;
	}

	FString PackageName;
	if (!GetSavePlaylistPackageName(PackageName))
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("SavePlaylistTransaction", "Save Sequencer Playlist"));

	// Saving into a new package
	const FString NewAssetName = FPackageName::GetLongPackageAssetName(PackageName);
	UPackage* NewPackage = CreatePackage(*PackageName);
	USequencerPlaylist* NewPlaylist = NewObject<USequencerPlaylist>(NewPackage, *NewAssetName, RF_Public | RF_Standalone | RF_Transactional);

	if (NewPlaylist)
	{
		// Within the transient package; no collision.
		Playlist->Rename(*NewPlaylist->GetName());

		NewPlaylist->Description = Playlist->Description;

		for (USequencerPlaylistItem* Item : Playlist->Items)
		{
			NewPlaylist->Items.Add(DuplicateObject<USequencerPlaylistItem>(Item, NewPlaylist));
		}

		NewPlaylist->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(NewPlaylist);

		FEditorFileUtils::PromptForCheckoutAndSave({ NewPackage }, false, false);
	}
}


void SSequencerPlaylistPanel::OnLoadPlaylist(const FAssetData& InPreset)
{
	FSlateApplication::Get().DismissAllMenus();

	USequencerPlaylist* Playlist = GetCheckedPlaylist();
	if (!Playlist)
	{
		return;
	}

	USequencerPlaylist* ImportedPlaylist = CastChecked<USequencerPlaylist>(InPreset.GetAsset());
	if (ImportedPlaylist)
	{
		FScopedTransaction Transaction(LOCTEXT("LoadPlaylistTransaction", "Load Sequencer Playlist"));

		// Within the transient package; no collision.
		Playlist->Rename(*ImportedPlaylist->GetName());

		Playlist->Description = ImportedPlaylist->Description;

		Playlist->Items.Empty();
		for (USequencerPlaylistItem* Item : ImportedPlaylist->Items)
		{
			Playlist->Items.Add(DuplicateObject<USequencerPlaylistItem>(Item, Playlist));
		}

		RegenerateRows();
	}
}


void SSequencerPlaylistPanel::OnNewPlaylist()
{
	USequencerPlaylist* Playlist = GetCheckedPlaylist();
	if (!Playlist)
	{
		return;
	}

	Playlist->Rename(*MakeUniqueObjectName(GetTransientPackage(), USequencerPlaylist::StaticClass(), "UntitledPlaylist").ToString());
	Playlist->Description = FText::GetEmpty();
	Playlist->Items.Empty();
	RegenerateRows();
}


void SSequencerPlaylistPanel::GetSearchStrings(const FSequencerPlaylistRowData& Row, TArray<FString>& OutSearchStrings)
{
	if (USequencerPlaylistItem* Item = Row.WeakItem.Get())
	{
		OutSearchStrings.Add(Item->GetDisplayName().ToString());
	}
}


void SSequencerPlaylistPanel::OnSearchTextChanged(const FText& InFilterText)
{
	SearchTextFilter->SetRawFilterText(InFilterText);
	SearchBox->SetError(SearchTextFilter->GetFilterErrorText());
}


FReply SSequencerPlaylistPanel::HandleClicked_PlayAll()
{
	USequencerPlaylistPlayer* Player = WeakPlayer.Get();
	if (!ensure(Player))
	{
		return FReply::Unhandled();
	}

	Player->PlayAll();
	return FReply::Handled();
}


FReply SSequencerPlaylistPanel::HandleClicked_StopAll()
{
	USequencerPlaylistPlayer* Player = WeakPlayer.Get();
	if (!ensure(Player))
	{
		return FReply::Unhandled();
	}

	Player->StopAll();
	return FReply::Handled();
}


FReply SSequencerPlaylistPanel::HandleClicked_ResetAll()
{
	USequencerPlaylistPlayer* Player = WeakPlayer.Get();
	if (!ensure(Player))
	{
		return FReply::Unhandled();
	}

	Player->ResetAll();
	return FReply::Handled();
}


FReply SSequencerPlaylistPanel::HandleClicked_AddSequence()
{
	USequencerPlaylist* Playlist = GetCheckedPlaylist();
	if (!Playlist)
	{
		return FReply::Unhandled();
	}

	Playlist->Items.Add(NewObject<USequencerPlaylistItem_Sequence>(Playlist));
	RegenerateRows(); // TODO: Incremental view update? (UETOOL-4644)

	return FReply::Handled();
}


FReply SSequencerPlaylistPanel::HandleClicked_Item_Play(TSharedPtr<SSequencerPlaylistItemWidget> ItemWidget)
{
	USequencerPlaylistPlayer* Player = WeakPlayer.Get();
	if (!ensure(Player))
	{
		return FReply::Unhandled();
	}

	Player->PlayItem(ItemWidget->GetItem());
	return FReply::Handled();
}


FReply SSequencerPlaylistPanel::HandleClicked_Item_Stop(TSharedPtr<SSequencerPlaylistItemWidget> ItemWidget)
{
	USequencerPlaylistPlayer* Player = WeakPlayer.Get();
	if (!ensure(Player))
	{
		return FReply::Unhandled();
	}

	Player->StopItem(ItemWidget->GetItem());
	return FReply::Handled();
}


FReply SSequencerPlaylistPanel::HandleClicked_Item_Reset(TSharedPtr<SSequencerPlaylistItemWidget> ItemWidget)
{
	USequencerPlaylistPlayer* Player = WeakPlayer.Get();
	if (!ensure(Player))
	{
		return FReply::Unhandled();
	}

	Player->ResetItem(ItemWidget->GetItem());
	return FReply::Handled();
}


FReply SSequencerPlaylistPanel::HandleClicked_Item_Remove(TSharedPtr<SSequencerPlaylistItemWidget> ItemWidget)
{
	USequencerPlaylist* Playlist = GetCheckedPlaylist();
	if (!Playlist)
	{
		return FReply::Unhandled();
	}

	ensure(Playlist->Items.RemoveSingle(ItemWidget->GetItem()));
	RegenerateRows(); // TODO: Incremental view update? (UETOOL-4644)
	return FReply::Handled();
}


bool SSequencerPlaylistPanel::HandleItemDetailsIsPropertyVisible(const FPropertyAndParent& PropertyAndParent)
{
	static TMap<FName, FName> PropertyNameToColumnName;
	static bool bMapInitialized = false;
	if (bMapInitialized == false)
	{
		PropertyNameToColumnName.Add("Sequence", ColumnName_Items);

		PropertyNameToColumnName.Add("StartFrameOffset", ColumnName_Offset);
		PropertyNameToColumnName.Add("EndFrameOffset", ColumnName_Offset);
		PropertyNameToColumnName.Add("bHoldAtFirstFrame", ColumnName_Hold);
		PropertyNameToColumnName.Add("NumLoops", ColumnName_Loop);

		bMapInitialized = true;
	}

	if (const FName* ColumnName = PropertyNameToColumnName.Find(PropertyAndParent.Property.GetFName()))
	{
		if (ItemListView->GetHeaderRow()->IsColumnVisible(*ColumnName))
		{
			return false;
		}
	}

	return true;
}


TOptional<EItemDropZone> SSequencerPlaylistPanel::HandleCanAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, TSharedPtr<FSequencerPlaylistRowData> RowData)
{
	TSharedPtr<FSequencerPlaylistItemDragDropOp> DragDropOperation = DragDropEvent.GetOperationAs<FSequencerPlaylistItemDragDropOp>();
	if (DragDropOperation.IsValid())
	{
		return DropZone == EItemDropZone::OntoItem ? EItemDropZone::BelowItem : DropZone;
	}

	return TOptional<EItemDropZone>();
}


FReply SSequencerPlaylistPanel::HandleAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, TSharedPtr<FSequencerPlaylistRowData> RowData)
{
	USequencerPlaylist* Playlist = GetCheckedPlaylist();
	if (!Playlist)
	{
		return FReply::Unhandled();
	}

	TSharedPtr<FSequencerPlaylistItemDragDropOp> Operation = DragDropEvent.GetOperationAs<FSequencerPlaylistItemDragDropOp>();
	if (Operation.IsValid())
	{
		// TODO: Not currently handling (potentially disjoint) multi-select,
		// in part because there's no Algo::StablePartition.
		// The ListView is set to ESelectionMode::Single for the time being.
		if (ensure(Operation->SelectedItems.Num() == 1))
		{
			const int32 SrcIndex = Operation->SelectedItems[0]->PlaylistIndex;
			const int32 DropTargetIndex = RowData->PlaylistIndex;

			if (SrcIndex == DropTargetIndex)
			{
				return FReply::Handled();
			}

			int32 DestIndexAdjustment = 0;
			if (DropZone == EItemDropZone::BelowItem)
			{
				DestIndexAdjustment += 1;
			}

			if (SrcIndex < DropTargetIndex)
			{
				DestIndexAdjustment -= 1;
			}

			const int32 DestIndex = DropTargetIndex + DestIndexAdjustment;
			TArray<TObjectPtr<USequencerPlaylistItem>>& Items = Playlist->Items;
			if (ensure(Items.IsValidIndex(SrcIndex)) && ensure(Items.IsValidIndex(DestIndex)))
			{
				USequencerPlaylistItem* ItemToMove = Items[SrcIndex];
				Items.RemoveAt(SrcIndex);
				Items.Insert(ItemToMove, DestIndex);
				RegenerateRows(); // TODO: Incremental view update? (UETOOL-4644)
				return FReply::Handled();
			}
		}
	}

	return FReply::Unhandled();
}


TSharedRef<FSequencerPlaylistItemDragDropOp> FSequencerPlaylistItemDragDropOp::New(const TArray<TSharedPtr<FSequencerPlaylistRowData>>& InSelectedItems)
{
	TSharedRef<FSequencerPlaylistItemDragDropOp> Operation = MakeShared<FSequencerPlaylistItemDragDropOp>();

	Operation->SelectedItems = InSelectedItems;

	Operation->MouseCursor = EMouseCursor::GrabHandClosed;
	Operation->Decorator = SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("Graph.ConnectorFeedback.Border"))
		.Content()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("ItemDragHint", "Move {0} {0}|plural(one=item,other=items)"), InSelectedItems.Num()))
			]
		];

	Operation->Construct();

	return Operation;
}


FSequencerPlaylistItemDragDropOp::~FSequencerPlaylistItemDragDropOp()
{
}


const FText SSequencerPlaylistItemWidget::PlayItemTooltipText(LOCTEXT("PlayItemButtonToolTip", "Play this item"));
const FText SSequencerPlaylistItemWidget::StopItemTooltipText(LOCTEXT("StopItemButtonToolTip", "Stop playback of this item"));
const FText SSequencerPlaylistItemWidget::ResetItemTooltipText(LOCTEXT("ResetItemButtonToolTip", "Reset this item"));


void SSequencerPlaylistItemWidget::Construct(const FArguments& InArgs, TSharedPtr<FSequencerPlaylistRowData> InRowData, const TSharedRef<STableViewBase>& OwnerTableView)
{
	check(InRowData && InRowData->WeakItem.IsValid());
	RowData = InRowData;

	TriggerMode = InArgs._TriggerMode;
	IsPlaying = InArgs._IsPlaying;

	PlayClickedDelegate = InArgs._OnPlayClicked;
	StopClickedDelegate = InArgs._OnStopClicked;
	ResetClickedDelegate = InArgs._OnResetClicked;
	RemoveClickedDelegate = InArgs._OnRemoveClicked;

	IsPropertyVisibleDelegate = InArgs._OnIsPropertyVisible;

	FSuperRowType::Construct(
		FSuperRowType::FArguments()
			.OnCanAcceptDrop(InArgs._OnCanAcceptDrop)
			.OnAcceptDrop(InArgs._OnAcceptDrop)
			.OnDragDetected(this, &SSequencerPlaylistItemWidget::HandleDragDetected)
		, OwnerTableView);
}


void SSequencerPlaylistItemWidget::ConstructChildren(ETableViewMode::Type InOwnerTableMode, const TAttribute<FMargin>& InPadding, const TSharedRef<SWidget>& InContent)
{
	// We wrap InContent with an overlay which facilitates dimming the row, obstructing hit test of
	// the controls behind, and centering Trigger Mode transport controls over the row on hover.

	TSharedRef<SSequencerPlaylistItemWidget> This = SharedThis(this);

	Content = InContent;

	ChildSlot
	.Padding(InPadding)
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			InContent
		]
		+ SOverlay::Slot()
		[
			SNew(SImage)
			.Visibility(this, &SSequencerPlaylistItemWidget::GetTriggerModeTransportVisibility)
			.Image(FSequencerPlaylistsStyle::Get().GetBrush("SequencerPlaylists.Item.Dim"))
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)
			.Visibility(this, &SSequencerPlaylistItemWidget::GetTriggerModeTransportVisibility)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(FSequencerPlaylistsStyle::Get(), "SequencerPlaylists.TransportButton.Play")
				.ContentPadding(0)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.OnClicked_Lambda([This]() { return This->PlayClickedDelegate.Execute(This); })
				.ToolTipText(PlayItemTooltipText)
				[
					SNew(SImage)
					.Image(FSequencerPlaylistsStyle::Get().GetBrush("SequencerPlaylists.Play"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(FSequencerPlaylistsStyle::Get(), "SequencerPlaylists.TransportButton.Stop")
				.ContentPadding(0)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.OnClicked_Lambda([This]() { return This->StopClickedDelegate.Execute(This); })
				.ToolTipText(StopItemTooltipText)
				[
					SNew(SImage)
					.Image(FSequencerPlaylistsStyle::Get().GetBrush("SequencerPlaylists.Stop"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(FSequencerPlaylistsStyle::Get(), "SequencerPlaylists.TransportButton.Reset")
				.ContentPadding(FMargin(0.0f, 1.0f, 0.0f, 0.0f))
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.OnClicked_Lambda([This]() { return This->ResetClickedDelegate.Execute(This); })
				.ToolTipText(ResetItemTooltipText)
				[
					SNew(STextBlock)
					.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.14"))
					.Text(FEditorFontGlyphs::Undo)
				]
			]
		]
	];

	InnerContentSlot = &ChildSlot.AsSlot();
}


BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedRef<SWidget> SSequencerPlaylistItemWidget::GenerateWidgetForColumn(const FName& ColumnName)
{
	USequencerPlaylistItem* Item = GetItem();

	if (!ensure(Item))
	{
		return SNullWidget::NullWidget;
	}

	TSharedRef<SSequencerPlaylistItemWidget> This = SharedThis(this);

	static const FName PropertyEditorModuleName("PropertyEditor");
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>(PropertyEditorModuleName);
	FSinglePropertyParams SinglePropParams;
	SinglePropParams.NamePlacement = EPropertyNamePlacement::Hidden;

	if (ColumnName == SSequencerPlaylistPanel::ColumnName_HoverTransport)
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			[
				SNew(SButton)
				.ButtonStyle(FSequencerPlaylistsStyle::Get(), "SequencerPlaylists.HoverTransport.Play")
				.ContentPadding(FMargin(1.0f, 3.0f, 0.0f, 0.0f))
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Visibility_Lambda([This]() { return (This->IsHovered() && !This->InTriggerMode()) ? EVisibility::Visible : EVisibility::Hidden; })
				.OnClicked_Lambda([This]() { return This->PlayClickedDelegate.Execute(This); })
				.ToolTipText(PlayItemTooltipText)
				[
					SNew(SImage)
					.Image(FSequencerPlaylistsStyle::Get().GetBrush("SequencerPlaylists.Play.Small"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			]
			+ SVerticalBox::Slot()
			[
				SNew(SButton)
				.ButtonStyle(FSequencerPlaylistsStyle::Get(), "SequencerPlaylists.HoverTransport.Stop")
				.ContentPadding(FMargin(0.0f, 0.0f, 0.0f, 1.0f))
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Visibility_Lambda([This]() { return (This->IsPlaying.Get() || (This->IsHovered() && !This->InTriggerMode())) ? EVisibility::Visible : EVisibility::Hidden; })
				.ForegroundColor_Lambda([This]() { return This->IsPlaying.Get() ? FStyleColors::AccentRed : FSlateColor::UseStyle(); })
				.OnClicked_Lambda([This]() { return This->StopClickedDelegate.Execute(This); })
				.ToolTipText(StopItemTooltipText)
				[
					SNew(SImage)
					.Image(FSequencerPlaylistsStyle::Get().GetBrush("SequencerPlaylists.Stop.Small"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			]
			+ SVerticalBox::Slot()
			[
				SNew(SButton)
				.ButtonStyle(FSequencerPlaylistsStyle::Get(), "SequencerPlaylists.HoverTransport.Reset")
				.ContentPadding(FMargin(1.0f, 0.0f, 0.0f, 3.0f))
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Visibility_Lambda([This]() { return (This->IsHovered() && !This->InTriggerMode()) ? EVisibility::Visible : EVisibility::Hidden; })
				.OnClicked_Lambda([This]() { return This->ResetClickedDelegate.Execute(This); })
				.ToolTipText(ResetItemTooltipText)
				[
					SNew(STextBlock)
					.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.9"))
					.Text(FEditorFontGlyphs::Undo)
				]
			];
	}

	if (ColumnName == SSequencerPlaylistPanel::ColumnName_Items)
	{
		USequencerPlaylistItem_Sequence* SequenceItem =
			CastChecked<USequencerPlaylistItem_Sequence>(Item);

		TSharedPtr<ISinglePropertyView> PropView = PropertyEditorModule.CreateSingleProperty(SequenceItem,
			GET_MEMBER_NAME_CHECKED(USequencerPlaylistItem_Sequence, Sequence),
			SinglePropParams);

		return SNew(SBox)
			.Padding(FMargin(8.0f, 0.0f, 0.0f, 0.0f))
			[
				PropView.ToSharedRef()
			];
	}

	if (ColumnName == SSequencerPlaylistPanel::ColumnName_Offset)
	{
		TSharedPtr<ISinglePropertyView> StartOffsetPropView = PropertyEditorModule.CreateSingleProperty(
			Item, GET_MEMBER_NAME_CHECKED(USequencerPlaylistItem, StartFrameOffset), SinglePropParams);

		TSharedPtr<ISinglePropertyView> EndOffsetPropView = PropertyEditorModule.CreateSingleProperty(
			Item, GET_MEMBER_NAME_CHECKED(USequencerPlaylistItem, EndFrameOffset), SinglePropParams);

		return SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			[
				StartOffsetPropView.ToSharedRef()
			]
			+ SVerticalBox::Slot()
			[
				EndOffsetPropView.ToSharedRef()
			];
	}

	if (ColumnName == SSequencerPlaylistPanel::ColumnName_Hold)
	{
		TSharedPtr<ISinglePropertyView> PropView = PropertyEditorModule.CreateSingleProperty(Item,
			GET_MEMBER_NAME_CHECKED(USequencerPlaylistItem, bHoldAtFirstFrame),
			SinglePropParams);

		return PropView.ToSharedRef();
	}

	if (ColumnName == SSequencerPlaylistPanel::ColumnName_Loop)
	{
		TSharedPtr<ISinglePropertyView> PropView = PropertyEditorModule.CreateSingleProperty(Item,
			GET_MEMBER_NAME_CHECKED(USequencerPlaylistItem, NumLoops),
			SinglePropParams);

		return PropView.ToSharedRef();
	}

	if (ColumnName == SSequencerPlaylistPanel::ColumnName_HoverDetails)
	{
		return SAssignNew(DetailsAnchor, SMenuAnchor)
			.Placement(MenuPlacement_MenuLeft)
			.OnGetMenuContent(This, &SSequencerPlaylistItemWidget::EnsureSelectedAndBuildContextMenu)
			[
				SNew(SBox)
				.HeightOverride(24.0f)
				.WidthOverride(10.0f)
				[
					SNew(SButton)
					.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
					.ContentPadding(0)
					.OnClicked_Lambda([This]() { This->DetailsAnchor->SetIsOpen(!This->DetailsAnchor->IsOpen()); return FReply::Handled(); })
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Center)
					.Visibility_Lambda([This]() { return This->IsHovered() ? EVisibility::Visible : EVisibility::Hidden; })
					[
						SNew(SImage)
						.ColorAndOpacity(FSlateColor::UseForeground())
						.Image(FSequencerPlaylistsStyle::Get().GetBrush("SequencerPlaylists.Ellipsis"))
					]
				]
			];
	}

	return SNullWidget::NullWidget;
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION


FReply SSequencerPlaylistItemWidget::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		TSharedRef<SWidget> MenuWidget = EnsureSelectedAndBuildContextMenu();
		const FWidgetPath WidgetPath = MouseEvent.GetEventPath() ? *MouseEvent.GetEventPath() : FWidgetPath();
		FSlateApplication::Get().PushMenu(SharedThis(this), WidgetPath, MenuWidget, MouseEvent.GetScreenSpacePosition(), FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
		return FReply::Handled().ReleaseMouseCapture().SetUserFocus(MenuWidget, EFocusCause::SetDirectly);
	}

	return FSuperRowType::OnMouseButtonUp(MyGeometry, MouseEvent);
}


FReply SSequencerPlaylistItemWidget::HandleDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	TSharedPtr<ITypedTableView<TSharedPtr<FSequencerPlaylistRowData>>> OwnerTable = OwnerTablePtr.Pin();
	if (OwnerTable)
	{
		return FReply::Handled().BeginDragDrop(FSequencerPlaylistItemDragDropOp::New(OwnerTable->GetSelectedItems()));
	}

	return FReply::Unhandled();
}


bool SSequencerPlaylistItemWidget::IsRowContentEnabled() const
{
	return GetTriggerModeTransportVisibility() == EVisibility::Hidden;
}


EVisibility SSequencerPlaylistItemWidget::GetTriggerModeTransportVisibility() const
{
	if (IsHovered() && InTriggerMode())
	{
		return EVisibility::Visible;
	}

	return EVisibility::Hidden;
}


TSharedRef<SWidget> SSequencerPlaylistItemWidget::EnsureSelectedAndBuildContextMenu()
{
	TArray<UObject*> SelectedItems;

	TSharedPtr<ITypedTableView<TSharedPtr<FSequencerPlaylistRowData>>> OwnerTable = OwnerTablePtr.Pin();
	if (OwnerTable)
	{
		TArray<TSharedPtr<FSequencerPlaylistRowData>> SelectedRows = OwnerTable->GetSelectedItems();
		SelectedItems.Reserve(SelectedRows.Num());
		for (const TSharedPtr<FSequencerPlaylistRowData>& SelectedRow : SelectedRows)
		{
			SelectedItems.Add(SelectedRow->WeakItem.Get());
		}

		if (!SelectedItems.Contains(GetItem()))
		{
			OwnerTable->Private_ClearSelection();
			OwnerTable->Private_SetItemSelection(RowData, true, true);
			OwnerTable->Private_SignalSelectionChanged(ESelectInfo::OnMouseClick);
			SelectedItems.Empty(1);
			SelectedItems.Add(GetItem());
		}
	}
	else
	{
		SelectedItems.Add(GetItem());
	}

	return BuildContextMenu(SelectedItems);
}


TSharedRef<SWidget> SSequencerPlaylistItemWidget::BuildContextMenu(const TArray<UObject*>& SelectedItems)
{
	TSharedRef<SSequencerPlaylistItemWidget> This = SharedThis(this);

	FSequencerPlaylistsModule& Module = static_cast<FSequencerPlaylistsModule&>(ISequencerPlaylistsModule::Get());
	FMenuBuilder MenuBuilder(true, Module.GetCommandList());

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("ItemContextPlaybackHeading", "Playback"));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("ItemContextPlay", "Play"),
			LOCTEXT("ItemContextPlayTooltip", "Trigger playback of item"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([This]() { This->PlayClickedDelegate.Execute(This); }))
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("ItemContextStop", "Stop"),
			LOCTEXT("ItemContextStopTooltip", "Stop playback of this item"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([This]() { This->StopClickedDelegate.Execute(This); }))
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("ItemContextReset", "Reset"),
			LOCTEXT("ItemContextResetTooltip", "Reset playback of this item"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([This]() { This->ResetClickedDelegate.Execute(This); }))
		);
	}

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("ItemContextDetailsHeading", "Details"));
	{
		FDetailsViewArgs DetailsViewArgs;
		{
			DetailsViewArgs.bAllowSearch = false;
			DetailsViewArgs.bCustomFilterAreaLocation = true;
			DetailsViewArgs.bCustomNameAreaLocation = true;
			DetailsViewArgs.bHideSelectionTip = true;
			DetailsViewArgs.bLockable = false;
			DetailsViewArgs.bSearchInitialKeyFocus = true;
			DetailsViewArgs.bUpdatesFromSelection = false;
			DetailsViewArgs.bShowOptions = false;
			DetailsViewArgs.bShowModifiedPropertiesOption = false;
			DetailsViewArgs.ColumnWidth = 0.45f;
		}

		TSharedRef<IDetailsView> DetailsView = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor").CreateDetailView(DetailsViewArgs);
		DetailsView->SetIsPropertyVisibleDelegate(IsPropertyVisibleDelegate);
		DetailsView->SetObjects(SelectedItems);
		MenuBuilder.AddWidget(DetailsView, FText::GetEmpty(), true);
	}

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("ItemContextEditHeading", "Edit"));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("ItemContextRemove", "Remove from Playlist"),
			LOCTEXT("ItemContextRemoveTooltip", "Remove this item from the Playlist"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "ContentBrowser.AssetActions.Delete"),
			FUIAction(FExecuteAction::CreateLambda([This]() { This->RemoveClickedDelegate.Execute(This); }))
		);
	}

	return MenuBuilder.MakeWidget();
}


#undef LOCTEXT_NAMESPACE
