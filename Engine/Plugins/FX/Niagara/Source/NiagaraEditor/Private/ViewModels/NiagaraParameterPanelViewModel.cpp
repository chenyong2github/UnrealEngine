// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraParameterPanelViewModel.h"
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

#define LOCTEXT_NAMESPACE "NiagaraParameterPanelViewModel"

FText NiagaraParameterPanelSectionID::OnGetSectionTitle(const NiagaraParameterPanelSectionID::Type InSection)
{
	/* Setup an appropriate name for the section for this node */
	FText SectionTitle;
	switch (InSection)
	{
	case NiagaraParameterPanelSectionID::ENGINE:
		SectionTitle = LOCTEXT("EngineParamSection", "Engine");
		break;
	case NiagaraParameterPanelSectionID::USER:
		SectionTitle = LOCTEXT("UserParamSection", "User");
		break;
	case NiagaraParameterPanelSectionID::SYSTEM:
		SectionTitle = LOCTEXT("SystemParamSection", "System");
		break;
	case NiagaraParameterPanelSectionID::EMITTER:
		SectionTitle = LOCTEXT("EmitterParamSection", "Emitter");
		break;
	case NiagaraParameterPanelSectionID::PARTICLES:
		SectionTitle = LOCTEXT("ParticlesParamSection", "Particles");
		break;
	case NiagaraParameterPanelSectionID::INPUTS:
		SectionTitle = LOCTEXT("InputsParamSection", "Inputs");
		break;
	case NiagaraParameterPanelSectionID::REFERENCES:
		SectionTitle = LOCTEXT("ReferencesParamSection", "References");
		break;
	case NiagaraParameterPanelSectionID::OUTPUTS:
		SectionTitle = LOCTEXT("OutputsParamSection", "Outputs");
		break;
	case NiagaraParameterPanelSectionID::LOCALS:
		SectionTitle = LOCTEXT("LocalsParamSection", "Locals");
		break;
	case NiagaraParameterPanelSectionID::INITIALVALUES:
		SectionTitle = LOCTEXT("InitialValuesParamSection", "Initial Values");
		break;
// 	case NiagaraParameterPanelSectionID::PARAMETERCOLLECTION: //@TODO implement parameter collection handling
// 		SectionTitle = NSLOCTEXT("GraphActionNode", "ParameterCollection", "Parameter Collection");
// 		break;
	case NiagaraParameterPanelSectionID::NONE:
	default:
		SectionTitle = FText::GetEmpty();
		break;
	}

	return SectionTitle;
}

const NiagaraParameterPanelSectionID::Type NiagaraParameterPanelSectionID::GetSectionForScope(ENiagaraParameterScope InScope)
{
	switch (InScope) {
	case ENiagaraParameterScope::Engine:
		return NiagaraParameterPanelSectionID::ENGINE;
	case ENiagaraParameterScope::Owner:
		return NiagaraParameterPanelSectionID::OWNER;
	case ENiagaraParameterScope::User:
		return NiagaraParameterPanelSectionID::USER;
	case ENiagaraParameterScope::System:
		return NiagaraParameterPanelSectionID::SYSTEM;
	case ENiagaraParameterScope::Emitter:
		return NiagaraParameterPanelSectionID::EMITTER;
	case ENiagaraParameterScope::Particles:
		return NiagaraParameterPanelSectionID::PARTICLES;
	case ENiagaraParameterScope::ScriptPersistent:
	case ENiagaraParameterScope::ScriptTransient:
		// This is a potential situation if a script alias param has not had its scope cached by compiling.
		return NiagaraParameterPanelSectionID::NONE;
	default:
		ensureMsgf(false, TEXT("Failed to find matching section ID for script parameter scope!"));
	};
	return NiagaraParameterPanelSectionID::NONE;
}

