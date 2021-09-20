// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewModels/NiagaraParameterPanelViewModel.h"

#include "NiagaraActions.h"
#include "NiagaraClipboard.h"
#include "NiagaraConstants.h"
#include "NiagaraEditorData.h"
#include "NiagaraEditorModule.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeAssignment.h"
#include "NiagaraNodeEmitter.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraNodeParameterMapGet.h"
#include "NiagaraObjectSelection.h"
#include "NiagaraParameterDefinitions.h"
#include "NiagaraScriptGraphViewModel.h"
#include "NiagaraSimulationStageBase.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"
#include "ViewModels/NiagaraScriptViewModel.h"
#include "ViewModels/NiagaraSystemSelectionViewModel.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/NiagaraSystemGraphSelectionViewModel.h"
#include "NiagaraSystemScriptViewModel.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "Widgets/SNiagaraParameterMenu.h"
#include "Widgets/SNiagaraParameterPanel.h"

#include "Framework/Commands/GenericCommands.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformApplicationMisc.h"
#include "ScopedTransaction.h"
#include "Widgets/SNullWidget.h"

#define LOCTEXT_NAMESPACE "NiagaraParameterPanelViewModel"

template<> TMap<UNiagaraScript*, TArray<FNiagaraScriptToolkitParameterPanelViewModel*>> TNiagaraViewModelManager<UNiagaraScript, FNiagaraScriptToolkitParameterPanelViewModel>::ObjectsToViewModels{};
template<> TMap<UNiagaraSystem*, TArray<FNiagaraSystemToolkitParameterPanelViewModel*>> TNiagaraViewModelManager<UNiagaraSystem, FNiagaraSystemToolkitParameterPanelViewModel>::ObjectsToViewModels{};


TArray<FNiagaraParameterPanelCategory> FNiagaraSystemToolkitParameterPanelViewModel::DefaultCategories;
TArray<FNiagaraParameterPanelCategory> FNiagaraSystemToolkitParameterPanelViewModel::DefaultAdvancedCategories;
TArray<FNiagaraParameterPanelCategory> FNiagaraScriptToolkitParameterPanelViewModel::DefaultCategories;
TArray<FNiagaraParameterPanelCategory> FNiagaraScriptToolkitParameterPanelViewModel::DefaultAdvancedCategories;
TArray<FNiagaraParameterPanelCategory> FNiagaraParameterDefinitionsToolkitParameterPanelViewModel::DefaultCategories;


///////////////////////////////////////////////////////////////////////////////
/// System Toolkit Parameter Panel Utilities								///
///////////////////////////////////////////////////////////////////////////////

TArray<UNiagaraGraph*> FNiagaraSystemToolkitParameterPanelUtilities::GetAllGraphs(const TSharedPtr<FNiagaraSystemViewModel>& SystemViewModel)
{
	TArray<UNiagaraGraph*> OutGraphs;

	// Helper lambda to null check graph weak object ptrs before adding them to the retval array.
	auto AddToOutGraphsChecked = [&OutGraphs](const TWeakObjectPtr<UNiagaraGraph>& WeakGraph) {
		UNiagaraGraph* Graph = WeakGraph.Get();
		if (Graph == nullptr)
		{
			ensureMsgf(false, TEXT("Encountered null graph when gathering editable graphs for system parameter panel viewmodel!"));
			return;
		}
		OutGraphs.Add(Graph);
	};

	if (SystemViewModel->GetEditMode() == ENiagaraSystemViewModelEditMode::SystemAsset)
	{
		TArray<UNiagaraGraph*> Graphs;
		AddToOutGraphsChecked(SystemViewModel->GetSystemScriptViewModel()->GetGraphViewModel()->GetGraph());

		const TArray<TSharedRef<FNiagaraEmitterHandleViewModel>>& EmitterHandleViewModels = SystemViewModel->GetEmitterHandleViewModels();
		for (const TSharedRef<FNiagaraEmitterHandleViewModel>& EmitterHandleViewModel : EmitterHandleViewModels)
		{
			FNiagaraEmitterHandle* EmitterHandle = EmitterHandleViewModel->GetEmitterHandle();
			if (EmitterHandle == nullptr)
			{
				continue;
			}
			UNiagaraGraph* Graph = Cast<UNiagaraScriptSource>(EmitterHandle->GetInstance()->GraphSource)->NodeGraph;
			if (Graph)
			{
				OutGraphs.Add(Graph);
			}
		}
	}
	else
	{
		FNiagaraEmitterHandle* EmitterHandle = SystemViewModel->GetEmitterHandleViewModels()[0]->GetEmitterHandle();
		if (EmitterHandle != nullptr)
		{
			OutGraphs.Add(Cast<UNiagaraScriptSource>(EmitterHandle->GetInstance()->GraphSource)->NodeGraph);
		}
	}
	return OutGraphs;
}

TArray<UNiagaraGraph*> FNiagaraSystemToolkitParameterPanelUtilities::GetEditableGraphs(const TSharedPtr<FNiagaraSystemViewModel>& SystemViewModel, const TWeakPtr<FNiagaraSystemGraphSelectionViewModel>& SystemGraphSelectionViewModelWeak)
{
	TArray<UNiagaraGraph*> EditableGraphs;

	// Helper lambda to null check graph weak object ptrs before adding them to the retval array.
	auto AddToEditableGraphChecked = [&EditableGraphs](const TWeakObjectPtr<UNiagaraGraph>& WeakGraph) {
		UNiagaraGraph* Graph = WeakGraph.Get();
		if (Graph == nullptr)
		{
			ensureMsgf(false, TEXT("Encountered null graph when gathering editable graphs for system parameter panel viewmodel!"));
			return;
		}
		EditableGraphs.Add(Graph);
	};

	if (SystemViewModel->GetEditMode() == ENiagaraSystemViewModelEditMode::SystemAsset && ensureMsgf(SystemGraphSelectionViewModelWeak.IsValid(), TEXT("SystemGraphSelectionViewModel was null for System edit mode!")))
	{
		for (const TWeakObjectPtr<UNiagaraGraph>& WeakGraph : SystemGraphSelectionViewModelWeak.Pin()->GetSelectedEmitterScriptGraphs())
		{
			AddToEditableGraphChecked(WeakGraph);
		}
		AddToEditableGraphChecked(SystemViewModel->GetSystemScriptViewModel()->GetGraphViewModel()->GetGraph());
	}
	else
	{
		EditableGraphs.Add(Cast<UNiagaraScriptSource>(SystemViewModel->GetEmitterHandleViewModels()[0]->GetEmitterHandle()->GetInstance()->GraphSource)->NodeGraph);
	}
	return EditableGraphs;
}

FReply FNiagaraSystemToolkitParameterPanelUtilities::CreateDragEventForParameterItem(const FNiagaraParameterPanelItemBase& DraggedItem, const FPointerEvent& MouseEvent, const TArray<FNiagaraGraphParameterReference>& GraphParameterReferencesForItem, const TSharedPtr<TArray<FName>>& ParametersWithNamespaceModifierRenamePending)
{
	const static FText TooltipFormat = LOCTEXT("Parameters", "Name: {0} \nType: {1}");

	//@todo(ng) refactor drag action to not carry around the reference collection; graph ptr goes unused.
	FNiagaraGraphParameterReferenceCollection ReferenceCollection = FNiagaraGraphParameterReferenceCollection(true);
	ReferenceCollection.Graph = nullptr;
	ReferenceCollection.ParameterReferences = GraphParameterReferencesForItem;
	const TArray<FNiagaraGraphParameterReferenceCollection> ReferenceCollectionArray = { ReferenceCollection };
	const FText Name = FNiagaraParameterUtilities::FormatParameterNameForTextDisplay(DraggedItem.GetVariable().GetName());
	const FText ToolTip = FText::Format(TooltipFormat, Name, DraggedItem.GetVariable().GetType().GetNameText());
	TSharedPtr<FEdGraphSchemaAction> ItemDragAction = MakeShared<FNiagaraParameterAction>(DraggedItem.ScriptVariable, FText::GetEmpty(), Name, ToolTip, 0, FText(), 0/*SectionID*/);
	TSharedPtr<FNiagaraParameterDragOperation> DragOperation = MakeShared<FNiagaraParameterDragOperation>(ItemDragAction);
	DragOperation->CurrentHoverText = FNiagaraParameterUtilities::FormatParameterNameForTextDisplay(DraggedItem.GetVariable().GetName());
	DragOperation->SetupDefaults();
	DragOperation->Construct();
	return FReply::Handled().BeginDragDrop(DragOperation.ToSharedRef());
}


///////////////////////////////////////////////////////////////////////////////
/// Script Toolkit Parameter Panel Utilities								///
///////////////////////////////////////////////////////////////////////////////

TArray<UNiagaraGraph*> FNiagaraScriptToolkitParameterPanelUtilities::GetEditableGraphs(const TSharedPtr<FNiagaraScriptViewModel>& ScriptViewModel)
{
	TArray<UNiagaraGraph*> EditableGraphs;
	EditableGraphs.Add(ScriptViewModel->GetGraphViewModel()->GetGraph());
	return EditableGraphs;
}

FReply FNiagaraScriptToolkitParameterPanelUtilities::CreateDragEventForParameterItem(const FNiagaraParameterPanelItemBase& DraggedItem, const FPointerEvent& MouseEvent, const TArray<FNiagaraGraphParameterReference>& GraphParameterReferencesForItem, const TSharedPtr<TArray<FName>>& ParametersWithNamespaceModifierRenamePending)
{
	const static FText TooltipFormat = LOCTEXT("Parameters", "Name: {0} \nType: {1}");

	//@todo(ng) refactor drag action to not carry around the reference collection; graph ptr goes unused.
	FNiagaraGraphParameterReferenceCollection ReferenceCollection = FNiagaraGraphParameterReferenceCollection(true);
	ReferenceCollection.Graph = nullptr;
	ReferenceCollection.ParameterReferences = GraphParameterReferencesForItem;
	const TArray<FNiagaraGraphParameterReferenceCollection> ReferenceCollectionArray = { ReferenceCollection };
	const FText Name = FNiagaraParameterUtilities::FormatParameterNameForTextDisplay(DraggedItem.GetVariable().GetName());
	const FText ToolTip = FText::Format(TooltipFormat, Name, DraggedItem.GetVariable().GetType().GetNameText());
	TSharedPtr<FEdGraphSchemaAction> ItemDragAction = MakeShared<FNiagaraParameterAction>(DraggedItem.ScriptVariable, FText::GetEmpty(), Name, ToolTip, 0, FText(), 0/*SectionID*/);
	TSharedPtr<FNiagaraParameterGraphDragOperation> DragOperation = FNiagaraParameterGraphDragOperation::New(ItemDragAction);
	DragOperation->SetAltDrag(MouseEvent.IsAltDown());
	DragOperation->SetCtrlDrag(MouseEvent.IsLeftControlDown() || MouseEvent.IsRightControlDown());
	return FReply::Handled().BeginDragDrop(DragOperation.ToSharedRef());
}


///////////////////////////////////////////////////////////////////////////////
/// Immutable Parameter Panel View Model									///
///////////////////////////////////////////////////////////////////////////////

void INiagaraImmutableParameterPanelViewModel::Refresh() const
{
	OnRequestRefreshDelegate.ExecuteIfBound();
}

void INiagaraImmutableParameterPanelViewModel::RefreshNextTick() const
{
	OnRequestRefreshNextTickDelegate.ExecuteIfBound();
}

void INiagaraImmutableParameterPanelViewModel::PostUndo(bool bSuccess)
{
	Refresh();
}

void INiagaraImmutableParameterPanelViewModel::CopyParameterReference(const FNiagaraParameterPanelItemBase& ItemToCopy) const
{
	FPlatformApplicationMisc::ClipboardCopy(*ItemToCopy.GetVariable().GetName().ToString());
}

bool INiagaraImmutableParameterPanelViewModel::GetCanCopyParameterReferenceAndToolTip(const FNiagaraParameterPanelItemBase& ItemToCopy, FText& OutCanCopyParameterToolTip) const
{
	OutCanCopyParameterToolTip = LOCTEXT("CopyReferenceToolTip", "Copy a string reference for this parameter to the clipboard.\nThis reference can be used in expressions and custom HLSL nodes.");
	return true;
}

void INiagaraImmutableParameterPanelViewModel::CopyParameterMetaData(const FNiagaraParameterPanelItemBase ItemToCopy) const
{
	for (const UNiagaraScriptVariable* ScriptVariable : GetEditableScriptVariablesWithName(ItemToCopy.GetVariable().GetName()))
	{
		UNiagaraClipboardContent* ClipboardContent = UNiagaraClipboardContent::Create();
		ClipboardContent->ScriptVariables.Add(ScriptVariable);
		FNiagaraEditorModule::Get().GetClipboard().SetClipboardContent(ClipboardContent);
		break;
	}
}

bool INiagaraImmutableParameterPanelViewModel::GetCanCopyParameterMetaDataAndToolTip(const FNiagaraParameterPanelItemBase& ItemToCopy, FText& OutCanCopyToolTip) const
{
	if (GetEditableScriptVariablesWithName(ItemToCopy.GetVariable().GetName()).Num() > 0)
	{
		return true;
	}
	return false;
}


///////////////////////////////////////////////////////////////////////////////
///	Parameter Panel View Model												///
///////////////////////////////////////////////////////////////////////////////

INiagaraParameterPanelViewModel::~INiagaraParameterPanelViewModel()
{
	// Clean up transient UNiagaraScriptVariables used as intermediate representations.
	for (auto It = TransientParameterToScriptVarMap.CreateIterator(); It; ++It)
	{
		UNiagaraScriptVariable* ScriptVar = It.Value();
		ScriptVar->RemoveFromRoot();
		ScriptVar = nullptr;
	}
}

bool INiagaraParameterPanelViewModel::GetCanDeleteParameterAndToolTip(const FNiagaraParameterPanelItem& ItemToDelete, FText& OutCanDeleteParameterToolTip) const
{
	if (ItemToDelete.bExternallyReferenced)
	{
		//@todo(ng) revise loctexts
		OutCanDeleteParameterToolTip = LOCTEXT("CantDeleteSelected_External", "This parameter is referenced in an external script and cannot be deleted.");
		return false;
	}
	OutCanDeleteParameterToolTip = LOCTEXT("DeleteSelected", "Delete the selected parameter.");
	return true;
}

bool INiagaraParameterPanelViewModel::GetCanPasteParameterMetaDataAndToolTip(FText& OutCanPasteToolTip)
{
	const UNiagaraClipboardContent* ClipboardContent = FNiagaraEditorModule::Get().GetClipboard().GetClipboardContent();
	if (ClipboardContent == nullptr || ClipboardContent->ScriptVariables.Num() != 1)
	{
		OutCanPasteToolTip = LOCTEXT("CantPasteMetaDataToolTip_Invalid", "Cannot Paste: There is not any parameter metadata to paste in the clipboard.");
		return false;
	}
	OutCanPasteToolTip = LOCTEXT("PasteMetaDataToolTip", "Paste the parameter metadata from the system clipboard to the selected parameters.");
	return true;
}

void INiagaraParameterPanelViewModel::PasteParameterMetaData(const TArray<FNiagaraParameterPanelItem> SelectedItems)
{
	const UNiagaraClipboardContent* ClipboardContent = FNiagaraEditorModule::Get().GetClipboard().GetClipboardContent();
	if (ClipboardContent == nullptr || ClipboardContent->ScriptVariables.Num() != 1 )
	{
		return;
	}

	TArray<UNiagaraScriptVariable*> TargetScriptVariables;
	for (const FNiagaraParameterPanelItem& Item : SelectedItems)
	{
		
		TargetScriptVariables.Append(GetEditableScriptVariablesWithName(Item.GetVariable().GetName()));
	}

	if (TargetScriptVariables.Num() > 0)
	{
		FScopedTransaction PasteMetadataTransaction(LOCTEXT("PasteMetadataTransaction", "Paste parameter metadata"));
		for (UNiagaraScriptVariable* TargetScriptVariable : TargetScriptVariables)
		{
			TargetScriptVariable->Modify();
			TargetScriptVariable->Metadata.CopyUserEditableMetaData(ClipboardContent->ScriptVariables[0]->Metadata);
			TargetScriptVariable->PostEditChange();
		}
	}
}

void INiagaraParameterPanelViewModel::DuplicateParameter(const FNiagaraParameterPanelItem ItemToDuplicate) const
{
	TSet<FName> ParameterNames;
	for (const FNiagaraParameterPanelItem& CachedViewedItem : CachedViewedItems)
	{
		ParameterNames.Add(CachedViewedItem.GetVariable().GetName());
	}
	const FName NewUniqueName = FNiagaraUtilities::GetUniqueName(ItemToDuplicate.GetVariable().GetName(), ParameterNames);
	FScopedTransaction Transaction(LOCTEXT("DuplicateParameterTransaction", "Duplicate parameter"));
	const bool bRequestRename = true;
	const bool bMakeUniqueName = true;
	AddParameter(FNiagaraVariable(ItemToDuplicate.GetVariable().GetType(), NewUniqueName), FNiagaraParameterPanelCategory(ItemToDuplicate.NamespaceMetaData), bRequestRename, bMakeUniqueName);
}

bool INiagaraParameterPanelViewModel::GetCanDuplicateParameterAndToolTip(const FNiagaraParameterPanelItem& ItemToDuplicate, FText& OutCanDuplicateParameterToolTip) const
{
	if (ItemToDuplicate.NamespaceMetaData.Options.Contains(ENiagaraNamespaceMetadataOptions::PreventEditingName))
	{
		OutCanDuplicateParameterToolTip = LOCTEXT("ParameterPanelViewModel_DuplicateParameter_PreventEditingName", "This parameter can not be duplicated because it does not support editing its name.");
		return false;
	}
	OutCanDuplicateParameterToolTip = LOCTEXT("ParameterPanellViewModel_DuplicateParameter", "Create a new parameter with the same type as this parameter.");
	return true;
}

bool INiagaraParameterPanelViewModel::GetCanRenameParameterAndToolTip(const FNiagaraParameterPanelItem& ItemToRename, const FText& NewVariableNameText, bool bCheckEmptyNameText, FText& OutCanRenameParameterToolTip) const
{
	if (ItemToRename.ScriptVariable->GetIsSubscribedToParameterDefinitions())
	{
		OutCanRenameParameterToolTip = LOCTEXT("ParameterPanelViewModel_RenameParameter_ParameterDefinition", "Cannot rename Parameter: Parameters linked to Parameter Definitions may only be renamed in the source Parameter Definitions asset.");
		return false;
	}
	else if (ItemToRename.bExternallyReferenced)
	{
		OutCanRenameParameterToolTip = LOCTEXT("ParameterPanelViewModel_RenameParameter_ExternallyReferenced", "Cannot rename Parameter: Parameter is from an externally referenced script and can't be directly edited.");
		return false;
	}
	else if (ItemToRename.NamespaceMetaData.Options.Contains(ENiagaraNamespaceMetadataOptions::PreventEditingName))
	{
		OutCanRenameParameterToolTip = LOCTEXT("ParameterPanelViewModel_RenameParameter_NamespaceMetaData", "Cannot rename Parameter: The namespace of this Parameter does not allow renaming.");
		return false;
	}

	const FString NewVariableNameString = NewVariableNameText.ToString();
	if(ItemToRename.GetVariable().GetName().ToString() != NewVariableNameString)
	{ 
		if (CachedViewedItems.ContainsByPredicate([NewVariableNameString](const FNiagaraParameterPanelItem& Item) {return Item.GetVariable().GetName().ToString() == NewVariableNameString; }))
		{
			OutCanRenameParameterToolTip = LOCTEXT("ParameterPanelViewModel_RenameParameter_NameAlias", "Cannot Rename Parameter: A Parameter with this name already exists.");
			return false;
		}
	}

	if (bCheckEmptyNameText && NewVariableNameText.IsEmptyOrWhitespace())
	{
		// The incoming name text will contain the namespace even if the parameter name entry is empty, so make a parameter handle to split out the name.
		const FNiagaraParameterHandle NewVariableNameHandle = FNiagaraParameterHandle(FName(*NewVariableNameString));
		if (NewVariableNameHandle.GetName().IsNone())
		{
			OutCanRenameParameterToolTip = LOCTEXT("ParameterPanelViewModel_RenameParameter_NameNone", "Parameter must have a name.");
			return false;
		}
	}

	OutCanRenameParameterToolTip = LOCTEXT("ParameterPanelViewModel_RenameParameter_CreatedInSystem", "Rename this Parameter and all usages in the System and Emitters.");
	return true;
}

