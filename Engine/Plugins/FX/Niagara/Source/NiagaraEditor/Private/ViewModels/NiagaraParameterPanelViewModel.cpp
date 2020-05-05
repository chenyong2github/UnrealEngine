// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewModels/NiagaraParameterPanelViewModel.h"
#include "NiagaraScriptVariable.h"
#include "NiagaraGraph.h"
#include "NiagaraActions.h"
#include "SGraphActionMenu.h"
#include "SNiagaraParameterNameView.h"
#include "NiagaraParameterNameViewModel.h"
#include "NiagaraStandaloneScriptViewModel.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "NiagaraConstants.h"
#include "EdGraphSchema_Niagara.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"
#include "NiagaraScriptGraphViewModel.h"
#include "NiagaraNodeParameterMapGet.h"
#include "NiagaraNodeParameterMapSet.h"
#include "NiagaraSystemScriptViewModel.h"
#include "ViewModels/NiagaraSystemSelectionViewModel.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraEmitter.h"
#include "NiagaraSystem.h"
#include "NiagaraObjectSelection.h"
#include "EdGraph/EdGraphSchema.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "NiagaraSystemEditorData.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "NiagaraEditorModule.h"
#include "NiagaraTypes.h"

#define LOCTEXT_NAMESPACE "NiagaraParameterPanelViewModel"

const TArray<TArray<ENiagaraParameterPanelCategory>> FNiagaraScriptToolkitParameterPanelViewModel::DefaultCategoryPaths = {
	{ENiagaraParameterPanelCategory::Input}
	, {ENiagaraParameterPanelCategory::Attributes, ENiagaraParameterPanelCategory::User}
	, {ENiagaraParameterPanelCategory::Attributes, ENiagaraParameterPanelCategory::Engine}
	, {ENiagaraParameterPanelCategory::Attributes, ENiagaraParameterPanelCategory::Owner}
	, {ENiagaraParameterPanelCategory::Attributes, ENiagaraParameterPanelCategory::System}
	, {ENiagaraParameterPanelCategory::Attributes, ENiagaraParameterPanelCategory::Emitter}
	, {ENiagaraParameterPanelCategory::Attributes, ENiagaraParameterPanelCategory::Particles}
	, {ENiagaraParameterPanelCategory::Output, ENiagaraParameterPanelCategory::System}
	, {ENiagaraParameterPanelCategory::Output, ENiagaraParameterPanelCategory::Emitter}
	, {ENiagaraParameterPanelCategory::Output, ENiagaraParameterPanelCategory::Particles}
	, {ENiagaraParameterPanelCategory::Output, ENiagaraParameterPanelCategory::ScriptTransient}
	, {ENiagaraParameterPanelCategory::Local}
};

const TArray<TArray<ENiagaraParameterPanelCategory>> FNiagaraSystemToolkitParameterPanelViewModel::DefaultCategoryPaths = {
	{ENiagaraParameterPanelCategory::User}
	, {ENiagaraParameterPanelCategory::Engine}
	, {ENiagaraParameterPanelCategory::Owner}
	, {ENiagaraParameterPanelCategory::System}
	, {ENiagaraParameterPanelCategory::Emitter}
	, {ENiagaraParameterPanelCategory::Particles}
};

TSharedRef<class SWidget> INiagaraParameterPanelViewModel::GetScriptParameterVisualWidget(const FNiagaraScriptVariableAndViewInfo& ScriptVarAndViewInfo) const
{
	TSharedPtr<FNiagaraParameterPanelEntryParameterNameViewModel> ParameterNameViewModel = MakeShared<FNiagaraParameterPanelEntryParameterNameViewModel>(ScriptVarAndViewInfo);
	ParameterNameViewModel->GetOnParameterRenamedDelegate().BindSP(this, &INiagaraParameterPanelViewModel::RenameParameter);
	ParameterNameViewModel->GetOnScopeSelectionChangedDelegate().BindSP(this, &INiagaraParameterPanelViewModel::ChangeParameterScope);
	ParameterNameViewModel->GetOnVerifyParameterRenamedDelegate().BindSP(this, &INiagaraParameterPanelViewModel::GetCanRenameParameterAndToolTip);

	TSharedPtr<SWidget> ScriptParameterVisualWidget = SNew(SNiagaraParameterNameView, ParameterNameViewModel);
	return ScriptParameterVisualWidget->AsShared();
}

TArray<TWeakObjectPtr<UNiagaraGraph>> FNiagaraScriptToolkitParameterPanelViewModel::GetEditableGraphs() const
{
	TArray<TWeakObjectPtr<UNiagaraGraph>> EditableGraphs;
	EditableGraphs.Add(TWeakObjectPtr<UNiagaraGraph>(ScriptViewModel->GetGraphViewModel()->GetGraph()));
	return EditableGraphs;
}

///////////////////////////////////////////////////////////////////////////////
/// System Toolkit Parameter Panel View Model								///
///////////////////////////////////////////////////////////////////////////////

FNiagaraSystemToolkitParameterPanelViewModel::FNiagaraSystemToolkitParameterPanelViewModel(TSharedPtr<FNiagaraSystemViewModel> InSystemViewModel)
{
	SystemViewModel = InSystemViewModel;
	OverviewSelectionViewModel = SystemViewModel->GetSelectionViewModel();
	SystemScriptGraph = SystemViewModel->GetSystemScriptViewModel()->GetGraphViewModel()->GetGraph();
	GEditor->RegisterForUndo(this);
}

FNiagaraSystemToolkitParameterPanelViewModel::~FNiagaraSystemToolkitParameterPanelViewModel()
{
	GEditor->UnregisterForUndo(this);
}

void FNiagaraSystemToolkitParameterPanelViewModel::InitBindings()
{
	if (SystemViewModel->GetEditMode() == ENiagaraSystemViewModelEditMode::SystemAsset)
	{
		SystemViewModel->GetSelectionViewModel()->OnEmitterHandleIdSelectionChanged().AddSP(this, &FNiagaraSystemToolkitParameterPanelViewModel::RefreshSelectedEmitterScriptGraphs);
		SystemViewModel->OnEmitterHandleViewModelsChanged().AddSP(this, &FNiagaraSystemToolkitParameterPanelViewModel::RefreshSelectedEmitterScriptGraphs);
	}
	SystemViewModel->OnSystemCompiled().AddSP(this, &FNiagaraSystemToolkitParameterPanelViewModel::Refresh);
}

void FNiagaraSystemToolkitParameterPanelViewModel::Refresh() const
{
	OnParameterPanelViewModelRefreshed.ExecuteIfBound();
}