const NiagaraParameterPanelSectionID::Type NiagaraParameterPanelSectionID::GetSectionForParameterMetaData(const FNiagaraVariableMetaData& MetaData)
{
	if (MetaData.Usage == ENiagaraScriptParameterUsage::Local)
	{
		return NiagaraParameterPanelSectionID::LOCALS;
	}
	else if (MetaData.Usage == ENiagaraScriptParameterUsage::InitialValueInput)
	{
		return NiagaraParameterPanelSectionID::INITIALVALUES;
	}
	else if (MetaData.Usage == ENiagaraScriptParameterUsage::Input)
	{
		if (MetaData.Scope == ENiagaraParameterScope::Input)
		{
			return NiagaraParameterPanelSectionID::INPUTS;
		}
		else
		{
			return NiagaraParameterPanelSectionID::REFERENCES;
		}
	}
	else if (MetaData.bIsStaticSwitch)
	{
		return NiagaraParameterPanelSectionID::INPUTS;
	}
	else if (MetaData.Usage == ENiagaraScriptParameterUsage::Output)
	{
		return NiagaraParameterPanelSectionID::OUTPUTS;
	}
	else if (MetaData.Usage == ENiagaraScriptParameterUsage::InputOutput)
	{
		ensureMsgf(false, TEXT("Encountered an InputOutput parameter usage when getting section ID for parameter panel!"));
		return NiagaraParameterPanelSectionID::REFERENCES;
	}
	else
	{
		ensureMsgf(false, TEXT("Failed to find matching section ID for script parameter usage!"));
	}
	return NiagaraParameterPanelSectionID::LOCALS;
}

ENiagaraParameterScope NiagaraParameterPanelSectionID::GetScopeForNewParametersInSection(NiagaraParameterPanelSectionID::Type InSection)
{
	switch (InSection) {
	case NiagaraParameterPanelSectionID::ENGINE:
		return ENiagaraParameterScope::Engine;
	case NiagaraParameterPanelSectionID::OWNER:
		return ENiagaraParameterScope::Owner;
	case NiagaraParameterPanelSectionID::USER:
		return ENiagaraParameterScope::User;
	case NiagaraParameterPanelSectionID::SYSTEM:
		return ENiagaraParameterScope::System;
	case NiagaraParameterPanelSectionID::EMITTER:
		return ENiagaraParameterScope::Emitter;
	case NiagaraParameterPanelSectionID::PARTICLES:
		return ENiagaraParameterScope::Particles;
	case NiagaraParameterPanelSectionID::LOCALS:
		return ENiagaraParameterScope::Local;
	case NiagaraParameterPanelSectionID::INITIALVALUES:
		return ENiagaraParameterScope::Particles;

	// Default to Particles scope if coming from section IDs that are not directly associated with scope.
	case NiagaraParameterPanelSectionID::INPUTS:
	case NiagaraParameterPanelSectionID::REFERENCES:
	case NiagaraParameterPanelSectionID::OUTPUTS:
		return ENiagaraParameterScope::Particles;

	case NiagaraParameterPanelSectionID::NONE:
		ensureMsgf(false, TEXT("Encountered invalid parameter panel section ID NONE when getting scope from section!"));
		break;
	};
	ensureMsgf(false, TEXT("Did not encounter a known section ID when getting scope from section!"));
	return ENiagaraParameterScope::Particles;
}

ENiagaraScriptParameterUsage NiagaraParameterPanelSectionID::GetUsageForNewParametersInSection(NiagaraParameterPanelSectionID::Type InSection)
{
	switch (InSection) {
	case NiagaraParameterPanelSectionID::INPUTS:
	case NiagaraParameterPanelSectionID::REFERENCES:
		return ENiagaraScriptParameterUsage::Input;
	case NiagaraParameterPanelSectionID::OUTPUTS:
		return ENiagaraScriptParameterUsage::Output;
	case NiagaraParameterPanelSectionID::LOCALS:
		return ENiagaraScriptParameterUsage::Local;
	case NiagaraParameterPanelSectionID::INITIALVALUES:
		return ENiagaraScriptParameterUsage::InitialValueInput;

	// By convention, default new parameters created in system editor to output usage.
	case NiagaraParameterPanelSectionID::ENGINE:
	case NiagaraParameterPanelSectionID::USER:
	case NiagaraParameterPanelSectionID::SYSTEM:
	case NiagaraParameterPanelSectionID::EMITTER:
	case NiagaraParameterPanelSectionID::PARTICLES:
		return ENiagaraScriptParameterUsage::Output;

	case NiagaraParameterPanelSectionID::NONE:
		ensureMsgf(false, TEXT("Encountered invalid parameter panel section ID NONE when getting usage from section!"));
		break;
	};
	ensureMsgf(false, TEXT("Did not encounter a known section ID when getting usage from section!"));
	return ENiagaraScriptParameterUsage::Output;
}

