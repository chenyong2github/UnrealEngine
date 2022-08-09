// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Views/List/ObjectMixerEditorList.h"
#include "Widgets/SWidget.h"

class FObjectMixerEditorList;
class SObjectMixerEditorMainPanel;

class OBJECTMIXEREDITOR_API FObjectMixerEditorMainPanel : public TSharedFromThis<FObjectMixerEditorMainPanel>
{
public:
	FObjectMixerEditorMainPanel();

	~FObjectMixerEditorMainPanel() = default;

	TSharedRef<SWidget> GetOrCreateWidget();

	/**
	 * Regenerate the list items and refresh the list. Call when adding or removing items.
	 */
	void RequestRebuildList() const;

	/**
	 * Refresh filters and sorting.
	 * Useful for when the list state has gone stale but the variable count has not changed.
	 */
	void RefreshList() const;

	TWeakPtr<FObjectMixerEditorList> GetEditorList() const
	{
		return EditorList;
	}

	void OnClassSelectionChanged(UClass* InNewClass) const;
	TObjectPtr<UClass> GetClassSelection() const;
	bool IsClassSelected(UClass* InNewClass) const;

private:

	TSharedPtr<SObjectMixerEditorMainPanel> MainPanelWidget;

	TSharedPtr<FObjectMixerEditorList> EditorList;
};
