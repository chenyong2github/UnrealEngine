// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Layout/Margin.h"
#include "Framework/SlateDelegates.h"
#include "Engine/World.h"

#include "SceneOutlinerFilters.h"

class FExtender;
struct FToolMenuContext;

enum class ESceneOutlinerMode : uint8
{
	/** Allows all actors to be browsed and selected; syncs selection with the editor; drag and drop attachment, stc. */
	ActorBrowsing,

	/** Sets the outliner to operate as an actor 'picker'. */
	ActorPicker,

	/** Sets the outliner to operate as a component 'picker'. */
	ComponentPicker,

	/** 
	 * Set the outliner to be a custom displayer of the scene hierarchy
	 * This mode allow a user to manually change the default behavior of the scene outliner
	 */
	Custom,
};

DECLARE_DELEGATE_TwoParams(FSceneOutlinerModifyContextMenu, FName& /* MenuName */, FToolMenuContext& /* MenuContext */);

namespace SceneOutliner
{
	/** Container for built in column types. Function-static so they are available without linking */
	struct FBuiltInColumnTypes
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
	};

	/** Visibility enum for scene outliner columns */
	enum class EColumnVisibility : uint8
	{
		/** This column defaults to being visible on the scene outliner */
		Visible,

		/** This column defaults to being invisible, yet still available on the scene outliner */
		Invisible,
	};

	/** Column information for the scene outliner */
	struct FColumnInfo
	{
		FColumnInfo(EColumnVisibility InVisibility, int32 InPriorityIndex, const FCreateSceneOutlinerColumn& InFactory = FCreateSceneOutlinerColumn())
			: Visibility(InVisibility), PriorityIndex(InPriorityIndex), Factory(InFactory)
		{}

		FColumnInfo() {}

		FColumnInfo(const FColumnInfo& InColumnInfo)
			: Visibility(InColumnInfo.Visibility), PriorityIndex(InColumnInfo.PriorityIndex), Factory(InColumnInfo.Factory)
		{}

		EColumnVisibility 	Visibility;
		uint8				PriorityIndex;

		FCreateSceneOutlinerColumn	Factory;
	};

	/** Default column information for the scene outliner */
	struct FDefaultColumnInfo : public FColumnInfo
	{
		FDefaultColumnInfo(const FColumnInfo& InColumnInfo, TOptional<ESceneOutlinerMode> InValidMode = TOptional<ESceneOutlinerMode>())
			: ColumnInfo(InColumnInfo), ValidMode(InValidMode)
		{}

		FColumnInfo ColumnInfo;

		// The valid mode for this column. If not set, this column will be valid for all.
		TOptional<ESceneOutlinerMode> ValidMode;
	};

	struct FSharedDataBase
	{
		/** Mode to operate in */
		ESceneOutlinerMode Mode;

		/**	Invoked whenever the user attempts to delete an actor from within a Scene Outliner in the actor browsing mode */
		FCustomSceneOutlinerDeleteDelegate CustomDelete;

		/** Override default context menu handling */
		FOnContextMenuOpening ContextMenuOverride;

		/** Modify context menu before display */
		FSceneOutlinerModifyContextMenu ModifyContextMenu;

		/** Map of column types available to the scene outliner, along with default ordering */
		TMap<FName, FColumnInfo> ColumnMap;
		
		/** Whether the Scene Outliner should display parent actors in a Tree */
		bool bShowParentTree : 1;

		/** True to only show folders in this outliner */
		bool bOnlyShowFolders : 1;

		/** Show transient objects */
		bool bShowTransient : 1;

	public:

		/** Constructor */
		FSharedDataBase()
			: Mode( ESceneOutlinerMode::ActorPicker )
			, bShowParentTree( true )
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
	struct FInitializationOptions : FSharedDataBase
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
		TSharedPtr<FOutlinerFilters> Filters;

		/**
		 * (Old comment) : Broadcasts whenever the Scene Outliners selection changes
		 *
		 * This variable will be depecrated as this give the false intuition that it will broadcast when the created scene outliner selection has changed.
		 * In truth this is only broadcasted by the scene outliners when they are in the actor browsing mode.
		 * Also it's by any scene outliner, not necessary the one instanced by this initialization options.
		 */
		UE_DEPRECATED(4.24, "Use FSceneOutlinerDelegates::Get().SelectionChanged instead.")
		FSimpleMulticastDelegate::FDelegate OnSelectionChanged;
		
		/** If not null, it will force the Scene Outliner to only display this world and it will remove the chose world from the UI */
		UWorld* SpecifiedWorldToDisplay;

	public:

		/** Constructor */
		FInitializationOptions()
			: bShowHeaderRow( true )
			, bShowSearchBox( true )
			, bFocusSearchBoxWhenOpened( false )
			, bShowCreateNewFolder( true )
			, Filters( new FOutlinerFilters )
			, SpecifiedWorldToDisplay( nullptr )
		{}
	};

	/** Outliner data that is shared between a scene outliner and its items */
	struct FSharedOutlinerData : FSharedDataBase, TSharedFromThis<FSharedOutlinerData>
	{
		/** Whether the scene outliner is currently displaying PlayWorld actors */
		bool bRepresentingPlayWorld;

		/** The world that we are representing */
		UWorld* RepresentingWorld;

		/** The world the user has chosen to display */
		TWeakObjectPtr<UWorld> UserChosenWorld;

		FSharedOutlinerData()
			: bRepresentingPlayWorld(false)
			, RepresentingWorld(nullptr)
			, UserChosenWorld(nullptr)
		{}
	};

	/** Default metrics for outliner tree items */
	struct FDefaultTreeItemMetrics
	{
		static int32	IconSize() { return 18; };
		static FMargin	IconPadding() { return FMargin(0.f, 0.f, 6.f, 0.f); };
	};

}	// namespace SceneOutliner
