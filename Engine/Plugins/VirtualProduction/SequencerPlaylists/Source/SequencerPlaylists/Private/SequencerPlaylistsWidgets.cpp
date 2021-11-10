// Copyright Epic Games, Inc. All Rights Reserved.

#include "SequencerPlaylistsWidgets.h"
#include "SequencerPlaylist.h"
#include "SequencerPlaylistItem.h"
#include "SequencerPlaylistItem_Sequence.h"
#include "SequencerPlaylistPlayer.h"
#include "SequencerPlaylistsStyle.h"

#include "AssetData.h"
#include "AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "ContentBrowserModule.h"
#include "FileHelpers.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IContentBrowserSingleton.h"
#include "ISinglePropertyView.h"
#include "LevelEditor.h"
#include "Misc/FileHelper.h"
#include "ScopedTransaction.h"
#include "SlateOptMacros.h"
#include "Styling/SlateIconFinder.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"


#define LOCTEXT_NAMESPACE "SequencerPlaylists"


const float SSequencerPlaylistPanel::DefaultWidth = 850.0f;


BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SSequencerPlaylistPanel::Construct(const FArguments& InArgs, USequencerPlaylistPlayer* InPlayer)
{
	check(InPlayer);
	WeakPlayer = InPlayer;

	const ISlateStyle& SpmStyle = FSequencerPlaylistsStyle::Get();

	SAssignNew(ItemList, SDragAndDropVerticalBox)
		.OnCanAcceptDrop(this, &SSequencerPlaylistPanel::HandleCanAcceptDrop)
		.OnAcceptDrop(this, &SSequencerPlaylistPanel::HandleAcceptDrop)
		.OnDragDetected(this, &SSequencerPlaylistPanel::HandleDragDetected);

	(*ItemList)
		.SetDropIndicator_Above(*SpmStyle.GetBrush("SequencerPlaylists.DropZone.Above"))
		.SetDropIndicator_Below(*SpmStyle.GetBrush("SequencerPlaylists.DropZone.Below"));

	RegenerateSequenceList();

	ChildSlot
	[
		SNew(SScrollBox)
		+ SScrollBox::Slot()
		[
			SNew(SComboButton)
			.ContentPadding(4.f)
			.OnGetMenuContent(this, &SSequencerPlaylistPanel::OnPresetGeneratePresetsMenu)
			.ButtonContent()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.Padding(4.f, 0.f, 4.f, 0.f)
				.AutoWidth()
				[
					SNew(SImage)
					.Image(FSlateIconFinder::FindIconBrushForClass(USequencerPlaylist::StaticClass()))
				]
				+ SHorizontalBox::Slot()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("PresetsToolbarButton", "Presets"))
				]
			]
		]
		+ SScrollBox::Slot()
		[
			SNew(SSpacer)
			.Size(FVector2D(1.0f, 10.0f))
		]
		+ SScrollBox::Slot()
		.Padding(10.0f)
		[
			// Play all
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(0.04f)
			[
				SNew(SBorder)
				.BorderImage(SpmStyle.GetBrush("SequencerPlaylists.SequenceBorderBrush"))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				[
					SNew(SButton)
					.Text(LOCTEXT("PlayAllButton", "Play All"))
					.HAlign(HAlign_Center)
					.OnClicked(this, &SSequencerPlaylistPanel::OnClicked_PlayAll)
				]
				+ SVerticalBox::Slot()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(2.0f, 1.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("HoldAllAtFirstFrameCheckLabel", "Hold All at First Frame"))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(4.0f, 1.0f, 10.0f, 0.0f)
					[
						SNew(SCheckBox)
						.IsChecked(ECheckBoxState::Undetermined)
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SNew(SButton)
						.Text(LOCTEXT("StopAllButton", "Stop All"))
						.HAlign(HAlign_Center)
						.OnClicked(this, &SSequencerPlaylistPanel::OnClicked_StopAll)
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SNew(SButton)
						.Text(LOCTEXT("ResetAllButton", "Reset All"))
						.HAlign(HAlign_Center)
						.OnClicked(this, &SSequencerPlaylistPanel::OnClicked_ResetAll)
					]
				]
			]
		]
		+ SScrollBox::Slot()
		[
			SNew(SSpacer)
			.Size(FVector2D(1.0f, 10.0f))
		]
		+ SScrollBox::Slot()
		[
			ItemList.ToSharedRef()
		]
		+ SScrollBox::Slot()
		.Padding(10.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("AddSequenceButton", "Add Sequence"))
				.HAlign(HAlign_Center)
				.OnClicked(this, &SSequencerPlaylistPanel::OnClicked_AddSequence)
			]
		]
	];
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION


BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SSequencerPlaylistPanel::RegenerateSequenceList()
{
	ItemList->ClearChildren();

	USequencerPlaylistPlayer* Player = WeakPlayer.Get();
	if (!Player)
	{
		return;
	}

	USequencerPlaylist* Playlist = Player->GetPlaylist();
	if (!Playlist)
	{
		return;
	}

	for (USequencerPlaylistItem* Item : Playlist->Items)
	{
		ItemList->AddSlot()
		[
			SNew(SSequencerPlaylistItemWidget, Item)
			.OnPlayClicked(SharedThis(this), &SSequencerPlaylistPanel::OnClicked_Item_Play)
			.OnStopClicked(SharedThis(this), &SSequencerPlaylistPanel::OnClicked_Item_Stop)
			.OnResetClicked(SharedThis(this), &SSequencerPlaylistPanel::OnClicked_Item_Reset)
			.OnCloseClicked(SharedThis(this), &SSequencerPlaylistPanel::OnClicked_Item_Close)
		];
	}
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION


TSharedRef<SWidget> SSequencerPlaylistPanel::OnPresetGeneratePresetsMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	IContentBrowserSingleton& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get();

	MenuBuilder.AddMenuEntry(
		LOCTEXT("SaveAsPreset_Text", "Save As Preset"),
		LOCTEXT("SaveAsPreset_Tip", "Save the current setup as a new preset that can be loaded at a later date"),
		FSlateIcon(FEditorStyle::Get().GetStyleSetName(), "AssetEditor.SaveAsset"),
		FUIAction(FExecuteAction::CreateSP(this, &SSequencerPlaylistPanel::OnSaveAsPreset))
	);

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

		AssetPickerConfig.AssetShowWarningText = LOCTEXT("NoPresets_Warning", "No Presets Found");
		AssetPickerConfig.Filter.ClassNames.Add(USequencerPlaylist::StaticClass()->GetFName());
		AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateSP(this, &SSequencerPlaylistPanel::OnLoadPreset);
	}

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("LoadPreset_MenuSection", "Load Preset"));
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
		SaveAssetDialogConfig.DialogTitleOverride = LOCTEXT("SavePresetDialogTitle", "Save Sequencer Playlist Preset");
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


bool GetSavePresetPackageName(FString& OutName)
{
	//ULiveLinkSettings* ConfigSettings = GetMutableDefault<ULiveLinkSettings>();

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
	FString DefaultName = LOCTEXT("NewPresetDefaultName", "NewSequencerPlaylistPreset").ToString();

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


void SSequencerPlaylistPanel::OnSaveAsPreset()
{
	USequencerPlaylist* Playlist = GetCheckedPlaylist();
	if (!Playlist)
	{
		return;
	}

	FString PackageName;
	if (!GetSavePresetPackageName(PackageName))
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("SaveAsPreset", "Save As Preset"));

	// Saving into a new package
	const FString NewAssetName = FPackageName::GetLongPackageAssetName(PackageName);
	UPackage* NewPackage = CreatePackage(*PackageName);
	USequencerPlaylist* NewPlaylist = NewObject<USequencerPlaylist>(NewPackage, *NewAssetName, RF_Public | RF_Standalone | RF_Transactional);

	if (NewPlaylist)
	{
		for (USequencerPlaylistItem* Item : Playlist->Items)
		{
			NewPlaylist->Items.Add(DuplicateObject<USequencerPlaylistItem>(Item, NewPlaylist));
		}

		NewPlaylist->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(NewPlaylist);

		FEditorFileUtils::PromptForCheckoutAndSave({ NewPackage }, false, false);
	}
}


