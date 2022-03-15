// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataLayerTreeItem.h"
#include "DataLayer/DataLayerEditorSubsystem.h"
#include "ISceneOutlinerTreeItem.h"
#include "SceneOutlinerStandaloneTypes.h"
#include "SDataLayerTreeLabel.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "DataLayer"

const FSceneOutlinerTreeItemType FDataLayerTreeItem::Type(&ISceneOutlinerTreeItem::Type);

FDataLayerTreeItem::FDataLayerTreeItem(UDataLayerInstance* InDataLayerInstance)
	: ISceneOutlinerTreeItem(Type)
	, DataLayerInstance(InDataLayerInstance)
	, ID(InDataLayerInstance)
	, bIsHighlighedtIfSelected(false)
{
	Flags.bIsExpanded = false;
}

FString FDataLayerTreeItem::GetDisplayString() const
{
	const UDataLayerInstance* DataLayerPtr = DataLayerInstance.Get();
	return DataLayerPtr ? DataLayerPtr->GetDataLayerShortName() : LOCTEXT("DataLayerForMissingDataLayer", "(Deleted Data Layer)").ToString();
}

bool FDataLayerTreeItem::GetVisibility() const
{
	const UDataLayerInstance* DataLayerPtr = DataLayerInstance.Get();
	return DataLayerPtr && DataLayerPtr->IsVisible();
}

bool FDataLayerTreeItem::CanInteract() const 
{
	return true;
}

TSharedRef<SWidget> FDataLayerTreeItem::GenerateLabelWidget(ISceneOutliner& Outliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow)
{
	return SNew(SDataLayerTreeLabel, *this, Outliner, InRow);
}

void FDataLayerTreeItem::OnVisibilityChanged(const bool bNewVisibility)
{
	if (UDataLayerInstance* DataLayerPtr = DataLayerInstance.Get())
	{
		UDataLayerEditorSubsystem::Get()->SetDataLayerVisibility(DataLayerPtr, bNewVisibility);
	}
}

bool FDataLayerTreeItem::ShouldBeHighlighted() const
{
	if (bIsHighlighedtIfSelected)
	{
		if (UDataLayerInstance* DataLayerPtr = DataLayerInstance.Get())
		{
			return UDataLayerEditorSubsystem::Get()->DoesDataLayerContainSelectedActors(DataLayerPtr);
		}
	}
	return false;
}

#undef LOCTEXT_NAMESPACE