bool INiagaraParameterPanelViewModel::GetCanSubscribeParameterToLibraryAndToolTip(const FNiagaraParameterPanelItem& ItemToModify, const bool bSubscribing, FText& OutCanSubscribeParameterToolTip) const
{
	if (ItemToModify.bExternallyReferenced)
	{
		OutCanSubscribeParameterToolTip = LOCTEXT("ParameterPanelViewModel_CannotSubscribeParameterToDefinition_ExternallyReferenced", "Cannot subscribe Parameter to Parameter Definitions: Parameter is from and externally referenced script and cannot be directly edited.");
		return false;
	}
	else if (ItemToModify.DefinitionMatchState == EParameterDefinitionMatchState::NoMatchingDefinitions)
	{
		OutCanSubscribeParameterToolTip = LOCTEXT("ParameterPanelViewModel_CannotSubscribeParameterToDefinition_NotNameAliased", "Cannot subscribe Parameter to Parameter Definitions: Parameter name does not match any names in available Parameter Definitions.");
		return false;
	}
	else if (ItemToModify.DefinitionMatchState == EParameterDefinitionMatchState::MatchingDefinitionNameButNotType)
	{
		OutCanSubscribeParameterToolTip = LOCTEXT("ParameterPanelViewModel_CannotSubscribeParameterToDefinition_TypeMismatch", "Cannot subscribe Parameter to Parameter Definitions: Parameter name matches Parameter Definition, but Parameter type does not match Parameter Definition.");
		return false;
	}
	else if (ItemToModify.DefinitionMatchState == EParameterDefinitionMatchState::MatchingMoreThanOneDefinition)
	{
		OutCanSubscribeParameterToolTip = LOCTEXT("ParameterPanelViewModel_CannotSubscribeParameterToDefinition_MultipleDefinitions", "Cannot subscribe Parameter to Parameter Definitions: More than one Parameter Definition is specifying a Parameter with this name and type.");
		return false;
	}
	else if (ItemToModify.DefinitionMatchState == EParameterDefinitionMatchState::MatchingOneDefinition)
	{
		if (bSubscribing)
		{
			OutCanSubscribeParameterToolTip = LOCTEXT("ParameterPanelViewModel_SubscribeParameterToDefinition", "Set this Parameter to automatically update its default value and metadata from a Parameter Definition.");
			return true;
		}
		else
		{
			OutCanSubscribeParameterToolTip = LOCTEXT("ParameterPanelViewModel_UnsubscribeParameterFromDefinition", "Unsubscribe this Parameter from the a Parameter Definition and do not synchronize the default value and metadata.");
			return true;
		}
	}
	
	ensureMsgf(false, TEXT("Tried to get whether a parameter could be linked to a definition but the parameter name match state was not known!"));
	return false;
}

void INiagaraParameterPanelViewModel::SetParameterIsSubscribedToLibrary(const FNiagaraParameterPanelItem ItemToModify, const bool bSubscribed) const
{
	if (ensureMsgf(ItemToModify.bExternallyReferenced == false, TEXT("Cannot modify an externally referenced parameter.")) == false)
	{
		return;
	}

	SetParameterIsSubscribedToLibrary(ItemToModify.ScriptVariable, bSubscribed);
}

void INiagaraParameterPanelViewModel::SetParameterNamespace(const FNiagaraParameterPanelItem ItemToModify, FNiagaraNamespaceMetadata NewNamespaceMetaData, bool bDuplicateParameter) const
{
	FName NewName = FNiagaraParameterUtilities::ChangeNamespace(ItemToModify.GetVariable().GetName(), NewNamespaceMetaData);
	if (NewName != NAME_None)
	{
		bool bParameterExists = CachedViewedItems.ContainsByPredicate([NewName](const FNiagaraParameterPanelItem& CachedViewedItem) {return CachedViewedItem.GetVariable().GetName() == NewName; });
		if (bDuplicateParameter)
		{
			FName NewUniqueName;
			if (bParameterExists)
			{
				TSet<FName> ParameterNames;
				for (const FNiagaraParameterPanelItem& CachedViewedItem : CachedViewedItems)
				{
					ParameterNames.Add(CachedViewedItem.GetVariable().GetName());
				}
				NewUniqueName = FNiagaraUtilities::GetUniqueName(NewName, ParameterNames);
			}
			else
			{
				NewUniqueName = NewName;
			}
			FScopedTransaction Transaction(LOCTEXT("DuplicateParameterToNewNamespaceTransaction", "Duplicate parameter to new namespace"));
			const bool bRequestRename = false;
			const bool bMakeUniqueName = true;
			AddParameter(FNiagaraVariable(ItemToModify.GetVariable().GetType(), NewUniqueName), FNiagaraParameterPanelCategory(NewNamespaceMetaData), bRequestRename, bMakeUniqueName);
		}
		else if (bParameterExists == false)
		{
			FScopedTransaction Transaction(LOCTEXT("ChangeNamespaceTransaction", "Change namespace"));
			RenameParameter(ItemToModify, NewName);
		}
	}
}

bool INiagaraParameterPanelViewModel::GetCanSetParameterNamespaceAndToolTip(const FNiagaraParameterPanelItem& ItemToModify, const FName NewNamespace, FText& OutCanSetParameterNamespaceToolTip) const 
{
	if (ItemToModify.ScriptVariable->GetIsSubscribedToParameterDefinitions())
	{
		OutCanSetParameterNamespaceToolTip = LOCTEXT("ParameterPanelViewModel_ChangeParameterNamespace_ParameterDefinitions", "Cannot change Parameter namespace: Parameters linked to Parameter Definitions may only have their namespace changed in the source Parameter Definitions asset.");
		return false;
	}
	else if (ItemToModify.bExternallyReferenced)
	{
		OutCanSetParameterNamespaceToolTip = LOCTEXT("ParameterPanelViewModel_ChangeParameterNamespace_ExternallyReferenced", "Cannot change Parameter namespace: Parameter is from an externally referenced script and can't be directly edited.");
		return false;
	}
	return true;
}

void INiagaraParameterPanelViewModel::SetParameterNamespaceModifier(const FNiagaraParameterPanelItem ItemToModify, const FName NewNamespaceModifier, bool bDuplicateParameter) const
{
	FName NewName = FNiagaraParameterUtilities::SetSpecificNamespaceModifier(ItemToModify.GetVariable().GetName(), NewNamespaceModifier);
	if (NewName != NAME_None)
	{
		TSet<FName> ParameterNames;
		for (const FNiagaraParameterPanelItem& CachedViewedItem : CachedViewedItems)
		{
			ParameterNames.Add(CachedViewedItem.GetVariable().GetName());
		}

		bool bParameterExists = ParameterNames.Contains(NewName);
		if (bDuplicateParameter)
		{
			FName NewUniqueName;
			if (bParameterExists)
			{
				NewUniqueName = FNiagaraUtilities::GetUniqueName(NewName, ParameterNames);
			}
			else
			{
				NewUniqueName = NewName;
			}
			FScopedTransaction Transaction(LOCTEXT("DuplicateParameterToWithCustomNamespaceModifierTransaction", "Duplicate parameter with custom namespace modifier"));
			const bool bRequestRename = false;
			const bool bMakeUniqueName = true;
			AddParameter(FNiagaraVariable(ItemToModify.GetVariable().GetType(), NewUniqueName), FNiagaraParameterPanelCategory(ItemToModify.NamespaceMetaData), bRequestRename, bMakeUniqueName);
		}
		else if (bParameterExists == false)
		{
			FScopedTransaction Transaction(LOCTEXT("SetCustomNamespaceModifierTransaction", "Set custom namespace modifier"));
			RenameParameter(ItemToModify, NewName);
		}
	}
}

bool INiagaraParameterPanelViewModel::GetCanSetParameterNamespaceModifierAndToolTip(const FNiagaraParameterPanelItem& ItemToModify, const FName NamespaceModifier, bool bDuplicateParameter, FText& OutCanSetParameterNamespaceModifierToolTip) const
{
	if (FNiagaraParameterUtilities::TestCanSetSpecificNamespaceModifierWithMessage(ItemToModify.GetVariable().GetName(), NamespaceModifier, OutCanSetParameterNamespaceModifierToolTip) == false)
	{
		return false;
	}

	if (bDuplicateParameter == false)
	{
		if (ItemToModify.ScriptVariable->GetIsSubscribedToParameterDefinitions())
		{
			OutCanSetParameterNamespaceModifierToolTip = LOCTEXT("ParameterPanelViewModel_ChangeParameterNamespaceModifier_ParameterDefinitions", "Cannot change Parameter namespace modifier: Parameters from Parameter Definitions may only have their namespace modifier changed in the source Parameter Definitions asset.");
			return false;
		}
		else if (ItemToModify.bExternallyReferenced)
		{
			OutCanSetParameterNamespaceModifierToolTip = LOCTEXT("CantChangeNamespaceModifierExternallyReferenced", "Parameter is from an externally referenced script and can't be directly edited.");
			return false;
		}
		else if (NamespaceModifier != NAME_None)
		{
			FName NewName = FNiagaraParameterUtilities::SetSpecificNamespaceModifier(ItemToModify.GetVariable().GetName(), NamespaceModifier);
			if (CachedViewedItems.ContainsByPredicate([NewName](const FNiagaraParameterPanelItem& CachedViewedItem) {return CachedViewedItem.GetVariable().GetName() == NewName; }))
			{
				OutCanSetParameterNamespaceModifierToolTip = LOCTEXT("CantChangeNamespaceModifierAlreadyExits", "Can't set this namespace modifier because it would create a parameter that already exists.");
				return false;
			}
		}
	}

	return true;
}

void INiagaraParameterPanelViewModel::SetParameterCustomNamespaceModifier(const FNiagaraParameterPanelItem ItemToModify, bool bDuplicateParameter) const
{
	if (bDuplicateParameter == false && ItemToModify.bExternallyReferenced)
	{
		return;
	}
	TSet<FName> ParameterNames;
	for (const FNiagaraParameterPanelItem& CachedViewedItem : CachedViewedItems)
	{
		ParameterNames.Add(CachedViewedItem.GetVariable().GetName());
	}
	FName NewName = FNiagaraParameterUtilities::SetCustomNamespaceModifier(ItemToModify.GetVariable().GetName(), ParameterNames);

	if (NewName == NAME_None)
	{
		return;
	}

	if (bDuplicateParameter)
	{
		bool bParameterExists = ParameterNames.Contains(NewName);
		FName NewUniqueName;
		if (bParameterExists)
		{
			NewUniqueName = FNiagaraUtilities::GetUniqueName(NewName, ParameterNames);
		}
		else
		{
			NewUniqueName = NewName;
		}
		FScopedTransaction Transaction(LOCTEXT("DuplicateParameterToWithCustomNamespaceModifierTransaction", "Duplicate parameter with custom namespace modifier"));
		const bool bRequestRename = false;
		const bool bMakeUniqueName = true;
		AddParameter(FNiagaraVariable(ItemToModify.GetVariable().GetType(), NewUniqueName), FNiagaraParameterPanelCategory(ItemToModify.NamespaceMetaData), bRequestRename, bMakeUniqueName);
		OnNotifyParameterPendingNamespaceModifierRenameDelegate.ExecuteIfBound(NewUniqueName);
	}
	else
	{
		if (ItemToModify.GetVariable().GetName() != NewName)
		{
			FScopedTransaction Transaction(LOCTEXT("SetCustomNamespaceModifierTransaction", "Set custom namespace modifier"));
			RenameParameter(ItemToModify, NewName);
		}
		OnNotifyParameterPendingNamespaceModifierRenameDelegate.ExecuteIfBound(NewName);
	}
}

bool INiagaraParameterPanelViewModel::GetCanSetParameterCustomNamespaceModifierAndToolTip(const FNiagaraParameterPanelItem& ItemToModify, bool bDuplicateParameter, FText& OutCanSetParameterNamespaceModifierToolTip) const
{
	if (ItemToModify.ScriptVariable->GetIsSubscribedToParameterDefinitions())
	{
		OutCanSetParameterNamespaceModifierToolTip = LOCTEXT("ParameterPanelViewModel_ChangeParameterCustomNamespace_ParameterDefinitions", "Cannot set Parameter custom namespace: Parameters linked to Parameter Definitions may only have a custom namespace set in the source Parameter Definitions asset.");
		return false;
	}
	else if (FNiagaraParameterUtilities::TestCanSetCustomNamespaceModifierWithMessage(ItemToModify.GetVariable().GetName(), OutCanSetParameterNamespaceModifierToolTip) == false)
	{
		return false;
	}

	if (bDuplicateParameter == false && ItemToModify.bExternallyReferenced)
	{
		OutCanSetParameterNamespaceModifierToolTip = LOCTEXT("CantChangeNamespaceModifierExternallyReferenced", "Parameter is from an externally referenced script and can't be directly edited.");
		return false;
	}

	return true;
}

void INiagaraParameterPanelViewModel::GetChangeNamespaceSubMenu(FMenuBuilder& MenuBuilder, bool bDuplicateParameter, FNiagaraParameterPanelItem Item) const
{
	TArray<FNiagaraParameterUtilities::FChangeNamespaceMenuData> MenuData;
	FNiagaraParameterUtilities::GetChangeNamespaceMenuData(Item.GetVariable().GetName(), GetParameterContext(), MenuData);

	FText CanChangeToolTip;
	bool bCanChange = true;
	if (bDuplicateParameter == false)
	{
		bCanChange = GetCanSetParameterNamespaceAndToolTip(Item, FName(), CanChangeToolTip);
	}

	for (const FNiagaraParameterUtilities::FChangeNamespaceMenuData& MenuDataItem : MenuData)
	{
		bool bCanChangeThisNamespace = bCanChange && MenuDataItem.bCanChange;
		FText CanChangeThisNamespaceToolTip = bCanChange ? MenuDataItem.CanChangeToolTip : CanChangeToolTip;
		if (bCanChangeThisNamespace && bDuplicateParameter == false)
		{
			// Check for an existing duplicate by name.
			FName NewName = FNiagaraParameterUtilities::ChangeNamespace(Item.GetVariable().GetName(), MenuDataItem.Metadata);
			if (CachedViewedItems.ContainsByPredicate([NewName](const FNiagaraParameterPanelItem& Item) {return Item.GetVariable().GetName() == NewName; }))
			{
				bCanChangeThisNamespace = false;
				CanChangeThisNamespaceToolTip = LOCTEXT("CantMoveAlreadyExits", "Can not move to this namespace because a parameter with this name already exists.");
			}
		}

		FUIAction Action = FUIAction(
			FExecuteAction::CreateSP(this, &INiagaraParameterPanelViewModel::SetParameterNamespace, Item, MenuDataItem.Metadata, bDuplicateParameter),
			FCanExecuteAction::CreateLambda([bCanChangeThisNamespace]() { return bCanChangeThisNamespace; }));

		TSharedRef<SWidget> MenuItemWidget = FNiagaraParameterUtilities::CreateNamespaceMenuItemWidget(MenuDataItem.NamespaceParameterName, CanChangeThisNamespaceToolTip);
		MenuBuilder.AddMenuEntry(Action, MenuItemWidget, NAME_None, CanChangeThisNamespaceToolTip);
	}
}

void INiagaraParameterPanelViewModel::GetChangeNamespaceModifierSubMenu(FMenuBuilder& MenuBuilder, bool bDuplicateParameter, FNiagaraParameterPanelItem Item) const
{
	TArray<FName> OptionalNamespaceModifiers;
	FNiagaraParameterUtilities::GetOptionalNamespaceModifiers(Item.GetVariable().GetName(), GetParameterContext(), OptionalNamespaceModifiers);

	for (const FName OptionalNamespaceModifier : OptionalNamespaceModifiers)
	{
		FText SetToolTip;
		bool bCanSetNamespaceModifier = GetCanSetParameterNamespaceModifierAndToolTip(Item, OptionalNamespaceModifier, bDuplicateParameter, SetToolTip);
		MenuBuilder.AddMenuEntry(
			FText::FromName(OptionalNamespaceModifier),
			SetToolTip,
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &INiagaraParameterPanelViewModel::SetParameterNamespaceModifier, Item, OptionalNamespaceModifier, bDuplicateParameter),
				FCanExecuteAction::CreateLambda([bCanSetNamespaceModifier]() {return bCanSetNamespaceModifier; })));
	}

	FText SetCustomToolTip;
	bool bCanSetCustomNamespaceModifier = GetCanSetParameterCustomNamespaceModifierAndToolTip(Item, bDuplicateParameter, SetCustomToolTip);
	MenuBuilder.AddMenuEntry(
		LOCTEXT("CustomNamespaceModifier", "Custom..."),
		SetCustomToolTip,
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &INiagaraParameterPanelViewModel::SetParameterCustomNamespaceModifier, Item, bDuplicateParameter),
			FCanExecuteAction::CreateLambda([bCanSetCustomNamespaceModifier] {return bCanSetCustomNamespaceModifier; })));

	FText SetNoneToolTip;
	bool bCanSetEmptyNamespaceModifier = GetCanSetParameterNamespaceModifierAndToolTip(Item, FName(NAME_None), bDuplicateParameter, SetNoneToolTip);
	MenuBuilder.AddMenuEntry(
		LOCTEXT("NoneNamespaceModifier", "Clear"),
		SetNoneToolTip,
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &INiagaraParameterPanelViewModel::SetParameterNamespaceModifier, Item, FName(NAME_None), bDuplicateParameter),
			FCanExecuteAction::CreateLambda([bCanSetEmptyNamespaceModifier] {return bCanSetEmptyNamespaceModifier; })));
}

void INiagaraParameterPanelViewModel::OnParameterItemActivated(const FNiagaraParameterPanelItem& ActivatedItem) const
{
	ActivatedItem.RequestRename();
}

const TArray<FNiagaraParameterPanelItem>& INiagaraParameterPanelViewModel::GetCachedViewedParameterItems() const
{
	return CachedViewedItems;
}

void INiagaraParameterPanelViewModel::SelectParameterItemByName(const FName ParameterName, const bool bRequestRename) const
{
	if (bRequestRename)
	{
		OnNotifyParameterPendingRenameDelegate.ExecuteIfBound(ParameterName);
	}
	else
	{
		OnSelectParameterItemByNameDelegate.ExecuteIfBound(ParameterName);
	}
}

void INiagaraParameterPanelViewModel::SubscribeParameterToLibraryIfMatchingDefinition(const UNiagaraScriptVariable* ScriptVarToModify, const FName ScriptVarName) const
{
	const TArray<FReservedParameter>* ReservedParametersForName = FNiagaraEditorModule::Get().GetReservedParametersManager()->FindReservedParametersByName(ScriptVarName);

	if (ReservedParametersForName == nullptr || ReservedParametersForName->Num() == 0)
	{
		// Do not call SetParameterIsSubscribedToLibrary() unless ScriptVarToModify is already marked as SubscribedToParameterDefinitions.
		if (ScriptVarToModify->GetIsSubscribedToParameterDefinitions())
		{
			SetParameterIsSubscribedToLibrary(ScriptVarToModify, false);
		}
	}
	else if (ReservedParametersForName->Num() == 1)
	{
		const FReservedParameter& ReservedParameterDefinition = (*ReservedParametersForName)[0];
		if (ReservedParameterDefinition.GetParameter().GetType() != ScriptVarToModify->Variable.GetType())
		{
			const FText TypeMismatchWarningTemplate = LOCTEXT("RenameParameter_DefinitionTypeMismatch", "Renamed parameter \"{0}\" with type {1}. Type does not match existing parameter definition \"{0}\" with type {2} from {3}! ");
			FText TypeMismatchWarning = FText::Format(
				TypeMismatchWarningTemplate,
				FText::FromName(ScriptVarName),
				ScriptVarToModify->Variable.GetType().GetNameText(),
				ReservedParameterDefinition.GetParameter().GetType().GetNameText(),
				FText::FromString(ReservedParameterDefinition.GetReservingDefinitionsAsset()->GetName()));
			FNiagaraEditorUtilities::WarnWithToastAndLog(TypeMismatchWarning);

			// Do not call SetParameterIsSubscribedToLibrary() unless ScriptVarToModify is already marked as SubscribedToParameterDefinitions.
			if (ScriptVarToModify->GetIsSubscribedToParameterDefinitions())
			{
				SetParameterIsSubscribedToLibrary(ScriptVarToModify, false);
			}
		}
		else
		{
			// NOTE: It is possible we are calling SetParameterIsSubscribedToLibrary() on a UNiagaraScriptVariable that is already marked as SubscribedToParameterDefinitions;
			// This is intended as SetParameterIsSubscribedToLibrary() will register a new link to a different parameter definition.
			SetParameterIsSubscribedToLibrary(ScriptVarToModify, true);
		}
	}
}

