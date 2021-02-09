// Copyright Epic Games, Inc. All Rights Reserved.

#include "SAssetPlacementPalette.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "AssetPlacementPaletteItem.h"
#include "Misc/MessageDialog.h"
#include "Misc/FeedbackContext.h"
#include "Modules/ModuleManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/Commands/UICommandList.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Layout/SScrollBorder.h"
#include "Widgets/Input/SSlider.h"
#include "EditorStyleSet.h"
#include "AssetThumbnail.h"
#include "PropertyEditorModule.h"

#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "PropertyCustomizationHelpers.h"
#include "IDetailsView.h"
#include "AssetSelection.h"
#include "ScopedTransaction.h"
#include "AssetData.h"
#include "Engine/StaticMesh.h"
#include "Editor.h"
#include "Widgets/Layout/SScrollBox.h"

#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Input/SSearchBox.h"
#include "AssetPlacementEdModeStyle.h"
#include "AssetPlacementSettings.h"
#include "Selection.h"
#include "Subsystems/PlacementSubsystem.h"

#define LOCTEXT_NAMESPACE "AssetPlacementMode"

////////////////////////////////////////////////
// SPlacementDragDropHandler
////////////////////////////////////////////////

/** Drag-drop zone for adding Placement types to the palette */
class SAssetPaletteDragDropHandler : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAssetPaletteDragDropHandler) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_EVENT(FOnDrop, OnDrop)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		bIsDragOn = false;
		OnDropDelegate = InArgs._OnDrop;

		this->ChildSlot
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(this, &SAssetPaletteDragDropHandler::GetBackgroundColor)
				.Padding(FMargin(100))
				[
					InArgs._Content.Widget
				]
			];
	}

	FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
	{
		bIsDragOn = false;
		if (OnDropDelegate.IsBound())
		{
			return OnDropDelegate.Execute(MyGeometry, DragDropEvent);
		}
		
		return FReply::Handled();
	}

	virtual void OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
	{
		bIsDragOn = true;
	}

	virtual void OnDragLeave(const FDragDropEvent& DragDropEvent) override
	{
		bIsDragOn = false;
	}

private:
	FSlateColor GetBackgroundColor() const
	{
		return bIsDragOn ? FLinearColor(1.0f, 0.6f, 0.1f, 0.9f) : FLinearColor(0.1f, 0.1f, 0.1f, 0.9f);
	}

private:
	FOnDrop OnDropDelegate;
	bool bIsDragOn;
};

////////////////////////////////////////////////
// SUneditablePlacementTypeWarning
////////////////////////////////////////////////
class SUneditableAssetTypeWarning : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SUneditableAssetTypeWarning)
		: _WarningText()
		, _OnHyperlinkClicked()
	{}

	/** The rich text to show in the warning */
	SLATE_ATTRIBUTE(FText, WarningText)

		/** Called when the hyperlink in the rich text is clicked */
		SLATE_EVENT(FSlateHyperlinkRun::FOnClick, OnHyperlinkClicked)

		SLATE_END_ARGS()

		/** Constructs the widget */
		void Construct(const FArguments& InArgs)
	{
		ChildSlot
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.Padding(2)
					[
						SNew(SImage)
						.Image(FAppStyle::Get().GetBrush("Icons.Warning"))
					]
					+ SHorizontalBox::Slot()
						.VAlign(VAlign_Center)
						.Padding(2)
						[
							SNew(SRichTextBlock)
							.DecoratorStyleSet(&FAppStyle::Get())
							.Justification(ETextJustify::Left)
							.TextStyle(FAppStyle::Get(), "DetailsView.BPMessageTextStyle")
							.Text(InArgs._WarningText)
							.AutoWrapText(true)
							+ SRichTextBlock::HyperlinkDecorator(TEXT("HyperlinkDecorator"), InArgs._OnHyperlinkClicked)
						]
				]
			];
	}
};

