// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SceneOutlinerFwd.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class ADisplayClusterRootActor;
class ADisplayClusterLightCardActor;
class ITableRow;
class FExtender;
class FUICommandList;
class SDisplayClusterLightCardEditor;
class STableViewBase;

class SSceneOutliner;
template<class T>
class STreeView;

/** Displays all of the light cards associated with a particular display cluster root actor in a list view */
class SDisplayClusterLightCardOutliner : public SCompoundWidget
{
public:
	struct FStageActorTreeItem
	{
		TWeakObjectPtr<AActor> Actor;

		FStageActorTreeItem() :
			Actor(nullptr)
		{ }
	};

public:
	SLATE_BEGIN_ARGS(SDisplayClusterLightCardOutliner)
	{}
	SLATE_END_ARGS()

	virtual ~SDisplayClusterLightCardOutliner() override;

	void Construct(const FArguments& InArgs, TSharedPtr<SDisplayClusterLightCardEditor> InLightCardEditor, TSharedPtr<FUICommandList> InCommandList);

	void SetRootActor(ADisplayClusterRootActor* NewRootActor);

	const TArray<TSharedPtr<FStageActorTreeItem>>& GetStageActorTreeItems() const { return StageActorTreeItems; }

	/** Gets a list of light card actors that are currently selected in the list */
	void GetSelectedActors(TArray<AActor*>& OutSelectedActors) const;

	template<typename T>
	TArray<T*> GetSelectedActorsAs() const
	{
		TArray<AActor*> SelectedActors;
		GetSelectedActors(SelectedActors);
		TArray<T*> OutArray;
		Algo::TransformIf(SelectedActors, OutArray, [](AActor* InItem)
		{
			return InItem && InItem->IsA<T>();
		},
		[](AActor* InItem)
		{
			return CastChecked<T>(InItem);
		});
		return OutArray;
	}

	/** Select light cards in the outliner and display their details */
	void SelectActors(const TArray<AActor*>& ActorsToSelect);

	/** Restores the last valid cached selection to the outliner */
	void RestoreCachedSelection();

private:
	/** Creates a world outliner based on the root actor world */
	void CreateWorldOutliner();
	
	/**
	 * Fill the outliner with available actors
	 * @return True if the list has been modified
	 */
	bool FillActorList();

	/** Called when the user clicks on a selection in the outliner */
	void OnOutlinerSelectionChanged(FSceneOutlinerTreeItemPtr TreeItem, ESelectInfo::Type Type);

	/** Return our stage actor tree item from the outliner tree item */
	TSharedPtr<FStageActorTreeItem> GetStageActorTreeItemFromOutliner(FSceneOutlinerTreeItemPtr InOutlinerTreeItem) const;

	/** Return an actor if the tree item is an actor tree item */
	AActor* GetActorFromTreeItem(FSceneOutlinerTreeItemPtr InOutlinerTreeItem) const;
	
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	/** Registers a context menu for use with tool menus */
	void RegisterContextMenu(FName& InName, struct FToolMenuContext& InContext);

private:
	/** Pointer to the light card editor that owns this widget */
	TWeakPtr<SDisplayClusterLightCardEditor> LightCardEditorPtr;

	/** A hierarchical list of stage actor tree items to be displayed in the tree view */
	TArray<TSharedPtr<FStageActorTreeItem>> StageActorTree;

	/** All actors currently tracked by the scene outliner */
	TMap<TObjectPtr<AActor>, TSharedPtr<FStageActorTreeItem>> TrackedActors;

	/** Cached actors currently selected. Used for synchronizing with the outliner mode */
	TArray<TWeakObjectPtr<AActor>> CachedSelectedActors;
	
	/** A flattened list of all the light card actors being displayed in the tree view */
	TArray<TSharedPtr<FStageActorTreeItem>> StageActorTreeItems;

	/** The active root actor whose light cards are being displayed */
	TWeakObjectPtr<ADisplayClusterRootActor> RootActor;

	/** The scene outliner managed by this widget */
	TSharedPtr<SSceneOutliner> SceneOutliner;

	/** The most recently selected item from the outliner */
	TWeakPtr<ISceneOutlinerTreeItem> MostRecentSelectedItem;

	/** Called when the outliner makes a selection change */
	FDelegateHandle SceneOutlinerSelectionChanged;
	
	/** Extenders for menus */
	TSharedPtr<FExtender> Extenders;

	/** Mapped commands for this list */
	TSharedPtr<FUICommandList> CommandList;
};