TSharedRef<SWidget> INiagaraParameterPanelViewModel::GetScriptParameterVisualWidget(FCreateWidgetForActionData* const InCreateData) const
{
	TSharedPtr<FNiagaraScriptVarAndViewInfoAction> ScriptVarAndViewInfoAction = StaticCastSharedPtr<FNiagaraScriptVarAndViewInfoAction>(InCreateData->Action);
	const FNiagaraScriptVariableAndViewInfo& ScriptVarAndViewInfo = ScriptVarAndViewInfoAction.Get()->ScriptVariableAndViewInfo;
	TSharedPtr<FNiagaraParameterPanelEntryParameterNameViewModel> ParameterNameViewModel = MakeShared<FNiagaraParameterPanelEntryParameterNameViewModel>(InCreateData, ScriptVarAndViewInfo);
	ParameterNameViewModel->GetOnParameterRenamedDelegate().BindSP(this, &INiagaraParameterPanelViewModel::RenameParameter);
	ParameterNameViewModel->GetOnScopeSelectionChangedDelegate().BindSP(this, &INiagaraParameterPanelViewModel::ChangeParameterScope);

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
}

void FNiagaraSystemToolkitParameterPanelViewModel::Refresh() const
{
	OnParameterPanelViewModelRefreshed.ExecuteIfBound();
}

void FNiagaraSystemToolkitParameterPanelViewModel::CollectStaticSections(TArray<int32>& StaticSectionIDs) const
{
	// Generic Emitter/System view, categorize by scope
	StaticSectionIDs.Add(NiagaraParameterPanelSectionID::USER);
	StaticSectionIDs.Add(NiagaraParameterPanelSectionID::ENGINE);
	StaticSectionIDs.Add(NiagaraParameterPanelSectionID::SYSTEM);
	StaticSectionIDs.Add(NiagaraParameterPanelSectionID::EMITTER);
	StaticSectionIDs.Add(NiagaraParameterPanelSectionID::PARTICLES);
}

NiagaraParameterPanelSectionID::Type FNiagaraSystemToolkitParameterPanelViewModel::GetSectionForVarAndViewInfo(const FNiagaraScriptVariableAndViewInfo& VarAndViewInfo) const
{
	// Generic Emitter/System view, categorize by scope
	if (VarAndViewInfo.MetaData.Usage == ENiagaraScriptParameterUsage::Input && VarAndViewInfo.MetaData.Scope != ENiagaraParameterScope::Input)
	{
		return NiagaraParameterPanelSectionID::GetSectionForScope(VarAndViewInfo.MetaData.Scope);
	}
	else
	{
		//return NiagaraParameterPanelSectionID::GetSectionForScope(VarAndViewInfo.ScriptVariableMetaData.LastPrecompileScope); //@todo(ng) cache the known scope in the niagarasystem
		ENiagaraParameterScope ReturnScope = ENiagaraParameterScope::None;
		ENiagaraScriptParameterUsage EmptyUsage;
		FName EmptyName;
		FNiagaraStackGraphUtilities::SetParameterMetaDataFromName(VarAndViewInfo.ScriptVariable.GetName(), ReturnScope, EmptyUsage, EmptyName);
		return NiagaraParameterPanelSectionID::GetSectionForScope(ReturnScope);
	}
}