const UNiagaraScriptVariable* FNiagaraSystemToolkitParameterPanelViewModel::AddParameter(FNiagaraVariable& VariableToAdd, const FNiagaraVariableMetaData& InVariableMetaDataToAssign)
{
	FScopedTransaction AddParameter(LOCTEXT("AddParameter", "Add Parameter"));
	bool bSystemIsSelected = OverviewSelectionViewModel->GetSystemIsSelected();

	UNiagaraGraph::FAddParameterOptions AddParameterOptions = UNiagaraGraph::FAddParameterOptions();
	AddParameterOptions.NewParameterScopeName = InVariableMetaDataToAssign.GetScopeName();
	AddParameterOptions.NewParameterUsage = InVariableMetaDataToAssign.GetUsage();
	AddParameterOptions.bMakeParameterNameUnique = true;
	AddParameterOptions.bAddedFromSystemEditor = true;

	ENiagaraParameterScope NewScope;
	FNiagaraEditorUtilities::GetVariableMetaDataScope(InVariableMetaDataToAssign, NewScope);
	if (NewScope == ENiagaraParameterScope::User)
	{
		UNiagaraSystem& System = SystemViewModel->GetSystem();
		System.Modify();
		UNiagaraSystemEditorData* SystemEditorData = CastChecked<UNiagaraSystemEditorData>(System.GetEditorData(), ECastCheckedType::NullChecked);
		SystemEditorData->Modify();
		bool bSuccess = FNiagaraEditorUtilities::AddParameter(VariableToAdd, System.GetExposedParameters(), System, &SystemEditorData->GetStackEditorData());
		Refresh();
	}
	else if (NewScope == ENiagaraParameterScope::System)
	{
		UNiagaraGraph* Graph = SystemViewModel->GetSystemScriptViewModel()->GetGraphViewModel()->GetGraph();
		Graph->Modify();
		const UNiagaraScriptVariable* NewScriptVar = Graph->AddParameter(VariableToAdd, AddParameterOptions);
		Refresh();
		return NewScriptVar;
	}
	else
	{
		for (TWeakObjectPtr<UNiagaraGraph> Graph : GetEditableEmitterScriptGraphs())
		{
			if (ensureMsgf(Graph.IsValid(), TEXT("Editable Emitter Script Graph was stale when adding parameter!")))
			{
				Graph->Modify();
				const UNiagaraScriptVariable* NewScriptVar = Graph->AddParameter(VariableToAdd, AddParameterOptions);
				Refresh();
				return NewScriptVar;
			}
		}
	}

	if (bSystemIsSelected)
	{
		//SystemViewModel-> //@todo(ng) handle user params
	}
	
	return nullptr;
}

void FNiagaraSystemToolkitParameterPanelViewModel::DeleteParameter(const FNiagaraVariable& TargetVariableToRemove, const FNiagaraVariableMetaData& TargetVariableMetaData)
{
	FScopedTransaction RemoveParameter(LOCTEXT("RemoveParameter", "Remove Parameter"));

	ENiagaraParameterScope TargetVariableScope = ENiagaraParameterScope::None;
	FNiagaraEditorUtilities::GetVariableMetaDataScope(TargetVariableMetaData, TargetVariableScope);

	if (TargetVariableMetaData.IsInputUsage() && TargetVariableScope == ENiagaraParameterScope::User)
	{
		SystemViewModel->GetSystem().Modify();
		SystemViewModel->GetSystem().GetExposedParameters().RemoveParameter(TargetVariableToRemove);
	}

	
	for (const TWeakObjectPtr<UNiagaraGraph>& GraphWeakPtr : SelectedEmitterScriptGraphs)
	{
		if (GraphWeakPtr.IsValid())
		{
			UNiagaraGraph* Graph = GraphWeakPtr.Get();
			Graph->Modify();
			Graph->RemoveParameter(TargetVariableToRemove);
		}
	}
	Refresh();
}

bool FNiagaraSystemToolkitParameterPanelViewModel::GetCanDeleteParameterAndToolTip(const FNiagaraVariable& TargetVariableToRemove, const FNiagaraVariableMetaData& TargetVariableMetaData, FText& OutCanDeleteParameterToolTip) const
{
	if (TargetVariableMetaData.GetWasCreatedInSystemEditor())
	{
		OutCanDeleteParameterToolTip = LOCTEXT("SystemToolkitParameterPanelViewModel_DeleteParameter_CreatedInSystem", "Delete this Parameter and remove any usages from the System and Emitters.");
		return true;
	}
	OutCanDeleteParameterToolTip = LOCTEXT("SystemToolkitParameterPanelViewModel_DeleteParameter_NotCreatedInSystem", "Cannot Delete this Parameter: Only Parameters created in the System Editor can be deleted from the System Editor.");
	return false;
}

void FNiagaraSystemToolkitParameterPanelViewModel::RenameParameter(const FNiagaraVariable& TargetVariableToRename, const FNiagaraVariableMetaData& TargetVariableMetaData, const FText& NewVariableNameText) const
{
	FScopedTransaction RenameParameter(LOCTEXT("RenameParameter", "Rename Parameter"));

	FName NewVariableName;
	FString TargetVariableNamespaceString;
	
	if (FNiagaraEditorUtilities::GetVariableMetaDataNamespaceString(TargetVariableMetaData, TargetVariableNamespaceString))
	{
		NewVariableName = FName(*(TargetVariableNamespaceString + NewVariableNameText.ToString()));
	}
	else
	{
		NewVariableName = FName(*NewVariableNameText.ToString());
	}

	ENiagaraParameterScope NewScope;
	FNiagaraEditorUtilities::GetVariableMetaDataScope(TargetVariableMetaData, NewScope);
	if (NewScope == ENiagaraParameterScope::User)
	{
		UNiagaraSystem& System = SystemViewModel->GetSystem();
		System.Modify();
		System.GetExposedParameters().RenameParameter(TargetVariableToRename, NewVariableName);
	}
	else if (NewScope == ENiagaraParameterScope::System)
	{
		UNiagaraGraph* Graph = SystemViewModel->GetSystemScriptViewModel()->GetGraphViewModel()->GetGraph();
		Graph->Modify();
		Graph->RenameParameter(TargetVariableToRename, NewVariableName);
	}
	else
	{
		for (TWeakObjectPtr<UNiagaraGraph> Graph : GetEditableEmitterScriptGraphs())
		{
			if (ensureMsgf(Graph.IsValid(), TEXT("Editable Emitter Script Graph was stale when adding parameter!")))
			{
				Graph->Modify();
				Graph->RenameParameter(TargetVariableToRename, NewVariableName);
			}
		}
	}

	Refresh();
}

void FNiagaraSystemToolkitParameterPanelViewModel::ChangeParameterScope(const FNiagaraVariable& TargetVariableToModify, const FNiagaraVariableMetaData& TargetVariableMetaData, const ENiagaraParameterScope NewVariableScope) const
{
	// Parameter scope is not editable for System toolkit.
	return;
}

bool FNiagaraSystemToolkitParameterPanelViewModel::CanModifyParameter(const FNiagaraVariable& TargetVariableToModify, const FNiagaraVariableMetaData& TargetVariableMetaData) const
{
	return TargetVariableMetaData.GetWasCreatedInSystemEditor() == true;
}

bool FNiagaraSystemToolkitParameterPanelViewModel::GetCanRenameParameterAndToolTip(const FNiagaraVariable& TargetVariableToRename, const FNiagaraVariableMetaData& TargetVariableMetaData, TOptional<const FText> NewVariableNameText, FText& OutCanRenameParameterToolTip) const
{
	if (TargetVariableMetaData.GetWasCreatedInSystemEditor())
	{
		if(NewVariableNameText.IsSet() && NewVariableNameText->IsEmptyOrWhitespace())
		{
			OutCanRenameParameterToolTip = LOCTEXT("SystemToolkitParameterPanelViewModel_RenameParameter_NameNone", "Parameter must have a name.");
			return false;
		}

		OutCanRenameParameterToolTip = LOCTEXT("SystemToolkitParameterPanelViewModel_RenameParameter_CreatedInSystem", "Rename this Parameter and all usages in the System and Emitters.");
		return true;
	}
	OutCanRenameParameterToolTip = LOCTEXT("SystemToolkitParameterPanelViewModel_RenameParameter_NotCreatedInSystem", "Cannot rename Parameter: Only Parameters created in the System Editor can be renamed from the System Editor.");
	return false;
}