void SSequencerPlaylistPanel::OnLoadPreset(const FAssetData& InPreset)
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
		FScopedTransaction Transaction(LOCTEXT("LoadPreset_Transaction", "Load Sequencer Playlist preset"));
		Playlist->Items.Empty();
		for (USequencerPlaylistItem* Item : ImportedPlaylist->Items)
		{
			Playlist->Items.Add(DuplicateObject<USequencerPlaylistItem>(Item, Playlist));
		}
		RegenerateSequenceList();
	}
}


FReply SSequencerPlaylistPanel::OnClicked_PlayAll()
{
	USequencerPlaylistPlayer* Player = WeakPlayer.Get();
	if (!ensure(Player))
	{
		return FReply::Unhandled();
	}

	Player->PlayAll();
	return FReply::Handled();
}


FReply SSequencerPlaylistPanel::OnClicked_StopAll()
{
	USequencerPlaylistPlayer* Player = WeakPlayer.Get();
	if (!ensure(Player))
	{
		return FReply::Unhandled();
	}

	Player->StopAll();
	return FReply::Handled();
}


FReply SSequencerPlaylistPanel::OnClicked_ResetAll()
{
	USequencerPlaylistPlayer* Player = WeakPlayer.Get();
	if (!ensure(Player))
	{
		return FReply::Unhandled();
	}

	Player->ResetAll();
	return FReply::Handled();
}


FReply SSequencerPlaylistPanel::OnClicked_AddSequence()
{
	USequencerPlaylist* Playlist = GetCheckedPlaylist();
	if (!Playlist)
	{
		return FReply::Unhandled();
	}

	Playlist->Items.Add(NewObject<USequencerPlaylistItem_Sequence>(Playlist));
	RegenerateSequenceList();

	return FReply::Handled();
}


FReply SSequencerPlaylistPanel::OnClicked_Item_Play(TSharedPtr<SSequencerPlaylistItemWidget> ItemWidget)
{
	USequencerPlaylistPlayer* Player = WeakPlayer.Get();
	if (!ensure(Player))
	{
		return FReply::Unhandled();
	}

	Player->PlayItem(ItemWidget->GetItem());
	return FReply::Handled();
}


FReply SSequencerPlaylistPanel::OnClicked_Item_Stop(TSharedPtr<SSequencerPlaylistItemWidget> ItemWidget)
{
	USequencerPlaylistPlayer* Player = WeakPlayer.Get();
	if (!ensure(Player))
	{
		return FReply::Unhandled();
	}

	Player->StopItem(ItemWidget->GetItem());
	return FReply::Handled();
}


FReply SSequencerPlaylistPanel::OnClicked_Item_Reset(TSharedPtr<SSequencerPlaylistItemWidget> ItemWidget)
{
	USequencerPlaylistPlayer* Player = WeakPlayer.Get();
	if (!ensure(Player))
	{
		return FReply::Unhandled();
	}

	Player->ResetItem(ItemWidget->GetItem());
	return FReply::Handled();
}


FReply SSequencerPlaylistPanel::OnClicked_Item_Close(TSharedPtr<SSequencerPlaylistItemWidget> ItemWidget)
{
	USequencerPlaylist* Playlist = GetCheckedPlaylist();
	if (!Playlist)
	{
		return FReply::Unhandled();
	}

	ensure(Playlist->Items.RemoveSingle(ItemWidget->GetItem()));
	RegenerateSequenceList();
	return FReply::Handled();
}

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


