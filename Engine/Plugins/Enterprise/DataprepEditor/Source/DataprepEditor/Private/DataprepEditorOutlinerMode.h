// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ActorMode.h"
#include "ActorTreeItem.h"
#include "ComponentTreeItem.h"

namespace DataprepEditorSceneOutlinerUtils
{
	/**
	* Use this struct to match the scene outliers selection to a dataprep editor selection
	*/
	struct FSynchroniseSelectionToSceneOutliner
	{
		FSynchroniseSelectionToSceneOutliner(TWeakPtr<FDataprepEditor> InDataprepEditor)
			: DataprepEditorPtr(InDataprepEditor)
		{
		};

		bool operator()(const ISceneOutlinerTreeItem& Item) const
		{
			if (const FActorTreeItem* ActorItem = Item.CastTo<FActorTreeItem>())
			{
				if (const FDataprepEditor* DataprepEditor = DataprepEditorPtr.Pin().Get())
				{
					return DataprepEditor->GetWorldItemsSelection().Contains(ActorItem->Actor);
				}
			}
			else if (const FComponentTreeItem* ComponentItem = Item.CastTo<FComponentTreeItem>())
			{
				if (const FDataprepEditor* DataprepEditor = DataprepEditorPtr.Pin().Get())
				{
					return DataprepEditor->GetWorldItemsSelection().Contains(ComponentItem->Component);
				}
			}

			return false;
		}
	private:
		TWeakPtr<FDataprepEditor> DataprepEditorPtr;
	};
}

class FDataprepEditorOutlinerMode : public FActorMode
{
public:
	FDataprepEditorOutlinerMode(SSceneOutliner* InSceneOutliner, TWeakPtr<FDataprepEditor> InDataprepEditor, TWeakObjectPtr<UWorld> InSpecifiedWorldToDisplay = nullptr)
		: FActorMode(FActorModeParams(InSceneOutliner, InSpecifiedWorldToDisplay, true, true))
		, DataprepEditorPtr(InDataprepEditor)
	{}

	virtual bool CanRenameItem(const ISceneOutlinerTreeItem& Item) const override { return false; }
	virtual ESelectionMode::Type GetSelectionMode() const override { return ESelectionMode::Multi; }

	virtual void OnItemSelectionChanged(FSceneOutlinerTreeItemPtr Item, ESelectInfo::Type SelectionType, const FSceneOutlinerItemSelection& Selection) override
	{
		auto DataprepEditor = DataprepEditorPtr.Pin();
		if (DataprepEditor)
		{
			DataprepEditor->OnSceneOutlinerSelectionChanged(Item, SelectionType);
		}
	}
private:
	TWeakPtr<FDataprepEditor> DataprepEditorPtr;
};