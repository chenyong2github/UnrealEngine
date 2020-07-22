// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ISceneOutlinerHierarchy.h"

namespace SceneOutliner
{
	class FActorFolderHierarchy : public ISceneOutlinerHierarchy
	{
	public:
		FActorFolderHierarchy(ISceneOutlinerMode* InMode, const TWeakObjectPtr<UWorld>& World);
		virtual ~FActorFolderHierarchy() {}


		/* Begin ISceneOutlinerHierarchy Implementation */
		virtual void FindChildren(const ITreeItem& Item, const TMap<FTreeItemID, FTreeItemPtr>& Items, TArray<FTreeItemPtr>& OutChildItems) const override;
		virtual FTreeItemPtr FindParent(const ITreeItem& Item, const TMap<FTreeItemID, FTreeItemPtr>& Items) const override;
		virtual void CreateItems(TArray<FTreeItemPtr>& OutItems) const override;
		virtual void CreateChildren(const FTreeItemPtr& Item, TArray<FTreeItemPtr>& OutChildren) const override;
		virtual FTreeItemPtr CreateParentItem(const FTreeItemPtr& Item) const override;
		/* End ISceneOutlinerHierarchy Implementation */
	private:
		/** Adds all the direct and indirect children of a world to OutItems */
		void CreateWorldChildren(UWorld* World, TArray<FTreeItemPtr>& OutItems) const;
	private:
		/** The world which this hierarchy is representing */
		TWeakObjectPtr<UWorld> RepresentingWorld;
	};
}