FReply SSequencerPlaylistPanel::HandleDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, int32 SlotIndex, SVerticalBox::FSlot* Slot)
{
	USequencerPlaylist* Playlist = GetCheckedPlaylist();
	if (!Playlist)
	{
		return FReply::Unhandled();
	}

	if (ensure(Playlist->Items.IsValidIndex(SlotIndex)))
	{
		TSharedPtr<SBox> DragWidget = SNew(SBox)
			.MinDesiredWidth(MyGeometry.GetLocalSize().X)
			[
				SNew(SSequencerPlaylistItemWidget, Playlist->Items[SlotIndex])
			];

		return FReply::Handled().BeginDragDrop(FSequencerPlaylistItemDragDropOp::New(SlotIndex, Slot, DragWidget));
	}

	return FReply::Unhandled();
}


TOptional<SDragAndDropVerticalBox::EItemDropZone> SSequencerPlaylistPanel::HandleCanAcceptDrop(const FDragDropEvent& DragDropEvent, SDragAndDropVerticalBox::EItemDropZone DropZone, SVerticalBox::FSlot* Slot)
{
	TSharedPtr<FSequencerPlaylistItemDragDropOp> DragDropOperation = DragDropEvent.GetOperationAs<FSequencerPlaylistItemDragDropOp>();
	if (DragDropOperation.IsValid())
	{
		return DropZone;
	}

	return TOptional<SDragAndDropVerticalBox::EItemDropZone>();
}


FReply SSequencerPlaylistPanel::HandleAcceptDrop(const FDragDropEvent& DragDropEvent, SDragAndDropVerticalBox::EItemDropZone DropZone, int32 SlotIndex, SVerticalBox::FSlot* Slot)
{
	USequencerPlaylist* Playlist = GetCheckedPlaylist();
	if (!Playlist)
	{
		return FReply::Unhandled();
	}

	TSharedPtr<FSequencerPlaylistItemDragDropOp> Operation = DragDropEvent.GetOperationAs<FSequencerPlaylistItemDragDropOp>();
	if (Operation.IsValid())
	{
		const int32 SrcIndex = Operation->SlotIndexBeingDragged;
		const int32 DstIndex = SlotIndex;
		TArray<TObjectPtr<USequencerPlaylistItem>>& Items = Playlist->Items;
		if (ensure(Items.IsValidIndex(SrcIndex)) && ensure(Items.IsValidIndex(DstIndex)))
		{
			USequencerPlaylistItem* ItemToMove = Items[SrcIndex];
			Items.RemoveAt(SrcIndex);
			Items.Insert(ItemToMove, DstIndex);
			return FReply::Handled();
		}
	}

	return FReply::Unhandled();
}


TSharedRef<FSequencerPlaylistItemDragDropOp> FSequencerPlaylistItemDragDropOp::New(int32 InSlotIndexBeingDragged, SVerticalBox::FSlot* InSlotBeingDragged, TSharedPtr<SWidget> WidgetToShow)
{
	TSharedRef<FSequencerPlaylistItemDragDropOp> Operation = MakeShared<FSequencerPlaylistItemDragDropOp>();

	Operation->MouseCursor = EMouseCursor::GrabHandClosed;
	Operation->SlotIndexBeingDragged = InSlotIndexBeingDragged;
	Operation->SlotBeingDragged = InSlotBeingDragged;
	Operation->WidgetToShow = WidgetToShow;

	Operation->Construct();

	return Operation;
}


FSequencerPlaylistItemDragDropOp::~FSequencerPlaylistItemDragDropOp()
{
}


TSharedPtr<SWidget> FSequencerPlaylistItemDragDropOp::GetDefaultDecorator() const
{
	return SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("ContentBrowser.AssetDragDropTooltipBackground"))
		.Content()
		[
			WidgetToShow.ToSharedRef()
		];
}


BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SSequencerPlaylistItemWidget::Construct(const FArguments& InArgs, USequencerPlaylistItem* InItem)
{
	check(InItem);
	WeakItem = InItem;

	USequencerPlaylistItem_Sequence* Item =
		CastChecked<USequencerPlaylistItem_Sequence>(InItem);

	const ISlateStyle& SpmStyle = FSequencerPlaylistsStyle::Get();

	static const FName PropertyEditorModuleName("PropertyEditor");
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>(PropertyEditorModuleName);

	FSinglePropertyParams SequenceViewParams;
	SequenceViewParams.bHideAssetThumbnail = true;
	TSharedPtr<ISinglePropertyView> PropViewSequence = PropertyEditorModule.CreateSingleProperty(
		Item, GET_MEMBER_NAME_CHECKED(USequencerPlaylistItem_Sequence, Sequence), SequenceViewParams);

	TSharedPtr<ISinglePropertyView> PropViewStartFrameOffset = PropertyEditorModule.CreateSingleProperty(
		Item, GET_MEMBER_NAME_CHECKED(USequencerPlaylistItem, StartFrameOffset), {});

	TSharedPtr<ISinglePropertyView> PropViewEndFrameOffset = PropertyEditorModule.CreateSingleProperty(
		Item, GET_MEMBER_NAME_CHECKED(USequencerPlaylistItem, EndFrameOffset), {});

	TSharedPtr<ISinglePropertyView> PropViewHold = PropertyEditorModule.CreateSingleProperty(
		Item, GET_MEMBER_NAME_CHECKED(USequencerPlaylistItem, bHoldAtFirstFrame), {});

	TSharedPtr<ISinglePropertyView> PropViewNumLoops = PropertyEditorModule.CreateSingleProperty(
		Item, GET_MEMBER_NAME_CHECKED(USequencerPlaylistItem, NumLoops), {});

	ChildSlot
	.Padding(10.0f)
	[
		SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(4.0f, 4.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(0.02f)
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SBorder)
				.BorderImage(SpmStyle.GetBrush("SequencerPlaylists.SequenceBorderBrush"))
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Center)
				[
					SNew(SImage)
					.Image(FCoreStyle::Get().GetBrush("Icons.DragHandle"))
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.HAlign(HAlign_Fill)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						PropViewSequence.ToSharedRef()
					]
					+ SHorizontalBox::Slot()
					.FillWidth(0.5f)
					[
						PropViewStartFrameOffset.ToSharedRef()
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SNew(SButton)
						.Text(LOCTEXT("PlaySequenceButton", "Play"))
						.HAlign(HAlign_Center)
						.OnClicked_Lambda([this, InArgs]()
							{
								return InArgs._OnPlayClicked.IsBound() ? InArgs._OnPlayClicked.Execute(SharedThis(this)) : FReply::Unhandled();
							})
					]
				]
				+ SVerticalBox::Slot()
				.HAlign(HAlign_Fill)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(0.5f)
					[
						PropViewHold.ToSharedRef()
					]
					+ SHorizontalBox::Slot()
					.FillWidth(0.5f)
					[
						PropViewNumLoops.ToSharedRef()
					]
					+ SHorizontalBox::Slot()
					.FillWidth(0.5f)
					[
						PropViewEndFrameOffset.ToSharedRef()
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						[
							SNew(SButton)
							.Text(LOCTEXT("StopSequenceButton", "Stop"))
							.HAlign(HAlign_Center)
							.OnClicked_Lambda([this, InArgs]()
								{
									return InArgs._OnStopClicked.IsBound() ? InArgs._OnStopClicked.Execute(SharedThis(this)) : FReply::Unhandled();
								})
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						[
							SNew(SButton)
							.Text(LOCTEXT("ResetSequenceButton", "Reset"))
							.HAlign(HAlign_Center)
							.OnClicked_Lambda([this, InArgs]()
								{
									return InArgs._OnResetClicked.IsBound() ? InArgs._OnResetClicked.Execute(SharedThis(this)) : FReply::Unhandled();
								})
						]
					]
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Top)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.OnClicked_Lambda([this, InArgs]()
					{
						return InArgs._OnCloseClicked.IsBound() ? InArgs._OnCloseClicked.Execute(SharedThis(this)) : FReply::Unhandled();
					})
				[
					SNew(SImage)
					.Image(FAppStyle::Get().GetBrush("Icons.X"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			]
		]
	];
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION


#undef LOCTEXT_NAMESPACE