bool INiagaraParameterPanelViewModel::CanMakeNewParameterOfType(const FNiagaraTypeDefinition& InType)
{
	return InType != FNiagaraTypeDefinition::GetParameterMapDef()
		&& InType != FNiagaraTypeDefinition::GetGenericNumericDef()
		&& !InType.IsInternalType();
}


///////////////////////////////////////////////////////////////////////////////
/// System Toolkit Parameter Panel View Model								///
///////////////////////////////////////////////////////////////////////////////

FNiagaraSystemToolkitParameterPanelViewModel::FNiagaraSystemToolkitParameterPanelViewModel(const TSharedPtr<FNiagaraSystemViewModel>& InSystemViewModel, const TWeakPtr<FNiagaraSystemGraphSelectionViewModel>& InSystemGraphSelectionViewModelWeak)
{
	SystemViewModel = InSystemViewModel;
	SystemGraphSelectionViewModelWeak = InSystemGraphSelectionViewModelWeak;
	SystemScriptGraph = SystemViewModel->GetSystemScriptViewModel()->GetGraphViewModel()->GetGraph();
}

FNiagaraSystemToolkitParameterPanelViewModel::FNiagaraSystemToolkitParameterPanelViewModel(const TSharedPtr<FNiagaraSystemViewModel>& InSystemViewModel)
	: FNiagaraSystemToolkitParameterPanelViewModel(InSystemViewModel, nullptr)
{}

void FNiagaraSystemToolkitParameterPanelViewModel::Cleanup()
{
	UnregisterViewModelWithMap(RegisteredHandle);

	UNiagaraSystem& System = SystemViewModel->GetSystem();

	auto GetGraphFromScript = [](UNiagaraScript* Script)->UNiagaraGraph* {
		return CastChecked<UNiagaraScriptSource>(Script->GetLatestSource())->NodeGraph;
	};

	TSet<UNiagaraGraph*> GraphsToRemoveCallbacks;
	GraphsToRemoveCallbacks.Add(GetGraphFromScript(System.GetSystemSpawnScript()));
	GraphsToRemoveCallbacks.Add(GetGraphFromScript(System.GetSystemUpdateScript()));
	for (TSharedRef<FNiagaraEmitterHandleViewModel> EmitterHandleViewModel : SystemViewModel->GetEmitterHandleViewModels())
	{
		if (FNiagaraEmitterHandle* EmitterHandle = EmitterHandleViewModel->GetEmitterHandle())
		{
			TArray<UNiagaraScript*> EmitterScripts;
			const bool bCompilableOnly = false;
			EmitterHandle->GetInstance()->GetScripts(EmitterScripts, bCompilableOnly);
			for (UNiagaraScript* EmitterScript : EmitterScripts)
			{
				GraphsToRemoveCallbacks.Add(GetGraphFromScript(EmitterScript));
			}
		}
	}

	for (UNiagaraGraph* Graph : GraphsToRemoveCallbacks)
	{
		if (FDelegateHandle* OnGraphChangedHandle = GraphIdToOnGraphChangedHandleMap.Find(Graph->GetUniqueID()))
		{
			Graph->RemoveOnGraphChangedHandler(*OnGraphChangedHandle);
		}
	}

	if (SystemViewModel->GetEditMode() == ENiagaraSystemViewModelEditMode::SystemAsset && SystemGraphSelectionViewModelWeak.IsValid())
	{
		SystemGraphSelectionViewModelWeak.Pin()->GetOnSelectedEmitterScriptGraphsRefreshedDelegate().RemoveAll(this);
	}

	if (SystemViewModel->GetEditMode() == ENiagaraSystemViewModelEditMode::SystemAsset)
	{
		System.GetExposedParameters().RemoveOnChangedHandler(UserParameterStoreChangedHandle);
	}

	SystemViewModel->OnSystemCompiled().RemoveAll(this);
	SystemViewModel->OnParameterRemovedExternally().RemoveAll(this);
	SystemViewModel->OnParameterRenamedExternally().RemoveAll(this);

	SystemViewModel->GetOnSubscribedParameterDefinitionsChangedDelegate().RemoveAll(this);
	SystemViewModel->OnEmitterHandleViewModelsChanged().RemoveAll(this);

}

void FNiagaraSystemToolkitParameterPanelViewModel::Init(const FSystemToolkitUIContext& InUIContext)
{
	UIContext = InUIContext;
	UNiagaraSystem& System = SystemViewModel->GetSystem();

	// Init bindings
	// Bind OnChanged() and OnNeedsRecompile() callbacks for all script source graphs.
	auto GetGraphFromScript = [](UNiagaraScript* Script)->UNiagaraGraph* {
		return CastChecked<UNiagaraScriptSource>(Script->GetLatestSource())->NodeGraph;
	};

	TSet<UNiagaraGraph*> GraphsToAddCallbacks;
	GraphsToAddCallbacks.Add(GetGraphFromScript(System.GetSystemSpawnScript()));
	GraphsToAddCallbacks.Add(GetGraphFromScript(System.GetSystemUpdateScript()));
	for (TSharedRef<FNiagaraEmitterHandleViewModel> EmitterHandleViewModel : SystemViewModel->GetEmitterHandleViewModels())
	{
		if (FNiagaraEmitterHandle* EmitterHandle = EmitterHandleViewModel->GetEmitterHandle())
		{
			TArray<UNiagaraScript*> EmitterScripts;
			const bool bCompilableOnly = false;
			EmitterHandle->GetInstance()->GetScripts(EmitterScripts, bCompilableOnly);
			for (UNiagaraScript* EmitterScript : EmitterScripts)
			{
				GraphsToAddCallbacks.Add(GetGraphFromScript(EmitterScript));
			}
		}
	}

	for (UNiagaraGraph* Graph : GraphsToAddCallbacks)
	{
		FDelegateHandle OnGraphChangedHandle = Graph->AddOnGraphChangedHandler(
			FOnGraphChanged::FDelegate::CreateRaw(this, &FNiagaraSystemToolkitParameterPanelViewModel::OnGraphChanged));
		GraphIdToOnGraphChangedHandleMap.Add(Graph->GetUniqueID(), OnGraphChangedHandle);
	}

	// Bind OnChanged() callback for system edit mode active emitter handle selection changing.
	if (SystemViewModel->GetEditMode() == ENiagaraSystemViewModelEditMode::SystemAsset && ensureMsgf(SystemGraphSelectionViewModelWeak.IsValid(), TEXT("SystemGraphSelectionViewModel was null for System edit mode!")))
	{
		SystemGraphSelectionViewModelWeak.Pin()->GetOnSelectedEmitterScriptGraphsRefreshedDelegate().AddSP(this, &FNiagaraSystemToolkitParameterPanelViewModel::Refresh);
	}

	// Bind OnChanged() for System Exposed Parameters (User Parameters).
	if (SystemViewModel->GetEditMode() == ENiagaraSystemViewModelEditMode::SystemAsset)
	{
		UserParameterStoreChangedHandle = System.GetExposedParameters().AddOnChangedHandler(
			FNiagaraParameterStore::FOnChanged::FDelegate::CreateSP(this, &FNiagaraSystemToolkitParameterPanelViewModel::RefreshNextTick));
	}

	// Bind OnChanged() bindings for compilation and external parameter modifications.
	SystemViewModel->OnSystemCompiled().AddSP(this, &FNiagaraSystemToolkitParameterPanelViewModel::RefreshNextTick);
	SystemViewModel->OnParameterRemovedExternally().AddSP(this, &FNiagaraSystemToolkitParameterPanelViewModel::OnParameterRemovedExternally);
	SystemViewModel->OnParameterRenamedExternally().AddSP(this, &FNiagaraSystemToolkitParameterPanelViewModel::OnParameterRenamedExternally);

	// Bind OnChanged() bindings for parameter definitions.
	SystemViewModel->GetOnSubscribedParameterDefinitionsChangedDelegate().AddSP(this, &FNiagaraSystemToolkitParameterPanelViewModel::Refresh);

	// Bind OnChanged() bindings for emitter handles changing.
	SystemViewModel->OnEmitterHandleViewModelsChanged().AddSP(this, &FNiagaraSystemToolkitParameterPanelViewModel::ReconcileOnGraphChangedBindings);

	// Init default categories
	if (DefaultCategories.Num() == 0)
	{
		TArray<FNiagaraNamespaceMetadata> NamespaceMetadata = GetDefault<UNiagaraEditorSettings>()->GetAllNamespaceMetadata();
		for (const FNiagaraNamespaceMetadata& NamespaceMetadatum : NamespaceMetadata)
		{
			if (NamespaceMetadatum.Options.Contains(ENiagaraNamespaceMetadataOptions::HideInSystem) == false)
			{
				if (NamespaceMetadatum.Options.Contains(ENiagaraNamespaceMetadataOptions::AdvancedInSystem))
				{
					DefaultAdvancedCategories.Add(NamespaceMetadatum);
				}
				else
				{
					DefaultCategories.Add(NamespaceMetadatum);
					DefaultAdvancedCategories.Add(NamespaceMetadatum);
				}
			}
		}
	}

	RegisteredHandle = RegisterViewModelWithMap(&System, this);
}

const TArray<UNiagaraScriptVariable*> FNiagaraSystemToolkitParameterPanelViewModel::GetEditableScriptVariablesWithName(const FName ParameterName) const
{
	TArray<UNiagaraScriptVariable*> EditableScriptVariables;
	for (const UNiagaraGraph* Graph : GetEditableGraphsConst())
	{
		UNiagaraScriptVariable* ParameterScriptVariable = Graph->GetScriptVariable(ParameterName);
		if (ParameterScriptVariable != nullptr)
		{
			EditableScriptVariables.Add(ParameterScriptVariable);
		}
	}
	return EditableScriptVariables;
}

void FNiagaraSystemToolkitParameterPanelViewModel::AddParameter(FNiagaraVariable NewVariable, const FNiagaraParameterPanelCategory Category, const bool bRequestRename, const bool bMakeUniqueName) const
{
	TGuardValue<bool> AddParameterRefreshGuard(bIsAddingParameter, true);
	bool bSuccess = false;
	UNiagaraSystem& System = SystemViewModel->GetSystem();

	FScopedTransaction AddTransaction(LOCTEXT("AddSystemParameterTransaction", "Add parameter to system."));
	System.Modify();
	const FGuid NamespaceId = FNiagaraEditorUtilities::GetNamespaceMetaDataForVariableName(NewVariable.GetName()).GetGuid();
	if (NamespaceId == FNiagaraEditorGuids::UserNamespaceMetaDataGuid)
	{
		bSuccess = FNiagaraEditorUtilities::AddParameter(NewVariable, System.GetExposedParameters(), System, nullptr);
	}
	else
	{
		UNiagaraEditorParametersAdapter* EditorParametersAdapter = SystemViewModel->GetEditorOnlyParametersAdapter();
		TArray<UNiagaraScriptVariable*>& EditorOnlyScriptVars = EditorParametersAdapter->GetParameters();
		bool bNewScriptVarAlreadyExists = EditorOnlyScriptVars.ContainsByPredicate([&NewVariable](const UNiagaraScriptVariable* ScriptVar) { return ScriptVar->Variable == NewVariable; });

		// unless the namespace prevents name changes we make sure the new parameter has a unique name  
		if (bMakeUniqueName && !Category.NamespaceMetaData.Options.Contains(ENiagaraNamespaceMetadataOptions::PreventEditingName))
		{
			TSet<FName> Names;
			for (UNiagaraScriptVariable* ScriptVar : EditorOnlyScriptVars)
			{
				Names.Add(ScriptVar->Variable.GetName());
			}
			NewVariable.SetName(FNiagaraUtilities::GetUniqueName(NewVariable.GetName(), Names));
			bNewScriptVarAlreadyExists = false;
		}

		if (bNewScriptVarAlreadyExists == false)
		{
			EditorParametersAdapter->Modify();
			UNiagaraScriptVariable* NewScriptVar = NewObject<UNiagaraScriptVariable>(EditorParametersAdapter, FName(), RF_Transactional);
			NewScriptVar->Init(NewVariable, FNiagaraVariableMetaData());
			NewScriptVar->SetIsStaticSwitch(false);
			NewScriptVar->SetIsSubscribedToParameterDefinitions(false);
			EditorOnlyScriptVars.Add(NewScriptVar);
			bSuccess = true;

			// Check if the new parameter has the same name and type as an existing parameter definition, and if so, link to the definition automatically.
			SubscribeParameterToLibraryIfMatchingDefinition(NewScriptVar, NewScriptVar->Variable.GetName());
		}
	}

	if (bSuccess)
	{
		Refresh();
		SelectParameterItemByName(NewVariable.GetName(), bRequestRename);
	}
}

bool FNiagaraSystemToolkitParameterPanelViewModel::GetCanAddParametersToCategory(FNiagaraParameterPanelCategory Category) const
{
	return GetEditableGraphsConst().Num() > 0 && Category.NamespaceMetaData.GetGuid() != FNiagaraEditorGuids::StaticSwitchNamespaceMetaDataGuid;
}

void FNiagaraSystemToolkitParameterPanelViewModel::DeleteParameter(const FNiagaraParameterPanelItem& ItemToDelete) const
{
	if (ItemToDelete.bExternallyReferenced)
	{
		return;
	}

	FScopedTransaction RemoveParameter(LOCTEXT("RemoveParameter", "Remove Parameter"));
	UNiagaraSystem& System = SystemViewModel->GetSystem();
	const FGuid& ScriptVarId = ItemToDelete.GetVariableMetaData().GetVariableGuid();
	System.Modify();
	System.GetExposedParameters().RemoveParameter(ItemToDelete.GetVariable());
	UNiagaraEditorParametersAdapter* EditorParametersAdapter = SystemViewModel->GetEditorOnlyParametersAdapter();
	EditorParametersAdapter->Modify();
	EditorParametersAdapter->GetParameters().RemoveAll([&ScriptVarId](const UNiagaraScriptVariable* ScriptVar) { return ScriptVar->Metadata.GetVariableGuid() == ScriptVarId; });

	// Update anything that was referencing that parameter
	System.HandleVariableRemoved(ItemToDelete.GetVariable(), true);
	Refresh();
}

void FNiagaraSystemToolkitParameterPanelViewModel::RenameParameter(const FNiagaraParameterPanelItem& ItemToRename, const FName NewName) const
{
	if (ensureMsgf(ItemToRename.bExternallyReferenced == false, TEXT("Can not modify an externally referenced parameter.")) == false)
	{
		return;
	}
	else if (ItemToRename.GetVariable().GetName() == NewName)
	{
		return;
	}

	FScopedTransaction RenameParameterTransaction(LOCTEXT("RenameParameter", "Rename parameter"));
	const FNiagaraVariable Parameter = ItemToRename.GetVariable();
	const FGuid& ScriptVarId = ItemToRename.GetVariableMetaData().GetVariableGuid();
	UNiagaraSystem& System = SystemViewModel->GetSystem();
	System.Modify();

	bool bExposedParametersRename = false;
	bool bEditorOnlyParametersRename = false;
	bool bAssignmentNodeRename = false;

	// Rename the parameter if it's in the parameter store for user parameters.
	if (System.GetExposedParameters().IndexOf(Parameter) != INDEX_NONE)
	{
		FNiagaraParameterStore* ExposedParametersStore = &System.GetExposedParameters();
		TArray<FNiagaraVariable> OwningParameters;
		ExposedParametersStore->GetParameters(OwningParameters);
		if (OwningParameters.ContainsByPredicate([NewName](const FNiagaraVariable& Variable) { return Variable.GetName() == NewName; }))
		{
			// If the parameter store already has a parameter with this name, remove the old parameter to prevent collisions.
			ExposedParametersStore->RemoveParameter(Parameter);
		}
		else
		{
			// Otherwise it's safe to rename.
			ExposedParametersStore->RenameParameter(Parameter, NewName);
		}
		bExposedParametersRename = true;
	}

	// Look for set parameters nodes or linked inputs which reference this parameter and rename if so.
	for (const FNiagaraGraphParameterReference& ParameterReference : GetGraphParameterReferencesForItem(ItemToRename))
	{
		UNiagaraNode* ReferenceNode = Cast<UNiagaraNode>(ParameterReference.Value);
		if (ReferenceNode != nullptr)
		{
			UNiagaraNodeAssignment* OwningAssignmentNode = ReferenceNode->GetTypedOuter<UNiagaraNodeAssignment>();
			if (OwningAssignmentNode != nullptr)
			{
				// If this is owned by a set variables node and it's not locked, update the assignment target on the assignment node.
				bAssignmentNodeRename |= FNiagaraStackGraphUtilities::TryRenameAssignmentTarget(*OwningAssignmentNode, Parameter, NewName);
			}
			else
			{
				// Otherwise if the reference node is a get node it's for a linked input so we can just update pin name.
				UNiagaraNodeParameterMapGet* ReferenceGetNode = Cast<UNiagaraNodeParameterMapGet>(ReferenceNode);
				if (ReferenceGetNode != nullptr)
				{
					if (ReferenceGetNode->Pins.ContainsByPredicate([&ParameterReference](UEdGraphPin* Pin) { return Pin->PersistentGuid == ParameterReference.Key; }))
					{
						ReferenceGetNode->GetNiagaraGraph()->RenameParameter(Parameter, NewName, true);
					}
				}
			}
		}
	}

	// Rename the parameter if it is owned directly as an editor only parameter.
	UNiagaraEditorParametersAdapter* EditorParametersAdapter = SystemViewModel->GetEditorOnlyParametersAdapter();
	if (UNiagaraScriptVariable** ScriptVariablePtr = EditorParametersAdapter->GetParameters().FindByPredicate([&ScriptVarId](const UNiagaraScriptVariable* ScriptVariable) { return ScriptVariable->Metadata.GetVariableGuid() == ScriptVarId; }))
	{
		EditorParametersAdapter->Modify();
		UNiagaraScriptVariable* ScriptVariable = *ScriptVariablePtr;
		ScriptVariable->Modify();
		ScriptVariable->Variable.SetName(NewName);
		ScriptVariable->UpdateChangeId();
		bEditorOnlyParametersRename = true;
	}

	// Check if the rename will give the same name and type as an existing parameter definition, and if so, link to the definition automatically.
	SubscribeParameterToLibraryIfMatchingDefinition(ItemToRename.ScriptVariable, NewName);

	// Handle renaming any renderer properties that might match.
	if (bExposedParametersRename | bEditorOnlyParametersRename | bAssignmentNodeRename)
	{
		if (Parameter.IsInNameSpace(FNiagaraConstants::ParticleAttributeNamespace) || Parameter.IsInNameSpace(FNiagaraConstants::EmitterNamespace))
		{
			for (const FNiagaraEmitterHandle* EmitterHandle : GetEditableEmitterHandles())
			{
				EmitterHandle->GetInstance()->HandleVariableRenamed(Parameter, FNiagaraVariableBase(Parameter.GetType(), NewName), true);
			}		
		}
		else
		{
			System.HandleVariableRenamed(Parameter, FNiagaraVariableBase(Parameter.GetType(), NewName), true);
		}

		Refresh();
		const bool bRequestRename = false;
		SelectParameterItemByName(NewName, bRequestRename);
	}
}

