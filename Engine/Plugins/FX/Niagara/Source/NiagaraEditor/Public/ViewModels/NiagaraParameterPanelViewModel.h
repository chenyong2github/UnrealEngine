// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraEditorCommon.h"
#include "Types/SlateEnums.h"
#include "EditorUndoClient.h"
#include "EdGraph/EdGraphSchema.h"
#include "Input/Events.h"
#include "Input/Reply.h"
#include "Templates/SharedPointer.h"

class UNiagaraSystem;
class FNiagaraSystemViewModel;
class UNiagaraSystemSelectionViewModel;
class UNiagaraGraph;
class FNiagaraScriptViewModel;
class FDelegateHandle;
struct FCreateWidgetForActionData;
class FNiagaraObjectSelection;
class UNiagaraScriptVariable;

/** Interface for view models to the parameter panel. */
class INiagaraParameterPanelViewModel : public TSharedFromThis<INiagaraParameterPanelViewModel>
{

public:
	/** Delegate to signal the view model's state has changed. */
	DECLARE_DELEGATE(FOnParameterPanelViewModelRefreshed);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnParameterPanelViewModelExternalSelectionChanged, const UObject*);

	virtual ~INiagaraParameterPanelViewModel() { }

	/** Separate method to call after ctor to create delegate bindings as we must be fully constructed to do so. */
	virtual void InitBindings() = 0;

	virtual void Refresh() const = 0;

	virtual const UNiagaraScriptVariable* AddParameter(FNiagaraVariable& InVariableToAdd, const FNiagaraVariableMetaData& InVariableMetaDataToAssign) = 0;

	virtual void DeleteParameter(const FNiagaraVariable& TargetVariableToRemove, const FNiagaraVariableMetaData& TargetVariableMetaData) = 0;

	virtual bool GetCanDeleteParameterAndToolTip(const FNiagaraVariable& TargetVariableToRemove, const FNiagaraVariableMetaData& TargetVariableMetaData, FText& OutCanDeleteParameterToolTip) const = 0;

	virtual void RenameParameter(const FNiagaraVariable& TargetVariableToRename, const FNiagaraVariableMetaData& TargetVariableMetaData, const FText& NewVariableNameText) const = 0;

	virtual void ChangeParameterScope(const FNiagaraVariable& TargetVariableToModify, const FNiagaraVariableMetaData& TargetVariableMetaData, const ENiagaraParameterScope NewVariableScope) const = 0;

	virtual bool CanModifyParameter(const FNiagaraVariable& TargetVariableToModify, const FNiagaraVariableMetaData& TargetVariableMetaData) const = 0;

	virtual bool GetCanRenameParameterAndToolTip(const FNiagaraVariable& TargetVariableToRename, const FNiagaraVariableMetaData& TargetVariableMetaData, TOptional<const FText> NewVariableNameText, FText& OutCanRenameParameterToolTip) const = 0;

	virtual void HandleActionSelected(const TSharedPtr<FEdGraphSchemaAction>& InAction, ESelectInfo::Type InSelectionType) {};

	virtual FReply HandleActionDragged(const TSharedPtr<FEdGraphSchemaAction>& InAction, const FPointerEvent& MouseEvent) const = 0;

	virtual const TArray<TArray<ENiagaraParameterPanelCategory>>& GetDefaultCategoryPaths() const = 0;

	virtual TArray<ENiagaraParameterPanelCategory> GetCategoriesForParameter(const FNiagaraScriptVariableAndViewInfo& ScriptVar) const = 0;

	virtual bool CanDropParameter(const FNiagaraVariable& InTargetScriptVariableToDrop) const { return false; };

	virtual bool CanDropParameters(const TArray<FNiagaraVariable>& InTargetScriptVariablesToDrop) const { return false; };

	virtual bool CanDragParameter(const FNiagaraVariable& InTargetScriptVariableToDrag) const { return false; };

	virtual bool CanDragParameters(const TArray<FNiagaraVariable>& InTargetScriptVariablesToDrag) const { return false; };

	/** Returns a list of Graphs that are valid for operations to edit their variables and/or metadata. 
	 *Should collect all Graphs that are currently selected, but also Graphs that are implicitly selected, e.g. the node graph for the script toolkit.
	 */
	virtual TArray<TWeakObjectPtr<UNiagaraGraph>> GetEditableGraphs() const = 0;

	virtual const TArray<FNiagaraScriptVariableAndViewInfo> GetViewedParameters() = 0;

	TSharedRef<class SWidget> GetScriptParameterVisualWidget(const FNiagaraScriptVariableAndViewInfo& ScriptVarAndViewInfo) const;

	FOnParameterPanelViewModelRefreshed& GetOnRefreshed() { return OnParameterPanelViewModelRefreshed; };

	FOnParameterPanelViewModelExternalSelectionChanged& GetExternalSelectionChanged() { return OnParameterPanelViewModelExternalSelectionChanged; }
