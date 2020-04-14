// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "Internationalization/Text.h"
#include "NiagaraTypes.h"
#include "NiagaraActions.h"
#include "Framework/Commands/Commands.h"
#include "Widgets/SItemSelector.h"

typedef SItemSelector<ENiagaraParameterPanelCategory, FNiagaraScriptVariableAndViewInfo> SNiagaraParameterPanelSelector;

class SGraphActionMenu;
class SEditableTextBox;
class SExpanderArrow;
class SSearchBox;
class SComboButton;
class SNiagaraGraphPinAdd;
class FNiagaraObjectSelection;
class UNiagaraGraph;
struct FEdGraphSchemaAction; //@todo(ng) cleanup
class FUICommandList;
class INiagaraParameterPanelViewModel;
struct FNiagaraVariableMetaData;

class FNiagaraParameterPanelCommands : public TCommands<FNiagaraParameterPanelCommands>
{
public:
	/** Constructor */
	FNiagaraParameterPanelCommands()
		: TCommands<FNiagaraParameterPanelCommands>(TEXT("NiagaraParameterMapViewCommands"), NSLOCTEXT("Contexts", "NiagaraParameterPanel", "NiagaraParameterPanel"), NAME_None, FEditorStyle::GetStyleSetName())
	{
	}

	// Basic operations
	TSharedPtr<FUICommandInfo> DeleteEntry;

	/** Initialize commands */
	virtual void RegisterCommands() override;
};

/** A widget for viewing and editing UNiagaraScriptVariables provided by an INiagaraParameterPanelViewModel */
class SNiagaraParameterPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiagaraParameterPanel)
	{}
	SLATE_END_ARGS();

	NIAGARAEDITOR_API ~SNiagaraParameterPanel();

	NIAGARAEDITOR_API void Construct(const FArguments& InArgs, const TSharedPtr<INiagaraParameterPanelViewModel>& InParameterPanelViewModel, const TSharedPtr<FUICommandList>& InToolkitCommands);

	TArray<ENiagaraParameterPanelCategory> OnGetCategoriesForItem(const FNiagaraScriptVariableAndViewInfo& Item);
	bool OnCompareCategoriesForEquality(const ENiagaraParameterPanelCategory& CategoryA, const ENiagaraParameterPanelCategory& CategoryB) const;
	bool OnCompareCategoriesForSorting(const ENiagaraParameterPanelCategory& CategoryA, const ENiagaraParameterPanelCategory& CategoryB) const;
	bool OnCompareItemsForEquality(const FNiagaraScriptVariableAndViewInfo& ItemA, const FNiagaraScriptVariableAndViewInfo& ItemB) const;
	bool OnCompareItemsForSorting(const FNiagaraScriptVariableAndViewInfo& ItemA, const FNiagaraScriptVariableAndViewInfo& ItemB) const;
	bool OnDoesItemMatchFilterText(const FText& FilterText, const FNiagaraScriptVariableAndViewInfo& Item);
	TSharedRef<SWidget> OnGenerateWidgetForCategory(const ENiagaraParameterPanelCategory& Category);
	TSharedRef<SWidget> OnGenerateWidgetForItem(const FNiagaraScriptVariableAndViewInfo& Item);
	const TArray<TArray<ENiagaraParameterPanelCategory>>& GetDefaultCategoryPaths() const;

	NIAGARAEDITOR_API virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
// 
// 	/** Whether the add parameter button should be enabled. */
// // 	bool ParameterAddEnabled() const; //@todo(ng) impl
// 
	void AddParameter(FNiagaraVariable NewVariable, const ENiagaraParameterPanelCategory Category);

	/** Refreshes the items for the item selector on the next tick. */
	void Refresh();

	static TSharedRef<SExpanderArrow> CreateCustomActionExpander(const struct FCustomExpanderData& ActionMenuData);

private:
	bool SelectParameterEntryByName(const FName& ParameterName) const;

	/** Function to bind to SNiagaraAddParameterMenus to filter types we allow creating */
	bool AllowMakeType(const FNiagaraTypeDefinition& InType) const;

	TSharedRef<SWidget> CreateAddToCategoryButton(const ENiagaraParameterPanelCategory Category, FText AddNewText, FName MetaDataTag);
// // 
// // 	/** Checks if the selected action has context menu */
// // 	bool SelectionHasContextMenu() const; //@todo(ng) impl
// // 
	TSharedRef<SWidget> OnGetParameterMenu(const ENiagaraParameterPanelCategory Category);

	static ENiagaraParameterScope GetScopeForNewParametersInCategory(const ENiagaraParameterPanelCategory Category);
	static bool GetCanAddParametersToCategory(const ENiagaraParameterPanelCategory Category);