void FNiagaraSystemToolkitParameterPanelViewModel::SetParameterIsSubscribedToLibrary(const UNiagaraScriptVariable* ScriptVarToModify, const bool bSubscribed) const
{
	const FText TransactionText = bSubscribed ? LOCTEXT("SubscribeParameter", "Subscribe parameter") : LOCTEXT("UnsubscribeParameter", "Unsubscribe parameter");
	FScopedTransaction SubscribeTransaction(TransactionText);
	SystemViewModel->SetParameterIsSubscribedToDefinitions(ScriptVarToModify->Metadata.GetVariableGuid(), bSubscribed);
	Refresh();
	UIContext.RefreshParameterDefinitionsPanel();
}

FReply FNiagaraSystemToolkitParameterPanelViewModel::OnParameterItemsDragged(const TArray<FNiagaraParameterPanelItem>& DraggedItems, const FPointerEvent& MouseEvent) const
{
	if (OnGetParametersWithNamespaceModifierRenamePendingDelegate.IsBound() == false)
	{
		ensureMsgf(false, TEXT("OnGetParametersWithNamespaceModifierRenamePendingDelegate was not bound when handling parameter drag in parameter panel view model! "));
		return FReply::Handled();
	}

	if(DraggedItems.Num() == 1)
	{ 
		const FNiagaraParameterPanelItem& DraggedItem = DraggedItems[0];
		return FNiagaraSystemToolkitParameterPanelUtilities::CreateDragEventForParameterItem(
			DraggedItem,
			MouseEvent,
			GetGraphParameterReferencesForItem(DraggedItem),
			OnGetParametersWithNamespaceModifierRenamePendingDelegate.Execute()
		);
	}

	return FReply::Handled();
}

TSharedPtr<SWidget> FNiagaraSystemToolkitParameterPanelViewModel::CreateContextMenuForItems(const TArray<FNiagaraParameterPanelItem>& Items, const TSharedPtr<FUICommandList>& ToolkitCommands)
{
	// Only create context menus when a single item is selected.
	if (Items.Num() == 1)
	{
		const FNiagaraParameterPanelItem& SelectedItem = Items[0];
		if (SelectedItem.ScriptVariable->GetIsStaticSwitch())
		{
			// Static switches do not have context menu actions for System Toolkits.
			return SNullWidget::NullWidget;
		}

		// Create a menu with all relevant operations.
		const bool bShouldCloseWindowAfterMenuSelection = true;
		FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, ToolkitCommands);
		MenuBuilder.BeginSection("Edit", LOCTEXT("EditMenuHeader", "Edit"));
		{
			FText CopyReferenceToolTip;
			GetCanCopyParameterReferenceAndToolTip(SelectedItem, CopyReferenceToolTip);
			MenuBuilder.AddMenuEntry(FGenericCommands::Get().Copy, NAME_None, LOCTEXT("CopyReference", "Copy Reference"), CopyReferenceToolTip);

			FText DeleteToolTip;
			GetCanDeleteParameterAndToolTip(SelectedItem, DeleteToolTip);
			MenuBuilder.AddMenuEntry(FNiagaraParameterPanelCommands::Get().DeleteItem, NAME_None, TAttribute<FText>(), DeleteToolTip);

			FText RenameToolTip;
			GetCanRenameParameterAndToolTip(SelectedItem, FText(), false, RenameToolTip);
			MenuBuilder.AddMenuEntry(FGenericCommands::Get().Rename, NAME_None, LOCTEXT("Rename", "Rename"), RenameToolTip);


			MenuBuilder.AddMenuSeparator();

			MenuBuilder.AddSubMenu(
				LOCTEXT("ChangeNamespace", "Change Namespace"),
				LOCTEXT("ChangeNamespaceToolTip", "Select a new namespace for the selected parameter."),
				FNewMenuDelegate::CreateSP(this, &FNiagaraSystemToolkitParameterPanelViewModel::GetChangeNamespaceSubMenu, false, SelectedItem));

			MenuBuilder.AddSubMenu(
				LOCTEXT("ChangeNamespaceModifier", "Change Namespace Modifier"),
				LOCTEXT("ChangeNamespaceModifierToolTip", "Edit the namespace modifier for the selected parameter."),
				FNewMenuDelegate::CreateSP(this, &FNiagaraSystemToolkitParameterPanelViewModel::GetChangeNamespaceModifierSubMenu, false, SelectedItem));
			

			MenuBuilder.AddMenuSeparator();

			FText DuplicateToolTip;
			bool bCanDuplicateParameter = GetCanDuplicateParameterAndToolTip(SelectedItem, DuplicateToolTip);
			MenuBuilder.AddMenuEntry(
				LOCTEXT("DuplicateParameter", "Duplicate"),
				DuplicateToolTip,
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSP(this, &FNiagaraSystemToolkitParameterPanelViewModel::DuplicateParameter, SelectedItem),
					FCanExecuteAction::CreateLambda([bCanDuplicateParameter](){return bCanDuplicateParameter;})));

			MenuBuilder.AddSubMenu(
				LOCTEXT("DuplicateToNewNamespace", "Duplicate to Namespace"),
				LOCTEXT("DuplicateToNewNamespaceToolTip", "Duplicate this parameter to a new namespace."),
				FNewMenuDelegate::CreateSP(this, &FNiagaraSystemToolkitParameterPanelViewModel::GetChangeNamespaceSubMenu, true, SelectedItem));

			MenuBuilder.AddSubMenu(
				LOCTEXT("DuplicateWithNewNamespaceModifier", "Duplicate with Namespace Modifier"),
				LOCTEXT("DupilcateWithNewNamespaceModifierToolTip", "Duplicate this parameter with a different namespace modifier."),
				FNewMenuDelegate::CreateSP(this, &FNiagaraSystemToolkitParameterPanelViewModel::GetChangeNamespaceModifierSubMenu, true, SelectedItem));
		}
		MenuBuilder.EndSection();
		return MenuBuilder.MakeWidget();
	}
	// More than one item selected, do not return a context menu.
	return SNullWidget::NullWidget;
}

FNiagaraParameterUtilities::EParameterContext FNiagaraSystemToolkitParameterPanelViewModel::GetParameterContext() const
{
	return FNiagaraParameterUtilities::EParameterContext::System;
}

const TArray<FNiagaraParameterPanelCategory>& FNiagaraSystemToolkitParameterPanelViewModel::GetDefaultCategories() const
{
//	const UNiagaraEditorSettings* NiagaraEditorSettings = GetDefault<UNiagaraEditorSettings>();
//	const bool bShowAdvanced = NiagaraEditorSettings->GetDisplayAdvancedParameterPanelCategories();
// 	if (bShowAdvanced)
// 	{
// 		CachedCurrentCategories = FNiagaraSystemToolkitParameterPanelViewModel::DefaultAdvancedCategories;
// 		return FNiagaraSystemToolkitParameterPanelViewModel::DefaultAdvancedCategories;
// 	}
	// For now always show advanced.
	CachedCurrentCategories = FNiagaraSystemToolkitParameterPanelViewModel::DefaultAdvancedCategories;
	return FNiagaraSystemToolkitParameterPanelViewModel::DefaultAdvancedCategories;
}

FMenuAndSearchBoxWidgets FNiagaraSystemToolkitParameterPanelViewModel::GetParameterMenu(FNiagaraParameterPanelCategory Category) const
{
	const bool bRequestRename = true;
	const bool bSkipSubscribedLibraries = false;
	const bool bMakeUniqueName = true;

	// Collect names of all parameters the System already owns to cull from the parameter add menu.
	TSet<FName> AdditionalCulledParameterNames;
	for (UNiagaraScriptVariable* EditorOnlyScriptVar : SystemViewModel->GetEditorOnlyParametersAdapter()->GetParameters())
	{
		AdditionalCulledParameterNames.Add(EditorOnlyScriptVar->Variable.GetName());
	}

	TSharedPtr<SNiagaraAddParameterFromPanelMenu> MenuWidget = SAssignNew(ParameterMenuWidget, SNiagaraAddParameterFromPanelMenu)
		.Graphs(GetEditableGraphsConst())
		.AvailableParameterDefinitions(SystemViewModel->GetAvailableParameterDefinitions(bSkipSubscribedLibraries))
		.SubscribedParameterDefinitions(SystemViewModel->GetSubscribedParameterDefinitions())
		.OnAddParameter(this, &FNiagaraSystemToolkitParameterPanelViewModel::AddParameter, Category, bRequestRename, bMakeUniqueName)
		.OnAddScriptVar(this, &FNiagaraSystemToolkitParameterPanelViewModel::AddScriptVariable)
		.OnAddParameterDefinitions(this, &FNiagaraSystemToolkitParameterPanelViewModel::AddParameterDefinitions)
		.OnAllowMakeType_Static(&INiagaraParameterPanelViewModel::CanMakeNewParameterOfType)
		.NamespaceId(Category.NamespaceMetaData.GetGuid())
		.ShowNamespaceCategory(false)
		.ShowGraphParameters(false)
		.AutoExpandMenu(false)
		.AdditionalCulledParameterNames(AdditionalCulledParameterNames);

	ParameterMenuSearchBoxWidget = MenuWidget->GetSearchBox();
	FMenuAndSearchBoxWidgets MenuAndSearchBoxWidgets;
	MenuAndSearchBoxWidgets.MenuWidget = MenuWidget;
	MenuAndSearchBoxWidgets.MenuSearchBoxWidget = ParameterMenuSearchBoxWidget;
	return MenuAndSearchBoxWidgets;
}

FReply FNiagaraSystemToolkitParameterPanelViewModel::HandleDragDropOperation(TSharedPtr<FDragDropOperation> DragDropOperation) const
{
	TSharedPtr<FNiagaraParameterGraphDragOperation> ParameterGraphDragDropOperation = StaticCastSharedPtr<FNiagaraParameterGraphDragOperation>(DragDropOperation);
	if (ParameterGraphDragDropOperation.IsValid() == false)
	{
		return FReply::Handled();
	}

	TSharedPtr<FEdGraphSchemaAction> SourceAction = ParameterGraphDragDropOperation->GetSourceAction();
	if (SourceAction.IsValid() == false)
	{
		return FReply::Handled();
	}

	TSharedPtr<FNiagaraParameterAction> SourceParameterAction = StaticCastSharedPtr<FNiagaraParameterAction>(SourceAction);
	if (SourceParameterAction.IsValid() == false)
	{
		return FReply::Handled();
	}

	AddScriptVariable(SourceParameterAction->GetScriptVar());
	return FReply::Handled();
}

bool FNiagaraSystemToolkitParameterPanelViewModel::GetCanHandleDragDropOperation(TSharedPtr<FDragDropOperation> DragDropOperation) const
{
	if (DragDropOperation->IsOfType<FNiagaraParameterGraphDragOperation>() == false)
	{
		return false;
	}
	TSharedPtr<FNiagaraParameterGraphDragOperation> ParameterGraphDragDropOperation = StaticCastSharedPtr<FNiagaraParameterGraphDragOperation>(DragDropOperation);

	const TSharedPtr<FEdGraphSchemaAction>& SourceAction = ParameterGraphDragDropOperation->GetSourceAction();
	if (SourceAction.IsValid() == false)
	{
		return false;
	}

	if (SourceAction->GetTypeId() != FNiagaraEditorStrings::FNiagaraParameterActionId)
	{
		return false;
	}
	const TSharedPtr<FNiagaraParameterAction>& SourceParameterAction = StaticCastSharedPtr<FNiagaraParameterAction>(SourceAction);

	const UNiagaraScriptVariable* ScriptVar = SourceParameterAction->GetScriptVar();
	if (ScriptVar == nullptr)
	{
		return false;
	}

	// Do not allow trying to create a new parameter from the drop action if that parameter name/type pair already exists.
	const FNiagaraVariable& Parameter = ScriptVar->Variable;
	if (SystemViewModel->GetAllScriptVars().ContainsByPredicate([Parameter](const UNiagaraScriptVariable* ScriptVar) { return ScriptVar->Variable == Parameter; }))
	{
		return false;
	}

	return true;
}

TSharedRef<SWidget> FNiagaraSystemToolkitParameterPanelViewModel::CreateAddParameterMenuForAssignmentNode(UNiagaraNodeAssignment* AssignmentNode, const TSharedPtr<SComboButton>& AddButton) const
{
	auto AddParameterLambda = [this, AssignmentNode](FNiagaraVariable& NewParameter) {
		// If an assignment target is already setting the associated parameter, make a unique name here so that the assignment target is valid.
		// Because the unique name is made before calling FNiagaraSystemToolkitParameterPanelViewModel::AddParameter, pass bMakeUniqueName = false.
		if (AssignmentNode->GetAssignmentTargets().Contains(NewParameter))
		{
			TSet<FName> Names;
			UNiagaraEditorParametersAdapter* EditorParametersAdapter = SystemViewModel->GetEditorOnlyParametersAdapter();
			TArray<UNiagaraScriptVariable*>& EditorOnlyScriptVars = EditorParametersAdapter->GetParameters();
			for (UNiagaraScriptVariable* ScriptVar : EditorOnlyScriptVars)
			{
				Names.Add(ScriptVar->Variable.GetName());
			}
			NewParameter.SetName(FNiagaraUtilities::GetUniqueName(NewParameter.GetName(), Names));
		}

		const FString VarDefaultValue = FNiagaraConstants::GetAttributeDefaultValue(NewParameter);
		FNiagaraParameterPanelCategory TempCategory = FNiagaraParameterPanelCategory(FNiagaraEditorUtilities::GetNamespaceMetaDataForVariableName(NewParameter.GetName()));
		const bool bRequestRename = true;
		const bool bMakeUniqueName = false;
		AddParameter(NewParameter, TempCategory, bRequestRename, bMakeUniqueName);
		AssignmentNode->AddParameter(NewParameter, VarDefaultValue);
	};

	auto AddScriptVarLambda = [this, AssignmentNode](const UNiagaraScriptVariable* NewScriptVar) {
		const FString VarDefaultValue = FNiagaraConstants::GetAttributeDefaultValue(NewScriptVar->Variable);
		AddScriptVariable(NewScriptVar);
		AssignmentNode->AddParameter(NewScriptVar->Variable, VarDefaultValue);
	};

	// Collect args for add menu widget construct
	TArray<UNiagaraGraph*> InGraphs = { AssignmentNode->GetNiagaraGraph() };
	UNiagaraSystem* OwningSystem = AssignmentNode->GetTypedOuter<UNiagaraSystem>();

	const bool bSkipSubscribedLibraries = false;
	const bool bIsParameterRead = true;
	FGuid NamespaceId;

	// Collect names of all parameters the assignment node already assigns to cull from the parameter add menu.
	TSet<FName> AdditionalCulledParameterNames;
	for (const FNiagaraVariable& AssignmentTargetVar : AssignmentNode->GetAssignmentTargets())
	{
		AdditionalCulledParameterNames.Add(AssignmentTargetVar.GetName());
	}

	TSharedRef<SNiagaraAddParameterFromPanelMenu> MenuWidget = SNew(SNiagaraAddParameterFromPanelMenu)
		.Graphs(InGraphs)
		.AvailableParameterDefinitions(SystemViewModel->GetAvailableParameterDefinitions(bSkipSubscribedLibraries))
		.SubscribedParameterDefinitions(SystemViewModel->GetSubscribedParameterDefinitions())
		.OnAddParameter_Lambda(AddParameterLambda)
		.OnAddScriptVar_Lambda(AddScriptVarLambda)
		.OnAddParameterDefinitions(this, &FNiagaraSystemToolkitParameterPanelViewModel::AddParameterDefinitions)
		.OnAllowMakeType_Static(&INiagaraParameterPanelViewModel::CanMakeNewParameterOfType)
		.AllowCreatingNew(true)
		.NamespaceId(FNiagaraEditorUtilities::GetNamespaceIdForUsage(FNiagaraStackGraphUtilities::GetOutputNodeUsage(*AssignmentNode)))
		.ShowNamespaceCategory(false)
		.ShowGraphParameters(false)
		.AutoExpandMenu(false)
		.AdditionalCulledParameterNames(AdditionalCulledParameterNames)
		.AssignmentNode(AssignmentNode);

	AddButton->SetMenuContentWidgetToFocus(MenuWidget->GetSearchBox());
	return MenuWidget;
}

TArray<FNiagaraVariable> FNiagaraSystemToolkitParameterPanelViewModel::GetEditableStaticSwitchParameters() const
{
	TArray<FNiagaraVariable> OutStaticSwitchParameters;
	for (const UNiagaraGraph* Graph : GetEditableGraphsConst())
	{
		OutStaticSwitchParameters.Append(Graph->FindStaticSwitchInputs());
	}
	return OutStaticSwitchParameters;
}

const TArray<FNiagaraGraphParameterReference> FNiagaraSystemToolkitParameterPanelViewModel::GetGraphParameterReferencesForItem(const FNiagaraParameterPanelItemBase& Item) const
{
	const FNiagaraParameterPanelItem& ParameterPanelItem = static_cast<const FNiagaraParameterPanelItem&>(Item);
	bool bGetReferencesAcrossAllGraphs = ParameterPanelItem.bExternallyReferenced == false;
	const TArray<UNiagaraGraph*> TargetGraphs = bGetReferencesAcrossAllGraphs ? GetAllGraphsConst() : GetEditableGraphsConst();

	// -For each selected graph perform a parameter map history traversal and collect all graph parameter references associated with the target FNiagaraParameterPanelItem.
	TArray<FNiagaraGraphParameterReference> GraphParameterReferences;
	for (const UNiagaraGraph* Graph : TargetGraphs)
	{
		TArray<UNiagaraNodeOutput*> OutputNodes;
		Graph->GetNodesOfClass<UNiagaraNodeOutput>(OutputNodes);
		for (UNiagaraNodeOutput* OutputNode : OutputNodes)
		{
			UNiagaraNode* NodeToTraverse = OutputNode;
			if (OutputNode->GetUsage() == ENiagaraScriptUsage::SystemSpawnScript || OutputNode->GetUsage() == ENiagaraScriptUsage::SystemUpdateScript)
			{
				// Traverse past the emitter nodes, otherwise the system scripts will pick up all of the emitter and particle script parameters.
				UEdGraphPin* InputPin = FNiagaraStackGraphUtilities::GetParameterMapInputPin(*NodeToTraverse);
				while (NodeToTraverse != nullptr && InputPin != nullptr && InputPin->LinkedTo.Num() == 1 &&
					(NodeToTraverse->IsA<UNiagaraNodeOutput>() || NodeToTraverse->IsA<UNiagaraNodeEmitter>()))
				{
					NodeToTraverse = Cast<UNiagaraNode>(InputPin->LinkedTo[0]->GetOwningNode());
					InputPin = NodeToTraverse != nullptr ? FNiagaraStackGraphUtilities::GetParameterMapInputPin(*NodeToTraverse) : nullptr;
				}
			}

			if (NodeToTraverse == nullptr)
			{
				continue;
			}

			bool bIgnoreDisabled = true;
			FNiagaraParameterMapHistoryBuilder Builder;
			UNiagaraEmitter* GraphOwningEmitter = Graph->GetTypedOuter<UNiagaraEmitter>();
			FCompileConstantResolver ConstantResolver = GraphOwningEmitter != nullptr
				? FCompileConstantResolver(GraphOwningEmitter, ENiagaraScriptUsage::Function)
				: FCompileConstantResolver();

			Builder.SetIgnoreDisabled(bIgnoreDisabled);
			Builder.ConstantResolver = ConstantResolver;
			FName StageName;
			ENiagaraScriptUsage StageUsage = OutputNode->GetUsage();
			if (StageUsage == ENiagaraScriptUsage::ParticleSimulationStageScript && GraphOwningEmitter)
			{
				UNiagaraSimulationStageBase* Base = GraphOwningEmitter->GetSimulationStageById(OutputNode->GetUsageId());
				if (Base)
				{
					StageName = Base->GetStackContextReplacementName();
				}
			}
			Builder.BeginUsage(StageUsage, StageName);
			NodeToTraverse->BuildParameterMapHistory(Builder, true, false);
			Builder.EndUsage();

			if (Builder.Histories.Num() != 1)
			{
				// We should only have traversed one emitter (have not visited more than one NiagaraNodeEmitter.)
				ensureMsgf(false, TEXT("Encountered more than one parameter map history when collecting graph parameter reference collections for system parameter panel view model!"));
			}

			const TArray<FName>& CustomIterationSourceNamespaces = Builder.Histories[0].IterationNamespaceOverridesEncountered;
			for (int32 VariableIndex = 0; VariableIndex < Builder.Histories[0].Variables.Num(); VariableIndex++)
			{
				if (Item.GetVariable() == Builder.Histories[0].Variables[VariableIndex])
				{
					for (const FNiagaraParameterMapHistory::FReadHistory& ReadHistory : Builder.Histories[0].PerVariableReadHistory[VariableIndex])
					{
						if (ReadHistory.ReadPin.Pin->GetOwningNode() != nullptr)
						{
							GraphParameterReferences.Add(FNiagaraGraphParameterReference(ReadHistory.ReadPin.Pin->PersistentGuid, ReadHistory.ReadPin.Pin->GetOwningNode()));
						}
					}

					for (const FModuleScopedPin& Write : Builder.Histories[0].PerVariableWriteHistory[VariableIndex])
					{
						if (Write.Pin->GetOwningNode() != nullptr)
						{
							GraphParameterReferences.Add(FNiagaraGraphParameterReference(Write.Pin->PersistentGuid, Write.Pin->GetOwningNode()));
						}
					}
				}
			}
		}
	}
	return GraphParameterReferences;
}

