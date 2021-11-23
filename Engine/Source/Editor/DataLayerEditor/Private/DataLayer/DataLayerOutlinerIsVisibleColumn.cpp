// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataLayerOutlinerIsVisibleColumn.h"
#include "DataLayer/DataLayerEditorSubsystem.h"
#include "DataLayerTreeItem.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Views/STreeView.h"
#include "ScopedTransaction.h"
#include "EditorStyleSet.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "DataLayer"

FName FDataLayerOutlinerIsVisibleColumn::GetID()
{
	static FName DataLayerOutlinerIsVisible("Data Layer Visibility");
	return DataLayerOutlinerIsVisible;
}

/** Widget responsible for managing the visibility for a single item */
class SDataLayerVisibilityWidget : public SVisibilityWidget
{
protected:

	virtual bool IsEnabled() const override
	{
		auto TreeItem = WeakTreeItem.Pin();
		if (FDataLayerTreeItem* DataLayerTreeItem = TreeItem.IsValid() ? TreeItem->CastTo<FDataLayerTreeItem>() : nullptr)
		{
			const UDataLayer* DataLayer = DataLayerTreeItem->GetDataLayer();
			const UDataLayer* ParentDataLayer = DataLayer ? DataLayer->GetParent() : nullptr;
			const bool bIsParentVisible = ParentDataLayer ? ParentDataLayer->IsEffectiveVisible() : true;
			return bIsParentVisible && DataLayer && DataLayer->GetWorld() && !DataLayer->GetWorld()->IsPlayInEditor() && DataLayer->IsEffectiveLoadedInEditor();
		}
		return false;
	}

	virtual const FSlateBrush* GetBrush() const override
	{
		bool bIsEffectiveVisible = false;
		auto TreeItem = WeakTreeItem.Pin();
		if (FDataLayerTreeItem* DataLayerTreeItem = TreeItem.IsValid() ? TreeItem->CastTo<FDataLayerTreeItem>() : nullptr)
		{
			UDataLayer* DataLayer = DataLayerTreeItem->GetDataLayer();
			bIsEffectiveVisible = DataLayer && DataLayer->IsEffectiveVisible();
		}

		if (bIsEffectiveVisible)
		{
			return IsHovered() ? VisibleHoveredBrush : VisibleNotHoveredBrush;
		}
		else
		{
			return IsHovered() ? NotVisibleHoveredBrush : NotVisibleNotHoveredBrush;
		}
	}

	virtual FSlateColor GetForegroundColor() const override
	{
		if (IsEnabled())
		{
			auto Outliner = WeakOutliner.Pin();
			auto TreeItem = WeakTreeItem.Pin();
			const bool bIsSelected = Outliner->GetTree().IsItemSelected(TreeItem.ToSharedRef());
			if (IsVisible() && !Row->IsHovered() && !bIsSelected)
			{
				return FLinearColor::Transparent;
			}
			return FAppStyle::Get().GetSlateColor("Colors.ForegroundHover");
		}
		else
		{
			return FLinearColor(FColorList::DimGrey);
		}
	}

	virtual bool ShouldPropagateVisibilityChangeOnChildren() const { return false; }
};

const TSharedRef<SWidget> FDataLayerOutlinerIsVisibleColumn::ConstructRowWidget(FSceneOutlinerTreeItemRef TreeItem, const STableRow<FSceneOutlinerTreeItemPtr>& Row)
{
	if (TreeItem->ShouldShowVisibilityState())
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SDataLayerVisibilityWidget, SharedThis(this), WeakOutliner, TreeItem, &Row)
			];
	}
	return SNullWidget::NullWidget;
}

#undef LOCTEXT_NAMESPACE