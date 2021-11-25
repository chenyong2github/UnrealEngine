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

	FDataLayerModeParams(SSceneOutliner* InSceneOutliner, SDataLayerBrowser* InDataLayerBrowser, const TWeakObjectPtr<UWorld>& InSpecifiedWorldToDisplay = nullptr, FOnSceneOutlinerItemPicked InOnItemPicked = FOnSceneOutlinerItemPicked());

	TWeakObjectPtr<UWorld> SpecifiedWorldToDisplay = nullptr;
	SDataLayerBrowser* DataLayerBrowser;
	SSceneOutliner* SceneOutliner;
	FOnSceneOutlinerItemPicked OnItemPicked;
};

DECLARE_DELEGATE_OneParam(FOnDataLayerPicked, UDataLayer*);

class FDataLayerMode : public ISceneOutlinerMode
{
public:
	enum class EItemSortOrder : int32
	{
		DataLayer = 0,
		Actor = 10,
		Unloaded = 20,
	};

	FDataLayerMode(const FDataLayerModeParams& Params);
	virtual ~FDataLayerMode();

	virtual void Rebuild() override;
	virtual TSharedPtr<SWidget> CreateContextMenu() override;
	virtual void CreateViewContent(FMenuBuilder& MenuBuilder) override;
	virtual int32 GetTypeSortPriority(const ISceneOutlinerTreeItem& Item) const override;
	virtual ESelectionMode::Type GetSelectionMode() const override { return ESelectionMode::Multi; }
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual bool IsInteractive() const override { return true; }
	virtual bool CanRename() const override { return true; }
	virtual bool CanRenameItem(const ISceneOutlinerTreeItem& Item) const override;
	virtual bool ShowStatusBar() const override { return true; }
	virtual bool ShowViewButton() const override { return true; }
	virtual bool ShowFilterOptions() const override { return true; }
	virtual FText GetStatusText() const override;
	virtual FSlateColor GetStatusTextColor() const override { return FSlateColor::UseForeground(); }

	virtual void SynchronizeSelection() override;
	virtual void OnItemDoubleClick(FSceneOutlinerTreeItemPtr Item) override;
	virtual void OnItemAdded(FSceneOutlinerTreeItemPtr Item) override;
	virtual void OnItemRemoved(FSceneOutlinerTreeItemPtr Item) override;
	virtual void OnItemPassesFilters(const ISceneOutlinerTreeItem& Item) override;
	virtual FReply OnKeyDown(const FKeyEvent& InKeyEvent) override;
	virtual void OnItemSelectionChanged(FSceneOutlinerTreeItemPtr TreeItem, ESelectInfo::Type SelectionType, const FSceneOutlinerItemSelection& Selection) override;

	virtual bool CanSupportDragAndDrop() const { return true; }
	virtual bool ParseDragDrop(FSceneOutlinerDragDropPayload& OutPayload, const FDragDropOperation& Operation) const override;
	virtual FSceneOutlinerDragValidationInfo ValidateDrop(const ISceneOutlinerTreeItem& DropTarget, const FSceneOutlinerDragDropPayload& Payload) const override;
	virtual void OnDrop(ISceneOutlinerTreeItem& DropTarget, const FSceneOutlinerDragDropPayload& Payload, const FSceneOutlinerDragValidationInfo& ValidationInfo) const override;
	virtual TSharedPtr<FDragDropOperation> CreateDragDropOperation(const TArray<FSceneOutlinerTreeItemPtr>& InTreeItems) const override;

	void DeleteItems(const TArray<TWeakPtr<ISceneOutlinerTreeItem>>& Items);
	SDataLayerBrowser* GetDataLayerBrowser() const;

	void BuildWorldPickerMenu(FMenuBuilder& MenuBuilder);

protected:
	virtual TUniquePtr<ISceneOutlinerHierarchy> CreateHierarchy() override;

