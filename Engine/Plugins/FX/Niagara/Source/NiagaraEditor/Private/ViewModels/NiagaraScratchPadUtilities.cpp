// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraScratchPadUtilities.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeAssignment.h"
#include "NiagaraEditorUtilities.h"

// TODO - Remove these duplicated functions and their includes.
#include "NiagaraNodeParameterMapBase.h"
#include "NiagaraNodeParameterMapGet.h"
#include "NiagaraNodeParameterMapSet.h"
#include "NiagaraNodeOutput.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"

// This function duplicated here from FNiagaraStackGraphUtilities because its signature needs to be changed to pass the system by pointer, but can't be changed in a point release.
void FNiagaraStackGraphUtilities_FindAffectedScripts(UNiagaraSystem* System, UNiagaraEmitter* Emitter, UNiagaraNodeFunctionCall& ModuleNode, TArray<TWeakObjectPtr<UNiagaraScript>>& OutAffectedScripts)
{
	UNiagaraNodeOutput* OutputNode = FNiagaraStackGraphUtilities::GetEmitterOutputNodeForStackNode(ModuleNode);

	if (OutputNode)
	{
		TArray<UNiagaraScript*> Scripts;
		if (Emitter != nullptr)
		{
			Emitter->GetScripts(Scripts, false);
		}

		if (System != nullptr)
		{
			OutAffectedScripts.Add(System->GetSystemSpawnScript());
			OutAffectedScripts.Add(System->GetSystemUpdateScript());
		}

		for (UNiagaraScript* Script : Scripts)
		{
			if (OutputNode->GetUsage() == ENiagaraScriptUsage::ParticleEventScript)
			{
				if (Script->GetUsage() == ENiagaraScriptUsage::ParticleEventScript && Script->GetUsageId() == OutputNode->GetUsageId())
				{
					OutAffectedScripts.Add(Script);
					break;
				}
			}
			else if (Script->ContainsUsage(OutputNode->GetUsage()))
			{
				OutAffectedScripts.Add(Script);
			}
		}
	}
}

// This function duplicated here from FNiagaraStackGraphUtilities because its signature needs to be changed to pass the system by pointer, but can't be changed in a point release.
void FNiagaraStackGraphUtilities_RenameReferencingParameters(UNiagaraSystem* System, UNiagaraEmitter* Emitter, UNiagaraNodeFunctionCall& FunctionCallNode, const FString& OldModuleName, const FString& NewModuleName)
{
	TMap<FName, FName> OldNameToNewNameMap;
	FNiagaraStackGraphUtilities::GatherRenamedStackFunctionInputAndOutputVariableNames(Emitter, FunctionCallNode, OldModuleName, NewModuleName, OldNameToNewNameMap);

	// local function to rename pins referencing the given module
	auto RenamePinsReferencingModule = [&OldNameToNewNameMap](UNiagaraNodeParameterMapBase* Node)
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			FName* NewName = OldNameToNewNameMap.Find(Pin->PinName);
			if (NewName != nullptr)
			{
				Node->SetPinName(Pin, *NewName);
			}
		}
	};

	UNiagaraNodeParameterMapSet* ParameterMapSet = FNiagaraStackGraphUtilities::GetStackFunctionOverrideNode(FunctionCallNode);
	if (ParameterMapSet != nullptr)
	{
		RenamePinsReferencingModule(ParameterMapSet);
	}

	TArray<TWeakObjectPtr<UNiagaraScript>> Scripts;
	FNiagaraStackGraphUtilities_FindAffectedScripts(System, Emitter, FunctionCallNode, Scripts);

	const UNiagaraNodeOutput* OutputNode = FNiagaraStackGraphUtilities::GetEmitterOutputNodeForStackNode(FunctionCallNode);
	UNiagaraGraph* OwningGraph = FunctionCallNode.GetNiagaraGraph();

	FString OwningEmitterName = Emitter != nullptr ? Emitter->GetUniqueEmitterName() : FString();

	for (TWeakObjectPtr<UNiagaraScript> Script : Scripts)
	{
		if (!Script.IsValid(false))
		{
			continue;
		}

		TArray<FNiagaraVariable> RapidIterationVariables;
		Script->RapidIterationParameters.GetParameters(RapidIterationVariables);

		for (FNiagaraVariable& Variable : RapidIterationVariables)
		{
			FString EmitterName, FunctionCallName, InputName;
			if (FNiagaraParameterMapHistory::SplitRapidIterationParameterName(Variable, EmitterName, FunctionCallName, InputName))
			{
				if (EmitterName == OwningEmitterName && FunctionCallName == OldModuleName)
				{
					FName NewParameterName(*(NewModuleName + TEXT(".") + InputName));
					FNiagaraVariable NewParameter = FNiagaraStackGraphUtilities::CreateRapidIterationParameter(EmitterName, Script->GetUsage(), NewParameterName, Variable.GetType());
					Script->RapidIterationParameters.RenameParameter(Variable, NewParameter.GetName());
				}
			}
		}

		if (UNiagaraScriptSource* ScriptSource = Cast<UNiagaraScriptSource>(Script->GetSource()))
		{
			// rename all parameter map get nodes that use the parameter name
			TArray<UNiagaraNodeParameterMapGet*> ParameterGetNodes;
			ScriptSource->NodeGraph->GetNodesOfClass<UNiagaraNodeParameterMapGet>(ParameterGetNodes);

			for (UNiagaraNodeParameterMapGet* Node : ParameterGetNodes)
			{
				RenamePinsReferencingModule(Node);
			}
		}
	}
}

