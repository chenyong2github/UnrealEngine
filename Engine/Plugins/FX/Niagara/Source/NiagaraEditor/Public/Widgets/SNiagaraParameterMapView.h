// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraTypes.h"
#include "Widgets/SCompoundWidget.h"
#include "Delegates/Delegate.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "PropertyEditorDelegates.h"
#include "EdGraph/EdGraphSchema.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "Framework/Commands/Commands.h"
#include "Framework/Commands/UICommandList.h"
#include "NiagaraGraph.h"
#include "NiagaraActions.h"
#include "EditorStyleSet.h"
#include "NiagaraEditorSettings.h"

class SGraphActionMenu;
class SEditableTextBox;
class SExpanderArrow;
class SSearchBox;
class SComboButton;
class FNiagaraObjectSelection;
class UNiagaraGraph;
struct FEdGraphSchemaAction;
struct FSlateBrush;
class UNiagaraSystem;
struct FNiagaraNamespaceMetadata;
class IToolTip;
class UNiagaraEditorSettings;

/* Enums to use when grouping the blueprint members in the list panel. The order here will determine the order in the list */
namespace NiagaraParameterMapSectionID
{
	enum Type
	{
		NONE,

		USER,

		SYSTEM,
		EMITTER,
		PARTICLE,

		MODULE_INPUT,
		STATIC_SWITCH,
		MODULE_LOCAL,
		MODULE_OUTPUT,

		ENGINE,
		TRANSIENT,
		PARAMETERCOLLECTION,
		DATA_INSTANCE,

		Num
	};

	static FText OnGetSectionTitle(const NiagaraParameterMapSectionID::Type InSection);
	void OnGetSectionNamespaces(const NiagaraParameterMapSectionID::Type InSection, TArray<FName>& OutSectionNamespaces);
	static NiagaraParameterMapSectionID::Type OnGetSectionFromVariable(const FNiagaraVariable& InVar, bool IsStaticSwitchVariable, FNiagaraParameterHandle& OutParameterHandle, const NiagaraParameterMapSectionID::Type DefaultType = NiagaraParameterMapSectionID::Type::NONE);
	static bool GetSectionIsAdvancedForScript(const NiagaraParameterMapSectionID::Type InSection);
	static bool GetSectionIsAdvancedForSystem(const NiagaraParameterMapSectionID::Type InSection);
};

/** A widget for viewing and editing a set of selected objects with a details panel. */
class SNiagaraParameterMapView : public SCompoundWidget
{
public:
	enum EToolkitType
	{
		SCRIPT,
		SYSTEM,
	};

	SLATE_BEGIN_ARGS(SNiagaraParameterMapView)
	{}
	SLATE_END_ARGS();

	NIAGARAEDITOR_API virtual ~SNiagaraParameterMapView();

	NIAGARAEDITOR_API void Construct(const FArguments& InArgs, const TArray<TSharedRef<FNiagaraObjectSelection>>& InSelectedObjects, const EToolkitType InToolkitType, const TSharedPtr<FUICommandList>& InToolkitCommands);

	NIAGARAEDITOR_API virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/** Whether the add parameter button should be enabled. */
	bool ParameterAddEnabled() const;

	/** Adds parameter to the graph parameter store and refreshes the menu. */
	void AddParameter(FNiagaraVariable NewVariable);
	void AddParameter(FNiagaraVariable NewVariable, bool bEnterRenameModeOnAdd);

	/** Refreshes the graphs used for this menu. */
	void Refresh(bool bRefreshMenu = true);

	static TSharedRef<SExpanderArrow> CreateCustomActionExpander(const struct FCustomExpanderData& ActionMenuData);

	static bool IsStaticSwitchParameter(const FNiagaraVariable& Variable, const TArray<TWeakObjectPtr<UNiagaraGraph>>& Graphs);

private:
	void NiagaraEditorSettingsChanged(const FString& PropertyName, const UNiagaraEditorSettings* NiagaraEditorSettings);
	
	TSharedRef<SWidget> GetViewOptionsMenu();
	static const FSlateBrush* GetViewOptionsBorderBrush();

	/** Function to bind to SNiagaraAddParameterMenus to filter types we allow creating in generic parameters*/
	bool AllowMakeTypeGeneric(const FNiagaraTypeDefinition& InType) const;

	/** Function to bind to SNiagaraAddParameterMenus to filter types we allow creating for attributes */
	bool AllowMakeTypeAttribute(const FNiagaraTypeDefinition& InType) const;

	/** Callback when the filter is changed, forces the action tree(s) to filter */
	void OnFilterTextChanged(const FText& InFilterText);

