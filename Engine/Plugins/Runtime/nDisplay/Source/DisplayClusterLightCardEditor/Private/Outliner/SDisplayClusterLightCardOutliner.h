// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SceneOutlinerFwd.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class FExtender;
class FUICommandList;
class ADisplayClusterRootActor;
class ADisplayClusterLightCardActor;
class SDisplayClusterLightCardEditor;
class ITableRow;
class STableViewBase;

class SSceneOutliner;
template<class T>
class STreeView;

/** Displays all of the light cards associated with a particular display cluster root actor in a list view */
class SDisplayClusterLightCardOutliner : public SCompoundWidget
{
public:
	struct FLightCardTreeItem
	{
		TWeakObjectPtr<ADisplayClusterLightCardActor> LightCardActor;
		FName ActorLayer;

		FLightCardTreeItem() :
			LightCardActor(nullptr),
			ActorLayer(NAME_None)
		{ }

		bool IsInActorLayer() { return !ActorLayer.IsNone() && LightCardActor.IsValid(); }
		bool IsActorLayer() { return !ActorLayer.IsNone() && !LightCardActor.IsValid(); }
	};

public:
	SLATE_BEGIN_ARGS(SDisplayClusterLightCardOutliner)
	{}
	SLATE_END_ARGS()

	virtual ~SDisplayClusterLightCardOutliner() override;

	void Construct(const FArguments& InArgs, TSharedPtr<SDisplayClusterLightCardEditor> InLightCardEditor, TSharedPtr<FUICommandList> InCommandList);

	void SetRootActor(ADisplayClusterRootActor* NewRootActor);

	const TArray<TSharedPtr<FLightCardTreeItem>>& GetLightCardActors() const { return LightCardActors; }

	/** Gets a list of light card actors that are currently selected in the list */
	void GetSelectedLightCards(TArray<ADisplayClusterLightCardActor*>& OutSelectedLightCards) const;

	/** Select light cards in the outliner and display their details */
	void SelectLightCards(const TArray<ADisplayClusterLightCardActor*>& LightCardsToSelect);

	/** Restores the last valid cached selection to the outliner */
	void RestoreCachedSelection();

private:
	/** Creates a world outliner based on the root actor world */
	void CreateWorldOutliner();
	
	/**
	 * Fill the LightCard list with available LightCards
	 * @return True if the list has been modified
	 */
	bool FillLightCardList();

	/** Called when the user clicks on a selection in the outliner */
	void OnOutlinerSelectionChanged(FSceneOutlinerTreeItemPtr TreeItem, ESelectInfo::Type Type);

	/** Return our light card tree item from the outliner tree item */
	TSharedPtr<FLightCardTreeItem> GetLightCardTreeItemFromOutliner(FSceneOutlinerTreeItemPtr InOutlinerTreeItem) const;

	/** Return an actor if the tree item is an actor tree item */
	AActor* GetActorFromTreeItem(FSceneOutlinerTreeItemPtr InOutlinerTreeItem) const;
	
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	/** Registers a context menu for use with tool menus */
	void RegisterContextMenu(FName& InName, struct FToolMenuContext& InContext);

private:
	/** Pointer to the light card editor that owns this widget */
	TWeakPtr<SDisplayClusterLightCardEditor> LightCardEditorPtr;

	/** A hierarchical list of light card tree items to be displayed in the tree view */
	TArray<TSharedPtr<FLightCardTreeItem>> LightCardTree;

	/** All actors currently tracked by the scene outliner */
	TMap<TObjectPtr<AActor>, TSharedPtr<FLightCardTreeItem>> TrackedActors;

	/** Cached actors currently selected. Used for synchronizing with the outliner mode */
	TArray<TWeakObjectPtr<ADisplayClusterLightCardActor>> CachedSelectedActors;
	
	/** A flattened list of all the light card actors being displayed in the tree view */
	TArray<TSharedPtr<FLightCardTreeItem>> LightCardActors;

	/** The active root actor whose light cards are being displayed */
	TWeakObjectPtr<ADisplayClusterRootActor> RootActor;

	/** The scene outliner managed by this widget */
	TSharedPtr<SSceneOutliner> SceneOutliner;

	/** The most recently selected item from the outliner */
	TWeakPtr<ISceneOutlinerTreeItem> MostRecentSelectedItem;

	/** Called when the outliner makes a selection change */
	FDelegateHandle SceneOutlinerSelectionChanged;
	
	/** The tree view widget displaying the light cards */
	TSharedPtr<STreeView<TSharedPtr<FLightCardTreeItem>>> LightCardTreeView;
	
	/** Extenders for menus */
	TSharedPtr<FExtender> Extenders;

	/** Mapped commands for this list */
	TSharedPtr<FUICommandList> CommandList;
};