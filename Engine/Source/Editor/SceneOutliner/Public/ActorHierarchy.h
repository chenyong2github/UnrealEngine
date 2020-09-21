// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ISceneOutlinerHierarchy.h"

class FActorHierarchy : public ISceneOutlinerHierarchy
{
public:
	virtual ~FActorHierarchy();

	static TUniquePtr<FActorHierarchy> Create(ISceneOutlinerMode* Mode, const TWeakObjectPtr<UWorld>& World);

	/** Find the parent of a tree item in an existing item map, it if exists. */
	virtual FSceneOutlinerTreeItemPtr FindParent(const ISceneOutlinerTreeItem& Item, const TMap<FSceneOutlinerTreeItemID, FSceneOutlinerTreeItemPtr>& Items) const override;

	/** Create a linearization of all applicable items in the hierarchy */
	virtual void CreateItems(TArray<FSceneOutlinerTreeItemPtr>& OutItems) const override;
	/** Create a linearization of all direct and indirect children of a given item in the hierarchy */
	virtual void CreateChildren(const FSceneOutlinerTreeItemPtr& Item, TArray<FSceneOutlinerTreeItemPtr>& OutChildren) const override;
	/** Forcibly create a parent item for a given tree item */
	virtual FSceneOutlinerTreeItemPtr CreateParentItem(const FSceneOutlinerTreeItemPtr& Item) const override;

	void SetShowingComponents(bool bInShowingComponents) { bShowingComponents = bInShowingComponents; }
	void SetShowingLevelInstances(bool bInShowingLevelInstances) { bShowingLevelInstances = bInShowingLevelInstances; }
private:
	/** Adds all the direct and indirect children of a world to OutItems */
	void CreateWorldChildren(UWorld* World, TArray<FSceneOutlinerTreeItemPtr>& OutItems) const;
	/** Create all component items for an actor if we are showing components and place them in OutItems */
	void CreateComponentItems(const AActor* Actor, TArray<FSceneOutlinerTreeItemPtr>& OutItems) const;
private:
	// Update the hierarchy when actor or world changing events occur

	void OnLevelActorAdded(AActor* InActor);
	void OnLevelActorDeleted(AActor* InActor);
		
	void OnLevelActorAttached(AActor* InActor, const AActor* InParent);
	void OnLevelActorDetached(AActor* InActor, const AActor* InParent);

	void OnLevelActorFolderChanged(const AActor* InActor, FName OldPath);

	void OnComponentsUpdated();

	void OnLevelActorListChanged();
		
	void OnLevelAdded(ULevel* InLevel, UWorld* InWorld);
	void OnLevelRemoved(ULevel* InLevel, UWorld* InWorld);

	/** Called when a folder is to be created */
	void OnBroadcastFolderCreate(UWorld& InWorld, FName NewPath);

	/** Called when a folder is to be moved */
	void OnBroadcastFolderMove(UWorld& InWorld, FName OldPath, FName NewPath);

	/** Called when a folder is to be deleted */
	void OnBroadcastFolderDelete(UWorld& InWorld, FName Path);

private:
	/** Send a an event indicating a full refresh of the hierarchy is required */
	void FullRefreshEvent();

	bool bShowingComponents = false;
	bool bShowingLevelInstances = false;

	TWeakObjectPtr<UWorld> RepresentingWorld;
private:
	FActorHierarchy(ISceneOutlinerMode* Mode, const TWeakObjectPtr<UWorld>& Worlds);

	FActorHierarchy(const FActorHierarchy&) = delete;
	FActorHierarchy& operator=(const FActorHierarchy&) = delete;
};
