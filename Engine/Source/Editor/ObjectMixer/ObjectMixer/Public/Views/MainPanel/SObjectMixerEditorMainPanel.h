// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ObjectFilter/ObjectMixerEditorObjectFilter.h"
#include "Views/List/ObjectMixerEditorListFilters/IObjectMixerEditorListFilter.h"

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

	const TSet<FName>& GetCurrentCategorySelection();
	
	void RebuildCategorySelector();

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

	// User Categorization
	
	TSharedPtr<class SWrapBox> CategorySelectorBox;
	TSet<FName> CurrentCategorySelection;

	void ResetCurrentCategorySelection()
	{
		CurrentCategorySelection.Reset();
	}
	
	void OnCategoryCheckedChanged(ECheckBoxState State, FName SectionName);
	ECheckBoxState IsCategoryChecked(FName Section) const;
};