	// SGraphActionMenu delegates
	FText GetFilterText() const;
	TSharedRef<SWidget> OnCreateWidgetForAction(struct FCreateWidgetForActionData* const InCreateData);
	void CollectAllActions(FGraphActionListBuilderBase& OutAllActions);
	void CollectStaticSections(TArray<int32>& StaticSectionIDs);
	FReply OnActionDragged(const TArray<TSharedPtr<FEdGraphSchemaAction>>& InActions, const FPointerEvent& MouseEvent);
	void OnActionSelected(const TArray<TSharedPtr<FEdGraphSchemaAction>>& InActions, ESelectInfo::Type InSelectionType);
	void OnActionDoubleClicked(const TArray<TSharedPtr<FEdGraphSchemaAction>>& InActions);
	TSharedPtr<SWidget> OnContextMenuOpening();
	FText OnGetSectionTitle(int32 InSectionID);
	TSharedPtr<IToolTip> OnGetSectionToolTip(int32 InSectionID);
	TSharedRef<SWidget> OnGetSectionWidget(TSharedRef<SWidget> RowWidget, int32 InSectionID);
	TSharedRef<SWidget> CreateAddToSectionButton(const NiagaraParameterMapSectionID::Type InSection, TWeakPtr<SWidget> WeakRowWidget, FText AddNewText, FName MetaDataTag);
	
	void CollectAllActionsForScriptToolkit(TMap<FNiagaraVariable, TArray<FNiagaraGraphParameterReferenceCollection>>& ParameterEntries);
	void CollectAllActionsForSystemToolkit(TMap<FNiagaraVariable, TArray<FNiagaraGraphParameterReferenceCollection>>& ParameterEntries);

	/** Checks if the selected action has context menu */
	bool SelectionHasContextMenu() const;
	
	TSharedRef<SWidget> OnGetParameterMenu(const NiagaraParameterMapSectionID::Type InSection = NiagaraParameterMapSectionID::NONE);
	EVisibility OnAddButtonTextVisibility(TWeakPtr<SWidget> RowWidget, const NiagaraParameterMapSectionID::Type InSection) const;

	void SelectedObjectsChanged();
	void EmptyGraphs();
	void AddGraph(UNiagaraGraph* Graph);
	void AddGraph(class UNiagaraScriptSourceBase* SourceBase);
	void OnGraphChanged(const struct FEdGraphEditAction& InAction);
	void OnSystemParameterStoreChanged();

	//Callbacks
	FText GetDeleteEntryToolTip() const;
	void OnDeleteEntry();
	bool CanDeleteEntry() const;
	FText GetRenameOnActionNodeToolTip() const;
	void OnRequestRenameOnActionNode();
	bool CanRequestRenameOnActionNode() const;
	void OnPostRenameActionNode(const FText& InText, FNiagaraParameterAction& InAction);

	bool GetSingleParameterActionForSelection(
		TSharedPtr<FNiagaraParameterAction>& OutParameterAction,
		FText& OutErrorMessage) const;

	void GetChangeNamespaceSubMenu(FMenuBuilder& MenuBuilder);
	void OnChangeNamespace(FNiagaraNamespaceMetadata Metadata);

	void GetChangeNamespaceModifierSubMenu(FMenuBuilder& MenuBuilder);

	FText GetAddNamespaceModifierToolTip() const;
	bool CanAddNamespaceModifier() const;
	void OnAddNamespaceModifier();

	FText GetRemoveNamespaceModifierToolTip() const;
	bool CanRemoveNamespaceModifier() const;
	void OnRemoveNamespaceModifier();

	FText GetEditNamespaceModifierToolTip() const;
	bool CanEditNamespaceModifier() const;
	void OnEditNamespaceModifier();

	FText GetCopyParameterReferenceToolTip() const;
	bool CanCopyParameterReference() const;
	void OnCopyParameterReference();

	void RenameParameter(FNiagaraVariable& Parameter, FName NewName);

	bool IsSystemToolkit();
	bool IsScriptToolkit();

	/** Delegate handler used to match an FName to an action in the list, used for renaming keys */
	bool HandleActionMatchesName(struct FEdGraphSchemaAction* InAction, const FName& InName) const;

private:

	/** Sets bNeedsRefresh to true. Causing the list to be refreshed next tick. */
	void RefreshActions();

	void HandleGraphSubObjectSelectionChanged(const UObject* NewSelection);

	/** Graph Action Menu for displaying all our variables and functions */
	TSharedPtr<SGraphActionMenu> GraphActionMenu;

	/** The filter box that handles filtering for both graph action menus. */
	TSharedPtr<SSearchBox> FilterBox;