void FNiagaraSystemToolkitParameterPanelViewModel::AddParameter(const FNiagaraVariable& InVariableToAdd, const FNiagaraVariableMetaData& InVariableMetaDataToAssign)
{
	FScopedTransaction AddParameter(LOCTEXT("AddParameter", "Add Parameter"));
	bool bSystemIsSelected = OverviewSelectionViewModel->GetSystemIsSelected();

	UNiagaraGraph::FAddParameterOptions AddParameterOptions = UNiagaraGraph::FAddParameterOptions();
	AddParameterOptions.NewParameterScope = InVariableMetaDataToAssign.Scope;
	AddParameterOptions.NewParameterUsage = InVariableMetaDataToAssign.Usage;
	AddParameterOptions.bAddedFromSystemEditor = true;
	FNiagaraVariable DuplicateVar = InVariableToAdd; //@todo(ng) rewrite

	if (InVariableMetaDataToAssign.Scope == ENiagaraParameterScope::User)
	{
		SystemViewModel->GetSystem().Modify();
		SystemViewModel->GetSystem().GetExposedParameters().AddParameter(InVariableToAdd); //@Todo(ng) copy ParameterMapView impl
	}
	else if (InVariableMetaDataToAssign.Scope == ENiagaraParameterScope::System)
	{
		UNiagaraGraph* Graph = SystemViewModel->GetSystemScriptViewModel()->GetGraphViewModel()->GetGraph();
		Graph->Modify();
		Graph->AddParameter(DuplicateVar, AddParameterOptions); //@todo(ng) verify
	}
	else
	{
		for (TWeakObjectPtr<UNiagaraGraph> Graph : GetEditableEmitterScriptGraphs())
		{
			if (ensureMsgf(Graph.IsValid(), TEXT("Editable Emitter Script Graph was stale when adding parameter!")))
			{
				Graph->Modify();
				Graph->AddParameter(DuplicateVar, AddParameterOptions);
			}
		}
	}

	if (bSystemIsSelected)
	{
		//SystemViewModel-> //@todo(ng) handle user params
	}
	
	Refresh();
}