FReply FNiagaraSystemToolkitParameterPanelViewModel::HandleActionDragged(const TSharedPtr<FEdGraphSchemaAction>& InAction, const FPointerEvent& MouseEvent) const
{
	const FText TooltipFormat = LOCTEXT("Parameters", "Name: {0} \nType: {1}\nScope: {2}\nUsage: {3}");
	const FNiagaraScriptVarAndViewInfoAction* ScriptVarAction = static_cast<FNiagaraScriptVarAndViewInfoAction*>(InAction.Get());
	const FNiagaraScriptVariableAndViewInfo& ScriptVarAndViewInfo = ScriptVarAction->ScriptVariableAndViewInfo;
	//NiagaraParameterPanelSectionID::Type Section = NiagaraParameterPanelSectionID::GetSectionForParameterMetaData(ScriptVarAndViewInfo.MetaData);
	const FNiagaraVariable& Var = ScriptVarAndViewInfo.ScriptVariable;
	const FText Name = FText::FromName(Var.GetName());
	const FText ScopeText = FText::FromName(ScriptVarAndViewInfo.MetaData.GetScopeName());
	const FText UsageText = StaticEnum<ENiagaraScriptParameterUsage>()->GetDisplayNameTextByValue((int64)ScriptVarAndViewInfo.MetaData.GetUsage());
	const FText Tooltip = FText::Format(TooltipFormat, FText::FromName(Var.GetName()), Var.GetType().GetNameText(), ScopeText, UsageText);

	TSharedPtr<FNiagaraParameterAction> ParameterAction = MakeShared<FNiagaraParameterAction>(Var, FText::GetEmpty(), Name, Tooltip, 0, FText(), TSharedPtr<TArray<FName>>(), 0);
	TSharedRef<FNiagaraParameterDragOperation> DragOperation = MakeShared<FNiagaraParameterDragOperation>(ParameterAction);
	DragOperation->CurrentHoverText = InAction->GetMenuDescription();
	DragOperation->SetupDefaults();
	DragOperation->Construct();
	return FReply::Handled().BeginDragDrop(DragOperation);
}

const TArray<TArray<ENiagaraParameterPanelCategory>>& FNiagaraSystemToolkitParameterPanelViewModel::GetDefaultCategoryPaths() const
{
	return FNiagaraSystemToolkitParameterPanelViewModel::DefaultCategoryPaths;
}

TArray<ENiagaraParameterPanelCategory> FNiagaraSystemToolkitParameterPanelViewModel::GetCategoriesForParameter(const FNiagaraScriptVariableAndViewInfo& ScriptVar) const
{
	TArray<ENiagaraParameterPanelCategory> Categories;
	
	if (ScriptVar.MetaData.GetIsStaticSwitch())
	{
		Categories.Add(ENiagaraParameterPanelCategory::StaticSwitch);
		return Categories;
	}

	const FNiagaraParameterScopeInfo* ScopeInfo = FNiagaraEditorModule::FindParameterScopeInfo(ScriptVar.MetaData.GetScopeName());
	if (ScopeInfo != nullptr)
	{
		const ENiagaraParameterScope ParameterScope = ScopeInfo->GetScope();
		switch (ParameterScope) {
		case ENiagaraParameterScope::User:
			Categories.Add(ENiagaraParameterPanelCategory::User);
			return Categories;
		case ENiagaraParameterScope::Engine:
			Categories.Add(ENiagaraParameterPanelCategory::Engine);
			return Categories;
		case ENiagaraParameterScope::Owner:
			Categories.Add(ENiagaraParameterPanelCategory::Owner);
			return Categories;
		case ENiagaraParameterScope::System:
			Categories.Add(ENiagaraParameterPanelCategory::System);
			return Categories;
		case ENiagaraParameterScope::Emitter:
			Categories.Add(ENiagaraParameterPanelCategory::Emitter);
			return Categories;
		case ENiagaraParameterScope::Particles:
			Categories.Add(ENiagaraParameterPanelCategory::Particles);
			return Categories;
		case ENiagaraParameterScope::ScriptTransient:
			Categories.Add(ENiagaraParameterPanelCategory::ScriptTransient);
			return Categories;
		case ENiagaraParameterScope::Custom:
			return Categories;
		default:
			ensureMsgf(false, TEXT("Unexpected parameter scope encountered when getting category for parameter panel entry!"));
		}
	}
	return Categories;
}

TArray<TWeakObjectPtr<UNiagaraGraph>> FNiagaraSystemToolkitParameterPanelViewModel::GetEditableGraphs() const
{
	TArray<TWeakObjectPtr<UNiagaraGraph>> EditableGraphs;
	if (SystemViewModel->GetEditMode() == ENiagaraSystemViewModelEditMode::SystemAsset)
	{
		EditableGraphs.Add(SystemScriptGraph);
		EditableGraphs.Append(SelectedEmitterScriptGraphs);
	}
	else
	{
		EditableGraphs.Add(static_cast<UNiagaraScriptSource*>(SystemViewModel->GetEmitterHandleViewModels()[0]->GetEmitterHandle()->GetInstance()->GraphSource)->NodeGraph);
	}
	return EditableGraphs;
}

const TArray<FNiagaraScriptVariableAndViewInfo> FNiagaraSystemToolkitParameterPanelViewModel::GetViewedParameters()
{
	TArray<FNiagaraScriptVariableAndViewInfo> ViewedParameters;
	for (TWeakObjectPtr<UNiagaraGraph> Graph : GetEditableGraphs())
	{
		if (ensureMsgf(Graph.IsValid(), TEXT("Invalid Graph visited when trying to get viewed parameters for system toolkit parameter panel!")))
		{
			const TMap<FNiagaraVariable, UNiagaraScriptVariable*>& GraphVarToScriptVarMap = Graph->GetAllMetaData();
			for (auto Iter = GraphVarToScriptVarMap.CreateConstIterator(); Iter; ++Iter)
			{
				const FNiagaraVariableMetaData& MetaData = Iter.Value()->Metadata;
				ENiagaraParameterScope MetaDataScope;
				FNiagaraEditorUtilities::GetVariableMetaDataScope(MetaData, MetaDataScope);

				if (MetaDataScope == ENiagaraParameterScope::None || MetaData.GetUsage() == ENiagaraScriptParameterUsage::None)
				{
					ensureMsgf(false, TEXT("Invalid MetaData found for graph variable: %s"), *Iter.Value()->Variable.GetName().ToString());
					continue;
				}
				else if (MetaDataScope == ENiagaraParameterScope::Local)
				{
					// Note, the MetaData.Usage being local is fine to display for the System toolkit.
					continue;
				}
				else if (MetaDataScope == ENiagaraParameterScope::Input) // Do not expose inputs as configurable values
				{
					continue;
				}
				else if (MetaDataScope == ENiagaraParameterScope::ScriptPersistent || MetaDataScope == ENiagaraParameterScope::ScriptTransient || MetaDataScope == ENiagaraParameterScope::Output)
				{
					//@todo(ng) Skip script alias parameters until we can resolve them!
					continue;
				}
				ViewedParameters.Add(FNiagaraScriptVariableAndViewInfo(Iter.Value()->Variable, MetaData));
			}
		}
	}

	TArray<FNiagaraVariable> UserVars;
	SystemViewModel->GetSystem().GetExposedParameters().GetParameters(UserVars);
	for (const FNiagaraVariable& Var : UserVars)
	{
		FNiagaraVariableMetaData MetaData;
		FNiagaraEditorUtilities::GetParameterMetaDataFromName(Var.GetName(), MetaData);
		ViewedParameters.Add(FNiagaraScriptVariableAndViewInfo(Var, MetaData));
	}
	CachedViewedParameters = ViewedParameters;
	return ViewedParameters;
}

