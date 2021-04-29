// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FolderTreeItem.h"

struct SCENEOUTLINER_API FActorFolderTreeItem : public FFolderTreeItem
{
public:
	/** Static type identifier for this tree item class */
	static const FSceneOutlinerTreeItemType Type;

	FActorFolderTreeItem(FName InPath, TWeakObjectPtr<UWorld> InWorld);

	/** The world which this folder belongs to */
	TWeakObjectPtr<UWorld> World;

	/* Begin ISceneOutlinerTreeItem Implementation */
	virtual bool IsValid() const override { return World.IsValid(); }
	virtual void OnExpansionChanged() override;
	virtual void Delete(FName InNewParentPath) override;
	virtual TSharedRef<SWidget> GenerateLabelWidget(ISceneOutliner& Outliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow) override;
	/* End FFolderTreeItem Implementation */
		
	/* Begin FFolderTreeItem Implementation */
	virtual FName MoveTo(const FName& NewParent) override;
private:
	virtual void CreateSubFolder(TWeakPtr<SSceneOutliner> WeakOutliner) override;
	/* End FFolderTreeItem Implementation */
};