const TArray<UNiagaraParameterDefinitions*> FNiagaraSystemToolkitParameterPanelViewModel::GetAvailableParameterDefinitions(bool bSkipSubscribedParameterDefinitions) const
{
	return SystemViewModel->GetAvailableParameterDefinitions(bSkipSubscribedParameterDefinitions);
}

const TArray<UNiagaraGraph*> FNiagaraSystemToolkitParameterPanelViewModel::GetAllGraphsConst() const
{
	return FNiagaraSystemToolkitParameterPanelUtilities::GetAllGraphs(SystemViewModel);
}

TArray<UNiagaraGraph*> FNiagaraSystemToolkitParameterPanelViewModel::GetEditableGraphs() const
{
	return FNiagaraSystemToolkitParameterPanelUtilities::GetEditableGraphs(SystemViewModel, SystemGraphSelectionViewModelWeak);
}

TArray<FNiagaraParameterPanelItem> FNiagaraSystemToolkitParameterPanelViewModel::GetViewedParameterItems() const
{
	// On the first time opening the parameter panel view model we are not guaranteed to call GetDefaultCategories() before GetViewedParameterItems(). 
	// We require CachedCurrentCategories being set as this is used to filter out parameter items that are being viewed. If CachedCurrentCategories 
	// is not set, call GetDefaultCategories() to initialize it. 
	if (CachedCurrentCategories.Num() == 0)
	{
		GetDefaultCategories();
	}

	TMap<FNiagaraVariable, FNiagaraParameterPanelItem> VisitedParameterToItemMap;
	TMap<FNiagaraVariable, UNiagaraScriptVariable*> ParameterToScriptVariableMap;
	const TArray<UNiagaraGraph*> EditableGraphs = GetEditableGraphsConst();
	const TArray<UNiagaraGraph*> AllGraphs = GetAllGraphsConst();
	const UNiagaraReservedParametersManager* ReservedParametersManager = FNiagaraEditorModule::Get().GetReservedParametersManager();

	// Collect all metadata to be packaged with the FNiagaraParameterPanelItems.
	for (const UNiagaraGraph* EditableGraph : EditableGraphs)
	{
		ParameterToScriptVariableMap.Append(EditableGraph->GetAllMetaData());
	}
	ParameterToScriptVariableMap.Append(TransientParameterToScriptVarMap);

	// Helper lambda to get all FNiagaraVariable parameters from a UNiagaraParameterCollection as FNiagaraParameterPanelItemArgs.
	auto CollectParamStore = [this, &ParameterToScriptVariableMap, &VisitedParameterToItemMap, ReservedParametersManager](const FNiagaraParameterStore* ParamStore){
		TArray<FNiagaraVariable> Vars;
		ParamStore->GetParameters(Vars);
		for (const FNiagaraVariable& Var : Vars)
		{
			UNiagaraScriptVariable* ScriptVar;
			if (UNiagaraScriptVariable* const* ScriptVarPtr = ParameterToScriptVariableMap.Find(Var))
			{
				ScriptVar = *ScriptVarPtr;
			}
			else
			{
				// Create a new UNiagaraScriptVariable to represent this parameter for the lifetime of the ParameterPanelViewModel.
				ScriptVar = NewObject<UNiagaraScriptVariable>(GetTransientPackage());
				ScriptVar->AddToRoot();
				ScriptVar->Init(Var, FNiagaraVariableMetaData());
				TransientParameterToScriptVarMap.Add(Var, ScriptVar);
			}

			FNiagaraParameterPanelItem Item = FNiagaraParameterPanelItem();
			Item.ScriptVariable = ScriptVar;
			Item.NamespaceMetaData = FNiagaraEditorUtilities::GetNamespaceMetaDataForVariableName(Var.GetName());
			Item.bExternallyReferenced = false;
			Item.bSourcedFromCustomStackContext = false;
			Item.ReferenceCount = 0;
			
			// Determine whether the item is name aliasing a parameter definitions's parameter.
			Item.DefinitionMatchState = ReservedParametersManager->GetDefinitionMatchStateForParameter(ScriptVar->Variable);

			VisitedParameterToItemMap.Add(Var, Item);
		}
	};

	// Collect user parameters from system.
	CollectParamStore(&SystemViewModel->GetSystem().GetExposedParameters());

	// Collect parameters added to the system asset.
	for (UNiagaraScriptVariable* EditorOnlyScriptVar : SystemViewModel->GetEditorOnlyParametersAdapter()->GetParameters())
	{
		ParameterToScriptVariableMap.Add(EditorOnlyScriptVar->Variable, EditorOnlyScriptVar);

		const FNiagaraVariable& Var = EditorOnlyScriptVar->Variable;
		FNiagaraParameterPanelItem Item = FNiagaraParameterPanelItem();
		Item.ScriptVariable = EditorOnlyScriptVar;
		Item.NamespaceMetaData = FNiagaraEditorUtilities::GetNamespaceMetaDataForVariableName(Var.GetName());
		Item.bExternallyReferenced = false;
		Item.bSourcedFromCustomStackContext = false;
		Item.ReferenceCount = 0;

		// Determine whether the item is name aliasing a parameter definitions's parameter.
		Item.DefinitionMatchState = ReservedParametersManager->GetDefinitionMatchStateForParameter(EditorOnlyScriptVar->Variable);

		VisitedParameterToItemMap.Add(Var, Item);
	}

	// Collect parameters for all emitters.
	TArray<FNiagaraVariable> VisitedInvalidParameters;

	// -For each selected graph perform a parameter map history traversal and record each unique visited parameter.
	for (const UNiagaraGraph* EditableGraph : EditableGraphs)
	{
		TArray<UNiagaraNodeOutput*> OutputNodes;
		EditableGraph->GetNodesOfClass<UNiagaraNodeOutput>(OutputNodes);
		for (UNiagaraNodeOutput* OutputNode : OutputNodes)
		{
			UNiagaraNode* NodeToTraverse = OutputNode;
			if (OutputNode->GetUsage() == ENiagaraScriptUsage::SystemSpawnScript || OutputNode->GetUsage() == ENiagaraScriptUsage::SystemUpdateScript)
			{
				// Traverse past the emitter nodes, otherwise the system scripts will pick up all of the emitter and particle script parameters.
				UEdGraphPin* InputPin = FNiagaraStackGraphUtilities::GetParameterMapInputPin(*NodeToTraverse);
				while (NodeToTraverse != nullptr && InputPin != nullptr && InputPin->LinkedTo.Num() == 1 &&
					(NodeToTraverse->IsA<UNiagaraNodeOutput>() || NodeToTraverse->IsA<UNiagaraNodeEmitter>()))
				{
					NodeToTraverse = Cast<UNiagaraNode>(InputPin->LinkedTo[0]->GetOwningNode());
					InputPin = NodeToTraverse != nullptr ? FNiagaraStackGraphUtilities::GetParameterMapInputPin(*NodeToTraverse) : nullptr;
				}
			}

			if (NodeToTraverse == nullptr)
			{
				continue;
			}

			bool bIgnoreDisabled = true;
			FNiagaraParameterMapHistoryBuilder Builder;
			UNiagaraEmitter* GraphOwningEmitter = EditableGraph->GetTypedOuter<UNiagaraEmitter>();
			FCompileConstantResolver ConstantResolver = GraphOwningEmitter != nullptr
				? FCompileConstantResolver(GraphOwningEmitter, OutputNode->GetUsage())
				: FCompileConstantResolver();
				 
			Builder.SetIgnoreDisabled(bIgnoreDisabled);
			Builder.ConstantResolver = ConstantResolver;
			FName StageName;
			ENiagaraScriptUsage StageUsage = OutputNode->GetUsage();
			if (StageUsage == ENiagaraScriptUsage::ParticleSimulationStageScript && GraphOwningEmitter)
			{
				UNiagaraSimulationStageBase* Base = GraphOwningEmitter->GetSimulationStageById(OutputNode->GetUsageId());
				if (Base)
				{
					StageName = Base->GetStackContextReplacementName();
				}
			}
			Builder.BeginUsage(StageUsage, StageName);
			NodeToTraverse->BuildParameterMapHistory(Builder, true, true);
			Builder.EndUsage();

			if (Builder.Histories.Num() != 1)
			{
				// We should only have traversed one emitter (have not visited more than one NiagaraNodeEmitter.)
				ensureMsgf(false, TEXT("Encountered more than one parameter map history when collecting parameters for system parameter panel view model!"));
			}
	
			// Get all UNiagaraScriptVariables of visited graphs in the ParameterToScriptVariableMap so that generated items are in sync.
			TSet<UNiagaraGraph*> VisitedExternalGraphs;
			for (const UEdGraphPin* MapPin : Builder.Histories[0].MapPinHistory)
			{
				const UNiagaraNodeFunctionCall* MapPinOuterFunctionCallNode = Cast<UNiagaraNodeFunctionCall>(MapPin->GetOuter());
				if (MapPinOuterFunctionCallNode != nullptr)
				{
					UNiagaraGraph* VisitedExternalGraph = MapPinOuterFunctionCallNode->GetCalledGraph();
					if (VisitedExternalGraph != nullptr)
					{
						VisitedExternalGraphs.Add(VisitedExternalGraph);
					}
				}
			}
			for (const UNiagaraGraph* VisitedExternalGraph : VisitedExternalGraphs)
			{
				ParameterToScriptVariableMap.Append(VisitedExternalGraph->GetAllMetaData());
			}

			const TArray<FName>& CustomIterationSourceNamespaces = Builder.Histories[0].IterationNamespaceOverridesEncountered;
			for (int32 VariableIndex = 0; VariableIndex < Builder.Histories[0].Variables.Num(); VariableIndex++)
			{
				const FNiagaraVariable& Var = Builder.Histories[0].Variables[VariableIndex];
				// If this variable has already been visited and does not have a valid namespace then skip it.
				if (VisitedInvalidParameters.Contains(Var))
				{
					continue;
				}

				if(FNiagaraParameterPanelItem* ItemPtr = VisitedParameterToItemMap.Find(Var))
				{
					// This variable has already been registered, increment the reference count.
					ItemPtr->ReferenceCount += Builder.Histories[0].PerVariableReadHistory[VariableIndex].Num() + Builder.Histories[0].PerVariableWriteHistory[VariableIndex].Num();
				}
				else
				{
					// This variable has not been registered, prepare the FNiagaraParameterPanelItem.
					// -First make sure the variable namespace is in a valid category. If not, skip it.
					FNiagaraNamespaceMetadata CandidateNamespaceMetaData = FNiagaraEditorUtilities::GetNamespaceMetaDataForVariableName(Var.GetName());
					if (CachedCurrentCategories.Contains(FNiagaraParameterPanelCategory(CandidateNamespaceMetaData)) == false)
					{
						VisitedInvalidParameters.Add(Var);
						continue;
					}

					// -Lookup the script variable.
					UNiagaraScriptVariable* const* ScriptVarPtr = ParameterToScriptVariableMap.Find(Var);
					UNiagaraScriptVariable* ScriptVar;
					if (ScriptVarPtr != nullptr)
					{
						ScriptVar = *ScriptVarPtr;
					}
					else
					{
						// Create a new UNiagaraScriptVariable to represent this parameter for the lifetime of the ParameterPanelViewModel.
						ScriptVar = NewObject<UNiagaraScriptVariable>(GetTransientPackage());
						ScriptVar->AddToRoot();
						ScriptVar->Init(Var, FNiagaraVariableMetaData());
						TransientParameterToScriptVarMap.Add(Var, ScriptVar);
					}

					bool bVarOnlyInTopLevelGraph = true;
					for (FModuleScopedPin& WritePin : Builder.Histories[0].PerVariableWriteHistory[VariableIndex])
					{
						UEdGraphNode* VariableOwningNode = WritePin.Pin->GetOwningNode();
						bVarOnlyInTopLevelGraph &= AllGraphs.Contains(static_cast<const UNiagaraGraph*>(VariableOwningNode->GetGraph()));
					}
					for (FNiagaraParameterMapHistory::FReadHistory& ReadPins : Builder.Histories[0].PerVariableReadHistory[VariableIndex])
					{
						bVarOnlyInTopLevelGraph &= !AllGraphs.Contains(static_cast<const UNiagaraGraph*>(ReadPins.ReadPin.Pin->GetOwningNode()->GetGraph()));
					}

					FNiagaraParameterPanelItem Item = FNiagaraParameterPanelItem();
					Item.ScriptVariable = ScriptVar;
					Item.NamespaceMetaData = CandidateNamespaceMetaData;
					Item.bExternallyReferenced = !bVarOnlyInTopLevelGraph;
					
					// -Determine whether the parameter is from a custom stack context.
					Item.bSourcedFromCustomStackContext = false;
					for (const FName CustomIterationNamespace : CustomIterationSourceNamespaces)
					{
						if (Var.IsInNameSpace(CustomIterationNamespace))
						{
							Item.bSourcedFromCustomStackContext = true;
							break;
						}
					}

					// Determine whether the item is name aliasing a parameter definitions's parameter.
					Item.DefinitionMatchState = ReservedParametersManager->GetDefinitionMatchStateForParameter(Item.ScriptVariable->Variable);

					// -Increment the reference count.
					Item.ReferenceCount += Builder.Histories[0].PerVariableReadHistory[VariableIndex].Num() + Builder.Histories[0].PerVariableWriteHistory[VariableIndex].Num();

					VisitedParameterToItemMap.Add(Var, Item);
				}
			}
		}
	}

	// Refresh the CachedViewedItems and return that as the latest array of viewed items.
	VisitedParameterToItemMap.GenerateValueArray(CachedViewedItems);
	return CachedViewedItems;
}

TArray<TWeakObjectPtr<UNiagaraGraph>> FNiagaraSystemToolkitParameterPanelViewModel::GetEditableEmitterScriptGraphs() const
{
	if (SystemViewModel->GetEditMode() == ENiagaraSystemViewModelEditMode::SystemAsset && ensureMsgf(SystemGraphSelectionViewModelWeak.IsValid(), TEXT("SystemGraphSelectionViewModel was null for System edit mode!")))
	{
		return SystemGraphSelectionViewModelWeak.Pin()->GetSelectedEmitterScriptGraphs();
	}
	else
	{
		TArray<TWeakObjectPtr<UNiagaraGraph>> EditableEmitterScriptGraphs;
		EditableEmitterScriptGraphs.Add(static_cast<UNiagaraScriptSource*>(SystemViewModel->GetEmitterHandleViewModels()[0]->GetEmitterHandle()->GetInstance()->GraphSource)->NodeGraph);
		return EditableEmitterScriptGraphs;
	}
}

TArray<FNiagaraEmitterHandle*> FNiagaraSystemToolkitParameterPanelViewModel::GetEditableEmitterHandles() const
{
	TArray<FNiagaraEmitterHandle*> EditableEmitterHandles;
	const TArray<FGuid>& SelectedEmitterHandleIds = SystemViewModel->GetSelectionViewModel()->GetSelectedEmitterHandleIds();

	const TArray<TSharedRef<FNiagaraEmitterHandleViewModel>>& EmitterHandleViewModels = SystemViewModel->GetEmitterHandleViewModels();
	for (const TSharedRef<FNiagaraEmitterHandleViewModel>& EmitterHandleViewModel : EmitterHandleViewModels)
	{
		if (SelectedEmitterHandleIds.Contains(EmitterHandleViewModel->GetId()))
		{
			EditableEmitterHandles.Add(EmitterHandleViewModel->GetEmitterHandle());
		}
	}
	return EditableEmitterHandles;
}

void FNiagaraSystemToolkitParameterPanelViewModel::AddScriptVariable(const UNiagaraScriptVariable* NewScriptVar) const
{
	if (NewScriptVar == nullptr)
	{
		ensureMsgf(false, TEXT("Encounted null script variable when adding parameter!"));
		return;
	}

	TGuardValue<bool> AddParameterRefreshGuard(bIsAddingParameter, true);
	bool bSuccess = false;
	UNiagaraSystem& System = SystemViewModel->GetSystem();

	FScopedTransaction AddTransaction(LOCTEXT("AddSystemParameterTransaction", "Add parameter to system."));
	System.Modify();
	const FGuid NamespaceId = FNiagaraEditorUtilities::GetNamespaceMetaDataForVariableName(NewScriptVar->Variable.GetName()).GetGuid();
	if (NamespaceId == FNiagaraEditorGuids::UserNamespaceMetaDataGuid)
	{
		FNiagaraVariable NewParameter = FNiagaraVariable(NewScriptVar->Variable);
		bSuccess = FNiagaraEditorUtilities::AddParameter(NewParameter, System.GetExposedParameters(), System, nullptr);
	}
	else
	{
		UNiagaraEditorParametersAdapter* EditorParametersAdapter = SystemViewModel->GetEditorOnlyParametersAdapter();
		TArray<UNiagaraScriptVariable*>& EditorOnlyScriptVars = EditorParametersAdapter->GetParameters();
		const FGuid& NewScriptVarId = NewScriptVar->Metadata.GetVariableGuid();
		bool bNewScriptVarAlreadyExists = EditorOnlyScriptVars.ContainsByPredicate([&NewScriptVarId](const UNiagaraScriptVariable* ScriptVar){ return ScriptVar->Metadata.GetVariableGuid() == NewScriptVarId; });
		if (bNewScriptVarAlreadyExists == false)
		{
			EditorParametersAdapter->Modify();
			UNiagaraScriptVariable* DupeNewScriptVar = CastChecked<UNiagaraScriptVariable>(StaticDuplicateObject(NewScriptVar, EditorParametersAdapter, FName()));
			DupeNewScriptVar->SetFlags(RF_Transactional);
			EditorOnlyScriptVars.Add(DupeNewScriptVar);
			bSuccess = true;
		}
	}

	if (bSuccess)
	{
		Refresh();
		const bool bRequestRename = false;
		SelectParameterItemByName(NewScriptVar->Variable.GetName(), bRequestRename);
	}
}

void FNiagaraSystemToolkitParameterPanelViewModel::AddParameterDefinitions(UNiagaraParameterDefinitions* NewParameterDefinitions) const
{
	FScopedTransaction AddTransaction(LOCTEXT("AddParameterDefinitions", "Add parameter definitions."));
	SystemViewModel->GetSystem().Modify();
	SystemViewModel->SubscribeToParameterDefinitions(NewParameterDefinitions);
}

void FNiagaraSystemToolkitParameterPanelViewModel::RemoveParameterDefinitions(const FGuid& ParameterDefinitionsToRemoveId) const
{
	FScopedTransaction RemoveTransaction(LOCTEXT("RemoveParameterDefinitions", "Remove parameter definitions."));
	SystemViewModel->GetSystem().Modify();
	SystemViewModel->UnsubscribeFromParameterDefinitions(ParameterDefinitionsToRemoveId);
	Refresh();
	UIContext.RefreshParameterDefinitionsPanel();
}

