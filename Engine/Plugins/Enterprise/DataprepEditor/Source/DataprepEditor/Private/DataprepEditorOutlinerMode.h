// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ActorMode.h"
#include "ActorTreeItem.h"
#include "ComponentTreeItem.h"

namespace DataprepEditorSceneOutlinerUtils
{
	using namespace SceneOutliner;

	/**
	* Use this struct to match the scene outliers selection to a dataprep editor selection
	*/
	struct FSynchroniseSelectionToSceneOutliner
	{
		FSynchroniseSelectionToSceneOutliner(TWeakPtr<FDataprepEditor> InDataprepEditor)
			: DataprepEditorPtr(InDataprepEditor)
		{
		};

		bool operator()(const ITreeItem& Item) const
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

class FDataprepEditorOutlinerMode : public SceneOutliner::FActorMode
{
public:
	FDataprepEditorOutlinerMode(SceneOutliner::SSceneOutliner* InSceneOutliner, TWeakPtr<FDataprepEditor> InDataprepEditor, TWeakObjectPtr<UWorld> InSpecifiedWorldToDisplay = nullptr)
		: SceneOutliner::FActorMode(InSceneOutliner, true, InSpecifiedWorldToDisplay)
		, DataprepEditorPtr(InDataprepEditor)
	{}

	virtual bool CanRenameItem(const SceneOutliner::ITreeItem& Item) const override { return false; }
	virtual ESelectionMode::Type GetSelectionMode() const override { return ESelectionMode::Multi; }

	virtual void OnItemSelectionChanged(SceneOutliner::FTreeItemPtr Item, ESelectInfo::Type SelectionType, const SceneOutliner::FItemSelection& Selection) override
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