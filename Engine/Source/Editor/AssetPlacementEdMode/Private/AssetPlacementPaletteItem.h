// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Attribute.h"
#include "Layout/Visibility.h"
#include "Input/Reply.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "Widgets/Views/STableViewBase.h"
#include "Widgets/Views/STableRow.h"
#include "SAssetPlacementPalette.h"

class FAssetThumbnailPool;
struct FPaletteItem;
class IAssetTypeActions;

namespace PlacementPaletteConstants
{
	const FInt32Interval ThumbnailSizeRange(32, 128);
}

typedef TSharedPtr<FPaletteItem> FAssetPlacementUIInfoPtr;

namespace AssetPlacementPaletteTreeColumns
{
	static const FName ColumnID_Type("Type");
};

class FAssetPlacementPaletteItemModel : public TSharedFromThis<FAssetPlacementPaletteItemModel>
{
public:
	FAssetPlacementPaletteItemModel(FAssetPlacementUIInfoPtr InTypeInfo, TSharedRef<class SAssetPlacementPalette> InFoliagePalette, TSharedPtr<class FAssetThumbnailPool> InThumbnailPool);

	/** @return The foliage palette that contains the item */
	TSharedPtr<SAssetPlacementPalette> GetAssetPalette() const;

	FAssetPlacementUIInfoPtr GetTypeUIInfo() const;

	/** @return The thumbnail widget for this item */
	TSharedRef<SWidget> GetThumbnailWidget() const;

	/** @return The tooltip widget for this item */
	TSharedRef<class SToolTip> CreateTooltipWidget() const;

	/** Gets the FName version of the displayed name of this item */
	FName GetDisplayFName() const;

	/** Gets the current search filter text */
	FText GetPaletteSearchText() const;

	/** @return Whether this palette item represents an instance of a foliage type blueprint class */
	bool IsBlueprint() const;

	/** @return Whether this palette item represents a foliage type asset */
	bool IsAsset() const;

private:
	/** Gets the visibility of the entire tooltip */
	EVisibility GetTooltipVisibility() const;

	/** Gets the visibility of the thumbnail in the tooltip */
	EVisibility GetTooltipThumbnailVisibility() const;

	/** Gets the source asset type text */
	FText GetSourceAssetTypeText() const;

	TWeakPtr<IAssetTypeActions> GetAssetTypeActions() const;

private:
	TSharedPtr<SWidget> ThumbnailWidget;
	FName DisplayFName;
	FAssetPlacementUIInfoPtr TypeInfo;
	TWeakPtr<SAssetPlacementPalette> AssetPalette;
	TWeakPtr<IAssetTypeActions> AssetTypeActions;
};

/** A tile representing a foliage type in the palette */
class SAssetPlacementPaletteItemTile : public STableRow<FAssetPlacementUIInfoPtr>
{
public:
	SLATE_BEGIN_ARGS(SAssetPlacementPaletteItemTile) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedRef<STableViewBase> InOwnerTableView, TSharedPtr<FAssetPlacementPaletteItemModel>& InModel);

private:
	static const float MinScaleForOverlayItems;
	TSharedPtr<FAssetPlacementPaletteItemModel> Model;
};

/** A tree row representing a foliage type in the palette */
class SAssetPlacementPaletteItemRow : public SMultiColumnTableRow<FAssetPlacementUIInfoPtr>
{
public:
	SLATE_BEGIN_ARGS(SAssetPlacementPaletteItemRow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedRef<STableViewBase> InOwnerTableView, TSharedPtr<FAssetPlacementPaletteItemModel>& InModel);
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override;

private:
	TSharedPtr<FAssetPlacementPaletteItemModel> Model;
};