////////////////////////////////////////////////
// SPlacementPalette
////////////////////////////////////////////////
void SAssetPlacementPalette::Construct(const FArguments& InArgs)
{
	bItemsNeedRebuild = false;
	bIsRebuildTimerRegistered = false;
	bIsRefreshTimerRegistered = false;
	PlacementSettings = InArgs._PlacementSettings;
	if (!PlacementSettings.IsValid())
	{
		PlacementSettings = GetMutableDefault<UAssetPlacementSettings>();
	}

	UICommandList = MakeShared<FUICommandList>();

	// Size of the thumbnail pool should be large enough to show a reasonable amount of Placement assets on screen at once,
	// otherwise some thumbnail images will appear duplicated.
	ThumbnailPool = MakeShared<FAssetThumbnailPool>(64);

	TypeFilter = MakeShared<PlacementTypeTextFilter>(PlacementTypeTextFilter::FItemToStringArray::CreateSP(this, &SAssetPlacementPalette::GetPaletteItemFilterString));

	const FText BlankText = FText::GetEmpty();

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.HAlign(HAlign_Fill)
		.AutoHeight()
		[
			// Top bar
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("DetailsView.CategoryTop"))
			.BorderBackgroundColor(FLinearColor(.6f, .6f, .6f, 1.0f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.VAlign(VAlign_Center)
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.FillWidth(.75f)
					[
						SAssignNew(SearchBoxPtr, SSearchBox)
						.HintText(LOCTEXT("SearchPlacementPaletteHint", "Search Palette"))
						.OnTextChanged(this, &SAssetPlacementPalette::OnSearchTextChanged)
					]

					// View Options
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SComboButton)
						.ForegroundColor(FSlateColor::UseForeground())
						.ButtonStyle(FAppStyle::Get(), "ToggleButton")
						.OnGetMenuContent(this, &SAssetPlacementPalette::GetViewOptionsMenuContent)
						.ButtonContent()
						[
							SNew(SBox)
							.VAlign(VAlign_Center)
							[
								SNew(SImage)
								.Image(FAppStyle::Get().GetBrush("GenericViewButton"))
							]
						]
					]
				]
			]
		]

		+ SVerticalBox::Slot()
		[
			SNew(SOverlay)

			+ SOverlay::Slot()
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				[
					SNew(SBox)
					.Visibility(this, &SAssetPlacementPalette::GetDropPlacementHintVisibility)
					.MinDesiredHeight(100)
					[
						SNew(SScaleBox)
						.Stretch(EStretch::ScaleToFit)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("Placement_DropStatic", "+ Drop Assets Here"))
							.ToolTipText(LOCTEXT("Placement_DropStatic_ToolTip", "Drag and drop asset types from the Content Browser to add them to the palette."))
						]
					]
				]

				+ SVerticalBox::Slot()
				.Padding(2.f, 0.f)
				[
					CreatePaletteViews()
				]
			]
			
			// Placement Mesh Drop Zone
			+ SOverlay::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			[
				SNew(SAssetPaletteDragDropHandler)
				.Visibility(this, &SAssetPlacementPalette::GetPlacementDropTargetVisibility)
				.OnDrop(this, &SAssetPlacementPalette::HandlePlacementDropped)
				[
					SNew(SScaleBox)
					.Stretch(EStretch::ScaleToFit)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("Placement_AddPlacementMesh", "+ Asset Type"))
						.ShadowOffset(FVector2D(1.f, 1.f))
					]
				]
			]
		]
	];

	UpdatePalette(true);
}

SAssetPlacementPalette::~SAssetPlacementPalette()
{
}

FReply SAssetPlacementPalette::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (UICommandList->ProcessCommandBindings(InKeyEvent))
	{
		return FReply::Handled();
	}

	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

void SAssetPlacementPalette::UpdatePalette(bool bRebuildItems)
{
	bItemsNeedRebuild |= bRebuildItems;

	if (!bIsRebuildTimerRegistered)
	{
		bIsRebuildTimerRegistered = true;
		RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SAssetPlacementPalette::UpdatePaletteItems));
	}
}

void SAssetPlacementPalette::RefreshPalette()
{
	// Do not register the refresh timer if we're pending a rebuild; rebuild should cause the palette to refresh
	if (!bIsRefreshTimerRegistered && !bIsRebuildTimerRegistered)
	{
		bIsRefreshTimerRegistered = true;
		RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SAssetPlacementPalette::RefreshPaletteItems));
	}
}

bool SAssetPlacementPalette::AnySelectedTileHovered() const
{
	for (FPlacementPaletteItemModelPtr& PaletteItem : GetActiveViewWidget()->GetSelectedItems())
	{
		TSharedPtr<ITableRow> Tile = TileViewWidget->WidgetFromItem(PaletteItem);
		if (Tile.IsValid() && Tile->AsWidget()->IsHovered())
		{
			return true;
		}
	}

	return false;
}

