// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Views/MainPanel/ObjectMixerEditorMainPanel.h"

#include "Templates/SharedPointer.h"
#include "Widgets/SWidget.h"

class SObjectMixerEditorList;

class OBJECTMIXEREDITOR_API FObjectMixerEditorList : public TSharedFromThis<FObjectMixerEditorList>
{
public:

	FObjectMixerEditorList(TSharedRef<FObjectMixerEditorMainPanel, ESPMode::ThreadSafe> InMainPanel)
	: MainPanelModelPtr(InMainPanel)
	{}

	virtual ~FObjectMixerEditorList();

	void FlushWidget();
	
	TSharedRef<SWidget> GetOrCreateWidget();

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

	void ExecuteListViewSearchOnAllRows(const FString& SearchString, const bool bShouldRefreshAfterward = true);

	void EvaluateIfRowsPassFilters(const bool bShouldRefreshAfterward = true) const;

	TWeakPtr<FObjectMixerEditorMainPanel> GetMainPanelModel();

	TWeakPtr<FObjectMixerEditorListRow> GetSoloRow();

	void SetSoloRow(TSharedRef<FObjectMixerEditorListRow> InRow);

	void ClearSoloRow();

private:

	TWeakPtr<FObjectMixerEditorMainPanel> MainPanelModelPtr;

	TSharedPtr<SObjectMixerEditorList> ListWidget;
};