void FNiagaraSystemToolkitParameterPanelViewModel::PostUndo(bool bSuccess)
{
	Refresh();
}

void FNiagaraSystemToolkitParameterPanelViewModel::RefreshSelectedEmitterScriptGraphs()
{
	SelectedEmitterScriptGraphs.Reset();

	const TArray<FGuid>& SelectedEmitterHandleIds = OverviewSelectionViewModel->GetSelectedEmitterHandleIds();

	if (SelectedEmitterHandleIds.Num() > 0)
	{
		const TArray<TSharedRef<FNiagaraEmitterHandleViewModel>>& EmitterHandleViewModels = SystemViewModel->GetEmitterHandleViewModels();
		for (const TSharedRef<FNiagaraEmitterHandleViewModel>& EmitterHandleViewModel : EmitterHandleViewModels)
		{
			if (SelectedEmitterHandleIds.Contains(EmitterHandleViewModel->GetId()))
			{
				SelectedEmitterScriptGraphs.Add(static_cast<UNiagaraScriptSource*>(EmitterHandleViewModel->GetEmitterHandle()->GetInstance()->GraphSource)->NodeGraph);
			}
		}
	}

	Refresh();
}

TArray<TWeakObjectPtr<UNiagaraGraph>> FNiagaraSystemToolkitParameterPanelViewModel::GetEditableEmitterScriptGraphs() const
{
	if (SystemViewModel->GetEditMode() == ENiagaraSystemViewModelEditMode::SystemAsset)
	{
		return SelectedEmitterScriptGraphs;
	}
	else
	{
		TArray<TWeakObjectPtr<UNiagaraGraph>> EditableEmitterScriptGraphs;
		EditableEmitterScriptGraphs.Add(static_cast<UNiagaraScriptSource*>(SystemViewModel->GetEmitterHandleViewModels()[0]->GetEmitterHandle()->GetInstance()->GraphSource)->NodeGraph);
		return EditableEmitterScriptGraphs;
	}
}

///////////////////////////////////////////////////////////////////////////////
/// Script Toolkit Parameter Panel View Model								///
///////////////////////////////////////////////////////////////////////////////

FNiagaraScriptToolkitParameterPanelViewModel::FNiagaraScriptToolkitParameterPanelViewModel(TSharedPtr<FNiagaraScriptViewModel> InScriptViewModel)
{
	ScriptViewModel = InScriptViewModel;
	VariableObjectSelection = ScriptViewModel->GetVariableSelection();
}

FNiagaraScriptToolkitParameterPanelViewModel::~FNiagaraScriptToolkitParameterPanelViewModel()
{
	UNiagaraGraph* NiagaraGraph = static_cast<UNiagaraGraph*>(ScriptViewModel->GetGraphViewModel()->GetGraph());
	NiagaraGraph->RemoveOnGraphChangedHandler(OnGraphChangedHandle);
	NiagaraGraph->RemoveOnGraphNeedsRecompileHandler(OnGraphNeedsRecompileHandle);
	NiagaraGraph->RegisterPinVisualWidgetProvider(nullptr);
	NiagaraGraph->OnSubObjectSelectionChanged().Remove(OnSubObjectSelectionHandle);

}

void FNiagaraScriptToolkitParameterPanelViewModel::InitBindings()
{
	UNiagaraGraph* NiagaraGraph = static_cast<UNiagaraGraph*>(ScriptViewModel->GetGraphViewModel()->GetGraph());
	OnGraphChangedHandle = NiagaraGraph->AddOnGraphChangedHandler(
		FOnGraphChanged::FDelegate::CreateRaw(this, &FNiagaraScriptToolkitParameterPanelViewModel::HandleOnGraphChanged));
	OnGraphNeedsRecompileHandle = NiagaraGraph->AddOnGraphNeedsRecompileHandler(
		FOnGraphChanged::FDelegate::CreateRaw(this, &FNiagaraScriptToolkitParameterPanelViewModel::HandleOnGraphChanged));
	ScriptVisualPinHandle = NiagaraGraph->RegisterPinVisualWidgetProvider(UNiagaraGraph::FOnGetPinVisualWidget::CreateSP(this, &FNiagaraScriptToolkitParameterPanelViewModel::GetScriptParameterVisualWidget));
	OnSubObjectSelectionHandle = NiagaraGraph->OnSubObjectSelectionChanged().AddSP(this, &FNiagaraScriptToolkitParameterPanelViewModel::HandleGraphSubObjectSelectionChanged);
}

void FNiagaraScriptToolkitParameterPanelViewModel::Refresh() const
{
	OnParameterPanelViewModelRefreshed.ExecuteIfBound();
}

void FNiagaraScriptToolkitParameterPanelViewModel::HandleGraphSubObjectSelectionChanged(const UObject* Obj)
{
	OnParameterPanelViewModelExternalSelectionChanged.Broadcast(Obj);
}

const UNiagaraScriptVariable* FNiagaraScriptToolkitParameterPanelViewModel::AddParameter(FNiagaraVariable& VariableToAdd, const FNiagaraVariableMetaData& InVariableMetaDataToAssign)
{
	ENiagaraScriptParameterUsage InVariableUsage = InVariableMetaDataToAssign.GetUsage();
	if (InVariableUsage == ENiagaraScriptParameterUsage::InputOutput)
	{
		ensureMsgf(false, TEXT("Unexpected usage encountered when adding parameter through parameter panel view model!"));
	}
	else
	{
		FScopedTransaction AddParameter(LOCTEXT("AddParameterFromParameterPanel", "Add Parameter"));
		UNiagaraGraph::FAddParameterOptions AddParameterOptions = UNiagaraGraph::FAddParameterOptions();

		AddParameterOptions.NewParameterScopeName = InVariableMetaDataToAssign.GetScopeName();
		AddParameterOptions.NewParameterUsage = InVariableMetaDataToAssign.GetUsage();
		AddParameterOptions.bMakeParameterNameUnique = true;

		UNiagaraGraph* Graph = ScriptViewModel->GetGraphViewModel()->GetGraph();
		Graph->Modify();
		const UNiagaraScriptVariable* NewScriptVariable = Graph->AddParameter(VariableToAdd, AddParameterOptions);
		Refresh();
		return NewScriptVariable;
	}
	return nullptr;
}

