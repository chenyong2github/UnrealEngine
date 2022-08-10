// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class FExtender;
class FUICommandList;
class ADisplayClusterRootActor;
class ADisplayClusterLightCardActor;
class SDisplayClusterLightCardEditor;
class ITableRow;
class STableViewBase;

template<class T>
class STreeView;

/** Displays all of the light cards associated with a particular display cluster root actor in a list view */
class SDisplayClusterLightCardList : public SCompoundWidget
{
public:
	DECLARE_DELEGATE(FOnLightCardListChanged)
	
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
	SLATE_BEGIN_ARGS(SDisplayClusterLightCardList)
	{}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<SDisplayClusterLightCardEditor> InLightCardEditor, TSharedPtr<class FUICommandList> InCommandList);

	void SetRootActor(ADisplayClusterRootActor* NewRootActor);

	const TArray<TSharedPtr<FLightCardTreeItem>>& GetLightCardActors() const { return LightCardActors; }

	/** Gets a list of light card actors that are currently selected in the list */
	void GetSelectedLightCards(TArray<ADisplayClusterLightCardActor*>& OutSelectedLightCards);

	void SelectLightCards(const TArray<ADisplayClusterLightCardActor*>& LightCardsToSelect);

	/** Refreshes the list */
	void Refresh();

private:
	/**
	 * Fill the LightCard list with available LightCards
	 * @return True if the list has been modified
	 */
	bool FillLightCardList();

	TSharedRef<ITableRow> GenerateTreeItemRow(TSharedPtr<FLightCardTreeItem> Item, const TSharedRef<STableViewBase>& OwnerTable);
	void GetChildrenForTreeItem(TSharedPtr<FLightCardTreeItem> InItem, TArray<TSharedPtr<FLightCardTreeItem>>& OutChildren);
	void OnTreeItemSelected(TSharedPtr<FLightCardTreeItem> InItem, ESelectInfo::Type SelectInfo);

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	
	TSharedPtr<SWidget> CreateContextMenu();
	
private:
	/** Pointer to the light card editor that owns this widget */
	TWeakPtr<SDisplayClusterLightCardEditor> LightCardEditorPtr;

	/** A hierarchical list of light card tree items to be displayed in the tree view */
	TArray<TSharedPtr<FLightCardTreeItem>> LightCardTree;

	/** A flattened list of all the light card actors being displayed in the tree view */
	TArray<TSharedPtr<FLightCardTreeItem>> LightCardActors;

	/** The active root actor whose light cards are being displayed */
	TWeakObjectPtr<ADisplayClusterRootActor> RootActor;

	/** The tree view widget displaying the light cards */
	TSharedPtr<STreeView<TSharedPtr<FLightCardTreeItem>>> LightCardTreeView;
	
	/** Extenders for menus */
	TSharedPtr<FExtender> Extenders;

	/** Mapped commands for this list */
	TSharedPtr<FUICommandList> CommandList;
};