// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "Internationalization/Text.h"
#include "NiagaraTypes.h"
#include "NiagaraActions.h"
#include "Framework/Commands/Commands.h"

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
	enum EToolkitType
	{
		SCRIPT,
		SYSTEM,
	};

	SLATE_BEGIN_ARGS(SNiagaraParameterPanel)
	{}
	SLATE_END_ARGS();

	~SNiagaraParameterPanel();

	void Construct(const FArguments& InArgs, const TSharedPtr<INiagaraParameterPanelViewModel>& InParameterPanelViewModel, const TSharedPtr<FUICommandList>& InToolkitCommands);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/** Whether the add parameter button should be enabled. */
// 	bool ParameterAddEnabled() const; //@todo(ng) impl

	void AddParameter(FNiagaraVariable NewVariable, const NiagaraParameterPanelSectionID::Type SectionID);

 	/** Refreshes the palette items for the graph action menu on the next tick. */
 	void Refresh();

	static TSharedRef<SExpanderArrow> CreateCustomActionExpander(const struct FCustomExpanderData& ActionMenuData);

	void SetVariablesObjectSelection(const TSharedRef<FNiagaraObjectSelection>& InVariablesObjectSelection);

private:
	/** Function to bind to SNiagaraAddParameterMenus to filter types we allow creating */
	bool AllowMakeType(const FNiagaraTypeDefinition& InType) const;

	// SGraphActionMenu delegates
	TSharedRef<SWidget> OnCreateWidgetForAction(struct FCreateWidgetForActionData* const InCreateData);
	void CollectAllActions(FGraphActionListBuilderBase& OutAllActions);
	void CollectStaticSections(TArray<int32>& StaticSectionIDs) const;
	FReply OnActionDragged(const TArray<TSharedPtr<FEdGraphSchemaAction>>& InActions, const FPointerEvent& MouseEvent);
	void OnActionSelected(const TArray<TSharedPtr<FEdGraphSchemaAction>>& InActions, ESelectInfo::Type InSelectionType);
// 	void OnActionDoubleClicked(const TArray<TSharedPtr<FEdGraphSchemaAction>>& InActions); //@todo(ng) impl
// 	TSharedPtr<SWidget> OnContextMenuOpening();
 	FText OnGetSectionTitle(int32 InSectionID);
	TSharedRef<SWidget> OnGetSectionWidget(TSharedRef<SWidget> RowWidget, int32 InSectionID);
	TSharedRef<SWidget> CreateAddToSectionButton(const NiagaraParameterPanelSectionID::Type InSection, TWeakPtr<SWidget> WeakRowWidget, FText AddNewText, FName MetaDataTag);
// 
// 	/** Checks if the selected action has context menu */
// 	bool SelectionHasContextMenu() const; //@todo(ng) impl
// 
	TSharedRef<SWidget> OnGetParameterMenu(const NiagaraParameterPanelSectionID::Type InSection);
	
	//EVisibility OnAddButtonTextVisibility(TWeakPtr<SWidget> RowWidget, const NiagaraParameterMapSectionID::Type InSection) const; //@todo(ng) impl


// 	//Callbacks
	/** Try to delete all entries that are selected. If only some selected entries pass CanDeleteEntry(), then only those selected entries will be deleted. */
	void TryDeleteEntries();
	/** Test if ALL selected entries cannot be deleted. If only one selected entry passes INiagaraParameterPanelViewModel::CanRemoveParameter(), return true. Per entry deletion handling is done in TryDeleteEntries(). */
	bool CanTryDeleteEntries() const;
	void OnRequestRenameOnActionNode();
	bool CanRequestRenameOnActionNode(TWeakPtr<struct FGraphActionNode> InSelectedNode) const;
	bool CanRequestRenameOnActionNode() const;
	void HandlePaletteItemParameterRenamed(const FText& InText, const FNiagaraScriptVarAndViewInfoAction& InAction);
	void HandlePaletteItemScopeComboSelectionChanged(ENiagaraParameterScope InScope, const FNiagaraScriptVarAndViewInfoAction& InAction);

	/** Delegate handler used to match an FName to an action in the list, used for renaming keys */
	bool HandleActionMatchesName(struct FEdGraphSchemaAction* InAction, const FName& InName) const;

private:
	/** Graph Action Menu for displaying all our variables and functions */
	TSharedPtr<SGraphActionMenu> GraphActionMenu;

	/** The filter box that handles filtering for both graph action menus. */
	TSharedPtr<SSearchBox> FilterBox;
 
	/** Add parameter buttons for all sections. */
	TArray<TSharedPtr<SComboButton>> AddParameterButtons;

	TSharedPtr<FNiagaraObjectSelection> SelectedVariableObjects;

	TSharedPtr<FUICommandList> ToolkitCommands; //@todo(ng) add Find And Rename Parameter command

	bool bNeedsRefresh;

	TSharedPtr<INiagaraParameterPanelViewModel> ParameterPanelViewModel;
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
		, _AutoExpandMenu(false)
		, _IsParameterRead(true) {}
		SLATE_EVENT(FOnAddParameter, OnAddParameter)
		SLATE_EVENT(FOnCollectCustomActions, OnCollectCustomActions)
		SLATE_EVENT(FOnAllowMakeType, OnAllowMakeType)
		SLATE_ATTRIBUTE(bool, AllowCreatingNew)
		SLATE_ATTRIBUTE(bool, ShowGraphParameters)
		SLATE_ATTRIBUTE(bool, AutoExpandMenu)
		SLATE_ATTRIBUTE(bool, IsParameterRead)
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, TArray<TWeakObjectPtr<UNiagaraGraph>> InGraphs, ENiagaraParameterScope InNewParameterScope);

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
	TAttribute<bool> AutoExpandMenu;
	TAttribute<bool> IsParameterRead;

	/** Default scope to give to new parameters created through this menu. Used when generating new variable names so that their namespace is correct. */
	ENiagaraParameterScope NewParameterScope;
	FText NewParameterScopeText;
};
