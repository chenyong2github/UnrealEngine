// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "SceneOutlinerStandaloneTypes.h"
#include "SceneOutlinerFwd.h"
#include "ITreeItem.h"

namespace SceneOutliner
{
	class ISceneOutlinerHierarchy : public TSharedFromThis<ISceneOutlinerHierarchy>
	{
	public:
		ISceneOutlinerHierarchy(ISceneOutlinerMode* InMode) : Mode(InMode) {}
		virtual ~ISceneOutlinerHierarchy() {}

		/** Find all direct children of a tree item in an existing item map, if any exist. */
		virtual void FindChildren(const ITreeItem& Item, const TMap<FTreeItemID, FTreeItemPtr>& Items, TArray<FTreeItemPtr>& OutChildItems) const = 0;
		/** Find the parent of a tree item in an existing item map, it if exists. */
		virtual FTreeItemPtr FindParent(const ITreeItem& Item, const TMap<FTreeItemID, FTreeItemPtr>& Items) const = 0;
		
		/** Create a linearization of all applicable items in the hierarchy */
		virtual void CreateItems(TArray<FTreeItemPtr>& OutItems) const = 0;
		/** Create a linearization of all direct and indirect children of a given item in the hierarchy */
		virtual void CreateChildren(const FTreeItemPtr& Item, TArray<FTreeItemPtr>& OutChildren) const = 0;
		/** Forcibly create a parent item for a given tree item */
		virtual FTreeItemPtr CreateParentItem(const FTreeItemPtr& Item) const = 0;

		DECLARE_EVENT_OneParam(ISceneOutlinerHierarchy, FHierarchyChangedEvent, FHierarchyChangedData)
		FHierarchyChangedEvent& OnHierarchyChanged() { return HierarchyChangedEvent; }

	protected:
		ISceneOutlinerMode* Mode;
		FHierarchyChangedEvent HierarchyChangedEvent;
	};
}