void FNiagaraScriptToolkitParameterPanelViewModel::DeleteParameter(const FNiagaraVariable& TargetVariableToRemove, const FNiagaraVariableMetaData& TargetVariableMetaData)
{
	FScopedTransaction RemoveParametersWithPins(LOCTEXT("RemoveParametersWithPins", "Remove parameter and referenced pins"));
	UNiagaraGraph* Graph = ScriptViewModel->GetGraphViewModel()->GetGraph();
	Graph->Modify();
	Graph->RemoveParameter(TargetVariableToRemove);
}

bool FNiagaraScriptToolkitParameterPanelViewModel::GetCanDeleteParameterAndToolTip(const FNiagaraVariable& TargetVariableToRemove, const FNiagaraVariableMetaData& TargetVariableMetaData, FText& OutCanDeleteParameterToolTip) const
{
	OutCanDeleteParameterToolTip = LOCTEXT("ScriptToolkitParameterPanelViewModel_DeleteParameter", "Delete this Parameter and remove any usages from the graph.");
	return true;
}

void FNiagaraScriptToolkitParameterPanelViewModel::RenameParameter(const FNiagaraVariable& TargetVariableToRename, const FNiagaraVariableMetaData& TargetVariableMetaData, const FText& NewVariableNameText) const
{
	FScopedTransaction RenameParameterAndReferencedPins(LOCTEXT("RenameParameterAndReferencedPins", "Rename parameter and referenced pins"));

	FName NewVariableName;
	if (TargetVariableMetaData.GetIsUsingLegacyNameString())
	{
		NewVariableName = FName(*NewVariableNameText.ToString());
	}
	else
	{
		FString TargetVariableNamespaceString;
		checkf(FNiagaraEditorUtilities::GetVariableMetaDataNamespaceString(TargetVariableMetaData, TargetVariableNamespaceString), TEXT("Tried to get namespace string for parameter using legacy name string edit mode!"));
		NewVariableName = FName(*(TargetVariableNamespaceString + NewVariableNameText.ToString()));
	}

	
	UNiagaraGraph* Graph = ScriptViewModel->GetGraphViewModel()->GetGraph();
	Graph->Modify();

	Graph->RenameParameter(TargetVariableToRename, NewVariableName, false, TargetVariableMetaData.GetScopeName());
}

void FNiagaraScriptToolkitParameterPanelViewModel::ChangeParameterScope(const FNiagaraVariable& TargetVariableToModify, const FNiagaraVariableMetaData& TargetVariableMetaData, const ENiagaraParameterScope NewVariableScope) const
{
	if (!FNiagaraEditorUtilities::IsScopeUserAssignable(TargetVariableMetaData.GetScopeName()))
	{
		FNiagaraEditorUtilities::WarnWithToastAndLog(FText::Format(LOCTEXT("ScopeNotUserAssignable","The selected scope {0} cannot be assigned by a user"), FText::FromName(TargetVariableMetaData.GetScopeName())));
		return;
	}

	if (ensureMsgf(TargetVariableMetaData.GetUsage() != ENiagaraScriptParameterUsage::Output, TEXT("Tried to change scope of output parameter!")))
	{
		FScopedTransaction ChangeParameterScopeAndReferencedPins(LOCTEXT("ChangeParameterScopeAndReferencedPins", "Change parameter scope, Rename parameter and referenced pins"));

		const FName TargetScopeName = FNiagaraEditorUtilities::GetScopeNameForParameterScope(NewVariableScope);
		FString NewNamespaceString;
		if (ensureMsgf(FNiagaraEditorUtilities::GetVariableMetaDataNamespaceStringForNewScope(TargetVariableMetaData, TargetScopeName, NewNamespaceString), TEXT("Tried to change scope of parameter with override name mode enabled!")))
		{
			FString NewNameString = NewNamespaceString;
			NewNamespaceString.Append(FNiagaraEditorUtilities::GetNamespacelessVariableNameString(TargetVariableToModify.GetName()));
			const FName NewVariableHLSLTokenName = FName(*NewNamespaceString);

			UNiagaraGraph* Graph = ScriptViewModel->GetGraphViewModel()->GetGraph();
			Graph->Modify();
			Graph->RenameParameter(TargetVariableToModify, NewVariableHLSLTokenName, false, TargetScopeName);
		}
	}
}

bool FNiagaraScriptToolkitParameterPanelViewModel::CanModifyParameter(const FNiagaraVariable& TargetVariableToModify, const FNiagaraVariableMetaData& TargetVariableMetaData) const
{
	//@todo return false for parameter library entries
	return true;
}