void SAssetPlacementPalette::ActivateAllSelectedTypes(bool bActivate) const
{
	// Apply the new check state to all of the selected types
	for (FPlacementPaletteItemModelPtr& PaletteItem : GetActiveViewWidget()->GetSelectedItems())
	{
		PaletteItem->SetTypeActiveInPalette(bActivate);
	}
}

void SAssetPlacementPalette::RefreshActivePaletteViewWidget()
{
	if (ActiveViewMode == EAssetPlacementPaletteViewMode::Thumbnail)
	{
		TileViewWidget->RequestListRefresh();
	}
	else
	{
		TreeViewWidget->RequestTreeRefresh();
	}
}

void SAssetPlacementPalette::AddPlacementType(const FAssetData& AssetData)
{
	if (AddPlacementTypeCombo.IsValid())
	{
		AddPlacementTypeCombo->SetIsOpen(false);
	}

	if (!AssetData.IsValid())
	{
		return;
	}

	if (AssetData.GetClass()->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_NotPlaceable))
	{
		return;
	}

	TScriptInterface<IAssetFactoryInterface> FactoryInterface;
	if (UPlacementSubsystem* PlacementSubystem = GEditor->GetEditorSubsystem<UPlacementSubsystem>())
	{
		FactoryInterface = PlacementSubystem->FindAssetFactoryFromAssetData(AssetData);
	}

	if (!FactoryInterface)
	{
		return;
	}

	// todo - can the palette items be a map? will this break the list view?
	if (PaletteItems.FindByPredicate([&AssetData](TSharedPtr<FAssetPlacementPaletteItemModel>& InItem) { return InItem.IsValid() && (InItem->GetTypeUIInfo()->AssetData.ObjectPath == AssetData.ObjectPath); }))
	{
		return;
	}

	FAssetPlacementUIInfoPtr PlacementInfo = MakeShared<FPaletteItem>();
	PlacementInfo->AssetData = AssetData;
	PlacementInfo->FactoryOverride = FactoryInterface;
	PaletteItems.Add(MakeShared<FAssetPlacementPaletteItemModel>(PlacementInfo, SharedThis(this), ThumbnailPool));
	if (PlacementSettings.IsValid())
	{
		PlacementSettings->PaletteItems.Add(*PlacementInfo);
	}
	UpdatePalette(true);
}

TSharedRef<SWidgetSwitcher> SAssetPlacementPalette::CreatePaletteViews()
{
	const FText BlankText = FText::GetEmpty();

	// Tile View Widget
	SAssignNew(TileViewWidget, SPlacementTypeTileView)
		.ListItemsSource(&FilteredItems)
		.SelectionMode(ESelectionMode::Multi)
		.OnGenerateTile(this, &SAssetPlacementPalette::GenerateTile)
		.OnContextMenuOpening(this, &SAssetPlacementPalette::ConstructPlacementTypeContextMenu)
		.OnSelectionChanged(this, &SAssetPlacementPalette::OnSelectionChanged)
		.ItemHeight(this, &SAssetPlacementPalette::GetScaledThumbnailSize)
		.ItemWidth(this, &SAssetPlacementPalette::GetScaledThumbnailSize)
		.ItemAlignment(EListItemAlignment::LeftAligned)
		.ClearSelectionOnClick(true)
		.OnMouseButtonDoubleClick(this, &SAssetPlacementPalette::OnItemDoubleClicked);

	// Tree View Widget
	SAssignNew(TreeViewWidget, SPlacementTypeTreeView)
		.TreeItemsSource(&FilteredItems)
		.SelectionMode(ESelectionMode::Multi)
		.OnGenerateRow(this, &SAssetPlacementPalette::TreeViewGenerateRow)
		.OnGetChildren(this, &SAssetPlacementPalette::TreeViewGetChildren)
		.OnContextMenuOpening(this, &SAssetPlacementPalette::ConstructPlacementTypeContextMenu)
		.OnSelectionChanged(this, &SAssetPlacementPalette::OnSelectionChanged)
		.OnMouseButtonDoubleClick(this, &SAssetPlacementPalette::OnItemDoubleClicked)
		.HeaderRow
		(
			// Toggle Active
			SAssignNew(TreeViewHeaderRow, SHeaderRow)
			+ SHeaderRow::Column(AssetPlacementPaletteTreeColumns::ColumnID_ToggleActive)
			[
				SNew(SCheckBox)
				.IsChecked(this, &SAssetPlacementPalette::GetState_AllMeshes)
			.OnCheckStateChanged(this, &SAssetPlacementPalette::OnCheckStateChanged_AllMeshes)
			]
			.DefaultLabel(BlankText)
			.HeaderContentPadding(FMargin(0, 1, 0, 1))
			.HAlignHeader(HAlign_Center)
			.HAlignCell(HAlign_Center)
			.FixedWidth(24)

			// Type
			+ SHeaderRow::Column(AssetPlacementPaletteTreeColumns::ColumnID_Type)
			.HeaderContentPadding(FMargin(10, 1, 0, 1))
			.SortMode(this, &SAssetPlacementPalette::GetMeshColumnSortMode)
			.OnSort(this, &SAssetPlacementPalette::OnTypeColumnSortModeChanged)
			.DefaultLabel(this, &SAssetPlacementPalette::GetTypeColumnHeaderText)
			.FillWidth(5.f)
		);

	// View Mode Switcher
	SAssignNew(WidgetSwitcher, SWidgetSwitcher);

	// Thumbnail View
	WidgetSwitcher->AddSlot((int32)EAssetPlacementPaletteViewMode::Thumbnail)
	[
		SNew(SScrollBorder, TileViewWidget.ToSharedRef())
		.Content()
		[
			TileViewWidget.ToSharedRef()
		]
	];

	// Tree View
	WidgetSwitcher->AddSlot((int32)EAssetPlacementPaletteViewMode::Tree)
	[
		SNew(SScrollBorder, TreeViewWidget.ToSharedRef())
		.Style(&FAssetPlacementEdModeStyle::Get().GetWidgetStyle<FScrollBorderStyle>("FoliageEditMode.TreeView.ScrollBorder"))
		.Content()
		[
			TreeViewWidget.ToSharedRef()
		]
	];

	WidgetSwitcher->SetActiveWidgetIndex((int32)ActiveViewMode);

	return WidgetSwitcher.ToSharedRef();
}

