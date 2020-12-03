// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SceneOutlinerFwd.h"
#include "ISceneOutlinerMode.h"
#include "DataLayerTreeItem.h"

class UDataLayerEditorSubsystem;
class SDataLayerBrowser;
class UWorld;

struct FDataLayerModeParams
{
	FDataLayerModeParams() {}

	FDataLayerModeParams(SSceneOutliner* InSceneOutliner, SDataLayerBrowser* InDataLayerBrowser, const TWeakObjectPtr<UWorld>& InSpecifiedWorldToDisplay = nullptr);

	TWeakObjectPtr<UWorld> SpecifiedWorldToDisplay = nullptr;
	SDataLayerBrowser* DataLayerBrowser;
	SSceneOutliner* SceneOutliner;
};

class FDataLayerMode : public ISceneOutlinerMode
{
public:
	enum class EItemSortOrder : int32
	{
		DataLayer = 0,
		Actor = 10
	};

	FDataLayerMode(const FDataLayerModeParams& Params);
	virtual ~FDataLayerMode();

	virtual void Rebuild() override;
	virtual TSharedPtr<SWidget> CreateContextMenu() override;

	virtual int32 GetTypeSortPriority(const ISceneOutlinerTreeItem& Item) const override;

	virtual ESelectionMode::Type GetSelectionMode() const override { return ESelectionMode::Multi; }
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual bool IsInteractive() const override { return true; }
	virtual bool CanRename() const override { return true; }
	virtual bool CanRenameItem(const ISceneOutlinerTreeItem& Item) const override { return (Item.IsValid() && (Item.IsA<FDataLayerTreeItem>())); }

	virtual void OnItemDoubleClick(FSceneOutlinerTreeItemPtr Item) override;
	virtual void OnItemAdded(FSceneOutlinerTreeItemPtr Item) override;
	virtual FReply OnKeyDown(const FKeyEvent& InKeyEvent) override;
	virtual void OnItemSelectionChanged(FSceneOutlinerTreeItemPtr TreeItem, ESelectInfo::Type SelectionType, const FSceneOutlinerItemSelection& Selection) override;

	virtual bool CanSupportDragAndDrop() const { return true; }
	virtual bool ParseDragDrop(FSceneOutlinerDragDropPayload& OutPayload, const FDragDropOperation& Operation) const override;
	virtual FSceneOutlinerDragValidationInfo ValidateDrop(const ISceneOutlinerTreeItem& DropTarget, const FSceneOutlinerDragDropPayload& Payload) const override;
	virtual void OnDrop(ISceneOutlinerTreeItem& DropTarget, const FSceneOutlinerDragDropPayload& Payload, const FSceneOutlinerDragValidationInfo& ValidationInfo) const override;
	virtual TSharedPtr<FDragDropOperation> CreateDragDropOperation(const TArray<FSceneOutlinerTreeItemPtr>& InTreeItems) const override;

	void DeleteItems(const TArray<TWeakPtr<ISceneOutlinerTreeItem>>& Items);
	SDataLayerBrowser* GetDataLayerBrowser() const;

protected:
	virtual TUniquePtr<ISceneOutlinerHierarchy> CreateHierarchy() override;

private:
	/* Private Helpers */
	void RegisterContextMenu();
	void ChooseRepresentingWorld();
	TArray<AActor*> GetActorsFromOperation(const FDragDropOperation& Operation, bool bOnlyFindFirst = false) const;
	TArray<UDataLayer*> GetSelectedDataLayers(SSceneOutliner* InSceneOutliner) const;

	/** The world which we are currently representing */
	TWeakObjectPtr<UWorld> RepresentingWorld;
	/** The world which the user manually selected */
	TWeakObjectPtr<UWorld> UserChosenWorld;
	/** The DataLayer browser */
	SDataLayerBrowser* DataLayerBrowser;
	/** The DataLayerEditorSubsystem */
	UDataLayerEditorSubsystem* DataLayerEditorSubsystem;
	/** If this mode was created to display a specific world, don't allow it to be reassigned */
	const TWeakObjectPtr<UWorld> SpecifiedWorldToDisplay;

	TSet<TWeakObjectPtr<const UDataLayer>> SelectedDataLayersSet;
	typedef TPair<TWeakObjectPtr<const UDataLayer>, TWeakObjectPtr<const AActor>> FSelectedDataLayerActor;
	TSet<FSelectedDataLayerActor> SelectedDataLayerActors;
};