	/** Should Editor DataLayers be hidden */
	bool bHideEditorDataLayers;
	/** Should Runtime DataLayers be hidden */
	bool bHideRuntimeDataLayers;
	/** Should DataLayers actors be hidden */
	bool bHideDataLayerActors;
	/** Should unloaded actors be hidden */
	bool bHideUnloadedActors;
	/** Should show only selected actors */
	bool bShowOnlySelectedActors;
	/** Should highlight DataLayers containing selected actors */
	bool bHighlightSelectedDataLayers;
	/** Delegate to call when an item is picked */
	FOnSceneOutlinerItemPicked OnItemPicked;

private:
	/* Private Helpers */
	void RegisterContextMenu();
	void ChooseRepresentingWorld();
	void OnSelectWorld(TWeakObjectPtr<UWorld> World);
	bool IsWorldChecked(TWeakObjectPtr<UWorld> World) const;
	TArray<AActor*> GetActorsFromOperation(const FDragDropOperation& Operation, bool bOnlyFindFirst = false) const;
	TArray<UDataLayer*> GetDataLayersFromOperation(const FDragDropOperation& Operation, bool bOnlyFindFirst = false) const;
	TArray<UDataLayer*> GetSelectedDataLayers(SSceneOutliner* InSceneOutliner) const;
	void SetParentDataLayer(const TArray<UDataLayer*> DataLayers, UDataLayer* ParentDataLayer) const;
	void OnLevelSelectionChanged(UObject* Obj);
	static void CreateDataLayerPicker(UToolMenu* InMenu, FOnDataLayerPicked OnDataLayerPicked, bool bInShowRoot = false);
	bool ShouldExpandDataLayer(const UDataLayer* DataLayer) const;
	bool ContainsSelectedChildDataLayer(const UDataLayer* DataLayer) const;
	void RefreshSelection();

	/** Filter factories */
	static TSharedRef<FSceneOutlinerFilter> CreateShowOnlySelectedActorsFilter();
	static TSharedRef<FSceneOutlinerFilter> CreateHideEditorDataLayersFilter();
	static TSharedRef<FSceneOutlinerFilter> CreateHideRuntimeDataLayersFilter();
	static TSharedRef<FSceneOutlinerFilter> CreateHideDataLayerActorsFilter();
	static TSharedRef<FSceneOutlinerFilter> CreateHideUnloadedActorsFilter();

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
	/** Number of datalayers which have passed through the filters */
	uint32 FilteredDataLayerCount = 0;
	/** List of datalayers which passed the regular filters and may or may not have passed the search filter */
	TSet<TWeakObjectPtr<UDataLayer>> ApplicableDataLayers;

	TSet<TWeakObjectPtr<const UDataLayer>> SelectedDataLayersSet;
	typedef TPair<TWeakObjectPtr<const UDataLayer>, TWeakObjectPtr<const AActor>> FSelectedDataLayerActor;
	TSet<FSelectedDataLayerActor> SelectedDataLayerActors;
};

class FDataLayerPickingMode : public FDataLayerMode
{
public:
	FDataLayerPickingMode(const FDataLayerModeParams& Params);
	virtual TSharedPtr<SWidget> CreateContextMenu() override { return nullptr; }
	virtual bool ShowStatusBar() const override { return false; }
	virtual bool ShowViewButton() const override { return false; }
	virtual bool ShowFilterOptions() const override { return false; }
	virtual bool SupportsKeyboardFocus() const override { return false; }
	virtual bool CanRename() const override { return false; }
	virtual bool CanRenameItem(const ISceneOutlinerTreeItem& Item) const override { return false; }
	virtual void OnItemDoubleClick(FSceneOutlinerTreeItemPtr Item) override {}
	virtual FReply OnKeyDown(const FKeyEvent& InKeyEvent) override { return FReply::Unhandled(); }
	virtual void OnItemSelectionChanged(FSceneOutlinerTreeItemPtr TreeItem, ESelectInfo::Type SelectionType, const FSceneOutlinerItemSelection& Selection) override;
	virtual void SynchronizeSelection() override {}

	static TSharedRef<SWidget> CreateDataLayerPickerWidget(FOnDataLayerPicked OnDataLayerPicked);
};
