// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Views/List/ObjectMixerEditorListRowData.h"

#include "ActorFolderTreeItem.h"

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "Folder.h"
#include "GameFramework/Actor.h"
#include "Layout/Visibility.h"
#include "PropertyHandle.h"
#include "SSceneOutliner.h"
#include "Templates/SharedPointer.h"
#include "UObject/Object.h"

struct OBJECTMIXEREDITOR_API FObjectMixerEditorListRowFolder : FActorFolderTreeItem
{
	explicit FObjectMixerEditorListRowFolder(
		const FFolder InFolder, 
		SSceneOutliner* InSceneOutliner, UWorld* World, const FText& InDisplayNameOverride = FText::GetEmpty())
	: FActorFolderTreeItem(InFolder, World)
	{
		RowData = FObjectMixerEditorListRowData(InSceneOutliner, InDisplayNameOverride);
	}
	
	FObjectMixerEditorListRowData RowData;

	/* Begin ISceneOutlinerTreeItem Implementation */
	static const FSceneOutlinerTreeItemType Type;
	virtual void OnVisibilityChanged(const bool bNewVisibility) override;
	/* End ISceneOutlinerTreeItem Implementation */
};
