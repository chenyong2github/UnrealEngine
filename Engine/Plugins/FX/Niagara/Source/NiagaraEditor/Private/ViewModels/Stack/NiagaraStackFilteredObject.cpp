// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewModels/Stack/NiagaraStackFilteredObject.h"

#include "EdGraphSchema_Niagara.h"
#include "NiagaraClipboard.h"
#include "NiagaraDataInterface.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraNodeParameterMapSet.h"
#include "ScopedTransaction.h"
#include "EdGraph/EdGraphPin.h"
#include "ViewModels/NiagaraEmitterViewModel.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/NiagaraScriptViewModel.h"
#include "ViewModels/NiagaraScriptGraphViewModel.h"
#include "ViewModels/Stack/NiagaraStackFunctionInput.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "ViewModels/Stack/NiagaraStackInputCategory.h"
#include "ViewModels/Stack/NiagaraStackModuleItem.h"
#include "ViewModels/Stack/NiagaraStackFunctionInputCollection.h"


#define LOCTEXT_NAMESPACE "UNiagaraStackFilteredObject"


UNiagaraStackFilteredObject::UNiagaraStackFilteredObject()
	: Emitter(nullptr)
{
}

void UNiagaraStackFilteredObject::Initialize(FRequiredEntryData InRequiredEntryData, FString InOwningStackItemEditorDataKey)
{
	checkf(Emitter == nullptr, TEXT("Can only initialize once."));
	FString ObjectStackEditorDataKey = FString::Printf(TEXT("%s-FilteredView"), *InOwningStackItemEditorDataKey);
	Super::Initialize(InRequiredEntryData, InOwningStackItemEditorDataKey, ObjectStackEditorDataKey);

	Emitter = GetEmitterViewModel()->GetEmitter();
	if (Emitter->GetEditorData())
	{
		Cast<UNiagaraEmitterEditorData>(Emitter->GetEditorData())->OnSummaryViewStateChanged().AddUObject(this, &UNiagaraStackFilteredObject::OnViewStateChanged);
	}
}

void UNiagaraStackFilteredObject::FinalizeInternal()
{
	if (Emitter->GetEditorData())
	{
		Cast<UNiagaraEmitterEditorData>(Emitter->GetEditorData())->OnSummaryViewStateChanged().RemoveAll(this);
	}
	
	Super::FinalizeInternal();
}

FText UNiagaraStackFilteredObject::GetDisplayName() const
{
	return LOCTEXT("FilteredInputCollectionDisplayName", "Filtered Inputs");
}

bool UNiagaraStackFilteredObject::GetShouldShowInStack() const
{
	return true;
}

bool UNiagaraStackFilteredObject::GetIsEnabled() const
{
	return true;
}

struct FInputData
{
	const UEdGraphPin* Pin;
	FNiagaraTypeDefinition Type;
	int32 SortKey;
	FText Category;
	bool bIsStatic;
	bool bIsHidden;

	TArray<FInputData*> Children;
	bool bIsChild = false;
};


void UNiagaraStackFilteredObject::ProcessInputsForModule(TMap<FGuid, UNiagaraStackFunctionInputCollection*>& NewKnownInputCollections, TArray<UNiagaraStackEntry*>& NewChildren, UNiagaraNodeFunctionCall* InputFunctionCallNode)
{

}

void UNiagaraStackFilteredObject::RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues)
{
	
	TSharedPtr<FNiagaraEmitterViewModel> ViewModel = GetEmitterViewModel();
	TSharedPtr<FNiagaraScriptViewModel> ScriptViewModelPinned = ViewModel->GetSharedScriptViewModel();
	checkf(ScriptViewModelPinned.IsValid(), TEXT("Can not refresh children when the script view model has been deleted."));

	ENiagaraScriptUsage ScriptUsage = ENiagaraScriptUsage::EmitterUpdateScript;
	FGuid ScriptUsageId = FGuid();

	AppendEmitterCategory(ScriptViewModelPinned, ENiagaraScriptUsage::EmitterSpawnScript, FGuid(), NewChildren, CurrentChildren, NewIssues);
 	AppendEmitterCategory(ScriptViewModelPinned, ENiagaraScriptUsage::EmitterUpdateScript, FGuid(), NewChildren, CurrentChildren, NewIssues);
	AppendEmitterCategory(ScriptViewModelPinned, ENiagaraScriptUsage::ParticleSpawnScript, FGuid(), NewChildren, CurrentChildren, NewIssues);
	AppendEmitterCategory(ScriptViewModelPinned, ENiagaraScriptUsage::ParticleUpdateScript, FGuid(), NewChildren, CurrentChildren, NewIssues);

	Super::RefreshChildrenInternal(CurrentChildren, NewChildren, NewIssues);
}

void UNiagaraStackFilteredObject::AppendEmitterCategory(TSharedPtr<FNiagaraScriptViewModel> ScriptViewModelPinned, ENiagaraScriptUsage ScriptUsage, FGuid ScriptUsageId, TArray<UNiagaraStackEntry*>& NewChildren, const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<FStackIssue>& NewIssues)
{
	UNiagaraGraph* Graph = ScriptViewModelPinned->GetGraphViewModel()->GetGraph();
	FText ErrorMessage;
	if (FNiagaraStackGraphUtilities::ValidateGraphForOutput(*Graph, ScriptUsage, ScriptUsageId, ErrorMessage) == true)
	{
		UNiagaraNodeOutput* MatchingOutputNode = Graph->FindEquivalentOutputNode(ScriptUsage, ScriptUsageId);

		TArray<UNiagaraNodeFunctionCall*> ModuleNodes;
		FNiagaraStackGraphUtilities::GetOrderedModuleNodes(*MatchingOutputNode, ModuleNodes);

		bool bIsFirst = true;
		for (UNiagaraNodeFunctionCall* ModuleNode : ModuleNodes)
		{
			if (ModuleNode && ModuleNode->ScriptIsValid())
			{
				RefreshChildrenForFunctionCall(ModuleNode, ModuleNode, CurrentChildren, NewChildren, NewIssues, true);
			}
		}
	}
}

void UNiagaraStackFilteredObject::PostRefreshChildrenInternal()
{
	Super::PostRefreshChildrenInternal();
}

void UNiagaraStackFilteredObject::OnViewStateChanged()
{
	if (!IsFinalized())
	{
		RefreshChildren();
	}
}


#undef LOCTEXT_NAMESPACE