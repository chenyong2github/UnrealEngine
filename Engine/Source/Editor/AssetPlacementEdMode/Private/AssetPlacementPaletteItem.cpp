// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetPlacementPaletteItem.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Input/SCheckBox.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Images/SImage.h"
#include "EditorStyleSet.h"

#include "AssetThumbnail.h"
#include "IAssetTools.h"
#include "IAssetTypeActions.h"
#include "AssetToolsModule.h"
#include "FoliageType.h"
#include "SAssetPlacementPalette.h"
#include "Engine/Blueprint.h"
#include "Factories/AssetFactoryInterface.h"
#include "AssetPlacementSettings.h"

#define LOCTEXT_NAMESPACE "AssetPlacementMode"

FAssetPlacementPaletteItemModel::FAssetPlacementPaletteItemModel(FAssetPlacementUIInfoPtr InTypeInfo, TSharedRef<SAssetPlacementPalette> InFoliagePalette, TSharedPtr<FAssetThumbnailPool> InThumbnailPool/*, FEdModeFoliage* InFoliageEditMode*/)
	: TypeInfo(InTypeInfo)
	, AssetPalette(InFoliagePalette)
{
	check(TypeInfo);
	DisplayFName = TypeInfo->AssetData.AssetName;

	int32 MaxThumbnailSize = PlacementPaletteConstants::ThumbnailSizeRange.Max;
	TSharedPtr<FAssetThumbnail> Thumbnail = MakeShared<FAssetThumbnail>(TypeInfo->AssetData, MaxThumbnailSize, MaxThumbnailSize, InThumbnailPool);
	
	FAssetThumbnailConfig ThumbnailConfig;
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	if (!IsBlueprint())
	{
		AssetTypeActions = AssetToolsModule.Get().GetAssetTypeActionsForClass(TypeInfo->AssetData.GetClass());
	}
	else
	{
		AssetTypeActions = AssetToolsModule.Get().GetAssetTypeActionsForClass(UBlueprint::StaticClass());
	}

	if (AssetTypeActions.IsValid())
	{
		ThumbnailConfig.AssetTypeColorOverride = AssetTypeActions.Pin()->GetTypeColor();
	}

	ThumbnailWidget = Thumbnail->MakeThumbnailWidget(ThumbnailConfig);
}

TSharedPtr<SAssetPlacementPalette> FAssetPlacementPaletteItemModel::GetAssetPalette() const
{
	return AssetPalette.Pin();
}

FAssetPlacementUIInfoPtr FAssetPlacementPaletteItemModel::GetTypeUIInfo() const
{
	return TypeInfo;
}

TSharedRef<SWidget> FAssetPlacementPaletteItemModel::GetThumbnailWidget() const
{
	return ThumbnailWidget.ToSharedRef();
}

TSharedRef<SToolTip> FAssetPlacementPaletteItemModel::CreateTooltipWidget() const
{
	return 
		SNew(SToolTip)
		.TextMargin(1)
		.BorderImage(FEditorStyle::GetBrush("ContentBrowser.TileViewTooltip.ToolTipBorder"))
		.Visibility(this, &FAssetPlacementPaletteItemModel::GetTooltipVisibility)
		[
			SNew(SBorder)
			.Padding(3.f)
			.BorderImage(FEditorStyle::GetBrush("ContentBrowser.TileViewTooltip.NonContentBorder"))
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBorder)
					.Padding(FMargin(6.f))
					.HAlign(HAlign_Left)
					.BorderImage(FEditorStyle::GetBrush("ContentBrowser.TileViewTooltip.ContentBorder"))
					[
						SNew(STextBlock)
						.Text(FText::FromName(DisplayFName))
						.Font(FEditorStyle::GetFontStyle("ContentBrowser.TileViewTooltip.NameFont"))
						.HighlightText(this, &FAssetPlacementPaletteItemModel::GetPaletteSearchText)
					]
				]
				
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.f, 3.f, 0.f, 0.f))
				[
					
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(FMargin(0.f, 0.f, 3.f, 0.f))
					[
						SNew(SBorder)
						.Padding(6.f)
						.HAlign(HAlign_Center)
						.Visibility(this, &FAssetPlacementPaletteItemModel::GetTooltipThumbnailVisibility)
						.BorderImage(FEditorStyle::GetBrush("ContentBrowser.TileViewTooltip.ContentBorder"))
						[
							SNew(SBox)
							.HeightOverride(64.f)
							.WidthOverride(64.f)
							[
								GetThumbnailWidget()
							]
						]
					]

					+ SHorizontalBox::Slot()
					[
						SNew(SBorder)
						.Padding(6.f)
						.BorderImage(FEditorStyle::GetBrush("ContentBrowser.TileViewTooltip.ContentBorder"))
						[
							SNew(SVerticalBox)

							+ SVerticalBox::Slot()
							.Padding(0, 1)
							.AutoHeight()
							[
								SNew(SHorizontalBox)
								
								+ SHorizontalBox::Slot()
								.AutoWidth()
								[
									SNew(STextBlock)
									.Text(LOCTEXT("SourceAssetTypeHeading", "Source Asset Type: "))
									.ColorAndOpacity(FSlateColor::UseSubduedForeground())
								]

								+ SHorizontalBox::Slot()
								.AutoWidth()
								[
									SNew(STextBlock)
									.Text(this, &FAssetPlacementPaletteItemModel::GetSourceAssetTypeText)
								]
							]
						]
					]					
				]
			]
		];
}

