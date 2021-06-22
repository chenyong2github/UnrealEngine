// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

struct FAssetData;
class UWorld;
struct FLevelSnapshotsEditorViewBuilder;

class SLevelSnapshotsEditorBrowser : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLevelSnapshotsEditorBrowser)
	{}

	/** Attribute for retrieving the current context */
	SLATE_ATTRIBUTE(FSoftObjectPath, OwningWorldPath)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<FLevelSnapshotsEditorViewBuilder>& InBuilder);
	void SelectAsset(const FAssetData& InAssetData) const;

private:
	void OnAssetDoubleClicked(const FAssetData& InAssetData) const;
	bool OnShouldFilterAsset(const FAssetData& InAssetData) const;
	TSharedPtr<SWidget> OnGetAssetContextMenu(const TArray<FAssetData>& SelectedAssets);

	TAttribute<FSoftObjectPath> OwningWorldPathAttribute;

	TWeakPtr<FLevelSnapshotsEditorViewBuilder> BuilderPtr;
};