void SAssetPlacementPalette::GetPaletteItemFilterString(FPlacementPaletteItemModelPtr PaletteItemModel, TArray<FString>& OutArray) const
{
	OutArray.Add(PaletteItemModel->GetDisplayFName().ToString());
}

void SAssetPlacementPalette::OnSearchTextChanged(const FText& InFilterText)
{
	TypeFilter->SetRawFilterText(InFilterText);
	SearchBoxPtr->SetError(TypeFilter->GetFilterErrorText());
	UpdatePalette();
}

TSharedRef<SWidget> SAssetPlacementPalette::GetAddPlacementTypePicker()
{
	return PropertyCustomizationHelpers::MakeAssetPickerWithMenu(FAssetData(),
		false,
		TArray<const UClass*>({UObject::StaticClass()}),
		TArray<UFactory*>(),
		FOnShouldFilterAsset::CreateSP(this, &SAssetPlacementPalette::ShouldFilterAsset),
		FOnAssetSelected::CreateSP(this, &SAssetPlacementPalette::AddPlacementType),
		FSimpleDelegate());
}

bool SAssetPlacementPalette::ShouldFilterAsset(const FAssetData& InAssetData)
{
	UClass* Class = InAssetData.GetClass();

	if (Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_NotPlaceable))
	{
		return true;
	}

	if (UPlacementSubsystem* PlacementSubsystem = GEditor->GetEditorSubsystem<UPlacementSubsystem>())
	{
		return (PlacementSubsystem->FindAssetFactoryFromAssetData(InAssetData) != nullptr);
	}

	return true;
}

void SAssetPlacementPalette::SetViewMode(EAssetPlacementPaletteViewMode NewViewMode)
{
	if (ActiveViewMode != NewViewMode)
	{
		ActiveViewMode = NewViewMode;
		switch (ActiveViewMode)
		{
		case EAssetPlacementPaletteViewMode::Thumbnail:
			// Set the tile selection to be the current tree selections
			TileViewWidget->ClearSelection();
			for (auto& TypeInfo : TreeViewWidget->GetSelectedItems())
			{
				TileViewWidget->SetItemSelection(TypeInfo, true);
			}
			break;
			
		case EAssetPlacementPaletteViewMode::Tree:
			// Set the tree selection to be the current tile selection
			TreeViewWidget->ClearSelection();
			for (auto& TypeInfo : TileViewWidget->GetSelectedItems())
			{
				TreeViewWidget->SetItemSelection(TypeInfo, true);
			}
			break;
		}

		WidgetSwitcher->SetActiveWidgetIndex((int32)ActiveViewMode);
		
		RefreshActivePaletteViewWidget();
	}
}