void FNiagaraSystemToolkitParameterPanelViewModel::OnGraphChanged(const struct FEdGraphEditAction& InAction) const
{
	RefreshNextTick();
}

void FNiagaraSystemToolkitParameterPanelViewModel::OnParameterRenamedExternally(const FNiagaraVariableBase& InOldVar, const FNiagaraVariableBase& InNewVar, UNiagaraEmitter* InOptionalEmitter)
{
	// See if this was the last reference to that parameter being renamed, if so, we need to update to a full rename and rename all locations where it was used that are downstream, like renderer bindings.

	// Emitter & Particle namespaces are just for the ones actively being worked on.
	if (InOldVar.IsInNameSpace(FNiagaraConstants::EmitterNamespace) ||
		InOldVar.IsInNameSpace(FNiagaraConstants::ParticleAttributeNamespace))
	{
		const TArray<FGuid>& SelectedEmitterHandleIds = SystemViewModel->GetSelectionViewModel()->GetSelectedEmitterHandleIds();

		// Note that we might have multiple selections and don't know explicitly which one changed, so we have to go through all independently and examine them.
		if (SelectedEmitterHandleIds.Num() > 0)
		{
			const TArray<TSharedRef<FNiagaraEmitterHandleViewModel>>& EmitterHandleViewModels = SystemViewModel->GetEmitterHandleViewModels();
			for (const TSharedRef<FNiagaraEmitterHandleViewModel>& EmitterHandleViewModel : EmitterHandleViewModels)
			{
				if (SelectedEmitterHandleIds.Contains(EmitterHandleViewModel->GetId()))
				{
					bool bFound = false;
					UNiagaraGraph* Graph = static_cast<UNiagaraScriptSource*>(EmitterHandleViewModel->GetEmitterHandle()->GetInstance()->GraphSource)->NodeGraph;
					if (Graph)
					{
						const TMap<FNiagaraVariable, FNiagaraGraphParameterReferenceCollection>& RefMap = Graph->GetParameterReferenceMap();
						const FNiagaraGraphParameterReferenceCollection* Coll = RefMap.Find(InOldVar);
						if (Coll)
						{
							bFound = true;
							break;
						}
					}

					if (!bFound)
					{
						if (const FNiagaraParameterPanelItem* FoundItemPtr = CachedViewedItems.FindByPredicate([&InOldVar](const FNiagaraParameterPanelItem& Item) { return Item.GetVariable() == InOldVar; }))
						{
							RenameParameter(*FoundItemPtr, InNewVar.GetName());
						}
					}
				}
			}
		}		
	}
	// User and System need to be checked for all graphs as they could be used anywhere.
	else if (InOldVar.IsInNameSpace(FNiagaraConstants::UserNamespace) ||
			 InOldVar.IsInNameSpace(FNiagaraConstants::SystemNamespace))
	{
		TArray<UNiagaraGraph*> Graphs;
		Graphs.Add(SystemScriptGraph.Get());

		const TArray<TSharedRef<FNiagaraEmitterHandleViewModel>>& EmitterHandleViewModels = SystemViewModel->GetEmitterHandleViewModels();
		for (const TSharedRef<FNiagaraEmitterHandleViewModel>& EmitterHandleViewModel : EmitterHandleViewModels)
		{
			UNiagaraGraph* Graph = static_cast<UNiagaraScriptSource*>(EmitterHandleViewModel->GetEmitterHandle()->GetInstance()->GraphSource)->NodeGraph;
			if (Graph)
			{ 
				Graphs.Add(Graph);
			}
		}

		bool bFound = false;
		for (UNiagaraGraph* Graph : Graphs)
		{
			const TMap<FNiagaraVariable, FNiagaraGraphParameterReferenceCollection>& RefMap = Graph->GetParameterReferenceMap();
			const FNiagaraGraphParameterReferenceCollection* Coll = RefMap.Find(InOldVar);
			if (Coll)
			{
				bFound = true;
				break;
			}
		}

		if (!bFound)
		{
			if (const FNiagaraParameterPanelItem* FoundItemPtr = CachedViewedItems.FindByPredicate([&InOldVar](const FNiagaraParameterPanelItem& Item) { return Item.GetVariable() == InOldVar; }))
			{
				RenameParameter(*FoundItemPtr, InNewVar.GetName());
			}
		}
	}

	Refresh();
}

void FNiagaraSystemToolkitParameterPanelViewModel::OnParameterRemovedExternally(const FNiagaraVariableBase& InOldVar,  UNiagaraEmitter* InOptionalEmitter)
{
	// See if this was the last reference to that parameter being renamed, if so, we need to update to a full rename and rename all locations where it was used that are downstream, like renderer bindings.

	// Emitter & Particle namespaces are just for the ones actively being worked on.
	if (InOldVar.IsInNameSpace(FNiagaraConstants::EmitterNamespace) ||
		InOldVar.IsInNameSpace(FNiagaraConstants::ParticleAttributeNamespace))
	{
		const TArray<FGuid>& SelectedEmitterHandleIds = SystemViewModel->GetSelectionViewModel()->GetSelectedEmitterHandleIds();

		// Note that we might have multiple selections and don't know explicitly which one changed, so we have to go through all independently and examine them.
		if (SelectedEmitterHandleIds.Num() > 0)
		{
			const TArray<TSharedRef<FNiagaraEmitterHandleViewModel>>& EmitterHandleViewModels = SystemViewModel->GetEmitterHandleViewModels();
			for (const TSharedRef<FNiagaraEmitterHandleViewModel>& EmitterHandleViewModel : EmitterHandleViewModels)
			{
				if (SelectedEmitterHandleIds.Contains(EmitterHandleViewModel->GetId()))
				{
					bool bFound = false;
					UNiagaraGraph* Graph = static_cast<UNiagaraScriptSource*>(EmitterHandleViewModel->GetEmitterHandle()->GetInstance()->GraphSource)->NodeGraph;
					if (Graph)
					{
						const TMap<FNiagaraVariable, FNiagaraGraphParameterReferenceCollection>& RefMap = Graph->GetParameterReferenceMap();
						const FNiagaraGraphParameterReferenceCollection* Coll = RefMap.Find(InOldVar);
						if (Coll)
						{
							bFound = true;
							break;
						}
					}

					if (!bFound)
					{
						EmitterHandleViewModel->GetEmitterHandle()->GetInstance()->HandleVariableRemoved(InOldVar, true);
					}
				}
			}
		}
	}
	// User and System need to be checked for all graphs as they could be used anywhere.
	else if (InOldVar.IsInNameSpace(FNiagaraConstants::UserNamespace) ||
		InOldVar.IsInNameSpace(FNiagaraConstants::SystemNamespace))
	{
		TArray<UNiagaraGraph*> Graphs;
		Graphs.Add(SystemScriptGraph.Get());

		const TArray<TSharedRef<FNiagaraEmitterHandleViewModel>>& EmitterHandleViewModels = SystemViewModel->GetEmitterHandleViewModels();
		for (const TSharedRef<FNiagaraEmitterHandleViewModel>& EmitterHandleViewModel : EmitterHandleViewModels)
		{
			UNiagaraGraph* Graph = static_cast<UNiagaraScriptSource*>(EmitterHandleViewModel->GetEmitterHandle()->GetInstance()->GraphSource)->NodeGraph;
			if (Graph)
				Graphs.Add(Graph);

		}

		bool bFound = false;
		for (UNiagaraGraph* Graph : Graphs)
		{
			const TMap<FNiagaraVariable, FNiagaraGraphParameterReferenceCollection>& RefMap = Graph->GetParameterReferenceMap();
			const FNiagaraGraphParameterReferenceCollection* Coll = RefMap.Find(InOldVar);
			if (Coll)
			{
				bFound = true;
				break;
			}
		}

		if (!bFound)
		{
			SystemViewModel->GetSystem().HandleVariableRemoved(InOldVar, true);
		}
	}

	Refresh();
}

void FNiagaraSystemToolkitParameterPanelViewModel::ReconcileOnGraphChangedBindings()
{
	UNiagaraSystem& System = SystemViewModel->GetSystem();

	auto GetGraphFromScript = [](UNiagaraScript* Script)->UNiagaraGraph* {
		return CastChecked<UNiagaraScriptSource>(Script->GetLatestSource())->NodeGraph;
	};

	TSet<uint32> UnvisitedGraphIds;
	GraphIdToOnGraphChangedHandleMap.GetKeys(UnvisitedGraphIds);
	UnvisitedGraphIds.Remove(GetGraphFromScript(System.GetSystemSpawnScript())->GetUniqueID());
	UnvisitedGraphIds.Remove(GetGraphFromScript(System.GetSystemUpdateScript())->GetUniqueID());

	TSet<UNiagaraGraph*> GraphsToAddCallbacks;
	for (TSharedRef<FNiagaraEmitterHandleViewModel> EmitterHandleViewModel : SystemViewModel->GetEmitterHandleViewModels())
	{
		if (FNiagaraEmitterHandle* EmitterHandle = EmitterHandleViewModel->GetEmitterHandle())
		{
			TArray<UNiagaraScript*> EmitterScripts;
			const bool bCompilableOnly = false;
			EmitterHandle->GetInstance()->GetScripts(EmitterScripts, bCompilableOnly);
			for (UNiagaraScript* EmitterScript : EmitterScripts)
			{
				GraphsToAddCallbacks.Add(GetGraphFromScript(EmitterScript));
			}
		}
	}

	for (UNiagaraGraph* Graph : GraphsToAddCallbacks)
	{
		UnvisitedGraphIds.Remove(Graph->GetUniqueID());
		if (GraphIdToOnGraphChangedHandleMap.Find(Graph->GetUniqueID()) == nullptr)
		{
			FDelegateHandle OnGraphChangedHandle = Graph->AddOnGraphChangedHandler(
				FOnGraphChanged::FDelegate::CreateRaw(this, &FNiagaraSystemToolkitParameterPanelViewModel::OnGraphChanged));
			GraphIdToOnGraphChangedHandleMap.Add(Graph->GetUniqueID(), OnGraphChangedHandle);
		}
	}

	for (const uint32& UnvisitedGraphId : UnvisitedGraphIds)
	{
		GraphIdToOnGraphChangedHandleMap.Remove(UnvisitedGraphId);
	}
}

///////////////////////////////////////////////////////////////////////////////
/// Script Toolkit Parameter Panel View Model								///
///////////////////////////////////////////////////////////////////////////////

FNiagaraScriptToolkitParameterPanelViewModel::FNiagaraScriptToolkitParameterPanelViewModel(TSharedPtr<FNiagaraScriptViewModel> InScriptViewModel)
{
	ScriptViewModel = InScriptViewModel;
	VariableObjectSelection = ScriptViewModel->GetVariableSelection();

	RegisteredHandle = RegisterViewModelWithMap(InScriptViewModel->GetStandaloneScript().Script, this);
}

void FNiagaraScriptToolkitParameterPanelViewModel::Cleanup()
{
	UnregisterViewModelWithMap(RegisteredHandle);

	UNiagaraGraph* NiagaraGraph = static_cast<UNiagaraGraph*>(ScriptViewModel->GetGraphViewModel()->GetGraph());
	NiagaraGraph->RemoveOnGraphChangedHandler(OnGraphChangedHandle);
	NiagaraGraph->RemoveOnGraphNeedsRecompileHandler(OnGraphNeedsRecompileHandle);
	NiagaraGraph->OnSubObjectSelectionChanged().Remove(OnSubObjectSelectionHandle);

	ScriptViewModel->GetOnSubscribedParameterDefinitionsChangedDelegate().RemoveAll(this);
}

void FNiagaraScriptToolkitParameterPanelViewModel::Init(const FScriptToolkitUIContext& InUIContext)
{
	UIContext = InUIContext;

	// Init bindings
	UNiagaraGraph* NiagaraGraph = static_cast<UNiagaraGraph*>(ScriptViewModel->GetGraphViewModel()->GetGraph());
	OnGraphChangedHandle = NiagaraGraph->AddOnGraphChangedHandler(
		FOnGraphChanged::FDelegate::CreateRaw(this, &FNiagaraScriptToolkitParameterPanelViewModel::OnGraphChanged));
	OnGraphNeedsRecompileHandle = NiagaraGraph->AddOnGraphNeedsRecompileHandler(
		FOnGraphChanged::FDelegate::CreateRaw(this, &FNiagaraScriptToolkitParameterPanelViewModel::OnGraphChanged));
	OnSubObjectSelectionHandle = NiagaraGraph->OnSubObjectSelectionChanged().AddSP(this, &FNiagaraScriptToolkitParameterPanelViewModel::OnGraphSubObjectSelectionChanged);

	ScriptViewModel->GetOnSubscribedParameterDefinitionsChangedDelegate().AddSP(this, &FNiagaraScriptToolkitParameterPanelViewModel::Refresh);

	// Init default categories
	if (DefaultCategories.Num() == 0)
	{
		TArray<FNiagaraNamespaceMetadata> NamespaceMetadata = GetDefault<UNiagaraEditorSettings>()->GetAllNamespaceMetadata();
		for (const FNiagaraNamespaceMetadata& NamespaceMetadatum : NamespaceMetadata)
		{
			if (NamespaceMetadatum.Options.Contains(ENiagaraNamespaceMetadataOptions::HideInScript) == false)
			{
				if (NamespaceMetadatum.Options.Contains(ENiagaraNamespaceMetadataOptions::AdvancedInScript))
				{
					DefaultAdvancedCategories.Add(NamespaceMetadatum);
				}
				else
				{
					DefaultCategories.Add(NamespaceMetadatum);
					DefaultAdvancedCategories.Add(NamespaceMetadatum);
				}
			}
		}
	}
}

void FNiagaraScriptToolkitParameterPanelViewModel::AddParameter(FNiagaraVariable NewVariable, const FNiagaraParameterPanelCategory Category, const bool bRequestRename, const bool bMakeUniqueName) const
{
	TGuardValue<bool> AddParameterRefreshGuard(bIsAddingParameter, true);
	bool bSuccess = false;

	if(bMakeUniqueName)
	{ 
		TSet<FName> Names;
		for (const UNiagaraGraph* Graph : GetEditableGraphsConst())
		{
			for (auto It = Graph->GetParameterReferenceMap().CreateConstIterator(); It; ++It)
			{
				Names.Add(It.Key().GetName());
			}
		}
		const FName NewUniqueName = FNiagaraUtilities::GetUniqueName(NewVariable.GetName(), Names);
		NewVariable.SetName(NewUniqueName);
	}

	FScopedTransaction AddTransaction(LOCTEXT("AddScriptParameterTransaction", "Add parameter to script."));
	for (UNiagaraGraph* Graph : GetEditableGraphs())
	{
		Graph->Modify();
		const UNiagaraScriptVariable* NewScriptVar = Graph->AddParameter(NewVariable);
		bSuccess = true;

		// Check if the new parameter has the same name and type as an existing parameter definition, and if so, link to the definition automatically.
		SubscribeParameterToLibraryIfMatchingDefinition(NewScriptVar, NewScriptVar->Variable.GetName());
	}

	if (bSuccess)
	{
		Refresh();
		SelectParameterItemByName(NewVariable.GetName(), bRequestRename);
	}
}

bool FNiagaraScriptToolkitParameterPanelViewModel::GetCanAddParametersToCategory(FNiagaraParameterPanelCategory Category) const
{
	return GetEditableGraphsConst().Num() > 0 && Category.NamespaceMetaData.GetGuid() != FNiagaraEditorGuids::StaticSwitchNamespaceMetaDataGuid;
}

void FNiagaraScriptToolkitParameterPanelViewModel::DeleteParameter(const FNiagaraParameterPanelItem& ItemToDelete) const
{
	if (ItemToDelete.bExternallyReferenced)
	{
		return;
	}

	FScopedTransaction RemoveParametersWithPins(LOCTEXT("RemoveParametersWithPins", "Remove parameter and referenced pins"));
	for (UNiagaraGraph* Graph : GetEditableGraphs())
	{
		UNiagaraScriptVariable* SelectedScriptVariable = Graph->GetScriptVariable(ItemToDelete.GetVariable());
		if (SelectedScriptVariable && VariableObjectSelection->GetSelectedObjects().Contains(SelectedScriptVariable))
		{
			VariableObjectSelection->ClearSelectedObjects();
		}
		Graph->RemoveParameter(ItemToDelete.GetVariable());
	}

	Refresh();
	UIContext.RefreshSelectionDetailsViewPanel();
}

void FNiagaraScriptToolkitParameterPanelViewModel::RenameParameter(const FNiagaraParameterPanelItem& ItemToRename, const FName NewName) const
{
	if (ensureMsgf(ItemToRename.bExternallyReferenced == false, TEXT("Can not modify an externally referenced parameter.")) == false)
	{
		return;
	}
	else if (ItemToRename.GetVariable().GetName() == NewName)
	{
		return;
	}

	RenameParameter(ItemToRename.ScriptVariable, NewName);
}

void FNiagaraScriptToolkitParameterPanelViewModel::RenameParameter(const UNiagaraScriptVariable* ScriptVarToRename, const FName NewName) const
{
	FScopedTransaction RenameTransaction(LOCTEXT("RenameParameterTransaction", "Rename parameter"));
	ScriptViewModel->GetStandaloneScript().Script->Modify();
	bool bSuccess = false;
	const FNiagaraVariable& Parameter = ScriptVarToRename->Variable;
	for (UNiagaraGraph* Graph : GetEditableGraphs())
	{
		const FNiagaraGraphParameterReferenceCollection* ReferenceCollection = Graph->GetParameterReferenceMap().Find(Parameter);
		if (ensureMsgf(ReferenceCollection != nullptr, TEXT("Parameter in view which wasn't in the reference collection.")) == false)
		{
			// Can't handle parameters with no reference collections.
			continue;
		}
		Graph->Modify();
		Graph->RenameParameter(Parameter, NewName);
		bSuccess = true;
	}

	// Check if the rename will give the same name and type as an existing parameter definition, and if so, link to the definition automatically.
	SubscribeParameterToLibraryIfMatchingDefinition(ScriptVarToRename, NewName);

	if (bSuccess)
	{
		Refresh();
		const bool bRequestRename = false;
		SelectParameterItemByName(NewName, bRequestRename);
	}
}

void FNiagaraScriptToolkitParameterPanelViewModel::RenameParameter(const FNiagaraVariable& VariableToRename, const FName NewName) const
{
	for (UNiagaraGraph* Graph : GetEditableGraphs())
	{
		if (const UNiagaraScriptVariable* const* FoundScriptVarToRenamePtr = Graph->GetAllMetaData().Find(VariableToRename))
		{
			RenameParameter(*FoundScriptVarToRenamePtr, NewName);
			return;
		}
	}
}

void FNiagaraScriptToolkitParameterPanelViewModel::DuplicateParameter(const FNiagaraParameterPanelItem ItemToDuplicate) const
{
	FScopedTransaction Transaction(LOCTEXT("DuplicateParameterTransaction", "Duplicate parameter"));
	TGuardValue<bool> AddParameterRefreshGuard(bIsAddingParameter, true);
	bool bSuccess = false;

	TSet<FName> Names;
	for (const UNiagaraGraph* Graph : GetEditableGraphsConst())
	{
		for (auto It = Graph->GetParameterReferenceMap().CreateConstIterator(); It; ++It)
		{
			Names.Add(It.Key().GetName());
		}
	}
	const FName NewUniqueName = FNiagaraUtilities::GetUniqueName(ItemToDuplicate.GetVariable().GetName(), Names);
	FNiagaraVariable NewVariable(ItemToDuplicate.GetVariable().GetType(), NewUniqueName);
	FNiagaraVariableMetaData ParameterMetadata = ItemToDuplicate.ScriptVariable ? ItemToDuplicate.ScriptVariable->Metadata : FNiagaraVariableMetaData();

	for (UNiagaraGraph* Graph : GetEditableGraphs())
	{
		Graph->Modify();
		Graph->AddParameter(NewVariable, ParameterMetadata, false, false);
		bSuccess = true;
	}

	if (bSuccess)
	{
		Refresh();
		SelectParameterItemByName(NewUniqueName, true);
	}
}

