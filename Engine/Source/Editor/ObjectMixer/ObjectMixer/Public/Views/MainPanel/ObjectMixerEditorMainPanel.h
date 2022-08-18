// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ObjectFilter/ObjectMixerEditorObjectFilter.h"
#include "Views/List/ObjectMixerEditorListFilters/IObjectMixerEditorListFilter.h"

#include "UObject/StrongObjectPtr.h"
#include "Widgets/SWidget.h"

class FObjectMixerEditorList;
class SObjectMixerEditorMainPanel;

DECLARE_MULTICAST_DELEGATE(FOnObjectMixerCategoryMapChanged)

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

	TWeakPtr<FObjectMixerEditorList> GetEditorListModel() const
	{
		return EditorListModel;
	}

	void RebuildCategorySelector();

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
	 * Get the row that has solo visibility. All other rows should be set to temporarily invisible in editor.
	 */
	TWeakPtr<FObjectMixerEditorListRow> GetSoloRow()
	{
		return SoloRow;
	}

	/**
	 * Set the row that has solo visibility. This does not set temporary editor invisibility for other rows.
	 */
	void SetSoloRow(TSharedRef<FObjectMixerEditorListRow> InRow)
	{
		SoloRow = InRow;
	}

	/**
	 * Clear the row that has solo visibility. This does not remove temporary editor invisibility for other rows.
	 */
	void ClearSoloRow()
	{
		SoloRow = nullptr;
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

	// User Categorization

	/**
	 * Add set of objects to a category in the map, or create a new category if one does not exist,
	 */
	void AddObjectsToCategory(const FName& CategoryName, const TSet<FSoftObjectPath>& ObjectsToAdd) const;
	void RemoveObjectsFromCategory(const FName& CategoryName, const TSet<FSoftObjectPath>& ObjectsToRemove) const;
	void RemoveCategory(const FName& CategoryName) const;
	bool IsObjectInCategory(const FName& CategoryName, const FSoftObjectPath& InObject) const;
	TSet<FName> GetCategoriesForObject(const FSoftObjectPath& InObject) const;
	TSet<FName> GetAllCategories() const;
	FOnObjectMixerCategoryMapChanged& GetOnObjectMixerCategoryMapChanged()
	{
		return OnObjectMixerCategoryMapChanged;
	}

	/**
	 * Returns the categories selected by the user. If the set is empty, consider "All" categories to be selected.
	 */
	const TSet<FName>& GetCurrentCategorySelection() const;

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
	EObjectMixerTreeViewMode TreeViewMode = EObjectMixerTreeViewMode::FolderObjectSubObject;

	TWeakPtr<FObjectMixerEditorListRow> SoloRow = nullptr;

	FName ModuleName = NAME_None;

	FOnObjectMixerCategoryMapChanged OnObjectMixerCategoryMapChanged;
};