bool SAssetPlacementPalette::IsActiveViewMode(EAssetPlacementPaletteViewMode ViewMode) const
{
	return ActiveViewMode == ViewMode;
}

void SAssetPlacementPalette::ToggleShowTooltips()
{
	bShowFullTooltips = !bShowFullTooltips;
}

bool SAssetPlacementPalette::ShouldShowTooltips() const
{
	return bShowFullTooltips;
}

FText SAssetPlacementPalette::GetSearchText() const
{
	return TypeFilter->GetRawFilterText();
}

void SAssetPlacementPalette::OnSelectionChanged(FPlacementPaletteItemModelPtr Item, ESelectInfo::Type SelectInfo)
{
	// Not yet implemented
}

void SAssetPlacementPalette::OnItemDoubleClicked(FPlacementPaletteItemModelPtr Item) const
{
	Item->SetTypeActiveInPalette(!Item->IsActive());
}

TSharedRef<SWidget> SAssetPlacementPalette::GetViewOptionsMenuContent()
{
	FMenuBuilder MenuBuilder(true, UICommandList);

	MenuBuilder.BeginSection("PlacementPaletteViewMode", LOCTEXT("ViewModeHeading", "Palette View Mode"));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("ThumbnailView", "Thumbnails"),
			LOCTEXT("ThumbnailView_ToolTip", "Display thumbnails for each Placement type in the palette."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SAssetPlacementPalette::SetViewMode, EAssetPlacementPaletteViewMode::Thumbnail),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &SAssetPlacementPalette::IsActiveViewMode, EAssetPlacementPaletteViewMode::Thumbnail)
				),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
			);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("ListView", "List"),
			LOCTEXT("ListView_ToolTip", "Display Placement types in the palette as a list."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SAssetPlacementPalette::SetViewMode, EAssetPlacementPaletteViewMode::Tree),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &SAssetPlacementPalette::IsActiveViewMode, EAssetPlacementPaletteViewMode::Tree)
				),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
			);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("PlacementPaletteViewOptions", LOCTEXT("ViewOptionsHeading", "View Options"));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("ShowTooltips", "Show Tooltips"),
			LOCTEXT("ShowTooltips_ToolTip", "Whether to show tooltips when hovering over Placement types in the palette."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SAssetPlacementPalette::ToggleShowTooltips),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &SAssetPlacementPalette::ShouldShowTooltips),
				FIsActionButtonVisible::CreateSP(this, &SAssetPlacementPalette::IsActiveViewMode, EAssetPlacementPaletteViewMode::Tree)
				),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
			);

		MenuBuilder.AddWidget(
			SNew(SHorizontalBox)
			.Visibility(this, &SAssetPlacementPalette::GetThumbnailScaleSliderVisibility)
			+ SHorizontalBox::Slot()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ThumbnailScaleLabel", "Scale"))
			]
			+SHorizontalBox::Slot()
			[
				SNew(SSlider)
				.ToolTipText(LOCTEXT("ThumbnailScaleToolTip", "Adjust the size of thumbnails."))
				.Value(this, &SAssetPlacementPalette::GetThumbnailScale)
				.OnValueChanged(this, &SAssetPlacementPalette::SetThumbnailScale)
				.OnMouseCaptureEnd(this, &SAssetPlacementPalette::RefreshActivePaletteViewWidget)
			],
			FText(),
			/*bNoIndent=*/true
			);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

TSharedPtr<SListView<FPlacementPaletteItemModelPtr>> SAssetPlacementPalette::GetActiveViewWidget() const
{
	if (ActiveViewMode == EAssetPlacementPaletteViewMode::Thumbnail)
	{
		return TileViewWidget;
	}
	else if (ActiveViewMode == EAssetPlacementPaletteViewMode::Tree)
	{
		return TreeViewWidget;
	}
	
	return nullptr;
}

EVisibility SAssetPlacementPalette::GetDropPlacementHintVisibility() const
{
	return FilteredItems.Num() ? EVisibility::Collapsed : EVisibility::Visible;
}

EVisibility SAssetPlacementPalette::GetPlacementDropTargetVisibility() const
{
	if (FSlateApplication::Get().IsDragDropping())
	{
		TArray<FAssetData> DraggedAssets = AssetUtil::ExtractAssetDataFromDrag(FSlateApplication::Get().GetDragDroppingContent());
		for (const FAssetData& AssetData : DraggedAssets)
		{
			if (AssetData.GetClass()->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_NotPlaceable))
			{
				continue;
			}

			if (AssetData.IsValid())
			{
				return EVisibility::Visible;
			}
		}
	}

	return EVisibility::Hidden;
}

