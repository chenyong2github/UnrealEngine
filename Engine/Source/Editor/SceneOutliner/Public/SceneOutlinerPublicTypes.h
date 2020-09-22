// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Layout/Margin.h"
#include "Framework/SlateDelegates.h"
#include "Engine/World.h"

#include "SceneOutlinerFilters.h"

class FExtender;
struct FToolMenuContext;

DECLARE_DELEGATE_TwoParams(FSceneOutlinerModifyContextMenu, FName& /* MenuName */, FToolMenuContext& /* MenuContext */);

/** A delegate used as a factory to defer mode creation in the outliner */
DECLARE_DELEGATE_RetVal_OneParam(ISceneOutlinerMode*, FCreateSceneOutlinerMode, SSceneOutliner*);

/** Container for built in column types. Function-static so they are available without linking */
struct FSceneOutlinerBuiltInColumnTypes
{
	/** The gutter column */
	static const FName& Gutter()
	{
		static FName Gutter("Gutter");
		return Gutter;
	}

	/** The item label column */
	static const FName& Label()
	{
		static FName Label("ItemLabel");
		return Label;
	}

	/** Generic actor info column */
	static FName& ActorInfo()
	{
		static FName ActorInfo("ActorInfo");
		return ActorInfo;
	}

	static FName& SourceControl()
	{
		static FName SourceControl("SourceControl");
		return SourceControl;
	}
};

/** Visibility enum for scene outliner columns */
enum class ESceneOutlinerColumnVisibility : uint8
{
	/** This column defaults to being visible on the scene outliner */
	Visible,

	/** This column defaults to being invisible, yet still available on the scene outliner */
	Invisible,
};

/** Column information for the scene outliner */
struct FSceneOutlinerColumnInfo
{
	FSceneOutlinerColumnInfo(ESceneOutlinerColumnVisibility InVisibility, int32 InPriorityIndex, const FCreateSceneOutlinerColumn& InFactory = FCreateSceneOutlinerColumn())
		: Visibility(InVisibility), PriorityIndex(InPriorityIndex), Factory(InFactory)
	{}

	FSceneOutlinerColumnInfo() {}

	FSceneOutlinerColumnInfo(const FSceneOutlinerColumnInfo& InColumnInfo)
		: Visibility(InColumnInfo.Visibility), PriorityIndex(InColumnInfo.PriorityIndex), Factory(InColumnInfo.Factory)
	{}

	ESceneOutlinerColumnVisibility 	Visibility;
	uint8				PriorityIndex;

	FCreateSceneOutlinerColumn	Factory;
};

/** Settings for the scene outliner which can be quieried publicly */
struct FSharedSceneOutlinerData
{
	/**	Invoked whenever the user attempts to delete an actor from within a Scene Outliner in the actor browsing mode */
	FCustomSceneOutlinerDeleteDelegate CustomDelete;

	/** Modify context menu before display */
	FSceneOutlinerModifyContextMenu ModifyContextMenu;

	/** Map of column types available to the scene outliner, along with default ordering */
	TMap<FName, FSceneOutlinerColumnInfo> ColumnMap;
		
	/** Whether the Scene Outliner should display parent actors in a Tree */
	bool bShowParentTree : 1;

	/** True to only show folders in this outliner */
	bool bOnlyShowFolders : 1;

	/** Show transient objects */
	bool bShowTransient : 1;

public:

	/** Constructor */
	FSharedSceneOutlinerData()
		: bShowParentTree( true )
		, bOnlyShowFolders( false )
		, bShowTransient( false )
	{}

	/** Set up a default array of columns for this outliner */
	void UseDefaultColumns();
};

/**
	* Settings for the Scene Outliner set by the programmer before spawning an instance of the widget.  This
	* is used to modify the outliner's behavior in various ways, such as filtering in or out specific classes
	* of actors.
	*/
struct FSceneOutlinerInitializationOptions : FSharedSceneOutlinerData
{
	/** True if we should draw the header row above the tree view */
	bool bShowHeaderRow : 1;

	/** Whether the Scene Outliner should expose its searchbox */
	bool bShowSearchBox : 1;

	/** If true, the search box will gain focus when the scene outliner is created */
	bool bFocusSearchBoxWhenOpened : 1;

	/** If true, the Scene Outliner will expose a Create New Folder button */
	bool bShowCreateNewFolder : 1;

	/** Optional collection of filters to use when filtering in the Scene Outliner */
	TSharedPtr<FSceneOutlinerFilters> Filters;		

	FCreateSceneOutlinerMode ModeFactory;

public:

	/** Constructor */
	FSceneOutlinerInitializationOptions()
		: bShowHeaderRow( true )
		, bShowSearchBox( true )
		, bFocusSearchBoxWhenOpened( false )
		, bShowCreateNewFolder( true )
		, Filters( new FSceneOutlinerFilters )
		, ModeFactory()
	{}
};

/** Default metrics for outliner tree items */
struct FSceneOutlinerDefaultTreeItemMetrics
{
	static int32	IconSize() { return 16; };
	static FMargin	IconPadding() { return FMargin(0.f, 1.f, 6.f, 1.f); };
};

/** A struct which gets, and caches the visibility of a tree item */
struct SCENEOUTLINER_API FSceneOutlinerVisibilityCache
{
	/** Map of tree item to visibility */
	mutable TMap<const ISceneOutlinerTreeItem*, bool> VisibilityInfo;

	/** Get an item's visibility based on its children */
	bool RecurseChildren(const ISceneOutlinerTreeItem& Item) const;

	bool GetVisibility(const ISceneOutlinerTreeItem& Item) const;
};