void FNiagaraSystemToolkitParameterPanelViewModel::RemoveParameter(const FNiagaraVariable& TargetVariableToRemove, const FNiagaraVariableMetaData& TargetVariableMetaData)
{
	FScopedTransaction RemoveParameter(LOCTEXT("RemoveParameter", "Remove Parameter"));
	if (TargetVariableMetaData.IsInputUsage() && TargetVariableMetaData.Scope == ENiagaraParameterScope::User) //@todo(ng) verify
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

bool FNiagaraSystemToolkitParameterPanelViewModel::CanRemoveParameter(const FNiagaraVariable& TargetVariableToRemove, const FNiagaraVariableMetaData& TargetVariableMetaData) const
{
	return TargetVariableMetaData.bCreatedInSystemEditor;
}

void FNiagaraSystemToolkitParameterPanelViewModel::RenameParameter(const FNiagaraVariable& TargetVariableToRename, const FNiagaraVariableMetaData& TargetVariableMetaData, const FText& NewVariableNameText) const
{
	FScopedTransaction RenameParameter(LOCTEXT("RenameParameter", "Rename Parameter"));
	const FString ExistingVariableScopeString = FNiagaraStackGraphUtilities::GetNamespaceStringForScriptParameterScope(TargetVariableMetaData.Scope);
	const FName NewVariableName = FName(*(ExistingVariableScopeString + NewVariableNameText.ToString()));
	for (const TWeakObjectPtr<UNiagaraGraph>& Graph : GetEditableEmitterScriptGraphs())
	{
		if (ensureMsgf(Graph.IsValid(), TEXT("Editable Emitter Script Graph was stale when renaming parameter!")))
		{
			Graph->Modify();
			Graph->RenameParameter(TargetVariableToRename, NewVariableName, TargetVariableMetaData.bIsStaticSwitch, TargetVariableMetaData.Scope); //@todo(ng) handle renaming system params
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
	return TargetVariableMetaData.bCreatedInSystemEditor == true;
}

bool FNiagaraSystemToolkitParameterPanelViewModel::CanRenameParameter(const FNiagaraVariable& TargetVariableToRename, const FNiagaraVariableMetaData& TargetVariableMetaData, const FText& NewVariableNameText) const
{
	return TargetVariableMetaData.bCreatedInSystemEditor == true;
}

FReply FNiagaraSystemToolkitParameterPanelViewModel::HandleActionDragged(const TSharedPtr<FEdGraphSchemaAction>& InAction, const FPointerEvent& MouseEvent) const
{
	const FText TooltipFormat = LOCTEXT("Parameters", "Name: {0} \nType: {1}"); //@todo(ng) move to static define
	const FNiagaraScriptVarAndViewInfoAction* ScriptVarAction = static_cast<FNiagaraScriptVarAndViewInfoAction*>(InAction.Get());
	const FNiagaraScriptVariableAndViewInfo& ScriptVarAndViewInfo = ScriptVarAction->ScriptVariableAndViewInfo;
	NiagaraParameterPanelSectionID::Type Section = NiagaraParameterPanelSectionID::GetSectionForParameterMetaData(ScriptVarAndViewInfo.MetaData);
	const FNiagaraVariable& Var = ScriptVarAndViewInfo.ScriptVariable;
	const FText Name = FText::FromName(Var.GetName());
	const FText Tooltip = FText::Format(TooltipFormat, FText::FromName(Var.GetName()), Var.GetType().GetNameText());

	TSharedPtr<FNiagaraParameterAction> ParameterAction = MakeShared<FNiagaraParameterAction>(Var, FText::GetEmpty(), Name, Tooltip, 0, FText(), Section);
	TSharedRef<FNiagaraParameterDragOperation> DragOperation = MakeShared<FNiagaraParameterDragOperation>(ParameterAction);
	DragOperation->CurrentHoverText = InAction->GetMenuDescription();
	DragOperation->SetupDefaults();
	DragOperation->Construct();
	return FReply::Handled().BeginDragDrop(DragOperation);
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

				if (MetaData.Scope == ENiagaraParameterScope::None || MetaData.Usage == ENiagaraScriptParameterUsage::None)
				{
					ensureMsgf(false, TEXT("Invalid MetaData found for graph variable: %s"), *Iter.Value()->Variable.GetName().ToString());
					continue;
				}
				else if (MetaData.Scope == ENiagaraParameterScope::Local)
				{
					// Note, the MetaData.Usage being local is fine to display for the System toolkit.
					continue;
				}
				else if (MetaData.Scope == ENiagaraParameterScope::Input) // Do not expose inputs as configurable values
				{
					continue;
				}
				else if (MetaData.Scope == ENiagaraParameterScope::ScriptPersistent || MetaData.Scope == ENiagaraParameterScope::ScriptTransient)
				{
					//@todo(ng) Skip script alias parameters until we can resolve them!
					continue;
				}
				ViewedParameters.Add(FNiagaraScriptVariableAndViewInfo(Iter.Value()->Variable, MetaData));
			}
		}
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

FNiagaraScriptToolkitParameterPanelViewModel::FNiagaraScriptToolkitParameterPanelViewModel(TSharedPtr<FNiagaraStandaloneScriptViewModel> InScriptViewModel)
{
	ScriptViewModel = InScriptViewModel;
	VariableObjectSelection = ScriptViewModel->GetVariableSelection();
}

FNiagaraScriptToolkitParameterPanelViewModel::~FNiagaraScriptToolkitParameterPanelViewModel()
{
	UNiagaraGraph* NiagaraGraph = static_cast<UNiagaraGraph*>(ScriptViewModel->GetGraphViewModel()->GetGraph());
	NiagaraGraph->RemoveOnGraphChangedHandler(OnGraphChangedHandle);
	NiagaraGraph->RemoveOnGraphNeedsRecompileHandler(OnGraphNeedsRecompileHandle);
}

void FNiagaraScriptToolkitParameterPanelViewModel::InitBindings()
{
	UNiagaraGraph* NiagaraGraph = static_cast<UNiagaraGraph*>(ScriptViewModel->GetGraphViewModel()->GetGraph());
	OnGraphChangedHandle = NiagaraGraph->AddOnGraphChangedHandler(
		FOnGraphChanged::FDelegate::CreateRaw(this, &FNiagaraScriptToolkitParameterPanelViewModel::HandleOnGraphChanged));
	OnGraphNeedsRecompileHandle = NiagaraGraph->AddOnGraphNeedsRecompileHandler(
		FOnGraphChanged::FDelegate::CreateRaw(this, &FNiagaraScriptToolkitParameterPanelViewModel::HandleOnGraphChanged));
	ScriptViewModel->GetGraphViewModel()->GetGraph()->RegisterPinVisualWidgetProvider(UNiagaraGraph::FOnGetPinVisualWidget::CreateSP(this, &FNiagaraScriptToolkitParameterPanelViewModel::GetScriptParameterVisualWidget));
}

void FNiagaraScriptToolkitParameterPanelViewModel::Refresh() const
{
	OnParameterPanelViewModelRefreshed.ExecuteIfBound();
}

void FNiagaraScriptToolkitParameterPanelViewModel::CollectStaticSections(TArray<int32>& StaticSectionIDs) const
{
	StaticSectionIDs.Add(NiagaraParameterPanelSectionID::INPUTS);
	StaticSectionIDs.Add(NiagaraParameterPanelSectionID::REFERENCES);
	StaticSectionIDs.Add(NiagaraParameterPanelSectionID::OUTPUTS);
	StaticSectionIDs.Add(NiagaraParameterPanelSectionID::LOCALS);
	StaticSectionIDs.Add(NiagaraParameterPanelSectionID::INITIALVALUES);
}

NiagaraParameterPanelSectionID::Type FNiagaraScriptToolkitParameterPanelViewModel::GetSectionForVarAndViewInfo(const FNiagaraScriptVariableAndViewInfo& VarAndViewInfo) const
{
	return NiagaraParameterPanelSectionID::GetSectionForParameterMetaData(VarAndViewInfo.MetaData);
}

void FNiagaraScriptToolkitParameterPanelViewModel::AddParameter(const FNiagaraVariable& InVariableToAdd, const FNiagaraVariableMetaData& InVariableMetaDataToAssign)
{
	FScopedTransaction AddParameter(LOCTEXT("AddParameter", "Add parameter and create new associated node"));
	UNiagaraGraph::FAddParameterOptions AddParameterOptions = UNiagaraGraph::FAddParameterOptions();
	AddParameterOptions.NewParameterScope = InVariableMetaDataToAssign.Scope;
	AddParameterOptions.NewParameterUsage = InVariableMetaDataToAssign.Usage;
	FNiagaraVariable DuplicateVar = InVariableToAdd; //@todo(ng) rewrite

	UNiagaraGraph* Graph = ScriptViewModel->GetGraphViewModel()->GetGraph();
	Graph->Modify();
	const FVector2D NewNodePos = Graph->GetGoodPlaceForNewNode();
	if (InVariableMetaDataToAssign.Usage == ENiagaraScriptParameterUsage::Input)
	{
		UNiagaraNodeParameterMapGet* NewMapGet = FNiagaraSchemaAction_NewNode::SpawnNodeFromTemplate<UNiagaraNodeParameterMapGet>(Graph, NewObject<UNiagaraNodeParameterMapGet>(), NewNodePos);
		NewMapGet->Modify();
		NewMapGet->AddParameter(DuplicateVar, AddParameterOptions);
	}
	else if (InVariableMetaDataToAssign.Usage == ENiagaraScriptParameterUsage::Output)
	{
		UNiagaraNodeParameterMapSet* NewMapSet = FNiagaraSchemaAction_NewNode::SpawnNodeFromTemplate<UNiagaraNodeParameterMapSet>(Graph, NewObject<UNiagaraNodeParameterMapSet>(), NewNodePos);
		NewMapSet->Modify();
		NewMapSet->AddParameter(DuplicateVar, AddParameterOptions);
	}
	else if (InVariableMetaDataToAssign.Usage == ENiagaraScriptParameterUsage::InputOutput)
	{
		ensureMsgf(false, TEXT("Unexpected usage encountered when adding parameter through parameter panel view model!"));
	}
	else
	{
		Graph->AddParameter(DuplicateVar, AddParameterOptions);
	}
}

void FNiagaraScriptToolkitParameterPanelViewModel::RemoveParameter(const FNiagaraVariable& TargetVariableToRemove, const FNiagaraVariableMetaData& TargetVariableMetaData)
{
	FScopedTransaction RemoveParametersWithPins(LOCTEXT("RemoveParametersWithPins", "Remove parameter and referenced pins"));
	UNiagaraGraph* Graph = ScriptViewModel->GetGraphViewModel()->GetGraph();
	Graph->Modify();
	Graph->RemoveParameter(TargetVariableToRemove);
}

bool FNiagaraScriptToolkitParameterPanelViewModel::CanRemoveParameter(const FNiagaraVariable& TargetVariableToRemove, const FNiagaraVariableMetaData& TargetVariableMetaData) const
{
	return true;
}

void FNiagaraScriptToolkitParameterPanelViewModel::RenameParameter(const FNiagaraVariable& TargetVariableToRename, const FNiagaraVariableMetaData& TargetVariableMetaData, const FText& NewVariableNameText) const
{
	FScopedTransaction RenameParameterAndReferencedPins(LOCTEXT("RenameParameterAndReferencedPins", "Rename parameter and referenced pins"));
	const FString ExistingVariableScopeString = FNiagaraStackGraphUtilities::GetNamespaceStringForScriptParameterMetaData(TargetVariableMetaData);
	const FName NewVariableName = FName(*(ExistingVariableScopeString + NewVariableNameText.ToString()));
	UNiagaraGraph* Graph = ScriptViewModel->GetGraphViewModel()->GetGraph();
	Graph->Modify();
	Graph->RenameParameter(TargetVariableToRename, NewVariableName, TargetVariableMetaData.bIsStaticSwitch, TargetVariableMetaData.Scope);
}

void FNiagaraScriptToolkitParameterPanelViewModel::ChangeParameterScope(const FNiagaraVariable& TargetVariableToModify, const FNiagaraVariableMetaData& TargetVariableMetaData, const ENiagaraParameterScope NewVariableScope) const
{
	if (ensureMsgf(TargetVariableMetaData.Usage != ENiagaraScriptParameterUsage::Output, TEXT("Tried to change scope of output parameter!")))
	{
		FScopedTransaction ChangeParameterScopeAndReferencedPins(LOCTEXT("ChangeParameterScopeAndReferencedPins", "Change parameter scope, Rename parameter and referenced pins"));

		bool bIsInitialValue = TargetVariableMetaData.Usage == ENiagaraScriptParameterUsage::InitialValueInput;
		const FName NewVariableName = FNiagaraStackGraphUtilities::GetVariableNameForScope(TargetVariableToModify.GetName(), NewVariableScope, bIsInitialValue);
		UNiagaraGraph* Graph = ScriptViewModel->GetGraphViewModel()->GetGraph();
		Graph->Modify();
		Graph->RenameParameter(TargetVariableToModify, NewVariableName, TargetVariableMetaData.bIsStaticSwitch, NewVariableScope);
	}
}

bool FNiagaraScriptToolkitParameterPanelViewModel::CanModifyParameter(const FNiagaraVariable& TargetVariableToModify, const FNiagaraVariableMetaData& TargetVariableMetaData) const
{
	//@todo return false for parameter library entries
	return true;
}

bool FNiagaraScriptToolkitParameterPanelViewModel::CanRenameParameter(const FNiagaraVariable& TargetVariableToRename, const FNiagaraVariableMetaData& TargetVariableMetaData, const FText& NewVariableNameText) const
{
	// Prevent name values that would alias an existing parameter
	FName NewName = FName(*NewVariableNameText.ToString());
	for (const FNiagaraScriptVariableAndViewInfo& ViewedParameter : CachedViewedParameters)
	{
		if (NewName == ViewedParameter.MetaData.CachedNamespacelessVariableName)
		{
			return false; //@todo(ng) wrap this into the verify logic
		}
	}
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

TSharedRef<SWidget> FNiagaraScriptToolkitParameterPanelViewModel::GetScriptParameterVisualWidget(const UEdGraphPin* Pin) const
{
	const UEdGraphSchema_Niagara* Schema = GetDefault<UEdGraphSchema_Niagara>();
	const FNiagaraVariable PinVar = Schema->PinToNiagaraVariable(Pin);

	const FNiagaraScriptVariableAndViewInfo* ScriptVarAndViewInfo = CachedViewedParameters.FindByPredicate([PinVar](const FNiagaraScriptVariableAndViewInfo& Entry) {return Entry.ScriptVariable == PinVar; });
	if (ScriptVarAndViewInfo != nullptr)
	{
		TSharedPtr<FNiagaraGraphPinParameterNameViewModel> ParameterNameViewModel = MakeShared<FNiagaraGraphPinParameterNameViewModel>(Pin, *ScriptVarAndViewInfo, this);
		TSharedPtr<SWidget> ScriptParameterVisualWidget = SNew(SNiagaraParameterNameView, ParameterNameViewModel);
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

			TSharedPtr<FNiagaraGraphPinParameterNameViewModel> ParameterNameViewModel = MakeShared<FNiagaraGraphPinParameterNameViewModel>(Pin, NewScriptVarAndViewInfo, this);
			TSharedPtr<SWidget> ScriptParameterVisualWidget = SNew(SNiagaraParameterNameView, ParameterNameViewModel);
			return ScriptParameterVisualWidget->AsShared();
		}
	}

	// Cannot resolve the parameter from the pin, put an error widget in.
	TSharedPtr<SWidget> ErrorTextBlock = SNew(STextBlock).Text(FText::FromString("Could not resolve parameter!")); //@todo(ng) make error item method
	return ErrorTextBlock->AsShared();
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
	const FName InName = InVarMetaData.CachedNamespacelessVariableName;
	for (const FNiagaraScriptVariableAndViewInfo& ViewedParameter : CachedViewedParameters)
	{
		if (InName == ViewedParameter.MetaData.CachedNamespacelessVariableName)
		{
			PerScopeInfo[(int32)ViewedParameter.MetaData.Scope].bEnabled = false;
			PerScopeInfo[(int32)ViewedParameter.MetaData.Scope].Tooltip = LOCTEXT("NiagaraInvalidScopeSelectionNameAlias", "Cannot select scope {0}: Parameter with same name already has this scope."); //@todo(ng) get scope
		}
	}

	// Prevent making Module namespace parameters in function and dynamic input scripts
	if (ScriptViewModel->GetStandaloneScript()->GetUsage() != ENiagaraScriptUsage::Module)
	{
		PerScopeInfo[(int32)ENiagaraParameterScope::Input].bEnabled = false;
		PerScopeInfo[(int32)ENiagaraParameterScope::Input].Tooltip = LOCTEXT("NiagaraInvalidScopeSelectionModule", "Cannot select scope {0}: Scope is only valid in Module Scripts."); //@todo(ng) get scope
	}

	const TArray<ENiagaraParameterScope> InvalidParameterScopes = ScriptViewModel->GetStandaloneScript()->GetUnsupportedParameterScopes();
	for (ENiagaraParameterScope InvalidScope : InvalidParameterScopes)
	{
		PerScopeInfo[(int32)InvalidScope].bEnabled = false;
		PerScopeInfo[(int32)InvalidScope].Tooltip = LOCTEXT("NiagaraInvalidScopeSelectionUsageBitmask", "Cannot select scope {0}: Script Usage flags do not support a usage with this scope."); //@todo(ng) rewrite
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

			if (MetaData.bIsStaticSwitch == false && (MetaData.Scope == ENiagaraParameterScope::None || MetaData.Usage == ENiagaraScriptParameterUsage::None) )
			{
				// Parameters that are not static switches must have a scope and usage set.
				ensureMsgf(false, TEXT("Invalid MetaData found for graph variable: %s"), *Iter.Value()->Variable.GetName().ToString());
				continue;
			}
			else if (MetaData.Usage == ENiagaraScriptParameterUsage::InputOutput)
			{
				// Need two stack entries to represent this script parameter as both an input and output.
				FNiagaraVariableMetaData InputVariableMetaData = MetaData;
				FNiagaraVariableMetaData OutputVariableMetaData = MetaData;
				InputVariableMetaData.Usage = ENiagaraScriptParameterUsage::Input;
				OutputVariableMetaData.Usage = ENiagaraScriptParameterUsage::Output;

				const TStaticArray<FScopeIsEnabledAndTooltip, (int32)ENiagaraParameterScope::Num> PerScopeInfo = GetParameterScopesEnabledAndTooltips(Variable, MetaData);
				ViewedParameters.Add(FNiagaraScriptVariableAndViewInfo(Variable, InputVariableMetaData, PerScopeInfo));
				ViewedParameters.Add(FNiagaraScriptVariableAndViewInfo(Variable, OutputVariableMetaData));
			}
			else if(MetaData.Usage == ENiagaraScriptParameterUsage::Input || MetaData.Usage == ENiagaraScriptParameterUsage::InitialValueInput)
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
