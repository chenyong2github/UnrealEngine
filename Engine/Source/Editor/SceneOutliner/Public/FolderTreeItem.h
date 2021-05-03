// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "ISceneOutlinerTreeItem.h"

class UToolMenu;

namespace SceneOutliner
{
	struct FFolderPathSelector
	{
		bool operator()(TWeakPtr<ISceneOutlinerTreeItem> Item, FName& DataOut) const;
	};
}	// namespace SceneOutliner


/** A tree item that represents a folder in the world */
struct SCENEOUTLINER_API FFolderTreeItem : ISceneOutlinerTreeItem
{
public:
	/** Static type identifier for this tree item class */
	static const FSceneOutlinerTreeItemType Type;

	DECLARE_DELEGATE_RetVal_OneParam(bool, FFilterPredicate, FName);
	DECLARE_DELEGATE_RetVal_OneParam(bool, FInteractivePredicate, FName);

	bool Filter(FFilterPredicate Pred) const
	{
		return Pred.Execute(Path);
	}

	bool GetInteractiveState(FInteractivePredicate Pred) const
	{
		return Pred.Execute(Path);
	}

	/** The path of this folder. / separated. */
	FName Path;

	/** The leaf name of this folder */
	FName LeafName;

	/** Constructor that takes a path to this folder (including leaf-name) */
	FFolderTreeItem(FName InPath);
	/** Constructor that takes a path to this folder and a subclass tree item type (used for subclassing FFolderTreeItem) */
	FFolderTreeItem(FName InPath, FSceneOutlinerTreeItemType Type);

	/* Begin ISceneOutlinerTreeItem Implementation */
	virtual bool IsValid() const override { return true; }
	virtual FSceneOutlinerTreeItemID GetID() const override;
	virtual FString GetDisplayString() const override;
	virtual bool CanInteract() const override;
	virtual void GenerateContextMenu(UToolMenu* Menu, SSceneOutliner& Outliner) override;
	/** Delete this folder, children will be reparented to provided new parent path */
	virtual void Delete(FName InNewParentPath) {}
	
	virtual bool ShouldShowPinnedState() const override { return true; }
	virtual bool HasPinnedStateInfo() const override { return false; }
	/* End ISceneOutlinerTreeItem Implementation */

	/** Move this folder to a new parent */
	virtual FName MoveTo(const FName& NewParent) { return FName(); }
private:
	/** Create a new folder as a child of this one */
	virtual void CreateSubFolder(TWeakPtr<SSceneOutliner> WeakOutliner) {}
	/** Duplicate folder hierarchy */
	void DuplicateHierarchy(TWeakPtr<SSceneOutliner> WeakOutliner);
};
