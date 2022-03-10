// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class ADisplayClusterRootActor;
class ITableRow;
class STableViewBase;

template<class T>
class STreeView;

/** Displays all of the light cards associated with a particular display cluster root actor in a list view */
class SDisplayClusterLightCardList : public SCompoundWidget
{
public:
	struct FLightCardTreeItem
	{
		TWeakObjectPtr<AActor> LightCardActor;
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

	void Construct(const FArguments& args);

	void SetRootActor(ADisplayClusterRootActor* NewRootActor);

	const TArray<TSharedPtr<FLightCardTreeItem>>& GetLightCardActors() const { return LightCardActors; }
private:
	/**
	 * Fill the LightCard list with available LightCards.
	 * @return True if the list has been modified.
	 */
	bool FillLightCardList();

	TSharedRef<ITableRow> GenerateTreeItemRow(TSharedPtr<FLightCardTreeItem> Item, const TSharedRef<STableViewBase>& OwnerTable);
	void GetChildrenForTreeItem(TSharedPtr<FLightCardTreeItem> InItem, TArray<TSharedPtr<FLightCardTreeItem>>& OutChildren);
	void OnTreeItemSelected(TSharedPtr<FLightCardTreeItem> InItem, ESelectInfo::Type SelectInfo);

private:
	/** A hierarchical list of light card tree items to be displayed in the tree view */
	TArray<TSharedPtr<FLightCardTreeItem>> LightCardTree;

	/** A flattened list of all the light card actors being displayed in the tree view */
	TArray<TSharedPtr<FLightCardTreeItem>> LightCardActors;

	/** The active root actor whose light cards are being displayed */
	TWeakObjectPtr<ADisplayClusterRootActor> RootActor;

	/** The tree view widget displaying the light cards */
	TSharedPtr<STreeView<TSharedPtr<FLightCardTreeItem>>> LightCardTreeView;
};