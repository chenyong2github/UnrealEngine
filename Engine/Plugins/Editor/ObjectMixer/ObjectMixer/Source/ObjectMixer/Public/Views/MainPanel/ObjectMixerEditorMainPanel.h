// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ObjectFilter/ObjectMixerEditorObjectFilter.h"
#include "Views/List/ObjectMixerEditorListFilters/IObjectMixerEditorListFilter.h"

#include "UObject/StrongObjectPtr.h"
#include "Widgets/SWidget.h"

class FObjectMixerEditorList;
class SObjectMixerEditorMainPanel;

DECLARE_MULTICAST_DELEGATE(FOnObjectMixerCollectionMapChanged)

class OBJECTMIXEREDITOR_API FObjectMixerEditorMainPanel : public TSharedFromThis<FObjectMixerEditorMainPanel>
{
public:
	FObjectMixerEditorMainPanel(const FName InModuleName)
	: ModuleName(InModuleName)
	{}

	~FObjectMixerEditorMainPanel() = default;

	void Init();

	TSharedRef<SWidget> GetOrCreateWidget();

	void RegenerateListModel();

	/**
	 * Regenerate the list items and refresh the list. Call when adding or removing items.
	 */
	void RequestRebuildList() const;

	/**
	 * Refresh filters and sorting.
	 * Useful for when the list state has gone stale but the variable count has not changed.
	 */
	void RefreshList() const;

	void RequestSyncEditorSelectionToListSelection() const;

	TWeakPtr<FObjectMixerEditorList> GetEditorListModel() const
	{
		return EditorListModel;
	}

	void RebuildCollectionSelector();

	FString GetSearchStringFromSearchInputField() const;

	void OnClassSelectionChanged(UClass* InNewClass);
	TObjectPtr<UClass> GetClassSelection() const;
	bool IsClassSelected(UClass* InNewClass) const;

	UObjectMixerObjectFilter* GetObjectFilter();

	void CacheObjectFilterObject();

	/**
	 * Get the style of the tree (flat list or hierarchy)
	 */
	EObjectMixerTreeViewMode GetTreeViewMode()
	{
		return TreeViewMode;
	}
	/**
	 * Set the style of the tree (flat list or hierarchy)
	 */
	void SetTreeViewMode(EObjectMixerTreeViewMode InViewMode)
	{
		TreeViewMode = InViewMode;
		RequestRebuildList();
	}

	/**
	 * Returns result from Filter->GetObjectClassesToFilter.
	 */
	TSet<UClass*> GetObjectClassesToFilter()
	{
		if (const UObjectMixerObjectFilter* Filter = GetObjectFilter())
		{
			return Filter->GetObjectClassesToFilter();
		}
		
		return {};
	}

	/**
	 * Returns result from Filter->GetObjectClassesToPlace.
	 */
	TSet<TSubclassOf<AActor>> GetObjectClassesToPlace()
	{
		TSet<TSubclassOf<AActor>> ReturnValue;

		if (const UObjectMixerObjectFilter* Filter = GetObjectFilter())
		{
			ReturnValue = Filter->GetObjectClassesToPlace();
		}
		
		return ReturnValue;
	}

	const TArray<TSharedRef<IObjectMixerEditorListFilter>>& GetShowFilters() const;

	/**
	 * Get the rows that have solo visibility. All other rows should be set to temporarily invisible in editor.
	 */
	TSet<TWeakPtr<FObjectMixerEditorListRow>> GetSoloRows()
	{
		return SoloRows;
	}

	/**
	 * Add a row that has solo visibility. This does not set temporary editor invisibility for other rows.
	 */
	void AddSoloRow(TSharedRef<FObjectMixerEditorListRow> InRow)
	{
		SoloRows.Add(InRow);
	}

	/**
	 * Remove a row that does not have solo visibility. This does not set temporary editor invisibility for other rows.
	 */
	void RemoveSoloRow(TSharedRef<FObjectMixerEditorListRow> InRow)
	{
		SoloRows.Remove(InRow);
	}

	/**
	 * Clear the rows that have solo visibility. This does not remove temporary editor invisibility for other rows.
	 */
	void ClearSoloRows()
	{
		SoloRows.Empty();
	}

	TSubclassOf<UObjectMixerObjectFilter> GetObjectFilterClass() const
	{
		return ObjectFilterClass;
	}

	void SetObjectFilterClass(UClass* InObjectFilterClass)
	{
		if (ensureAlwaysMsgf(InObjectFilterClass->IsChildOf(UObjectMixerObjectFilter::StaticClass()), TEXT("%hs: Class '%s' is not a child of UObjectMixerObjectFilter."), __FUNCTION__, *InObjectFilterClass->GetName()))
		{
			ObjectFilterClass = InObjectFilterClass;
			CacheObjectFilterObject();
			RequestRebuildList();
		}
	}

	FName GetModuleName() const
	{
		return ModuleName;
	}

	// User Collections

	/**
	 * Add set of objects to a collection in the map, or create a new collection if one does not exist,
	 */
	void AddObjectsToCollection(const FName& CollectionName, const TSet<FSoftObjectPath>& ObjectsToAdd) const;
	void RemoveObjectsFromCollection(const FName& CollectionName, const TSet<FSoftObjectPath>& ObjectsToRemove) const;
	void RemoveCollection(const FName& CollectionName) const;
	void ReorderCollection(const FName& CollectionToMoveName, const FName& CollectionInsertBeforeName) const;
	bool IsObjectInCollection(const FName& CollectionName, const FSoftObjectPath& InObject) const;
	TSet<FName> GetCollectionsForObject(const FSoftObjectPath& InObject) const;
	TArray<FName> GetAllCollectionNames() const;
	FOnObjectMixerCollectionMapChanged& GetOnObjectMixerCollectionMapChanged()
	{
		return OnObjectMixerCollectionMapChanged;
	}

	/**
	 * Returns the collections selected by the user. If the set is empty, consider "All" collections to be selected.
	 */
	const TSet<FName>& GetCurrentCollectionSelection() const;

private:

	TSharedPtr<SObjectMixerEditorMainPanel> MainPanelWidget;

	TSharedPtr<FObjectMixerEditorList> EditorListModel;

	TStrongObjectPtr<UObjectMixerObjectFilter> ObjectFilterPtr;

	/**
	 * The class used to generate property edit columns
	 */
	TSubclassOf<UObjectMixerObjectFilter> ObjectFilterClass;

	/**
	 * Determines the style of the tree (flat list or hierarchy)
	 */
	EObjectMixerTreeViewMode TreeViewMode = EObjectMixerTreeViewMode::Folders;

	TSet<TWeakPtr<FObjectMixerEditorListRow>> SoloRows = {};

	FName ModuleName = NAME_None;

	FOnObjectMixerCollectionMapChanged OnObjectMixerCollectionMapChanged;
};