void FNiagaraScriptToolkitParameterPanelViewModel::SetParameterIsSubscribedToLibrary(const UNiagaraScriptVariable* ScriptVarToModify, const bool bSubscribed) const
{
	const FText TransactionText = bSubscribed ? LOCTEXT("SubscribeParameter", "Subscribe parameter") : LOCTEXT("UnsubscribeParameter", "Unsubscribe parameter");
	FScopedTransaction SubscribeTransaction(TransactionText);
	ScriptViewModel->GetStandaloneScript().Script->Modify();
	ScriptViewModel->SetParameterIsSubscribedToDefinitions(ScriptVarToModify->Metadata.GetVariableGuid(), bSubscribed);
	Refresh();
	UIContext.RefreshParameterDefinitionsPanel();
	UIContext.RefreshSelectionDetailsViewPanel();
}

FReply FNiagaraScriptToolkitParameterPanelViewModel::OnParameterItemsDragged(const TArray<FNiagaraParameterPanelItem>& DraggedItems, const FPointerEvent& MouseEvent) const
{
	if (OnGetParametersWithNamespaceModifierRenamePendingDelegate.IsBound() == false)
	{
		ensureMsgf(false, TEXT("OnGetParametersWithNamespaceModifierRenamePendingDelegate was not bound when handling parameter drag in parameter panel view model! "));
		return FReply::Handled();
	}

	if (DraggedItems.Num() == 1)
	{
		const FNiagaraParameterPanelItemBase& DraggedItem = DraggedItems[0];
		return FNiagaraScriptToolkitParameterPanelUtilities::CreateDragEventForParameterItem(
			DraggedItem,
			MouseEvent,
			GetGraphParameterReferencesForItem(DraggedItem),
			OnGetParametersWithNamespaceModifierRenamePendingDelegate.Execute()
		);
	}

	return FReply::Handled();
}

TSharedPtr<SWidget> FNiagaraScriptToolkitParameterPanelViewModel::CreateContextMenuForItems(const TArray<FNiagaraParameterPanelItem>& Items, const TSharedPtr<FUICommandList>& ToolkitCommands)
{
	// Create a menu with all relevant operations.
	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, ToolkitCommands);

	// Only create context menus when a single item is selected.
	if (Items.Num() == 1)
	{
		const FNiagaraParameterPanelItem& SelectedItem = Items[0];
		const FNiagaraParameterPanelItemBase& SelectedItemBase = Items[0];

		// helper lambda to add copy/paste metadata actions.
		auto AddMetaDataContextMenuEntries = [this, &MenuBuilder, &Items, &SelectedItem, &SelectedItemBase]() {
			FText CopyParameterMetaDataToolTip;
			const bool bCanCopyParameterMetaData = GetCanCopyParameterMetaDataAndToolTip(SelectedItem, CopyParameterMetaDataToolTip);
			MenuBuilder.AddMenuEntry(
				LOCTEXT("CopyParameterMetadata", "Copy Metadata"),
				CopyParameterMetaDataToolTip,
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSP(this, &INiagaraImmutableParameterPanelViewModel::CopyParameterMetaData, SelectedItemBase),
					FCanExecuteAction::CreateLambda([bCanCopyParameterMetaData]() { return bCanCopyParameterMetaData; })));

			FText PasteParameterMetaDataToolTip;
			const bool bCanPasteParameterMetaData = GetCanPasteParameterMetaDataAndToolTip(PasteParameterMetaDataToolTip);
			MenuBuilder.AddMenuEntry(
				LOCTEXT("PasteParameterMetadata", "Paste Metadata"),
				PasteParameterMetaDataToolTip,
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSP(this, &INiagaraParameterPanelViewModel::PasteParameterMetaData, Items),
					FCanExecuteAction::CreateLambda([bCanPasteParameterMetaData]() { return bCanPasteParameterMetaData; })));
		};

		MenuBuilder.BeginSection("Edit", LOCTEXT("EditMenuHeader", "Edit"));
		{
			if (SelectedItem.ScriptVariable->GetIsStaticSwitch())
			{
				// Only allow modifying metadata for static switches.
				AddMetaDataContextMenuEntries();
			}
			else
			{ 
				FText CopyReferenceToolTip;
				GetCanCopyParameterReferenceAndToolTip(SelectedItem, CopyReferenceToolTip);
				MenuBuilder.AddMenuEntry(FGenericCommands::Get().Copy, NAME_None, LOCTEXT("CopyReference", "Copy Reference"), CopyReferenceToolTip);

				FText DeleteToolTip;
				GetCanDeleteParameterAndToolTip(SelectedItem, DeleteToolTip);
				MenuBuilder.AddMenuEntry(FNiagaraParameterPanelCommands::Get().DeleteItem, NAME_None, TAttribute<FText>(), DeleteToolTip);

				FText RenameToolTip;
				GetCanRenameParameterAndToolTip(SelectedItem, FText(), false, RenameToolTip);
				MenuBuilder.AddMenuEntry(FGenericCommands::Get().Rename, NAME_None, LOCTEXT("Rename", "Rename"), RenameToolTip);


				MenuBuilder.AddMenuSeparator();

				AddMetaDataContextMenuEntries();


				MenuBuilder.AddMenuSeparator();

				MenuBuilder.AddSubMenu(
					LOCTEXT("ChangeNamespace", "Change Namespace"),
					LOCTEXT("ChangeNamespaceToolTip", "Select a new namespace for the selected parameter."),
					FNewMenuDelegate::CreateSP(this, &FNiagaraScriptToolkitParameterPanelViewModel::GetChangeNamespaceSubMenu, false, SelectedItem));

				MenuBuilder.AddSubMenu(
					LOCTEXT("ChangeNamespaceModifier", "Change Namespace Modifier"),
					LOCTEXT("ChangeNamespaceModifierToolTip", "Edit the namespace modifier for the selected parameter."),
					FNewMenuDelegate::CreateSP(this, &FNiagaraScriptToolkitParameterPanelViewModel::GetChangeNamespaceModifierSubMenu, false, SelectedItem));


				MenuBuilder.AddMenuSeparator();

				FText DuplicateToolTip;
				bool bCanDuplicateParameter = GetCanDuplicateParameterAndToolTip(SelectedItem, DuplicateToolTip);
				MenuBuilder.AddMenuEntry(
					LOCTEXT("DuplicateParameter", "Duplicate"),
					DuplicateToolTip,
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateSP(this, &FNiagaraScriptToolkitParameterPanelViewModel::DuplicateParameter, SelectedItem),
						FCanExecuteAction::CreateLambda([bCanDuplicateParameter]() {return bCanDuplicateParameter; })));

				MenuBuilder.AddSubMenu(
					LOCTEXT("DuplicateToNewNamespace", "Duplicate to Namespace"),
					LOCTEXT("DuplicateToNewNamespaceToolTip", "Duplicate this parameter to a new namespace."),
					FNewMenuDelegate::CreateSP(this, &FNiagaraScriptToolkitParameterPanelViewModel::GetChangeNamespaceSubMenu, true, SelectedItem));

				MenuBuilder.AddSubMenu(
					LOCTEXT("DuplicateWithNewNamespaceModifier", "Duplicate with Namespace Modifier"),
					LOCTEXT("DupilcateWithNewNamespaceModifierToolTip", "Duplicate this parameter with a different namespace modifier."),
					FNewMenuDelegate::CreateSP(this, &FNiagaraScriptToolkitParameterPanelViewModel::GetChangeNamespaceModifierSubMenu, true, SelectedItem));

			}
		}
		MenuBuilder.EndSection();
		return MenuBuilder.MakeWidget();
	}
	// More than one item selected, do not return a context menu.
	return SNullWidget::NullWidget;
}

FNiagaraParameterUtilities::EParameterContext FNiagaraScriptToolkitParameterPanelViewModel::GetParameterContext() const
{
	return FNiagaraParameterUtilities::EParameterContext::Script;
}

void FNiagaraScriptToolkitParameterPanelViewModel::OnParameterItemSelected(const FNiagaraParameterPanelItem& SelectedItem, ESelectInfo::Type SelectInfo) const
{
	for (const UNiagaraGraph* Graph : GetEditableGraphsConst())
	{
		if (UNiagaraScriptVariable* SelectedScriptVariable = Graph->GetScriptVariable(SelectedItem.GetVariable()))
		{
			VariableObjectSelection->SetSelectedObject(SelectedScriptVariable);
			return;
		}
	}
}

const TArray<FNiagaraParameterPanelCategory>& FNiagaraScriptToolkitParameterPanelViewModel::GetDefaultCategories() const
{
// 	const UNiagaraEditorSettings* NiagaraEditorSettings = GetDefault<UNiagaraEditorSettings>();
// 	const bool bShowAdvanced = NiagaraEditorSettings->GetDisplayAdvancedParameterPanelCategories();
// 	if (bShowAdvanced)
// 	{
// 		CachedCurrentCategories = FNiagaraScriptToolkitParameterPanelViewModel::DefaultAdvancedCategories;
// 		return FNiagaraScriptToolkitParameterPanelViewModel::DefaultAdvancedCategories;
// 	}
	// For now always show advanced.
	CachedCurrentCategories = FNiagaraScriptToolkitParameterPanelViewModel::DefaultAdvancedCategories;
	return FNiagaraScriptToolkitParameterPanelViewModel::DefaultAdvancedCategories;
}

FMenuAndSearchBoxWidgets FNiagaraScriptToolkitParameterPanelViewModel::GetParameterMenu(FNiagaraParameterPanelCategory Category) const
{
	const bool bRequestRename = true;
	const bool bSkipSubscribedLibraries = false;
	const bool bMakeUniqueName = true;

	TSharedPtr<SNiagaraAddParameterFromPanelMenu> MenuWidget = SAssignNew(ParameterMenuWidget, SNiagaraAddParameterFromPanelMenu)
		.Graphs(GetEditableGraphsConst())
		.AvailableParameterDefinitions(ScriptViewModel->GetAvailableParameterDefinitions(bSkipSubscribedLibraries))
 		.SubscribedParameterDefinitions(ScriptViewModel->GetSubscribedParameterDefinitions())
		.OnAddParameter(this, &FNiagaraScriptToolkitParameterPanelViewModel::AddParameter, Category, bRequestRename, bMakeUniqueName)
		.OnAddScriptVar(this, &FNiagaraScriptToolkitParameterPanelViewModel::AddScriptVariable)
		.OnAddParameterDefinitions(this, &FNiagaraScriptToolkitParameterPanelViewModel::AddParameterDefinitions)
		.OnAllowMakeType_Static(&INiagaraParameterPanelViewModel::CanMakeNewParameterOfType)
		.NamespaceId(Category.NamespaceMetaData.GetGuid())
		.ShowNamespaceCategory(false)
		.ShowGraphParameters(false)
		.AutoExpandMenu(false)
		.CullParameterActionsAlreadyInGraph(true);

	ParameterMenuSearchBoxWidget = MenuWidget->GetSearchBox();
	FMenuAndSearchBoxWidgets MenuAndSearchBoxWidgets;
	MenuAndSearchBoxWidgets.MenuWidget = MenuWidget;
	MenuAndSearchBoxWidgets.MenuSearchBoxWidget = ParameterMenuSearchBoxWidget;

	return MenuAndSearchBoxWidgets;
}

FReply FNiagaraScriptToolkitParameterPanelViewModel::HandleDragDropOperation(TSharedPtr<FDragDropOperation> DragDropOperation) const
{
	TSharedPtr<FNiagaraParameterGraphDragOperation> ParameterGraphDragDropOperation = StaticCastSharedPtr<FNiagaraParameterGraphDragOperation>(DragDropOperation);
	if (ParameterGraphDragDropOperation.IsValid() == false)
	{
		return FReply::Handled();
	}

	TSharedPtr<FEdGraphSchemaAction> SourceAction = ParameterGraphDragDropOperation->GetSourceAction();
	if (SourceAction.IsValid() == false)
	{
		return FReply::Handled();
	}

	TSharedPtr<FNiagaraParameterAction> SourceParameterAction = StaticCastSharedPtr<FNiagaraParameterAction>(SourceAction);
	if (SourceParameterAction.IsValid() == false)
	{
		return FReply::Handled();
	}

	AddScriptVariable(SourceParameterAction->GetScriptVar());
	return FReply::Handled();
}

bool FNiagaraScriptToolkitParameterPanelViewModel::GetCanHandleDragDropOperation(TSharedPtr<FDragDropOperation> DragDropOperation) const
{
	if (DragDropOperation->IsOfType<FNiagaraParameterGraphDragOperation>() == false)
	{
		return false;
	}
	TSharedPtr<FNiagaraParameterGraphDragOperation> ParameterGraphDragDropOperation = StaticCastSharedPtr<FNiagaraParameterGraphDragOperation>(DragDropOperation);

	const TSharedPtr<FEdGraphSchemaAction>& SourceAction = ParameterGraphDragDropOperation->GetSourceAction();
	if (SourceAction.IsValid() == false)
	{
		return false;
	}

	if (SourceAction->GetTypeId() != FNiagaraEditorStrings::FNiagaraParameterActionId)
	{
		return false;
	}
	const TSharedPtr<FNiagaraParameterAction>& SourceParameterAction = StaticCastSharedPtr<FNiagaraParameterAction>(SourceAction);

	const UNiagaraScriptVariable* ScriptVar = SourceParameterAction->GetScriptVar();
	if (ScriptVar == nullptr)
	{
		return false;
	}

	// Do not allow trying to create a new parameter from the drop action if that parameter name/type pair already exists.
	const FNiagaraVariable& Parameter = ScriptVar->Variable;
	if (ScriptViewModel->GetAllScriptVars().ContainsByPredicate([Parameter](const UNiagaraScriptVariable* ScriptVar) { return ScriptVar->Variable == Parameter; }))
	{
		return false;
	}

	return true;
}

void FNiagaraScriptToolkitParameterPanelViewModel::SetParameterIsOverridingLibraryDefaultValue(const FNiagaraParameterPanelItem ItemToModify, const bool bOverriding) const
{
	if (ensureMsgf(ItemToModify.bExternallyReferenced == false, TEXT("Cannot modify an externally referenced parameter.")) == false)
	{
		return;
	}

	const FText TransactionText = bOverriding ? LOCTEXT("OverrideParameterDefinitionDefaulValue", "Override Parameter Definition Default") : LOCTEXT("UseParameterDefinitionDefaultValue", "Stop Overriding Parameter Definition Default");
	FScopedTransaction OverrideTransaction(TransactionText);
	ScriptViewModel->GetStandaloneScript().Script->Modify();
	ScriptViewModel->SetParameterIsOverridingLibraryDefaultValue(ItemToModify.ScriptVariable->Metadata.GetVariableGuid(), bOverriding);
	Refresh();
	UIContext.RefreshParameterDefinitionsPanel();
	UIContext.RefreshSelectionDetailsViewPanel();
}

TArray<UNiagaraGraph*> FNiagaraScriptToolkitParameterPanelViewModel::GetEditableGraphs() const
{
	return FNiagaraScriptToolkitParameterPanelUtilities::GetEditableGraphs(ScriptViewModel);
}

const TArray<UNiagaraScriptVariable*> FNiagaraScriptToolkitParameterPanelViewModel::GetEditableScriptVariablesWithName(const FName ParameterName) const
{
	TArray<UNiagaraScriptVariable*> EditableScriptVariables;
	for (const UNiagaraGraph* Graph : GetEditableGraphsConst())
	{
		UNiagaraScriptVariable* ParameterScriptVariable = Graph->GetScriptVariable(ParameterName);
		if (ParameterScriptVariable != nullptr)
		{
			EditableScriptVariables.Add(ParameterScriptVariable);
		}
	}
	return EditableScriptVariables;
}

TArray<FNiagaraVariable> FNiagaraScriptToolkitParameterPanelViewModel::GetEditableStaticSwitchParameters() const
{
	TArray<FNiagaraVariable> OutStaticSwitchParameters;
	for (const UNiagaraGraph* Graph : GetEditableGraphsConst())
	{
		OutStaticSwitchParameters.Append(Graph->FindStaticSwitchInputs());
	}
	return OutStaticSwitchParameters;
}

const TArray<FNiagaraGraphParameterReference> FNiagaraScriptToolkitParameterPanelViewModel::GetGraphParameterReferencesForItem(const FNiagaraParameterPanelItemBase& Item) const
{
	TArray<FNiagaraGraphParameterReference> ParameterReferences;
	for (const UNiagaraGraph* Graph : GetEditableGraphsConst())
	{
		const FNiagaraGraphParameterReferenceCollection* GraphParameterReferenceCollectionPtr = Graph->GetParameterReferenceMap().Find(Item.GetVariable());
		if (GraphParameterReferenceCollectionPtr)
		{
			ParameterReferences.Append(GraphParameterReferenceCollectionPtr->ParameterReferences);
		}
	}
	return ParameterReferences;
}

const TArray<UNiagaraParameterDefinitions*> FNiagaraScriptToolkitParameterPanelViewModel::GetAvailableParameterDefinitions(bool bSkipSubscribedParameterDefinitions) const
{
	return ScriptViewModel->GetAvailableParameterDefinitions(bSkipSubscribedParameterDefinitions);
}

TArray<FNiagaraParameterPanelItem> FNiagaraScriptToolkitParameterPanelViewModel::GetViewedParameterItems() const
{
	// On the first time opening the parameter panel view model we are not guaranteed to call GetDefaultCategories() before GetViewedParameterItems(). 
	// We require CachedCurrentCategories being set as this is used to filter out parameter items that are being viewed. If CachedCurrentCategories 
	// is not set, call GetDefaultCategories() to initialize it. 
	if (CachedCurrentCategories.Num() == 0)
	{
		GetDefaultCategories();
	}

	TMap<FNiagaraVariable, FNiagaraParameterPanelItem> VisitedParameterToItemMap;
	TArray<FNiagaraVariable> VisitedInvalidParameters;
	const UNiagaraReservedParametersManager* ReservedParametersManager = FNiagaraEditorModule::Get().GetReservedParametersManager();

	// For scripts we use the reference maps cached in the graph to collect parameters.
	for (const UNiagaraGraph* Graph : GetEditableGraphsConst())
	{
		TMap<FNiagaraVariable, UNiagaraScriptVariable*> ParameterToScriptVariableMap;
		ParameterToScriptVariableMap.Append(Graph->GetAllMetaData());
		// Collect all subgraphs to get their UNiagaraScriptVariables to resolve metadata for parameters in the parameter reference map.
		TSet<UNiagaraGraph*> SubGraphs;
		TArray<UNiagaraNodeFunctionCall*> FunctionCallNodes;
		Graph->GetNodesOfClass<UNiagaraNodeFunctionCall>(FunctionCallNodes);
		for (UNiagaraNodeFunctionCall* FunctionCallNode : FunctionCallNodes)
		{
			UNiagaraScriptSource* FunctionScriptSource = FunctionCallNode->GetFunctionScriptSource();
			if (FunctionScriptSource)
			{
				ParameterToScriptVariableMap.Append(FunctionScriptSource->NodeGraph->GetAllMetaData());
			}
		}

		for (const TPair<FNiagaraVariable, FNiagaraGraphParameterReferenceCollection>& ParameterElement : Graph->GetParameterReferenceMap())
		{
			const FNiagaraVariable& Var = ParameterElement.Key;
			// If this variable has already been visited and does not have a valid namespace then skip it.
			if (VisitedInvalidParameters.Contains(Var))
			{
				continue;
			}

			if (FNiagaraParameterPanelItem* ItemPtr = VisitedParameterToItemMap.Find(Var))
			{
				// This variable has already been registered, increment the reference count.
				ItemPtr->ReferenceCount += ParameterElement.Value.ParameterReferences.Num();
			}
			else
			{
				// This variable has not been registered, prepare the FNiagaraParameterPanelItem.
				// -First lookup the script variable.
				UNiagaraScriptVariable* const* ScriptVarPtr = ParameterToScriptVariableMap.Find(Var);
				UNiagaraScriptVariable* ScriptVar = ScriptVarPtr != nullptr ? *ScriptVarPtr : nullptr;
				if (!ScriptVar)
				{
					// Create a new UNiagaraScriptVariable to represent this parameter for the lifetime of the ParameterPanelViewModel.
					ScriptVar = NewObject<UNiagaraScriptVariable>(GetTransientPackage());
					ScriptVar->AddToRoot();
					ScriptVar->Init(Var, FNiagaraVariableMetaData());
					TransientParameterToScriptVarMap.Add(Var, ScriptVar);
				}

				// -Now make sure the variable namespace is in a valid category. If not, skip it.
				FNiagaraNamespaceMetadata CandidateNamespaceMetaData;
				if (ScriptVar->GetIsStaticSwitch())
				{
					CandidateNamespaceMetaData = FNiagaraEditorUtilities::GetNamespaceMetaDataForId(FNiagaraEditorGuids::StaticSwitchNamespaceMetaDataGuid);
				}
				else
				{
					CandidateNamespaceMetaData = FNiagaraEditorUtilities::GetNamespaceMetaDataForVariableName(Var.GetName());
				}

				if (CachedCurrentCategories.Contains(FNiagaraParameterPanelCategory(CandidateNamespaceMetaData)) == false)
				{
					VisitedInvalidParameters.Add(Var);
					continue;
				}

				FNiagaraParameterPanelItem Item = FNiagaraParameterPanelItem();
				Item.ScriptVariable = ScriptVar;
				Item.NamespaceMetaData = CandidateNamespaceMetaData;
				Item.bExternallyReferenced = false;
				Item.bSourcedFromCustomStackContext = false;

				// Determine whether the item is name aliasing a parameter definitions's parameter.
				Item.DefinitionMatchState = ReservedParametersManager->GetDefinitionMatchStateForParameter(ScriptVar->Variable);

				// -Increment the reference count.
				Item.ReferenceCount += ParameterElement.Value.ParameterReferences.Num();

				VisitedParameterToItemMap.Add(Var, Item);
			}
		}
	}

	// Refresh the CachedViewedItems and return that as the latest array of viewed items.
	VisitedParameterToItemMap.GenerateValueArray(CachedViewedItems);
	return CachedViewedItems;
}