FReply SAssetPlacementPalette::HandlePlacementDropped(const FGeometry& DropZoneGeometry, const FDragDropEvent& DragDropEvent)
{
	TArray<FAssetData> DroppedAssetData = AssetUtil::ExtractAssetDataFromDrag(DragDropEvent);
	if (DroppedAssetData.Num() > 0)
	{
		// Treat the entire drop as a transaction (in case multiples types are being added)
		const FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "PlacementMode_DragDropTypesTransaction", "Drag-drop Placement"));

		for (auto& AssetData : DroppedAssetData)
		{
			AddPlacementType(AssetData);
		}
	}

	return FReply::Handled();
}

TSharedPtr<SWidget> SAssetPlacementPalette::ConstructPlacementTypeContextMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);
	return MenuBuilder.MakeWidget();
}

void SAssetPlacementPalette::OnActivatePlacementTypes()
{
	for (FPlacementPaletteItemModelPtr& PaletteItem : GetActiveViewWidget()->GetSelectedItems())
	{
		PaletteItem->SetTypeActiveInPalette(true);
	}
}

bool SAssetPlacementPalette::OnCanActivatePlacementTypes() const
{
	// At least one selected item must be inactive
	for (FPlacementPaletteItemModelPtr& PaletteItem : GetActiveViewWidget()->GetSelectedItems())
	{
		if (!PaletteItem->IsActive())
		{
			return true;
		}
	}

	return false;
}

void SAssetPlacementPalette::OnDeactivatePlacementTypes()
{
	for (FPlacementPaletteItemModelPtr& PaletteItem : GetActiveViewWidget()->GetSelectedItems())
	{
		PaletteItem->SetTypeActiveInPalette(false);
	}
}

bool SAssetPlacementPalette::OnCanDeactivatePlacementTypes() const
{
	// At least one selected item must be active
	for (FPlacementPaletteItemModelPtr& PaletteItem : GetActiveViewWidget()->GetSelectedItems())
	{
		if (PaletteItem->IsActive())
		{
			return true;
		}
	}

	return false;
}

void SAssetPlacementPalette::FillReplacePlacementTypeSubmenu(FMenuBuilder& MenuBuilder)
{
	FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	FAssetPickerConfig AssetPickerConfig;
	AssetPickerConfig.Filter.bRecursiveClasses = true;
	AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateSP(this, &SAssetPlacementPalette::OnReplacePlacementTypeSelected);
	AssetPickerConfig.InitialAssetViewType = EAssetViewType::List;
	AssetPickerConfig.bAllowNullSelection = false;

	TSharedRef<SWidget> MenuContent = SNew(SBox)
		.WidthOverride(384)
		.HeightOverride(500)
		[
			ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig)
		];

	MenuBuilder.AddWidget(MenuContent, FText::GetEmpty(), true);
}

void SAssetPlacementPalette::OnReplacePlacementTypeSelected(const FAssetData& AssetData)
{
	FSlateApplication::Get().DismissAllMenus();

	// not yet implemented
	// should replace existing matching elements in level with the newly selected asset data
	// and update the palette
}

void SAssetPlacementPalette::OnRemovePlacementType()
{
	TArray<FPlacementPaletteItemModelPtr> PaletteCopy = PaletteItems;
	for (FPlacementPaletteItemModelPtr& PaletteItem : GetActiveViewWidget()->GetSelectedItems())
	{
		PaletteCopy.Remove(PaletteItem);
	}

	PaletteItems = MoveTemp(PaletteCopy);
	GetActiveViewWidget()->RequestListRefresh();
}

void SAssetPlacementPalette::OnShowPlacementTypeInCB()
{
	TArray<FAssetData> SelectedAssets;
	for (FPlacementPaletteItemModelPtr& PaletteItem : GetActiveViewWidget()->GetSelectedItems())
	{
		if (PaletteItem.IsValid() && PaletteItem->GetTypeUIInfo().IsValid())
		{
			SelectedAssets.Add(PaletteItem->GetTypeUIInfo()->AssetData);
		}
	}

	if (SelectedAssets.Num())
	{
		GEditor->SyncBrowserToObjects(SelectedAssets);
	}
}

void SAssetPlacementPalette::OnReflectSelectionInPalette()
{
	TArray<FAssetData> SelectedPlacementTypes;
	for (UObject* SelectedObject : GEditor->GetSelectedObjects()->GetElementSelectionSet()->GetSelectedObjects())
	{
		SelectedPlacementTypes.Add(FAssetData(SelectedObject));
	}
	SelectPlacementTypesInPalette(SelectedPlacementTypes);
}