protected:
	FOnParameterPanelViewModelRefreshed OnParameterPanelViewModelRefreshed;
	FOnParameterPanelViewModelExternalSelectionChanged OnParameterPanelViewModelExternalSelectionChanged;
	/** Cached list of parameters sent to SNiagarParameterPanel, updated whenever GetViewedParameters is called. */
	TArray<FNiagaraScriptVariableAndViewInfo> CachedViewedParameters;
};

class FNiagaraSystemToolkitParameterPanelViewModel : public INiagaraParameterPanelViewModel, public FEditorUndoClient
{
public:
	/** Construct a SystemToolkit Parameter Panel View Model from a System View Model. */
	FNiagaraSystemToolkitParameterPanelViewModel(TSharedPtr<FNiagaraSystemViewModel> InSystemViewModel);

	~FNiagaraSystemToolkitParameterPanelViewModel();

	/** Begin INiagaraParameterPanelViewModel interface. */
	virtual void InitBindings() override;

	virtual void Refresh() const override;

	virtual const UNiagaraScriptVariable* AddParameter(FNiagaraVariable& VariableToAdd, const FNiagaraVariableMetaData& VariableMetaDataToAssign) override;

	virtual void DeleteParameter(const FNiagaraVariable& TargetVariableToRemove, const FNiagaraVariableMetaData& TargetVariableMetaData) override;

	virtual bool GetCanDeleteParameterAndToolTip(const FNiagaraVariable& TargetVariableToRemove, const FNiagaraVariableMetaData& TargetVariableMetaData, FText& OutCanDeleteParameterToolTip) const override;

	virtual void RenameParameter(const FNiagaraVariable& TargetVariableToRename, const FNiagaraVariableMetaData& TargetVariableMetaData, const FText& NewVariableNameText) const override;

	virtual void ChangeParameterScope(const FNiagaraVariable& TargetVariableToModify, const FNiagaraVariableMetaData& TargetVariableMetaData, const ENiagaraParameterScope NewVariableScope) const override;

	virtual bool CanModifyParameter(const FNiagaraVariable& TargetVariableToModify, const FNiagaraVariableMetaData& TargetVariableMetaData) const override;

	virtual bool GetCanRenameParameterAndToolTip(const FNiagaraVariable& TargetVariableToRename, const FNiagaraVariableMetaData& TargetVariableMetaData, TOptional<const FText> NewVariableNameText, FText& OutCanRenameParameterToolTip) const override;

	virtual FReply HandleActionDragged(const TSharedPtr<FEdGraphSchemaAction>& InAction, const FPointerEvent& MouseEvent) const override;

	virtual const TArray<TArray<ENiagaraParameterPanelCategory>>& GetDefaultCategoryPaths() const override;

	virtual TArray<ENiagaraParameterPanelCategory> GetCategoriesForParameter(const FNiagaraScriptVariableAndViewInfo& ScriptVar) const override;

	virtual TArray<TWeakObjectPtr<UNiagaraGraph>> GetEditableGraphs() const override;

	virtual const TArray<FNiagaraScriptVariableAndViewInfo> GetViewedParameters() override;
	/** End INiagaraParameterPanelViewModel interface. */

	/** Begin FEditorUndoClient Interface */
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override { PostUndo(bSuccess); }
	/** End FEditorUndoClient Interface */

private:
	//** Updates the SelectedEmitterScriptGraphs array and then calls Refresh(). Use if the Emitter Script graphs change (e.g. Emitter deleted from System). */
	void RefreshSelectedEmitterScriptGraphs();

	TArray<TWeakObjectPtr<UNiagaraGraph>> GetEditableEmitterScriptGraphs() const;

	// Graphs viewed to gather UNiagaraScriptVariables that are displayed by the Parameter Panel.
	TWeakObjectPtr<UNiagaraGraph> SystemScriptGraph;
	TArray<TWeakObjectPtr<UNiagaraGraph>>  SelectedEmitterScriptGraphs;