bool FNiagaraScriptToolkitParameterPanelViewModel::GetCanRenameParameterAndToolTip(const FNiagaraVariable& TargetVariableToRename, const FNiagaraVariableMetaData& TargetVariableMetaData, TOptional<const FText> NewVariableNameText, FText& OutCanRenameParameterToolTip) const
{
	if (TargetVariableMetaData.GetIsStaticSwitch())
	{
		OutCanRenameParameterToolTip = LOCTEXT("ScriptToolkitParameterPanelViewModel_RenameParameter_StaticSwitch", "Cannot rename static switches through the parameter panel. Rename the static switch through the associated node's selected details panel.");
		return false;
	}

	if (NewVariableNameText.IsSet())
	{
		if (NewVariableNameText->IsEmptyOrWhitespace())
		{
			OutCanRenameParameterToolTip = LOCTEXT("ScriptToolkitParameterPanelViewModel_RenameParameter_NameNone", "Parameter must have a name.");
			return false;
		}

		FName NewName = FName(*NewVariableNameText->ToString());
		TArray<FName> AliasScopeNames;
		auto FindParameterNameAlias = [this](const TArray<FName>& AliasScopeNames, const FName& NewName)->const FNiagaraScriptVariableAndViewInfo* /* OutMatchedParameter*/ {
			for (const FNiagaraScriptVariableAndViewInfo& ViewedParameter : CachedViewedParameters)
			{
				FName ParameterName;
				if (AliasScopeNames.Contains(ViewedParameter.MetaData.GetScopeName()))
				{
					if (ViewedParameter.MetaData.GetParameterName(ParameterName))
					{
						if (NewName == ParameterName)
						{
							return &ViewedParameter;
						}
					}
				}
			}
			return nullptr;
		};

		// Prevent name values that would alias an existing parameter with relevant usage
		FText GenericNameAliasToolTip = LOCTEXT("ScriptToolkitParameterPanelViewModel_RenameParameter_Alias", "Cannot rename Parameter: A Parameter already exists with name {0} but different type.");
		FName ExistingName;
		ensureMsgf(TargetVariableMetaData.GetParameterName(ExistingName), TEXT("Failed to get namespaceless parameter name from metadata on rename verify!"));
		if (NewName != ExistingName)
		{
			if ((TargetVariableMetaData.GetUsage() == ENiagaraScriptParameterUsage::Input && TargetVariableMetaData.GetScopeName() == FNiagaraConstants::InputScopeName) || TargetVariableMetaData.GetUsage() == ENiagaraScriptParameterUsage::Output)
			{
				// Inputs may not name alias outputs and vice versa
				AliasScopeNames.Add(FNiagaraConstants::InputScopeName);
				AliasScopeNames.Add(FNiagaraConstants::OutputScopeName);
				AliasScopeNames.Add(FNiagaraConstants::UniqueOutputScopeName);
				const FNiagaraScriptVariableAndViewInfo* NameAliasedVariableInfo = FindParameterNameAlias(AliasScopeNames, NewName);
				if (NameAliasedVariableInfo != nullptr)
				{
					ENiagaraScriptParameterUsage TargetVarUsage = TargetVariableMetaData.GetUsage();
					ENiagaraScriptParameterUsage AliasVarUsage = NameAliasedVariableInfo->MetaData.GetUsage();
					if (TargetVarUsage == AliasVarUsage)
					{
						OutCanRenameParameterToolTip = FText::Format(GenericNameAliasToolTip, NewVariableNameText.GetValue());
					}
					else if (TargetVarUsage == ENiagaraScriptParameterUsage::Input && AliasVarUsage == ENiagaraScriptParameterUsage::Output)
					{
						OutCanRenameParameterToolTip = FText::Format(LOCTEXT("ScriptToolkitParameterPanelViewModel_RenameParameter_InOutAlias", "Cannot rename Input Parameter: An Output Parameter already exists with name {0}."), NewVariableNameText.GetValue());
					}
					else if (TargetVarUsage == ENiagaraScriptParameterUsage::Output && AliasVarUsage == ENiagaraScriptParameterUsage::Input)
					{
						OutCanRenameParameterToolTip = FText::Format(LOCTEXT("ScriptToolkitParameterPanelViewModel_RenameParameter_OutInAlias", "Cannot rename Output Parameter: An Input Parameter already exists with name {0}."), NewVariableNameText.GetValue());
					}
					else
					{
						ensureMsgf(false, TEXT("Unexpected usages encountered when checking name aliases for input/output rename!"));
						OutCanRenameParameterToolTip = FText::Format(GenericNameAliasToolTip, NewVariableNameText.GetValue());
					}
					return false;
				}
			}
			else
			{
				AliasScopeNames.Add(TargetVariableMetaData.GetScopeName());
				const FNiagaraScriptVariableAndViewInfo* NameAliasedVariableInfo = FindParameterNameAlias(AliasScopeNames, NewName);
				if (NameAliasedVariableInfo != nullptr)
				{
					if (TargetVariableToRename.GetType() != NameAliasedVariableInfo->ScriptVariable.GetType())
					{ 
						OutCanRenameParameterToolTip = FText::Format(GenericNameAliasToolTip, NewVariableNameText.GetValue());
						return false;
					}
					return true;
				}
			}
		}
	}

	OutCanRenameParameterToolTip = LOCTEXT("ScriptToolkitParameterPanelViewModel_RenameParameter", "Rename this Parameter and all usages in the graph.");
	return true;
}

void FNiagaraScriptToolkitParameterPanelViewModel::HandleActionSelected(const TSharedPtr<FEdGraphSchemaAction>& InAction, ESelectInfo::Type InSelectionType)
{
	const FNiagaraScriptVarAndViewInfoAction* Action = static_cast<FNiagaraScriptVarAndViewInfoAction*>(InAction.Get());
	UNiagaraScriptVariable** ScriptVarPtr = ScriptViewModel->GetGraphViewModel()->GetGraph()->GetAllMetaData().Find(Action->ScriptVariableAndViewInfo.ScriptVariable);
	if (ensureMsgf(ScriptVarPtr != nullptr, TEXT("Failed to get UNiagaraScriptVariable from selected action!")))
	{
		VariableObjectSelection->SetSelectedObject(*ScriptVarPtr);
	}
}

FReply FNiagaraScriptToolkitParameterPanelViewModel::HandleActionDragged(const TSharedPtr<FEdGraphSchemaAction>& InAction, const FPointerEvent& MouseEvent) const
{
	TSharedRef<FNiagaraParameterGraphDragOperation> DragOperation = FNiagaraParameterGraphDragOperation::New(InAction); //@todo(ng) do not drag drop static switches
	DragOperation->SetAltDrag(MouseEvent.IsAltDown());
	DragOperation->SetCtrlDrag(MouseEvent.IsLeftControlDown() || MouseEvent.IsRightControlDown());
	return FReply::Handled().BeginDragDrop(DragOperation);
}

const TArray<TArray<ENiagaraParameterPanelCategory>>& FNiagaraScriptToolkitParameterPanelViewModel::GetDefaultCategoryPaths() const
{
	return FNiagaraScriptToolkitParameterPanelViewModel::DefaultCategoryPaths;
}

TArray<ENiagaraParameterPanelCategory> FNiagaraScriptToolkitParameterPanelViewModel::GetCategoriesForParameter(const FNiagaraScriptVariableAndViewInfo& ScriptVar) const
{
	TArray<ENiagaraParameterPanelCategory> Categories;

	if (ScriptVar.MetaData.GetIsStaticSwitch())
	{
		Categories.Add(ENiagaraParameterPanelCategory::Input);
		Categories.Add(ENiagaraParameterPanelCategory::StaticSwitch);
		return Categories;
	}

	const ENiagaraScriptParameterUsage& Usage = ScriptVar.MetaData.GetUsage();
	if (Usage == ENiagaraScriptParameterUsage::Input || Usage == ENiagaraScriptParameterUsage::Output)
	{
		const FNiagaraParameterScopeInfo* ScopeInfo = FNiagaraEditorModule::FindParameterScopeInfo(ScriptVar.MetaData.GetScopeName());
		if (ScopeInfo != nullptr)
		{
			if (Usage == ENiagaraScriptParameterUsage::Input)
			{
				if (ScopeInfo->GetNamespaceString() == PARAM_MAP_MODULE_STR)
				{
					Categories.Add(ENiagaraParameterPanelCategory::Input);
					return Categories;
				}
				Categories.Add(ENiagaraParameterPanelCategory::Attributes);
			}
			else
			{
				Categories.Add(ENiagaraParameterPanelCategory::Output);
			}
			
			const ENiagaraParameterScope& ParameterScope = ScopeInfo->GetScope();
			switch (ParameterScope) {
			case ENiagaraParameterScope::User:
				Categories.Add(ENiagaraParameterPanelCategory::User);
				return Categories;
			case ENiagaraParameterScope::Engine:
				Categories.Add(ENiagaraParameterPanelCategory::Engine);
				return Categories;
			case ENiagaraParameterScope::Owner:
				Categories.Add(ENiagaraParameterPanelCategory::Owner);
				return Categories;
			case ENiagaraParameterScope::System:
				Categories.Add(ENiagaraParameterPanelCategory::System);
				return Categories;
			case ENiagaraParameterScope::Emitter:
				Categories.Add(ENiagaraParameterPanelCategory::Emitter);
				return Categories;
			case ENiagaraParameterScope::Particles:
				Categories.Add(ENiagaraParameterPanelCategory::Particles);
				return Categories;
			case ENiagaraParameterScope::ScriptTransient:
				Categories.Add(ENiagaraParameterPanelCategory::ScriptTransient);
				return Categories;
			case ENiagaraParameterScope::Output:
				Categories.Add(ENiagaraParameterPanelCategory::Output);
				return Categories;
			default:
				ensureMsgf(false, TEXT("Unexpected ENiagaraParameterScope encountered when getting categories for parameter panel entry!"));
				return Categories;
			}
		}
		else
		{
			ensureMsgf(false, TEXT("Unregistered ScopeName encountered when getting categories for parameter panel entry! Name: %s"), *ScriptVar.MetaData.GetScopeName().ToString());
			return Categories;
		}
	}
	else if (Usage == ENiagaraScriptParameterUsage::Local)
	{
		Categories.Add(ENiagaraParameterPanelCategory::Local);
		return Categories;
	}
	else if (Usage == ENiagaraScriptParameterUsage::InputOutput)
	{
		ensureMsgf(false, TEXT("Encountered InputOutput usage entry when getting categories for parameter panel entry! InputOuput usage should be split to separate Input and Output usage entries!"));
		return Categories;
	}
	else
	{
		ensureMsgf(false, TEXT("Illegal ENiagaraScriptParameterUsage encountered when getting categories for parameter panel entry!"));
		return Categories;
	}

	return Categories;
}