void SAssetPlacementPalette::SelectPlacementTypesInPalette(const TArray<FAssetData>& PlacementTypes)
{
	TArray<FPlacementPaletteItemModelPtr> SelectedItems;
	SelectedItems.Reserve(PlacementTypes.Num());

	for (FPlacementPaletteItemModelPtr& PaletteItem : FilteredItems)
	{
		if (PlacementTypes.Contains(PaletteItem->GetTypeUIInfo()->AssetData))
		{
			SelectedItems.Add(PaletteItem);
		}
	}
	
	GetActiveViewWidget()->ClearSelection();
	GetActiveViewWidget()->SetItemSelection(SelectedItems, true);
}

void SAssetPlacementPalette::ExecuteOnSelectedItemPlacementTypes(TFunctionRef<void(const TArray<FAssetData>&)> ExecuteFunc)
{
	TArray<FPlacementPaletteItemModelPtr> SelectedItems = GetActiveViewWidget()->GetSelectedItems();
	TArray<FAssetData> PlacementTypes;
	PlacementTypes.Reserve(SelectedItems.Num());
	for (FPlacementPaletteItemModelPtr& PaletteItem : SelectedItems)
	{
		PlacementTypes.Add(PaletteItem->GetTypeUIInfo()->AssetData);
	}
	ExecuteFunc(PlacementTypes);
}

void SAssetPlacementPalette::OnSelectAllInstances()
{
	ExecuteOnSelectedItemPlacementTypes([&](const TArray<FAssetData>& PlacementTypes)
	{
		//PlacementEditMode->SelectInstances(PlacementTypes, true);
	});	
}

void SAssetPlacementPalette::OnDeselectAllInstances()
{
	ExecuteOnSelectedItemPlacementTypes([&](const TArray<FAssetData>& PlacementTypes)
	{
		//PlacementEditMode->SelectInstances(PlacementTypes, false);
	});
}

void SAssetPlacementPalette::OnSelectInvalidInstances()
{
	ExecuteOnSelectedItemPlacementTypes([&](const TArray<FAssetData>& PlacementTypes)
	{
		//PlacementEditMode->SelectInvalidInstances(PlacementTypes);
	});
}

bool SAssetPlacementPalette::CanSelectInstances() const
{
	return false;
	//return PlacementEditMode->UISettings.GetSelectToolSelected() || PlacementEditMode->UISettings.GetLassoSelectToolSelected();
}

// THUMBNAIL VIEW

TSharedRef<ITableRow> SAssetPlacementPalette::GenerateTile(FPlacementPaletteItemModelPtr Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SAssetPlacementPaletteItemTile, OwnerTable, Item);

	// Refresh the palette to ensure that thumbnails are correct
	RefreshPalette();
}

float SAssetPlacementPalette::GetScaledThumbnailSize() const
{
	const FInt32Interval& SizeRange = PlacementPaletteConstants::ThumbnailSizeRange;
	return SizeRange.Min + SizeRange.Size() * GetThumbnailScale();// PlacementEditMode->UISettings.GetPaletteThumbnailScale();
}

float SAssetPlacementPalette::GetThumbnailScale() const
{
	return PaletteThumbnailScale;
}

void SAssetPlacementPalette::SetThumbnailScale(float InScale)
{
	PaletteThumbnailScale = FMath::Clamp(InScale, 0.f, 1.f);
}

EVisibility SAssetPlacementPalette::GetThumbnailScaleSliderVisibility() const
{
	return (ActiveViewMode == EAssetPlacementPaletteViewMode::Thumbnail) ? EVisibility::Visible : EVisibility::Collapsed;
}

// TREE VIEW

TSharedRef<ITableRow> SAssetPlacementPalette::TreeViewGenerateRow(FPlacementPaletteItemModelPtr Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SAssetPlacementPaletteItemRow, OwnerTable, Item);
}

void SAssetPlacementPalette::TreeViewGetChildren(FPlacementPaletteItemModelPtr Item, TArray<FPlacementPaletteItemModelPtr>& OutChildren)
{
	// Items do not have any children
}