	/** Add parameter buttons for all sections. */
	TArray<TSharedPtr<SComboButton>> AddParameterButtons;

	/** The selected objects being viewed and edited by this widget. */
	TSharedPtr<FNiagaraObjectSelection> SelectedScriptObjects;
	TSharedPtr<FNiagaraObjectSelection> SelectedVariableObjects;

	TArray<TWeakObjectPtr<UNiagaraGraph>> Graphs;
	TWeakObjectPtr<UNiagaraSystem> CachedSystem;
	FDelegateHandle UserParameterStoreChangedHandle;
	FDelegateHandle AddedParameterStoreChangedHandle;

	/** The handle to the graph changed delegate. */
	TArray<FDelegateHandle> OnGraphChangedHandles;
	TArray<FDelegateHandle> OnRecompileHandles;
	FDelegateHandle OnSubObjectSelectionChangedHandle;

	EToolkitType ToolkitType;
	TSharedPtr<FUICommandList> ToolkitCommands;

	TArray<int32> HiddenSectionIDs;

	bool bNeedsRefresh;
	bool bIsAddingParameter;
};

class SNiagaraAddParameterMenu : public SCompoundWidget
{
public:
	/** Delegate that can be used to create a widget for a particular action */
	DECLARE_DELEGATE_OneParam(FOnAddParameter, FNiagaraVariable);
	DECLARE_DELEGATE_TwoParams(FOnCollectCustomActions, FGraphActionListBuilderBase&, bool&);
	DECLARE_DELEGATE_RetVal_OneParam(bool, FOnAllowMakeType, const FNiagaraTypeDefinition&);

	SLATE_BEGIN_ARGS(SNiagaraAddParameterMenu)
		: _Section(NiagaraParameterMapSectionID::NONE)
		, _AllowCreatingNew(true)
		, _ShowNamespaceCategory(true)
		, _ShowGraphParameters(true)
		, _AutoExpandMenu(false)
		, _IsParameterRead(true) {}
		SLATE_EVENT(FOnAddParameter, OnAddParameter)
		SLATE_EVENT(FOnCollectCustomActions, OnCollectCustomActions)
		SLATE_EVENT(FOnAllowMakeType, OnAllowMakeType)
		SLATE_ATTRIBUTE(NiagaraParameterMapSectionID::Type, Section)
		SLATE_ATTRIBUTE(bool, AllowCreatingNew)
		SLATE_ATTRIBUTE(bool, ShowNamespaceCategory)
		SLATE_ATTRIBUTE(bool, ShowGraphParameters)
		SLATE_ATTRIBUTE(bool, AutoExpandMenu)
		SLATE_ATTRIBUTE(bool, IsParameterRead)
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, TArray<TWeakObjectPtr<UNiagaraGraph>> InGraphs);

	TSharedRef<SEditableTextBox> GetSearchBox();

	void AddParameterGroup(
		FGraphActionListBuilderBase& OutActions,
		TArray<FNiagaraVariable>& Variables,
		const NiagaraParameterMapSectionID::Type InSection = NiagaraParameterMapSectionID::NONE,
		const FText& Category = FText::GetEmpty(),
		const FString& RootCategory = FString(),
		const bool bSort = true,
		const bool bCustomName = true,
		bool bForMakeNew = false);
	
	void CollectParameterCollectionsActions(FGraphActionListBuilderBase& OutActions);
	void CollectMakeNew(FGraphActionListBuilderBase& OutActions, const NiagaraParameterMapSectionID::Type InSection = NiagaraParameterMapSectionID::NONE);

private:
	void OnActionSelected(const TArray<TSharedPtr<FEdGraphSchemaAction>>& SelectedActions, ESelectInfo::Type InSelectionType);
	void CollectAllActions(FGraphActionListBuilderBase& OutAllActions);
	void AddParameterSelected(FNiagaraVariable NewVariable, const bool bCreateCustomName = true, const NiagaraParameterMapSectionID::Type InSection = NiagaraParameterMapSectionID::NONE);
	
	TSharedPtr<SGraphActionMenu> GraphMenu;

	/** Delegate that gets fired when a parameter was added. */
	FOnAddParameter OnAddParameter;
	FOnCollectCustomActions OnCollectCustomActions;
	FOnAllowMakeType OnAllowMakeType;

	TArray<TWeakObjectPtr<UNiagaraGraph>> Graphs;

	TAttribute<NiagaraParameterMapSectionID::Type> Section;
	TAttribute<bool> AllowCreatingNew;
	TAttribute<bool> ShowNamespaceCategory;
	TAttribute<bool> ShowGraphParameters;
	TAttribute<bool> AutoExpandMenu;
	TAttribute<bool> IsParameterRead;
};
