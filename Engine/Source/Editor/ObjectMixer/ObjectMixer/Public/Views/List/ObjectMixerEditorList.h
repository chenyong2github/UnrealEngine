// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ObjectFilter/ObjectMixerEditorObjectFilter.h"

#include "UObject/StrongObjectPtr.h"
#include "Widgets/SWidget.h"

struct FObjectMixerEditorListRow;
typedef TSharedPtr<FObjectMixerEditorListRow> FObjectMixerEditorListRowPtr;

class SObjectMixerEditorList;

class OBJECTMIXEREDITOR_API FObjectMixerEditorList : public TSharedFromThis<FObjectMixerEditorList>
{
public:

	FObjectMixerEditorList() = default;

	virtual ~FObjectMixerEditorList();

	void FlushWidget();
	TSharedRef<SWidget> GetOrCreateWidget();

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

	void SetSearchString(const FString& SearchString);

	void ClearList() const;

	/**
	 * Regenerate the list items and refresh the list. Call when adding or removing variables.
	 */
	void RequestRebuildList() const;

	/**
	 * Refresh filters and sorting.
	 * Useful for when the list state has gone stale but the item count has not changed.
	 */
	void RefreshList() const;

	bool IsClassSelected(UClass* InNewClass) const;

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

private:

	/**
	 * The class used to generate property edit columns
	 */
	TSubclassOf<UObjectMixerObjectFilter> ObjectFilterClass;

	/**
	 * Determines the style of the tree (flat list or hierarchy)
	 */
	EObjectMixerTreeViewMode TreeViewMode = EObjectMixerTreeViewMode::FolderObjectSubObject;

	TSharedPtr<SObjectMixerEditorList> ListWidget;

	TStrongObjectPtr<UObjectMixerObjectFilter> ObjectFilterPtr;

	TWeakPtr<FObjectMixerEditorListRow> SoloRow = nullptr;

	// Delegates
	TFunction<void()> RebuildDelegate = [this]()
	{
		RequestRebuildList();
	};
	
	TSet<FDelegateHandle> DelegateHandles;
};
