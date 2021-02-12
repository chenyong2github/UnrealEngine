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
#include "Editor.h"
#include "Widgets/Layout/SScrollBox.h"

#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Input/SSearchBox.h"
#include "AssetPlacementEdModeStyle.h"
#include "AssetPlacementSettings.h"
#include "Subsystems/PlacementSubsystem.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"

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

	// Check that we don't already have this item in the palette
	if (PaletteItems.FindByPredicate([&AssetData](TSharedPtr<FAssetPlacementPaletteItemModel>& InItem) { return InItem.IsValid() && (InItem->GetTypeUIInfo()->AssetData.ObjectPath == AssetData.ObjectPath); }))
	{
		return;
	}

	// Try to load the asset async so it's ready to place.
	UAssetManager::GetStreamableManager().RequestAsyncLoad(AssetData.ToSoftObjectPath());

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
		.OnGenerateTile(this, &SAssetPlacementPalette::GenerateTile)
		.OnContextMenuOpening(this, &SAssetPlacementPalette::ConstructPlacementTypeContextMenu)
		.ItemHeight(this, &SAssetPlacementPalette::GetScaledThumbnailSize)
		.ItemWidth(this, &SAssetPlacementPalette::GetScaledThumbnailSize)
		.ItemAlignment(EListItemAlignment::LeftAligned);

	// Tree View Widget
	SAssignNew(TreeViewWidget, SPlacementTypeTreeView)
		.TreeItemsSource(&FilteredItems)
		.OnGenerateRow(this, &SAssetPlacementPalette::TreeViewGenerateRow)
		.OnGetChildren(this, &SAssetPlacementPalette::TreeViewGetChildren)
		.OnContextMenuOpening(this, &SAssetPlacementPalette::ConstructPlacementTypeContextMenu)
		.HeaderRow
		(
			SAssignNew(TreeViewHeaderRow, SHeaderRow)
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

void SAssetPlacementPalette::OnShowPlacementTypeInCB()
{
	TArray<FAssetData> FilteredAssets;
	for (FPlacementPaletteItemModelPtr& PaletteItem : FilteredItems)
	{
		if (PaletteItem.IsValid() && PaletteItem->GetTypeUIInfo().IsValid())
		{
			FilteredAssets.Add(PaletteItem->GetTypeUIInfo()->AssetData);
		}
	}

	if (FilteredAssets.Num())
	{
		GEditor->SyncBrowserToObjects(FilteredAssets);
	}
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
	return SizeRange.Min + SizeRange.Size() * GetThumbnailScale();
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

EActiveTimerReturnType SAssetPlacementPalette::UpdatePaletteItems(double InCurrentTime, float InDeltaTime)
{
	if (bItemsNeedRebuild)
	{
		bItemsNeedRebuild = false;
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
