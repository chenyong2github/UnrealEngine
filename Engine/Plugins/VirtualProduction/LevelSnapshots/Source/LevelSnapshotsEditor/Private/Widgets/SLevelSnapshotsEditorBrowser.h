// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ContentBrowserDelegates.h"
#include "Views/SnapshotEditorViewData.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

struct FAssetData;
struct FAssetViewCustomColumn;
class UWorld;
struct FSnapshotEditorViewData;
class SToolTip;

class SLevelSnapshotsEditorBrowser : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLevelSnapshotsEditorBrowser)
	{}

	/** Attribute for retrieving the current context */
	SLATE_ATTRIBUTE(FSoftObjectPath, OwningWorldPath)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const FSnapshotEditorViewData& InViewBuildData);
	void SelectAsset(const FAssetData& InAssetData) const;

private:
	TSharedRef<SToolTip> CreateCustomTooltip(FAssetData& AssetData);
	TArray<FAssetViewCustomColumn> GetCustomColumns() const;
	void OnAssetDoubleClicked(const FAssetData& InAssetData) const;
	bool OnShouldFilterAsset(const FAssetData& InAssetData) const;
	TSharedPtr<SWidget> OnGetAssetContextMenu(const TArray<FAssetData>& SelectedAssets);

	TAttribute<FSoftObjectPath> OwningWorldPathAttribute;

	FSnapshotEditorViewData ViewBuildData;
};