TSharedRef<SWidget> FNiagaraScriptToolkitParameterPanelViewModel::GetScriptParameterVisualWidget(const UEdGraphPin* Pin) const
{
	const UEdGraphSchema_Niagara* Schema = GetDefault<UEdGraphSchema_Niagara>();
	const FNiagaraVariable PinVar = Schema->PinToNiagaraVariable(Pin);

	bool bRenameImmediate = false;
	UNiagaraNode* OwnerNode = Cast<UNiagaraNode>(Pin->GetOwningNode());
	if (OwnerNode && OwnerNode->IsPinNameEditableUponCreation(Pin))
	{
		bRenameImmediate = true;
	}
	else if (FNiagaraConstants::IsNiagaraConstant(PinVar))
	{
		bRenameImmediate = false;
	}

	const FNiagaraScriptVariableAndViewInfo* ScriptVarAndViewInfo = CachedViewedParameters.FindByPredicate([PinVar](const FNiagaraScriptVariableAndViewInfo& Entry) {return Entry.ScriptVariable == PinVar; });
	if (ScriptVarAndViewInfo != nullptr)
	{
		TSharedPtr<FNiagaraGraphPinParameterNameViewModel> ParameterNameViewModel = MakeShared<FNiagaraGraphPinParameterNameViewModel>(const_cast<UEdGraphPin*>(Pin), *ScriptVarAndViewInfo, this);
		TSharedPtr<SNiagaraParameterNameView> ScriptParameterVisualWidget = SNew(SNiagaraParameterNameView, ParameterNameViewModel);
		ScriptParameterVisualWidget->SetPendingRename(bRenameImmediate);
		return ScriptParameterVisualWidget->AsShared();
	}
	else
	{
		// Failed to find the parameter name in the cache, try to find the variable in the graph script variables and generate view info.
		const UNiagaraScriptVariable* const* ScriptVarPtr = ScriptViewModel->GetGraphViewModel()->GetGraph()->GetAllMetaData().Find(PinVar);
		if (ScriptVarPtr != nullptr)
		{
			const UNiagaraScriptVariable* ScriptVar = *ScriptVarPtr;
			const TStaticArray<FScopeIsEnabledAndTooltip, (int32)ENiagaraParameterScope::Num> PerScopeInfo = GetParameterScopesEnabledAndTooltips(ScriptVar->Variable, ScriptVar->Metadata);
			FNiagaraScriptVariableAndViewInfo NewScriptVarAndViewInfo = FNiagaraScriptVariableAndViewInfo(ScriptVar->Variable, ScriptVar->Metadata, PerScopeInfo);

			TSharedPtr<FNiagaraGraphPinParameterNameViewModel> ParameterNameViewModel = MakeShared<FNiagaraGraphPinParameterNameViewModel>(const_cast<UEdGraphPin*>(Pin), NewScriptVarAndViewInfo, this);
			TSharedPtr<SNiagaraParameterNameView> ScriptParameterVisualWidget = SNew(SNiagaraParameterNameView, ParameterNameViewModel);
			ScriptParameterVisualWidget->SetPendingRename(bRenameImmediate);
			return ScriptParameterVisualWidget->AsShared();
		}
	}

	// Cannot resolve the parameter from the pin, put an error widget in.
	FNiagaraVariable StandInParameter = Schema->PinToNiagaraVariable(Pin, false);
	FNiagaraVariableMetaData StandInMetaData;
	FNiagaraEditorUtilities::GetParameterMetaDataFromName(StandInParameter.GetName(), StandInMetaData);
	StandInMetaData.SetUsage(ENiagaraScriptParameterUsage::Local);
	
	FNiagaraScriptVariableAndViewInfo StandInScriptVarAndViewInfo = FNiagaraScriptVariableAndViewInfo(StandInParameter, StandInMetaData);
	TSharedPtr<FNiagaraGraphPinParameterNameViewModel> ParameterNameViewModel = MakeShared<FNiagaraGraphPinParameterNameViewModel>(const_cast<UEdGraphPin*>(Pin), StandInScriptVarAndViewInfo, this);
	TSharedPtr<SNiagaraParameterNameView> ScriptParameterVisualWidget = SNew(SNiagaraParameterNameView, ParameterNameViewModel);
	ScriptParameterVisualWidget->SetPendingRename(bRenameImmediate);
	return ScriptParameterVisualWidget->AsShared();
}