void FNiagaraScratchPadUtilities::FixFunctionInputsFromFunctionScriptRename(UNiagaraNodeFunctionCall& FunctionCallNode, FName NewScriptName)
{
	FString OldFunctionName = FunctionCallNode.GetFunctionName();
	if (OldFunctionName == NewScriptName.ToString())
	{
		return;
	}

	FunctionCallNode.Modify();
	FunctionCallNode.SuggestName(FString());
	const FString NewFunctionName = FunctionCallNode.GetFunctionName();
	UNiagaraSystem* System = FunctionCallNode.GetTypedOuter<UNiagaraSystem>();
	UNiagaraEmitter* Emitter = FunctionCallNode.GetTypedOuter<UNiagaraEmitter>();
	FNiagaraStackGraphUtilities_RenameReferencingParameters(System, Emitter, FunctionCallNode, OldFunctionName, NewFunctionName);
}

void FNiagaraScratchPadUtilities::FixExternalScratchPadScriptsForEmitter(UNiagaraSystem& SourceSystem, UNiagaraEmitter& TargetEmitter)
{
	UNiagaraSystem* TargetSystem = TargetEmitter.GetTypedOuter<UNiagaraSystem>();
	if (TargetSystem != &SourceSystem)
	{
		UNiagaraScriptSource* EmitterScriptSource = Cast<UNiagaraScriptSource>(TargetEmitter.GraphSource);
		if (ensureMsgf(EmitterScriptSource != nullptr, TEXT("Emitter script source was null. Target Emitter: %s"), *TargetEmitter.GetPathName()))
		{
			// Find function call nodes which reference external scratch pad scripts.
			TArray<UNiagaraNodeFunctionCall*> AllFunctionCallNodes;
			EmitterScriptSource->NodeGraph->GetNodesOfClass<UNiagaraNodeFunctionCall>(AllFunctionCallNodes);

			TArray<UNiagaraNodeFunctionCall*> ExternalScratchFunctionCallNodes;
			for (UNiagaraNodeFunctionCall* FunctionCallNode : AllFunctionCallNodes)
			{
				if (FunctionCallNode->IsA<UNiagaraNodeAssignment>() == false &&
					FunctionCallNode->FunctionScript != nullptr &&
					FunctionCallNode->FunctionScript->IsAsset() == false)
				{
					UNiagaraSystem* ScriptOwningSystem = FunctionCallNode->FunctionScript->GetTypedOuter<UNiagaraSystem>();
					if (ScriptOwningSystem == &SourceSystem)
					{
						ExternalScratchFunctionCallNodes.Add(FunctionCallNode);
					}
				}
			}

			// Determine the destination for the copies of the external scratch pad scripts.
			UObject* TargetScratchPadScriptOuter = nullptr;
			TArray<UNiagaraScript*>* TargetScratchPadScriptArray = nullptr;
			if (TargetEmitter.IsAsset())
			{
				TargetScratchPadScriptOuter = &TargetEmitter;
				TargetScratchPadScriptArray = &TargetEmitter.ScratchPadScripts;
			}
			else if (TargetSystem != nullptr)
			{
				TargetScratchPadScriptOuter = TargetSystem;
				TargetScratchPadScriptArray = &TargetSystem->ScratchPadScripts;
			}

			// Fix up the external scratch scripts.
			if (ensureMsgf(TargetScratchPadScriptOuter != nullptr && TargetScratchPadScriptArray != nullptr,
				TEXT("Failed to find target outer and array for fixing up  external scratch pad scripts.  Target Emitter: %s"), *TargetEmitter.GetPathName()))
			{
				// Collate the function call nodes by the scratch script they call.
				TMap<UNiagaraScript*, TArray<UNiagaraNodeFunctionCall*>> ExternalScratchPadScriptToFunctionCallNodes;
				for (UNiagaraNodeFunctionCall* ExternalScratchFunctionCallNode : ExternalScratchFunctionCallNodes)
				{
					TArray<UNiagaraNodeFunctionCall*>& FunctionCallNodesForScript = ExternalScratchPadScriptToFunctionCallNodes.FindOrAdd(ExternalScratchFunctionCallNode->FunctionScript);
					FunctionCallNodesForScript.Add(ExternalScratchFunctionCallNode);
				}

				// Collect the current scratch pad script names to fix up duplicates.
				TSet<FName> ExistingScratchPadScriptNames;
				for (UNiagaraScript* ExistingScratchPadScript : (*TargetScratchPadScriptArray))
				{
					ExistingScratchPadScriptNames.Add(*ExistingScratchPadScript->GetName());
				}

				// Copy the scripts, rename them, and fix up the function calls.
				for (TPair<UNiagaraScript*, TArray<UNiagaraNodeFunctionCall*>> ExternalScratchPadScriptFunctionCallNodesPair : ExternalScratchPadScriptToFunctionCallNodes)
				{
					UNiagaraScript* ExternalScratchPadScript = ExternalScratchPadScriptFunctionCallNodesPair.Key;
					TArray<UNiagaraNodeFunctionCall*>& FunctionCallNodes = ExternalScratchPadScriptFunctionCallNodesPair.Value;

					FName TargetName = *ExternalScratchPadScript->GetName();
					FName UniqueTargetName = FNiagaraEditorUtilities::GetUniqueObjectName<UNiagaraScript>(TargetScratchPadScriptOuter, TargetName.ToString());
					UNiagaraScript* TargetScratchPadScriptCopy = CastChecked<UNiagaraScript>(StaticDuplicateObject(ExternalScratchPadScript, TargetScratchPadScriptOuter, UniqueTargetName));
					TargetScratchPadScriptArray->Add(TargetScratchPadScriptCopy);

					for (UNiagaraNodeFunctionCall* FunctionCallNode : FunctionCallNodes)
					{
						FunctionCallNode->FunctionScript = TargetScratchPadScriptCopy;
						FixFunctionInputsFromFunctionScriptRename(*FunctionCallNode, UniqueTargetName);
						FunctionCallNode->MarkNodeRequiresSynchronization(TEXT("Fix externally referenced scratch pad scripts."), true);
					}

					ExistingScratchPadScriptNames.Add(UniqueTargetName);
				}
			}
		}
	}
}