void FNiagaraScriptToolkitParameterPanelViewModel::AddScriptVariable(const UNiagaraScriptVariable* NewScriptVar) const
{
	bool bSuccess = false;
	FScopedTransaction AddTransaction(LOCTEXT("AddScriptParameterTransaction", "Add parameter to script."));
	for (UNiagaraGraph* Graph : GetEditableGraphs())
	{
		Graph->Modify();
		Graph->AddParameter(NewScriptVar);
		bSuccess = true;
	}

	if (bSuccess)
	{
		Refresh();
		const bool bRequestRename = false;
		SelectParameterItemByName(NewScriptVar->Variable.GetName(), bRequestRename);
	}
}

void FNiagaraScriptToolkitParameterPanelViewModel::AddParameterDefinitions(UNiagaraParameterDefinitions* NewParameterDefinitions) const
{
	FScopedTransaction AddTransaction(LOCTEXT("AddParameterDefinitions", "Add parameter definitions."));
	ScriptViewModel->GetStandaloneScript().Script->Modify();
	ScriptViewModel->SubscribeToParameterDefinitions(NewParameterDefinitions);
	UIContext.RefreshParameterDefinitionsPanel();
}

void FNiagaraScriptToolkitParameterPanelViewModel::RemoveParameterDefinitions(const FGuid& ParameterDefinitionsToRemoveId) const
{
	FScopedTransaction RemoveTransaction(LOCTEXT("RemoveParameterDefinitions", "Remove parameter definitions."));
	ScriptViewModel->GetStandaloneScript().Script->Modify();
	ScriptViewModel->UnsubscribeFromParameterDefinitions(ParameterDefinitionsToRemoveId);
	Refresh();
	UIContext.RefreshParameterDefinitionsPanel();
}

void FNiagaraScriptToolkitParameterPanelViewModel::OnGraphChanged(const struct FEdGraphEditAction& InAction) const
{
	RefreshNextTick();
}

void FNiagaraScriptToolkitParameterPanelViewModel::OnGraphSubObjectSelectionChanged(const UObject* Obj) const
{
	OnParameterPanelViewModelExternalSelectionChangedDelegate.Broadcast(Obj);
}

///////////////////////////////////////////////////////////////////////////////
/// Parameter Definitions Toolkit Parameter Panel View Model				///
///////////////////////////////////////////////////////////////////////////////

FNiagaraParameterDefinitionsToolkitParameterPanelViewModel::FNiagaraParameterDefinitionsToolkitParameterPanelViewModel(UNiagaraParameterDefinitions* InParameterDefinitions, const TSharedPtr<FNiagaraObjectSelection>& InObjectSelection)
{
	ParameterDefinitionsWeak = InParameterDefinitions;
	VariableObjectSelection = InObjectSelection;
}

void FNiagaraParameterDefinitionsToolkitParameterPanelViewModel::Init(const FParameterDefinitionsToolkitUIContext& InUIContext)
{
	UIContext = InUIContext;

	ParameterDefinitionsWeak.Get()->GetOnParameterDefinitionsChanged().AddSP(this, &FNiagaraParameterDefinitionsToolkitParameterPanelViewModel::Refresh);

	// Init default categories
	if (DefaultCategories.Num() == 0)
	{
		DefaultCategories.Add(FNiagaraEditorUtilities::GetNamespaceMetaDataForId(FNiagaraEditorGuids::SystemNamespaceMetaDataGuid));
		DefaultCategories.Add(FNiagaraEditorUtilities::GetNamespaceMetaDataForId(FNiagaraEditorGuids::EmitterNamespaceMetaDataGuid));
		DefaultCategories.Add(FNiagaraEditorUtilities::GetNamespaceMetaDataForId(FNiagaraEditorGuids::ParticleAttributeNamespaceMetaDataGuid));
	}
}

void FNiagaraParameterDefinitionsToolkitParameterPanelViewModel::Cleanup()
{
	if (ParameterDefinitionsWeak.IsValid())
	{
		ParameterDefinitionsWeak.Get()->GetOnParameterDefinitionsChanged().RemoveAll(this);
	}
}

void FNiagaraParameterDefinitionsToolkitParameterPanelViewModel::AddParameter(FNiagaraVariable NewVariable, const FNiagaraParameterPanelCategory Category, const bool bRequestRename, const bool bMakeUniqueName) const
{
	TGuardValue<bool> AddParameterRefreshGuard(bIsAddingParameter, true);
	bool bSuccess = false;

	if (bMakeUniqueName)
	{
		TSet<FName> Names;
		for (const UNiagaraScriptVariable* ScriptVar : ParameterDefinitionsWeak.Get()->GetParametersConst())
		{
			Names.Add(ScriptVar->Variable.GetName());
		}
		const FName NewUniqueName = FNiagaraUtilities::GetUniqueName(NewVariable.GetName(), Names);
		NewVariable.SetName(NewUniqueName);
	}

	FScopedTransaction AddTransaction(LOCTEXT("ParameterDefinitionsToolkitParameterPanelViewModel_AddParameter", "Add Parameter"));
	ParameterDefinitionsWeak.Get()->AddParameter(NewVariable);
	SelectParameterItemByName(NewVariable.GetName(), bRequestRename);
}

bool FNiagaraParameterDefinitionsToolkitParameterPanelViewModel::GetCanAddParametersToCategory(FNiagaraParameterPanelCategory Category) const
{
	return true;
}

void FNiagaraParameterDefinitionsToolkitParameterPanelViewModel::DeleteParameter(const FNiagaraParameterPanelItem& ItemToDelete) const
{
	FScopedTransaction RemoveParameter(LOCTEXT("ParameterDefinitionsToolkitParameterPanelViewModel_RemoveParameter", "Remove Parameter"));
	ParameterDefinitionsWeak.Get()->RemoveParameter(ItemToDelete.GetVariable());
	VariableObjectSelection->ClearSelectedObjects();
	UIContext.RefreshSelectionDetailsViewPanel();
}

void FNiagaraParameterDefinitionsToolkitParameterPanelViewModel::RenameParameter(const FNiagaraParameterPanelItem& ItemToRename, const FName NewName) const
{
	FScopedTransaction RenameParameter(LOCTEXT("ParameterDefinitionsToolkitParameterPanelViewModel_RenameParameter", "Rename Parameter"));
	ParameterDefinitionsWeak.Get()->RenameParameter(ItemToRename.GetVariable(), NewName);
}

void FNiagaraParameterDefinitionsToolkitParameterPanelViewModel::SetParameterIsSubscribedToLibrary(const UNiagaraScriptVariable* ScriptVarToModify, const bool bSubscribed) const
{
	// Do nothing, parameter definitions parameters are always subscribed to their parent parameter definitions.
	ensureMsgf(false, TEXT("Tried to set a parameter definitions defined parameter subscribing! This should not be reachable."));
}

TSharedPtr<SWidget> FNiagaraParameterDefinitionsToolkitParameterPanelViewModel::CreateContextMenuForItems(const TArray<FNiagaraParameterPanelItem>& Items, const TSharedPtr<FUICommandList>& ToolkitCommands)
{
	// Only create context menus when a single item is selected.
	if (Items.Num() == 1)
	{
		const FNiagaraParameterPanelItem& SelectedItem = Items[0];

		// Create a menu with all relevant operations.
		const bool bShouldCloseWindowAfterMenuSelection = true;
		FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, ToolkitCommands);
		MenuBuilder.BeginSection("Edit", LOCTEXT("EditMenuHeader", "Edit"));
		{
			FText CopyReferenceToolTip;
			GetCanCopyParameterReferenceAndToolTip(SelectedItem, CopyReferenceToolTip);
			MenuBuilder.AddMenuEntry(FGenericCommands::Get().Copy, NAME_None, LOCTEXT("CopyReference", "Copy Reference"), CopyReferenceToolTip);

			FText DeleteToolTip;
			GetCanDeleteParameterAndToolTip(SelectedItem, DeleteToolTip);
			MenuBuilder.AddMenuEntry(FNiagaraParameterPanelCommands::Get().DeleteItem, NAME_None, TAttribute<FText>(), DeleteToolTip);

			FText RenameToolTip;
			GetCanRenameParameterAndToolTip(SelectedItem, FText(), false, RenameToolTip);
			MenuBuilder.AddMenuEntry(FGenericCommands::Get().Rename, NAME_None, LOCTEXT("Rename", "Rename"), RenameToolTip);


			MenuBuilder.AddMenuSeparator();

			MenuBuilder.AddSubMenu(
				LOCTEXT("ChangeNamespace", "Change Namespace"),
				LOCTEXT("ChangeNamespaceToolTip", "Select a new namespace for the selected parameter."),
				FNewMenuDelegate::CreateSP(this, &FNiagaraParameterDefinitionsToolkitParameterPanelViewModel::GetChangeNamespaceSubMenu, false, SelectedItem));

			MenuBuilder.AddSubMenu(
				LOCTEXT("ChangeNamespaceModifier", "Change Namespace Modifier"),
				LOCTEXT("ChangeNamespaceModifierToolTip", "Edit the namespace modifier for the selected parameter."),
				FNewMenuDelegate::CreateSP(this, &FNiagaraParameterDefinitionsToolkitParameterPanelViewModel::GetChangeNamespaceModifierSubMenu, false, SelectedItem));


			MenuBuilder.AddMenuSeparator();

			FText DuplicateToolTip;
			bool bCanDuplicateParameter = GetCanDuplicateParameterAndToolTip(SelectedItem, DuplicateToolTip);
			MenuBuilder.AddMenuEntry(
				LOCTEXT("DuplicateParameter", "Duplicate"),
				DuplicateToolTip,
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSP(this, &FNiagaraParameterDefinitionsToolkitParameterPanelViewModel::DuplicateParameter, SelectedItem),
					FCanExecuteAction::CreateLambda([bCanDuplicateParameter]() {return bCanDuplicateParameter; })));

			MenuBuilder.AddSubMenu(
				LOCTEXT("DuplicateToNewNamespace", "Duplicate to Namespace"),
				LOCTEXT("DuplicateToNewNamespaceToolTip", "Duplicate this parameter to a new namespace."),
				FNewMenuDelegate::CreateSP(this, &FNiagaraParameterDefinitionsToolkitParameterPanelViewModel::GetChangeNamespaceSubMenu, true, SelectedItem));

			MenuBuilder.AddSubMenu(
				LOCTEXT("DuplicateWithNewNamespaceModifier", "Duplicate with Namespace Modifier"),
				LOCTEXT("DupilcateWithNewNamespaceModifierToolTip", "Duplicate this parameter with a different namespace modifier."),
				FNewMenuDelegate::CreateSP(this, &FNiagaraParameterDefinitionsToolkitParameterPanelViewModel::GetChangeNamespaceModifierSubMenu, true, SelectedItem));
		}
		MenuBuilder.EndSection();
		return MenuBuilder.MakeWidget();
	}
	// More than one item selected, do not return a context menu.
	return SNullWidget::NullWidget;
}

FNiagaraParameterUtilities::EParameterContext FNiagaraParameterDefinitionsToolkitParameterPanelViewModel::GetParameterContext() const
{
	return FNiagaraParameterUtilities::EParameterContext::System;
}

const TArray<UNiagaraScriptVariable*> FNiagaraParameterDefinitionsToolkitParameterPanelViewModel::GetEditableScriptVariablesWithName(const FName ParameterName) const
{
	const TArray<UNiagaraScriptVariable*>& LibraryParameters = ParameterDefinitionsWeak.Get()->GetParametersConst();
	return LibraryParameters.FilterByPredicate([ParameterName](const UNiagaraScriptVariable* ScriptVariable){return ScriptVariable->Variable.GetName() == ParameterName;});
}

const TArray<FNiagaraGraphParameterReference> FNiagaraParameterDefinitionsToolkitParameterPanelViewModel::GetGraphParameterReferencesForItem(const FNiagaraParameterPanelItemBase& Item) const
{
	return TArray<FNiagaraGraphParameterReference>();
}

const TArray<UNiagaraParameterDefinitions*> FNiagaraParameterDefinitionsToolkitParameterPanelViewModel::GetAvailableParameterDefinitions(bool bSkipSubscribedParameterDefinitions) const
{
	// NOTE: Parameter library toolkit does not subscribe to parameter libraries directly and as such returns an empty array.
	return TArray<UNiagaraParameterDefinitions*>();
}

TArray<FNiagaraVariable> FNiagaraParameterDefinitionsToolkitParameterPanelViewModel::GetEditableStaticSwitchParameters() const
{
	// Parameter libraries do not have static switch parameters.
	return TArray<FNiagaraVariable>();
}

TArray<FNiagaraParameterPanelItem> FNiagaraParameterDefinitionsToolkitParameterPanelViewModel::GetViewedParameterItems() const
{
	CachedViewedItems.Reset();
	for (const UNiagaraScriptVariable* const& ScriptVar : ParameterDefinitionsWeak.Get()->GetParametersConst())
	{
		FNiagaraParameterPanelItem& Item = CachedViewedItems.AddDefaulted_GetRef();
		Item.ScriptVariable = ScriptVar;
		Item.NamespaceMetaData = FNiagaraEditorUtilities::GetNamespaceMetaDataForVariableName(ScriptVar->Variable.GetName());
		Item.bExternallyReferenced = false;
		Item.bSourcedFromCustomStackContext = false;
		Item.ReferenceCount = 1;
		Item.DefinitionMatchState = EParameterDefinitionMatchState::MatchingOneDefinition;
	}
	return CachedViewedItems;
}

bool FNiagaraParameterDefinitionsToolkitParameterPanelViewModel::GetCanRenameParameterAndToolTip(const FNiagaraParameterPanelItem& ItemToRename, const FText& NewVariableNameText, bool bCheckEmptyNameText, FText& OutCanRenameParameterToolTip) const
{
	const FName NewVariableName = FName(*NewVariableNameText.ToString());
	if(ItemToRename.GetVariable().GetName() != NewVariableName)
	{ 
		if (CachedViewedItems.ContainsByPredicate([NewVariableName](const FNiagaraParameterPanelItem& Item) {return Item.GetVariable().GetName() == NewVariableName; }))
		{
			OutCanRenameParameterToolTip = LOCTEXT("ParameterPanelLibraryViewModel_RenameParameter_NameAlias", "Cannot Rename Parameter: A Parameter with this name already exists in this definition asset.");
			return false;
		}
		else if (FNiagaraEditorModule::Get().GetReservedParametersManager()->GetNumReservedParametersByName(NewVariableName) > 0)
		{
			OutCanRenameParameterToolTip = LOCTEXT("ParameterPanelLibraryViewModel_RenameParameter_ReservedDefinitionName", "Cannot Rename Parameter: A Parameter with this name already exists in another definition asset.");
			return false;
		}
	}

	if (bCheckEmptyNameText && NewVariableNameText.IsEmptyOrWhitespace())
	{
		// The incoming name text will contain the namespace even if the parameter name entry is empty, so make a parameter handle to split out the name.
		const FNiagaraParameterHandle NewVariableNameHandle = FNiagaraParameterHandle(NewVariableName);
		if (NewVariableNameHandle.GetName().IsNone())
		{
			OutCanRenameParameterToolTip = LOCTEXT("ParameterPanelLibraryViewModel_RenameParameter_NameNone", "Parameter must have a name.");
			return false;
		}
	}

	OutCanRenameParameterToolTip = LOCTEXT("ParameterPanelViewModel_RenameParameter_CreatedInDefinition", "Rename this Parameter for all Systems, Emitters and Scripts using this Definition.");
	return true;
}

const TArray<FNiagaraParameterPanelCategory>& FNiagaraParameterDefinitionsToolkitParameterPanelViewModel::GetDefaultCategories() const
{
	return FNiagaraParameterDefinitionsToolkitParameterPanelViewModel::DefaultCategories;
}

FMenuAndSearchBoxWidgets FNiagaraParameterDefinitionsToolkitParameterPanelViewModel::GetParameterMenu(FNiagaraParameterPanelCategory Category) const
{
	const bool bRequestRename = true;
	const bool bMakeUniqueName = true;

	TSharedPtr<SNiagaraAddParameterFromPanelMenu> MenuWidget = SAssignNew(ParameterMenuWidget, SNiagaraAddParameterFromPanelMenu)
	.OnAddParameter(this, &FNiagaraParameterDefinitionsToolkitParameterPanelViewModel::AddParameter, Category, bRequestRename, bMakeUniqueName)
	.OnAllowMakeType_Static(&INiagaraParameterPanelViewModel::CanMakeNewParameterOfType)
	.NamespaceId(Category.NamespaceMetaData.GetGuid())
	.ShowNamespaceCategory(false)
	.ShowGraphParameters(false)
	.AutoExpandMenu(false);

	ParameterMenuSearchBoxWidget = MenuWidget->GetSearchBox();
	FMenuAndSearchBoxWidgets MenuAndSearchBoxWidgets;
	MenuAndSearchBoxWidgets.MenuWidget = MenuWidget;
	MenuAndSearchBoxWidgets.MenuSearchBoxWidget = ParameterMenuSearchBoxWidget;
	return MenuAndSearchBoxWidgets;
}

FReply FNiagaraParameterDefinitionsToolkitParameterPanelViewModel::HandleDragDropOperation(TSharedPtr<FDragDropOperation> DropOperation) const
{
	ensureMsgf(false, TEXT("Tried to handle drag drop op in parameter definitions parameter panel viewmodel!"));
	return FReply::Handled();
}

bool FNiagaraParameterDefinitionsToolkitParameterPanelViewModel::GetCanHandleDragDropOperation(TSharedPtr<FDragDropOperation> DragDropOperation) const
{
	return false;
}

void FNiagaraParameterDefinitionsToolkitParameterPanelViewModel::OnParameterItemSelected(const FNiagaraParameterPanelItem& SelectedItem, ESelectInfo::Type SelectInfo) const
{
	if (UNiagaraScriptVariable* SelectedScriptVariable = ParameterDefinitionsWeak.Get()->GetScriptVariable(SelectedItem.GetVariable()))
	{
		VariableObjectSelection->SetSelectedObject(SelectedScriptVariable);
	}
}

#undef LOCTEXT_NAMESPACE // "FNiagaraParameterPanelViewModel"
