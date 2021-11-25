// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataLayerTreeItem.h"
#include "DataLayer/DataLayerEditorSubsystem.h"
#include "ISceneOutlinerTreeItem.h"
#include "SceneOutlinerStandaloneTypes.h"
#include "SDataLayerTreeLabel.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "DataLayer"

const FSceneOutlinerTreeItemType FDataLayerTreeItem::Type(&ISceneOutlinerTreeItem::Type);

FDataLayerTreeItem::FDataLayerTreeItem(UDataLayer* InDataLayer)
	: ISceneOutlinerTreeItem(Type)
	, DataLayer(InDataLayer)
	, ID(InDataLayer)
	, bIsHighlighedtIfSelected(false)
{
	Flags.bIsExpanded = false;
}

FString FDataLayerTreeItem::GetDisplayString() const
{
	const UDataLayer* DataLayerPtr = DataLayer.Get();
	return DataLayerPtr ? DataLayerPtr->GetDataLayerLabel().ToString() : LOCTEXT("DataLayerForMissingDataLayer", "(Deleted Data Layer)").ToString();
}

bool FDataLayerTreeItem::GetVisibility() const
{
	const UDataLayer* DataLayerPtr = DataLayer.Get();
	return DataLayerPtr && DataLayerPtr->IsVisible();
}

TSharedRef<SWidget> FDataLayerTreeItem::GenerateLabelWidget(ISceneOutliner& Outliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow)
{
	return SNew(SDataLayerTreeLabel, *this, Outliner, InRow);
}

void FDataLayerTreeItem::OnVisibilityChanged(const bool bNewVisibility)
{
	if (UDataLayer* DataLayerPtr = DataLayer.Get())
	{
		UDataLayerEditorSubsystem::Get()->SetDataLayerVisibility(DataLayerPtr, bNewVisibility);
	}
}

bool FDataLayerTreeItem::ShouldBeHighlighted() const
{
	if (bIsHighlighedtIfSelected)
	{
		if (UDataLayer* DataLayerPtr = DataLayer.Get())
		{
			return UDataLayerEditorSubsystem::Get()->DoesDataLayerContainSelectedActors(DataLayerPtr);
		}
	}
	return false;
}

#undef LOCTEXT_NAMESPACE