TSharedRef<SCheckBox> FAssetPlacementPaletteItemModel::CreateActivationCheckBox(TAttribute<bool> IsItemWidgetSelected, TAttribute<EVisibility> InVisibility)
{
	return
		SNew(SCheckBox)
		.Padding(0.f)
		.OnCheckStateChanged(this, &FAssetPlacementPaletteItemModel::HandleCheckStateChanged, IsItemWidgetSelected)
		.Visibility(InVisibility)
		.IsChecked(this, &FAssetPlacementPaletteItemModel::GetCheckBoxState)
		.ToolTipText(LOCTEXT("TileCheckboxTooltip", "Check to activate the currently selected types in the palette"));
}

FName FAssetPlacementPaletteItemModel::GetDisplayFName() const
{
	return DisplayFName;
}

FText FAssetPlacementPaletteItemModel::GetPaletteSearchText() const
{
	if (AssetPalette.IsValid())
	{
		return AssetPalette.Pin()->GetSearchText();
	}
	else
	{
		return FText();
	}
}

void FAssetPlacementPaletteItemModel::SetTypeActiveInPalette(bool bSetActiveInPalette)
{
	bSelected = bSetActiveInPalette;
}

bool FAssetPlacementPaletteItemModel::IsActive() const
{
	return bSelected;
}

bool FAssetPlacementPaletteItemModel::IsBlueprint() const
{
	return TypeInfo && (TypeInfo->AssetData.GetClass()->GetDefaultObject()->IsA<UBlueprint>() || (TypeInfo->AssetData.GetClass()->ClassGeneratedBy != nullptr));
}

bool FAssetPlacementPaletteItemModel::IsAsset() const
{
	return TypeInfo && TypeInfo->AssetData.IsValid();
}

void FAssetPlacementPaletteItemModel::HandleCheckStateChanged(const ECheckBoxState NewCheckedState, TAttribute<bool> IsItemWidgetSelected)
{
	if (!IsItemWidgetSelected.IsSet()) { return; }

	const bool bShouldActivate = NewCheckedState == ECheckBoxState::Checked;
	if (!IsItemWidgetSelected.Get())
	{
		SetTypeActiveInPalette(bShouldActivate);
	}
	else if (AssetPalette.IsValid())
	{
		AssetPalette.Pin()->ActivateAllSelectedTypes(bShouldActivate);
	}
}

