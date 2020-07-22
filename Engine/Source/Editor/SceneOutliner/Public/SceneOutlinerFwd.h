// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/CoreDelegates.h"

class ISceneOutliner;
class ISceneOutlinerColumn;

namespace SceneOutliner
{
	struct FTreeItemID;

	struct FInitializationOptions;
	struct FSharedOutlinerData;
	
	struct ITreeItem;
	struct FActorTreeItem;
	struct FWorldTreeItem;
	struct FFolderTreeItem;
	struct FComponentTreeItem;
	
	typedef TSharedPtr<ITreeItem> FTreeItemPtr;
	typedef TSharedRef<ITreeItem> FTreeItemRef;

	typedef TMap<FTreeItemID, FTreeItemPtr> FTreeItemMap;

	class ISceneOutlinerHierarchy;
	class ISceneOutlinerMode;

	class SSceneOutliner;

	class FOutlinerFilter;
	struct FOutlinerFilters;

	struct FDragDropPayload;
	struct FDragValidationInfo;

	/** Typedef to define an array of folder names, used during dragging */
	typedef TArray<FName> FFolderPaths;
}

DECLARE_DELEGATE_OneParam( FOnSceneOutlinerItemPicked, TSharedRef<SceneOutliner::ITreeItem> );

DECLARE_DELEGATE_OneParam( FCustomSceneOutlinerDeleteDelegate, const TArray<TWeakPtr<SceneOutliner::ITreeItem>>&  )

/** A delegate used to factory a new column type */
DECLARE_DELEGATE_RetVal_OneParam( TSharedRef< ISceneOutlinerColumn >, FCreateSceneOutlinerColumn, ISceneOutliner& );

/** A delegate used to factory a new filter type */
DECLARE_DELEGATE_RetVal( TSharedRef< SceneOutliner::FOutlinerFilter >, FCreateSceneOutlinerFilter );