// 	
// 	//EVisibility OnAddButtonTextVisibility(TWeakPtr<SWidget> RowWidget, const NiagaraParameterMapSectionID::Type InSection) const; //@todo(ng) impl
// 
// 
// // 	//Callbacks
// 	/** Try to delete all entries that are selected. If only some selected entries pass CanDeleteEntry(), then only those selected entries will be deleted. */
// 	void TryDeleteEntries();
// 	/** Test if ALL selected entries cannot be deleted. If only one selected entry passes INiagaraParameterPanelViewModel::CanRemoveParameter(), return true. Per entry deletion handling is done in TryDeleteEntries(). */
// 	bool CanTryDeleteEntries() const;
// 	void OnRequestRenameOnActionNode();
// 	bool CanRequestRenameOnActionNode(TWeakPtr<struct FGraphActionNode> InSelectedNode) const;
// 	bool CanRequestRenameOnActionNode() const;
// 	void HandlePaletteItemParameterRenamed(const FText& InText, const FNiagaraScriptVarAndViewInfoAction& InAction);
// 	void HandlePaletteItemScopeComboSelectionChanged(ENiagaraParameterScope InScope, const FNiagaraScriptVarAndViewInfoAction& InAction);
	void HandleExternalSelectionChanged(const UObject* Obj);
// 
// 	/** Delegate handler used to match an FName to an action in the list, used for renaming keys */
// 	bool HandleActionMatchesName(struct FEdGraphSchemaAction* InAction, const FName& InName) const;
// 
// private:
// 	/** Graph Action Menu for displaying all our variables and functions */
// 	TSharedPtr<SGraphActionMenu> GraphActionMenu;
// 
// 	/** The filter box that handles filtering for both graph action menus. */
// 	TSharedPtr<SSearchBox> FilterBox;
//  
// 	/** Add parameter buttons for all sections. */
	TMap<ENiagaraParameterPanelCategory, TSharedPtr<SComboButton>> AddParameterButtons;
// 
// 	TSharedPtr<FNiagaraObjectSelection> SelectedVariableObjects;
// 
	TSharedPtr<FUICommandList> ToolkitCommands; //@todo(ng) add Find And Rename Parameter command
// 
	bool bNeedsRefresh;
// 	bool bGraphActionPendingRename;
// 
	TSharedPtr<INiagaraParameterPanelViewModel> ParameterPanelViewModel;
	TSharedPtr<SNiagaraParameterPanelSelector> ItemSelector;
};

class SNiagaraAddParameterMenu2 : public SCompoundWidget
{
public:
	/** Delegate that can be used to create a widget for a particular action */
	DECLARE_DELEGATE_OneParam(FOnAddParameter, FNiagaraVariable);
	DECLARE_DELEGATE_TwoParams(FOnCollectCustomActions, FGraphActionListBuilderBase&, bool&);
	DECLARE_DELEGATE_RetVal_OneParam(bool, FOnAllowMakeType, const FNiagaraTypeDefinition&);

	SLATE_BEGIN_ARGS(SNiagaraAddParameterMenu2)
		: _AllowCreatingNew(true)
		, _ShowGraphParameters(true)
		, _ShowKnownConstantParametersFilter(ENiagaraParameterPanelCategory::None)
		, _AutoExpandMenu(false)
		, _IsParameterRead(true)
		, _NewParameterScope(ENiagaraParameterScope::Particles) {}
		SLATE_EVENT(FOnAddParameter, OnAddParameter)
		SLATE_EVENT(FOnCollectCustomActions, OnCollectCustomActions)
		SLATE_EVENT(FOnAllowMakeType, OnAllowMakeType)
		SLATE_ATTRIBUTE(bool, AllowCreatingNew)
		SLATE_ATTRIBUTE(bool, ShowGraphParameters)
		SLATE_ATTRIBUTE(ENiagaraParameterPanelCategory, ShowKnownConstantParametersFilter)
		SLATE_ATTRIBUTE(bool, AutoExpandMenu)
		SLATE_ATTRIBUTE(bool, IsParameterRead)
		SLATE_ATTRIBUTE(FString, NewParameterNamespace)
		SLATE_ARGUMENT(ENiagaraParameterScope, NewParameterScope)
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, TArray<TWeakObjectPtr<UNiagaraGraph>> InGraphs);

	TSharedRef<SEditableTextBox> GetSearchBox();

private:
	void OnActionSelected(const TArray<TSharedPtr<FEdGraphSchemaAction>>& SelectedActions, ESelectInfo::Type InSelectionType);
	void CollectAllActions(FGraphActionListBuilderBase& OutAllActions);
	void AddParameterSelected(FNiagaraVariable NewVariable);
	void AddParameterAndMetaDataSelected(FNiagaraVariable NewVariable, const FNiagaraVariableMetaData NewVariableMetaData);

	TSharedPtr<SGraphActionMenu> GraphMenu;

	/** Delegate that gets fired when a parameter was added. */
	FOnAddParameter OnAddParameter;
	FOnCollectCustomActions OnCollectCustomActions;
	FOnAllowMakeType OnAllowMakeType;

	TArray<TWeakObjectPtr<UNiagaraGraph>> Graphs;

	TAttribute<bool> AllowCreatingNew;
	TAttribute<bool> ShowGraphParameters;
	TAttribute<ENiagaraParameterPanelCategory> ShowKnownConstantParametersFilter;
	TAttribute<bool> AutoExpandMenu;
	TAttribute<bool> IsParameterRead;
	TAttribute<FString> NewParameterNamespace;

	/** Default scope to give to new parameters created through this menu. Used when generating new variable names so that their namespace is correct. */
	ENiagaraParameterScope NewParameterScope;
	FText NewParameterScopeText;
};