ECheckBoxState FAssetPlacementPaletteItemModel::GetCheckBoxState() const
{
	return bSelected ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

EVisibility FAssetPlacementPaletteItemModel::GetTooltipVisibility() const
{
	return (AssetPalette.IsValid() && AssetPalette.Pin()->ShouldShowTooltips()) ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
}

EVisibility FAssetPlacementPaletteItemModel::GetTooltipThumbnailVisibility() const
{
	
	return (AssetPalette.IsValid() && AssetPalette.Pin()->IsActiveViewMode(EAssetPlacementPaletteViewMode::Tree)) ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
}

FText FAssetPlacementPaletteItemModel::GetSourceAssetTypeText() const
{
	if (AssetTypeActions.IsValid())
	{
		return AssetTypeActions.Pin()->GetName();
	}

	if (!TypeInfo->AssetData.AssetClass.IsNone())
	{
		return FText::FromName(TypeInfo->AssetData.AssetClass);
	}

	return FText::FromName(TypeInfo->AssetData.AssetName);
}

////////////////////////////////////////////////
// SAssetPlacementPaletteItemTile
////////////////////////////////////////////////

const float SAssetPlacementPaletteItemTile::MinScaleForOverlayItems = 0.2f;

void SAssetPlacementPaletteItemTile::Construct(const FArguments& InArgs, TSharedRef<STableViewBase> InOwnerTableView, TSharedPtr<FAssetPlacementPaletteItemModel>& InModel)
{
	Model = InModel;

	auto IsSelectedGetter = TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateSP(this, &SAssetPlacementPaletteItemTile::IsSelected));
	auto CheckBoxVisibility = TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &SAssetPlacementPaletteItemTile::GetCheckBoxVisibility));

	STableRow<FAssetPlacementUIInfoPtr>::Construct(
		STableRow<FAssetPlacementUIInfoPtr>::FArguments()
		.Style(FEditorStyle::Get(), "ContentBrowser.AssetListView.TableRow")
		.Padding(1.f)
		.Content()
		[
			SNew(SOverlay)
			.ToolTip(Model->CreateTooltipWidget())
			
			// Thumbnail
			+ SOverlay::Slot()
			[
				SNew(SBorder)
				.Padding(4.f)
				.BorderImage(FEditorStyle::GetBrush("ContentBrowser.ThumbnailShadow"))
				.ForegroundColor(FLinearColor::White)
				.ColorAndOpacity(this, &SAssetPlacementPaletteItemTile::GetTileColorAndOpacity)
				[
					Model->GetThumbnailWidget()
				]
			]
			
			// Checkbox
			+ SOverlay::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Top)
			.Padding(FMargin(3.f))
			[
				SNew(SBorder)
				.BorderImage(FEditorStyle::GetBrush("ContentBrowser.ThumbnailShadow"))
				.BorderBackgroundColor(FLinearColor::Black)
				.ForegroundColor(FLinearColor::White)
				.Padding(3.f)
				[
					Model->CreateActivationCheckBox(IsSelectedGetter, CheckBoxVisibility)
				]
			]
		], InOwnerTableView);
}

FLinearColor SAssetPlacementPaletteItemTile::GetTileColorAndOpacity() const
{
	return Model->IsActive() ? FLinearColor::White : FLinearColor(0.5f, 0.5f, 0.5f, 1.f);
}

EVisibility SAssetPlacementPaletteItemTile::GetCheckBoxVisibility() const
{
	return CanShowOverlayItems() && (IsHovered() || (IsSelected() && Model->GetAssetPalette()->AnySelectedTileHovered())) ? EVisibility::Visible : EVisibility::Collapsed;
}

bool SAssetPlacementPaletteItemTile::CanShowOverlayItems() const
{
	return true;
}

////////////////////////////////////////////////
// SAssetPlacementPaletteItemRow
////////////////////////////////////////////////

void SAssetPlacementPaletteItemRow::Construct(const FArguments& InArgs, TSharedRef<STableViewBase> InOwnerTableView, TSharedPtr<FAssetPlacementPaletteItemModel>& InModel)
{
	Model = InModel;
	SMultiColumnTableRow<FAssetPlacementUIInfoPtr>::Construct(FSuperRowType::FArguments(), InOwnerTableView);

	SetToolTip(Model->CreateTooltipWidget());
}

TSharedRef<SWidget> SAssetPlacementPaletteItemRow::GenerateWidgetForColumn(const FName& ColumnName)
{
	TSharedPtr<SWidget> TableRowContent = SNullWidget::NullWidget;

	if (ColumnName == AssetPlacementPaletteTreeColumns::ColumnID_ToggleActive)
	{
		TAttribute<bool> IsSelectedGetter = MakeAttributeSP<bool>(this, &SAssetPlacementPaletteItemTile::IsSelected);
		TableRowContent = Model->CreateActivationCheckBox(IsSelectedGetter);
	} 
	else if (ColumnName == AssetPlacementPaletteTreeColumns::ColumnID_Type)
	{
		TableRowContent =
			SNew(SHorizontalBox)
			
			+SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SExpanderArrow, SharedThis(this))
			]
			
			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(FText::FromName(Model->GetDisplayFName()))
				.HighlightText(Model.ToSharedRef(), &FAssetPlacementPaletteItemModel::GetPaletteSearchText)
			];
	}

	return TableRowContent.ToSharedRef();
}

#undef LOCTEXT_NAMESPACE