	TSharedPtr<FNiagaraSystemViewModel> SystemViewModel;
	UNiagaraSystemSelectionViewModel* OverviewSelectionViewModel;

	static const TArray<TArray<ENiagaraParameterPanelCategory>> DefaultCategoryPaths;
};

class FNiagaraScriptToolkitParameterPanelViewModel : public INiagaraParameterPanelViewModel
{
public:
	/** Construct a ScriptToolkit Parameter Panel View Model from a Script View Model. */
	FNiagaraScriptToolkitParameterPanelViewModel(TSharedPtr<FNiagaraScriptViewModel> InScriptViewModel);

	~FNiagaraScriptToolkitParameterPanelViewModel();

	/** Begin INiagaraParameterPanelViewModel interface. */
	virtual void InitBindings() override;

	virtual void Refresh() const override;

	virtual const UNiagaraScriptVariable* AddParameter(FNiagaraVariable& VariableToAdd, const FNiagaraVariableMetaData& VariableMetaDataToAssign) override;

	virtual void DeleteParameter(const FNiagaraVariable& TargetVariableToRemove, const FNiagaraVariableMetaData& TargetVariableMetaData) override;

	virtual bool GetCanDeleteParameterAndToolTip(const FNiagaraVariable& TargetVariableToRemove, const FNiagaraVariableMetaData& TargetVariableMetaData, FText& OutCanDeleteParameterToolTip) const override;

	virtual void RenameParameter(const FNiagaraVariable& TargetVariableToRename, const FNiagaraVariableMetaData& TargetVariableMetaData, const FText& NewVariableNameText) const override;

	virtual void ChangeParameterScope(const FNiagaraVariable& TargetVariableToModify, const FNiagaraVariableMetaData& TargetVariableMetaData, const ENiagaraParameterScope NewVariableScope) const override;

	virtual bool CanModifyParameter(const FNiagaraVariable& TargetVariableToModify, const FNiagaraVariableMetaData& TargetVariableMetaData) const override;

	virtual bool GetCanRenameParameterAndToolTip(const FNiagaraVariable& TargetVariableToRename, const FNiagaraVariableMetaData& TargetVariableMetaData, TOptional<const FText> NewVariableNameText, FText& OutCanRenameParameterToolTip) const override;

	virtual void HandleActionSelected(const TSharedPtr<FEdGraphSchemaAction>& InAction, ESelectInfo::Type InSelectionType) override;
	
	void HandleGraphSubObjectSelectionChanged(const UObject* Obj);

	virtual FReply HandleActionDragged(const TSharedPtr<FEdGraphSchemaAction>& InAction, const FPointerEvent& MouseEvent) const override;

	virtual const TArray<TArray<ENiagaraParameterPanelCategory>>& GetDefaultCategoryPaths() const override;

	virtual TArray<ENiagaraParameterPanelCategory> GetCategoriesForParameter(const FNiagaraScriptVariableAndViewInfo& ScriptVar) const override;

	virtual TArray<TWeakObjectPtr<UNiagaraGraph>> GetEditableGraphs() const override;

	virtual const TArray<FNiagaraScriptVariableAndViewInfo> GetViewedParameters() override;
	/** End INiagaraParameterPanelViewModel interface. */

	void RenamePin(const UEdGraphPin* TargetPinToRename, const FText& NewNameText) const;
	void ChangePinScope(const UEdGraphPin* TargetPin, const ENiagaraParameterScope NewScope) const;

private:
	TSharedPtr<FNiagaraScriptViewModel> ScriptViewModel;

	FDelegateHandle OnGraphChangedHandle;
	FDelegateHandle OnGraphNeedsRecompileHandle;
	FDelegateHandle ScriptVisualPinHandle;
	FDelegateHandle OnSubObjectSelectionHandle;

	void HandleOnGraphChanged(const struct FEdGraphEditAction& InAction);

	TStaticArray<FScopeIsEnabledAndTooltip, (int32)ENiagaraParameterScope::Num> GetParameterScopesEnabledAndTooltips(const FNiagaraVariable& InVar, const FNiagaraVariableMetaData& InVarMetaData) const;

	TSharedRef<class SWidget> GetScriptParameterVisualWidget(const UEdGraphPin* Pin) const;

	TSharedPtr<FNiagaraObjectSelection> VariableObjectSelection;

	static const TArray<TArray<ENiagaraParameterPanelCategory>> DefaultCategoryPaths;
};