ECheckBoxState SAssetPlacementPalette::GetState_AllMeshes() const
{
	bool bHasChecked = false;
	bool bHasUnchecked = false;

	for (const FPlacementPaletteItemModelPtr& PaletteItem : FilteredItems)
	{
		if (PaletteItem->IsActive())
		{
			bHasChecked = true;
		}
		else
		{
			bHasUnchecked = true;
		}

		if (bHasChecked && bHasUnchecked)
		{
			return ECheckBoxState::Undetermined;
		}
	}

	return bHasChecked ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SAssetPlacementPalette::OnCheckStateChanged_AllMeshes(ECheckBoxState InState)
{
	const bool bActivate = InState == ECheckBoxState::Checked;
	for (FPlacementPaletteItemModelPtr& PaletteItem : FilteredItems)
	{
		PaletteItem->SetTypeActiveInPalette(bActivate);
	}
}

FText SAssetPlacementPalette::GetTypeColumnHeaderText() const
{
	return LOCTEXT("PlacementTypeHeader", "Asset Type");
}

EColumnSortMode::Type SAssetPlacementPalette::GetMeshColumnSortMode() const
{
	return ActiveSortOrder;
}

void SAssetPlacementPalette::OnTypeColumnSortModeChanged(EColumnSortPriority::Type InPriority, const FName& InColumnName, EColumnSortMode::Type InSortMode)
{
	if (ActiveSortOrder == InSortMode)
	{
		return;
	}

	ActiveSortOrder = InSortMode;

	if (ActiveSortOrder != EColumnSortMode::None)
	{
		auto CompareEntry = [this](const FPlacementPaletteItemModelPtr& A, const FPlacementPaletteItemModelPtr& B)
		{
			bool CompareResult = (A->GetDisplayFName().GetComparisonIndex().CompareLexical(B->GetDisplayFName().GetComparisonIndex()) <= 0);
			return (ActiveSortOrder == EColumnSortMode::Ascending) ? CompareResult : !CompareResult;
		};

		PaletteItems.Sort(CompareEntry);
	}
}

void SAssetPlacementPalette::OnEditPlacementTypeBlueprintHyperlinkClicked(const FSlateHyperlinkRun::FMetadata& Metadata)
{
	UBlueprint* Blueprint = nullptr; 
	
	// Get the first selected Placement type blueprint
	for (FPlacementPaletteItemModelPtr& PaletteItem : GetActiveViewWidget()->GetSelectedItems())
	{
		Blueprint = Cast<UBlueprint>(PaletteItem->GetTypeUIInfo()->AssetData.GetClass()->ClassGeneratedBy);
		if (Blueprint != nullptr)
		{
			break;
		}
	}

	if (Blueprint)
	{
		// Open the blueprint
		GEditor->EditObject(Blueprint);
	}
}

EActiveTimerReturnType SAssetPlacementPalette::UpdatePaletteItems(double InCurrentTime, float InDeltaTime)
{
	if (bItemsNeedRebuild)
	{
		bItemsNeedRebuild = false;

		// Cache the currently selected items
		auto ActiveViewWidget = GetActiveViewWidget();
		TArray<FPlacementPaletteItemModelPtr> PreviouslySelectedItems = ActiveViewWidget->GetSelectedItems();
		ActiveViewWidget->ClearSelection();

		// Restore the selection
		for (auto& PrevSelectedItem : PreviouslySelectedItems)
		{
			// Select any replacements for previously selected Placement types
			for (auto& Item : PaletteItems)
			{
				if (Item->GetDisplayFName() == PrevSelectedItem->GetDisplayFName())
				{
					ActiveViewWidget->SetItemSelection(Item, true);
					break;
				}
			}
		}
	}

	// Update the filtered items
	FilteredItems.Empty();
	for (auto& Item : PaletteItems)
	{
		if (TypeFilter->PassesFilter(Item))
		{
			FilteredItems.Add(Item);
		}

		FPaletteItem NewItem;
		NewItem.AssetData = Item->GetTypeUIInfo()->AssetData;
		NewItem.bIsEnabled = false;
	}

	// Refresh the appropriate view
	RefreshActivePaletteViewWidget();

	bIsRebuildTimerRegistered = false;
	return EActiveTimerReturnType::Stop;
}

EActiveTimerReturnType SAssetPlacementPalette::RefreshPaletteItems(double InCurrentTime, float InDeltaTime)
{
	// Do not refresh the palette if we're waiting on a rebuild
	if (!bItemsNeedRebuild)
	{
		RefreshActivePaletteViewWidget();
	}

	bIsRefreshTimerRegistered = false;
	return EActiveTimerReturnType::Stop;
}

#undef LOCTEXT_NAMESPACE
