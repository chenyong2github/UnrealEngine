// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ObjectFilter/ObjectMixerEditorObjectFilter.h"
#include "Styling/SlateTypes.h"
#include "Widgets/SCompoundWidget.h"

class FObjectMixerEditorMainPanel;

class OBJECTMIXEREDITOR_API SObjectMixerEditorMainPanel final : public SCompoundWidget
{

public:

	SLATE_BEGIN_ARGS(SObjectMixerEditorMainPanel)
	{}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<FObjectMixerEditorMainPanel>& InMainPanel);

	TWeakPtr<FObjectMixerEditorMainPanel> GetMainPanelModel()
	{
		return MainPanelModel;
	}

	FText GetSearchTextFromSearchInputField() const;
	FString GetSearchStringFromSearchInputField() const;
	void SetSearchStringInSearchInputField(const FString InSearchString) const;
	void ExecuteListViewSearchOnAllRows(const FString& SearchString, const bool bShouldRefreshAfterward = true);

	/**
	 * Determines the style of the tree (flat list or hierarchy)
	 */
	EObjectMixerTreeViewMode GetTreeViewMode();
	/**
	 * Determine the style of the tree (flat list or hierarchy)
	 */
	void SetTreeViewMode(EObjectMixerTreeViewMode InViewMode);

	void ToggleFilterActive(const FString& FilterName);

	const TArray<TSharedRef<class IObjectMixerEditorListFilter>>& GetShowFilters()
	{
		return ShowFilters;	
	}

	const TSet<FName>& GetCurrentCollectionSelection();
	
	void RebuildCollectionSelector();

	bool RequestRemoveCollection(const FName& CollectionName);
	bool RequestDuplicateCollection(const FName& CollectionToDuplicateName, FName& DesiredDuplicateName) const;
	bool RequestRenameCollection(const FName& CollectionNameToRename, const FName& NewCollectionName);
	bool DoesCollectionExist(const FName& CollectionName) const;
	
	void OnCollectionCheckedStateChanged(ECheckBoxState State, FName CollectionName);
	ECheckBoxState IsCollectionChecked(FName Section) const;

	virtual ~SObjectMixerEditorMainPanel() override;

private:

	/** A reference to the struct that controls this widget */
	TWeakPtr<FObjectMixerEditorMainPanel> MainPanelModel;

	TArray<TSharedRef<IObjectMixerEditorListFilter>> ShowFilters;

	TSharedPtr<class SSearchBox> SearchBoxPtr;
	TSharedPtr<class SComboButton> ViewOptionsComboButton;

	TSharedRef<SWidget> GenerateToolbar();
	TSharedRef<SWidget> OnGenerateAddObjectButtonMenu() const;

	TSharedRef<SWidget> OnGenerateFilterClassMenu();
	TSharedRef<SWidget> BuildShowOptionsMenu();

	void OnSearchTextChanged(const FText& Text);

	// User Collections
	
	TSharedPtr<class SWrapBox> CollectionSelectorBox;
	TSet<FName> CurrentCollectionSelection;

	void ResetCurrentCollectionSelection()
	{
		CurrentCollectionSelection.Reset();
	}
};
