// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Views/MainPanel/ObjectMixerEditorMainPanel.h"

#include "Templates/SharedPointer.h"
#include "Widgets/SWidget.h"

class SObjectMixerEditorList;

class OBJECTMIXEREDITOR_API FObjectMixerEditorList : public TSharedFromThis<FObjectMixerEditorList>
{
public:

	FObjectMixerEditorList(TSharedRef<FObjectMixerEditorMainPanel, ESPMode::ThreadSafe> InMainPanel);

	virtual ~FObjectMixerEditorList();

	void FlushWidget();
	
	TSharedRef<SWidget> GetOrCreateWidget();

	void OnPreFilterChange();
	void OnPostFilterChange();

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

	void RequestSyncEditorSelectionToListSelection() const;

	void ExecuteListViewSearchOnAllRows(const FString& SearchString, const bool bShouldRefreshAfterward = true);

	void EvaluateIfRowsPassFilters(const bool bShouldRefreshAfterward = true) const;

	TSet<TWeakPtr<FObjectMixerEditorListRow>> GetSoloRows() const;
	void ClearSoloRows();

	/** Returns true if at least one row is set to Solo. */
	bool IsListInSoloState() const;

	/**
	 * Determines whether rows' objects should be temporarily hidden in editor based on each row's visibility rules,
	 * then sets each object's visibility in editor.
	 */
	void EvaluateAndSetEditorVisibilityPerRow();

	TWeakPtr<FObjectMixerEditorMainPanel> GetMainPanelModel();

private:

	TWeakPtr<FObjectMixerEditorMainPanel> MainPanelModelPtr;

	TSharedPtr<SObjectMixerEditorList> ListWidget;
};