TStaticArray<FScopeIsEnabledAndTooltip, (int32)ENiagaraParameterScope::Num> FNiagaraScriptToolkitParameterPanelViewModel::GetParameterScopesEnabledAndTooltips(const FNiagaraVariable& InVar, const FNiagaraVariableMetaData& InVarMetaData) const
{
	TStaticArray<FScopeIsEnabledAndTooltip, (int32)ENiagaraParameterScope::Num> PerScopeInfo;
	UEnum* ParameterScopeEnum = FNiagaraTypeDefinition::GetParameterScopeEnum();

	// Add defaulted entries for every possible enum value
	TMap<ENiagaraParameterScope, TPair<bool, const FText>> OutEntriesAndInfo;
	for (int32 i = 0; i < ParameterScopeEnum->NumEnums() - 1; i++)
	{
		if (ParameterScopeEnum->HasMetaData(TEXT("Hidden"), i) == false)
		{
			PerScopeInfo[i] = FScopeIsEnabledAndTooltip(true, FText()); //@todo(ng) put the scope in the tooltip
		}
	}

	// Prevent setting enum values that would alias an existing parameter
	FName InName;
	if (InVarMetaData.GetParameterName(InName))
	{
		/*for (const FNiagaraScriptVariableAndViewInfo& ViewedParameter : CachedViewedParameters)
		{
			FName CachedParameterName;
			if (ViewedParameter.MetaData.GetParameterName(CachedParameterName))
			{
				if (InName == CachedParameterName)
				{
					ENiagaraParameterScope ViewedParameterScope;
					FNiagaraEditorUtilities::GetVariableMetaDataScope(ViewedParameter.MetaData, ViewedParameterScope);

					PerScopeInfo[(int32)ViewedParameterScope].bEnabled = false;
					PerScopeInfo[(int32)ViewedParameterScope].Tooltip = FText::Format(LOCTEXT("NiagaraInvalidScopeSelectionNameAlias", "Cannot select scope '{0}': Parameter with same name already has this scope.")
						, FText::FromName(FNiagaraEditorUtilities::GetScopeNameForParameterScope(ViewedParameterScope)));
				}
			}
		}*/

		// Prevent making Module namespace parameters in function and dynamic input scripts
		if (ScriptViewModel->GetStandaloneScript()->GetUsage() != ENiagaraScriptUsage::Module)
		{
			PerScopeInfo[(int32)ENiagaraParameterScope::Input].bEnabled = false;
			PerScopeInfo[(int32)ENiagaraParameterScope::Input].Tooltip = FText::Format(LOCTEXT("NiagaraInvalidScopeSelectionModule", "Cannot select scope '{0}': Scope is only valid in Module Scripts.")
				, FText::FromName(FNiagaraEditorUtilities::GetScopeNameForParameterScope(ENiagaraParameterScope::Input)));
		}

		const TArray<ENiagaraParameterScope> InvalidParameterScopes = ScriptViewModel->GetStandaloneScript()->GetUnsupportedParameterScopes();
		for (ENiagaraParameterScope InvalidScope : InvalidParameterScopes)
		{
			PerScopeInfo[(int32)InvalidScope].bEnabled = false;
			PerScopeInfo[(int32)InvalidScope].Tooltip = FText::Format(LOCTEXT("NiagaraInvalidScopeSelectionUsageBitmask", "Cannot select scope '{0}': Script Usage flags do not support a usage with this scope.")
				, FText::FromName(FNiagaraEditorUtilities::GetScopeNameForParameterScope(InvalidScope)));
		}
	}
	else
	{
		// Failed to get parameter name as an override name is set, allow any scope.
	}

	return PerScopeInfo;
}

const TArray<FNiagaraScriptVariableAndViewInfo> FNiagaraScriptToolkitParameterPanelViewModel::GetViewedParameters()
{
	TArray<FNiagaraScriptVariableAndViewInfo> ViewedParameters; //@todo(ng) cached viewed parameters are too behind for GetParameterScopesEnabledAndTooltips, refactor
	UNiagaraGraph* ViewedGraph = ScriptViewModel->GetGraphViewModel()->GetGraph();
	if (ensureMsgf(ViewedGraph != nullptr, TEXT("Invalid Graph found when trying to get viewed parameters for script toolkit parameter panel!")))
	{
		const TMap<FNiagaraVariable, UNiagaraScriptVariable*>& GraphVarToScriptVarMap = ViewedGraph->GetAllMetaData();
		for (auto Iter = GraphVarToScriptVarMap.CreateConstIterator(); Iter; ++Iter)
		{
			const FNiagaraVariable& Variable = Iter.Value()->Variable;

			if (Variable.GetName() == FNiagaraConstants::InputPinName || Variable.GetName() == FNiagaraConstants::OutputPinName)
			{
				//@todo Pins leaked into variable maps at some point, need to clean.
				continue;
			}

			const FNiagaraVariableMetaData& MetaData = Iter.Value()->Metadata;
			ENiagaraParameterScope MetaDataScope = ENiagaraParameterScope::None;
			if (MetaData.GetIsStaticSwitch() == false)
			{
				FNiagaraEditorUtilities::GetVariableMetaDataScope(MetaData, MetaDataScope);
				if (MetaDataScope == ENiagaraParameterScope::None || MetaData.GetUsage() == ENiagaraScriptParameterUsage::None)
				{
					// Parameters that are not static switches must have a scope and usage set.
					ensureMsgf(false, TEXT("Invalid MetaData found for graph variable: %s"), *Iter.Value()->Variable.GetName().ToString());
					continue;
				}
			}

			if (MetaData.GetUsage() == ENiagaraScriptParameterUsage::InputOutput)
			{
				// Need two stack entries to represent this script parameter as both an input and output.
				FNiagaraVariableMetaData InputVariableMetaData = MetaData;
				FNiagaraVariableMetaData OutputVariableMetaData = MetaData;
				InputVariableMetaData.SetUsage( ENiagaraScriptParameterUsage::Input);
				OutputVariableMetaData.SetUsage(ENiagaraScriptParameterUsage::Output);

				const TStaticArray<FScopeIsEnabledAndTooltip, (int32)ENiagaraParameterScope::Num> PerScopeInfo = GetParameterScopesEnabledAndTooltips(Variable, MetaData);
				ViewedParameters.Add(FNiagaraScriptVariableAndViewInfo(Variable, InputVariableMetaData, PerScopeInfo));
				ViewedParameters.Add(FNiagaraScriptVariableAndViewInfo(Variable, OutputVariableMetaData));
			}
			else if(MetaData.GetUsage() == ENiagaraScriptParameterUsage::Input || MetaData.GetUsage() == ENiagaraScriptParameterUsage::InitialValueInput)
			{
				const TStaticArray<FScopeIsEnabledAndTooltip, (int32)ENiagaraParameterScope::Num> PerScopeInfo = GetParameterScopesEnabledAndTooltips(Variable, MetaData);
				ViewedParameters.Add(FNiagaraScriptVariableAndViewInfo(Variable, MetaData, PerScopeInfo));
			}
			else
			{
				ViewedParameters.Add(FNiagaraScriptVariableAndViewInfo(Variable, MetaData));
			}
			
		}
	}
	CachedViewedParameters = ViewedParameters;
	return ViewedParameters;
}

void FNiagaraScriptToolkitParameterPanelViewModel::RenamePin(const UEdGraphPin* TargetPinToRename, const FText& NewNameText) const
{
	UNiagaraScriptVariable* ScriptVarToRename = ScriptViewModel->GetGraphViewModel()->GetGraph()->GetScriptVariable(TargetPinToRename->GetFName());
	if (ensureMsgf(ScriptVarToRename != nullptr, TEXT("Failed to find script variable with same name as pin while renaming pin!")))
	{
		RenameParameter(ScriptVarToRename->Variable, ScriptVarToRename->Metadata, NewNameText);
	}
}

void FNiagaraScriptToolkitParameterPanelViewModel::ChangePinScope(const UEdGraphPin* TargetPin, const ENiagaraParameterScope NewScope) const
{
	if (ensureMsgf(TargetPin->Direction != EEdGraphPinDirection::EGPD_Input, TEXT("Tried to edit scope of input pin, this should not happen!")))
	{
		UNiagaraScriptVariable* ScriptVarToRename = ScriptViewModel->GetGraphViewModel()->GetGraph()->GetScriptVariable(TargetPin->GetFName());
		if (ensureMsgf(ScriptVarToRename != nullptr, TEXT("Failed to find script variable with same name as pin while changing pin scope!")))
		{
			ChangeParameterScope(ScriptVarToRename->Variable, ScriptVarToRename->Metadata, NewScope);
		}
	}
}

void FNiagaraScriptToolkitParameterPanelViewModel::HandleOnGraphChanged(const struct FEdGraphEditAction& InAction)
{
	Refresh();
}

#undef LOCTEXT_NAMESPACE // "FNiagaraParameterPanelViewModel"
