// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Attribute.h"
#include "SceneOutlinerFwd.h"
#include "SceneOutlinerStandaloneTypes.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

struct FDataLayerTreeItem;
class UDataLayer;

struct SDataLayerTreeLabel : FSceneOutlinerCommonLabelData, public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SDataLayerTreeLabel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, FDataLayerTreeItem& DataLayerItem, ISceneOutliner& SceneOutliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow);

private:

	FSlateFontInfo GetDisplayNameFont() const;
	FText GetDisplayText() const;
	FText GetTooltipText() const;
	FText GetTypeText() const;
	EVisibility GetTypeTextVisibility() const;
	const FSlateBrush* GetIcon() const;
	FText GetIconTooltip() const;
	FSlateColor GetForegroundColor() const;
	bool OnVerifyItemLabelChanged(const FText& InLabel, FText& OutErrorMessage);
	void OnLabelCommitted(const FText& InLabel, ETextCommit::Type InCommitInfo);
	bool ShouldBeHighlighted() const;

	TWeakPtr<FDataLayerTreeItem> TreeItemPtr;
	TWeakObjectPtr<UDataLayer> DataLayerPtr;
	TAttribute<FText> HighlightText;
};