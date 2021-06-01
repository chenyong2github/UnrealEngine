// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraScriptMergeManager.h"
#include "NiagaraEmitter.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraSimulationStageBase.h"
#include "NiagaraRendererProperties.h"
#include "EdGraphSchema_Niagara.h"
#include "NiagaraGraph.h"
#include "NiagaraDataInterface.h"
#include "NiagaraNodeInput.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeCustomHlsl.h"
#include "NiagaraNodeAssignment.h"
#include "NiagaraNodeParameterMapGet.h"
#include "NiagaraNodeParameterMapSet.h"
#include "NiagaraEditorUtilities.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "NiagaraClipboard.h"
#include "NiagaraEditorModule.h"
#include "NiagaraEmitterEditorData.h"
#include "NiagaraStackEditorData.h"
#include "Framework/Application/SlateApplication.h"
#include "Interfaces/ISlateNullRendererModule.h"

#include "UObject/PropertyPortFlags.h"

#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Modules/ModuleManager.h"
#include "NiagaraConstants.h"
#include "NiagaraScriptVariable.h"
#include "NiagaraSystemEditorData.h"
#include "NiagaraSystemFactoryNew.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"
#include "ViewModels/Stack/NiagaraStackModuleItem.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"

#define LOCTEXT_NAMESPACE "NiagaraScriptMergeManager"

DECLARE_CYCLE_STAT(TEXT("Niagara - ScriptMergeManager - DiffEmitters"), STAT_NiagaraEditor_ScriptMergeManager_DiffEmitters, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("Niagara - ScriptMergeManager - MergeEmitter"), STAT_NiagaraEditor_ScriptMergeManager_MergeEmitter, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("Niagara - ScriptMergeManager - IsModuleInputDifferentFromBase"), STAT_NiagaraEditor_ScriptMergeManager_IsModuleInputDifferentFromBase, STATGROUP_NiagaraEditor);

int32 GNiagaraForceFailIfPreviouslyNotSetOnMerge = 0;
static FAutoConsoleVariableRef CVarForceErrorIfMissingDefaultOnMergeh(
	TEXT("fx.ForceFailIfPreviouslyNotSetOnMerge"),
	GNiagaraForceFailIfPreviouslyNotSetOnMerge,
	TEXT("If > 0, when merging in from parent emitters swap linked variables in the stack to be \"Fail If Previously Not Set\" for their default type. \n"),
	ECVF_Default
);

FNiagaraStackFunctionInputOverrideMergeAdapter::FNiagaraStackFunctionInputOverrideMergeAdapter(
	const UNiagaraEmitter& InOwningEmitter,
	UNiagaraScript& InOwningScript,
	UNiagaraNodeFunctionCall& InOwningFunctionCallNode,
	UEdGraphPin& InOverridePin
)
	: OwningScript(&InOwningScript)
	, OwningFunctionCallNode(&InOwningFunctionCallNode)
	, OverridePin(&InOverridePin)
	, DataValueObject(nullptr)
{
	
	InputName = FNiagaraParameterHandle(OverridePin->PinName).GetName().ToString();
	OverrideNode = CastChecked<UNiagaraNodeParameterMapSet>(OverridePin->GetOwningNode());
	const UEdGraphSchema_Niagara* NiagaraSchema = GetDefault<UEdGraphSchema_Niagara>();
	Type = NiagaraSchema->PinToTypeDefinition(OverridePin);

	if (OverridePin->LinkedTo.Num() == 0)
	{
		LocalValueString = OverridePin->DefaultValue;
	}
	else if (OverridePin->LinkedTo.Num() == 1)
	{
		OverrideValueNodePersistentId = OverridePin->LinkedTo[0]->GetOwningNode()->NodeGuid;

		if (OverridePin->LinkedTo[0]->GetOwningNode()->IsA<UNiagaraNodeParameterMapGet>())
		{
			LinkedValueHandle = FNiagaraParameterHandle(OverridePin->LinkedTo[0]->PinName);
		}
		else if (OverridePin->LinkedTo[0]->GetOwningNode()->IsA<UNiagaraNodeInput>())
		{
			UNiagaraNodeInput* DataInputNode = CastChecked<UNiagaraNodeInput>(OverridePin->LinkedTo[0]->GetOwningNode());
			DataValueInputName = DataInputNode->Input.GetName();
			DataValueObject = DataInputNode->GetDataInterface();
		}
		else if (OverridePin->LinkedTo[0]->GetOwningNode()->IsA<UNiagaraNodeFunctionCall>())
		{
			DynamicValueFunction = MakeShared<FNiagaraStackFunctionMergeAdapter>(InOwningEmitter, *OwningScript.Get(), *CastChecked<UNiagaraNodeFunctionCall>(OverridePin->LinkedTo[0]->GetOwningNode()), INDEX_NONE);
		}
		else
		{
			UE_LOG(LogNiagaraEditor, Error, TEXT("Invalid Stack Graph - Unsupported input node connection. Owning Node: %s"), *OverrideNode->GetPathName());
		}
	}
	else
	{
		UE_LOG(LogNiagaraEditor, Error, TEXT("Invalid Stack Graph - Input had multiple connections. Owning Node: %s"), *OverrideNode->GetPathName());
	}
}

FNiagaraStackFunctionInputOverrideMergeAdapter::FNiagaraStackFunctionInputOverrideMergeAdapter(
	UNiagaraScript& InOwningScript,
	UNiagaraNodeFunctionCall& InOwningFunctionCallNode,
	FStringView InInputName,
	FNiagaraVariable InRapidIterationParameter
)
	: OwningScript(&InOwningScript)
	, OwningFunctionCallNode(&InOwningFunctionCallNode)
	, InputName(InInputName)
	, Type(InRapidIterationParameter.GetType())
	, OverridePin(nullptr)
	, LocalValueRapidIterationParameter(InRapidIterationParameter)
	, DataValueObject(nullptr)
{
}

FNiagaraStackFunctionInputOverrideMergeAdapter::FNiagaraStackFunctionInputOverrideMergeAdapter(UEdGraphPin* InStaticSwitchPin)
	: OwningScript(nullptr)
	, OwningFunctionCallNode(CastChecked<UNiagaraNodeFunctionCall>(InStaticSwitchPin->GetOwningNode()))
	, InputName(InStaticSwitchPin->PinName.ToString())
	, OverridePin(nullptr)
	, DataValueObject(nullptr)
	, StaticSwitchValue(InStaticSwitchPin->DefaultValue)
{
	const UEdGraphSchema_Niagara* NiagaraSchema = GetDefault<UEdGraphSchema_Niagara>();
	Type = NiagaraSchema->PinToTypeDefinition(InStaticSwitchPin);
}

UNiagaraScript* FNiagaraStackFunctionInputOverrideMergeAdapter::GetOwningScript() const
{
	return OwningScript.Get();
}

UNiagaraNodeFunctionCall* FNiagaraStackFunctionInputOverrideMergeAdapter::GetOwningFunctionCall() const
{
	return OwningFunctionCallNode.Get();
}

FString FNiagaraStackFunctionInputOverrideMergeAdapter::GetInputName() const
{
	return InputName;
}

UNiagaraNodeParameterMapSet* FNiagaraStackFunctionInputOverrideMergeAdapter::GetOverrideNode() const
{
	return OverrideNode.Get();
}

const FNiagaraTypeDefinition& FNiagaraStackFunctionInputOverrideMergeAdapter::GetType() const
{
	return Type;
}

UEdGraphPin* FNiagaraStackFunctionInputOverrideMergeAdapter::GetOverridePin() const
{
	return OverridePin;
}

const FGuid& FNiagaraStackFunctionInputOverrideMergeAdapter::GetOverrideNodeId() const
{
	return OverrideValueNodePersistentId;
}

TOptional<FString> FNiagaraStackFunctionInputOverrideMergeAdapter::GetLocalValueString() const
{
	return LocalValueString;
}

TOptional<FNiagaraVariable> FNiagaraStackFunctionInputOverrideMergeAdapter::GetLocalValueRapidIterationParameter() const
{
	return LocalValueRapidIterationParameter;
}

TOptional<FNiagaraParameterHandle> FNiagaraStackFunctionInputOverrideMergeAdapter::GetLinkedValueHandle() const
{
	return LinkedValueHandle;
}

TOptional<FName> FNiagaraStackFunctionInputOverrideMergeAdapter::GetDataValueInputName() const
{
	return DataValueInputName;
}

UNiagaraDataInterface* FNiagaraStackFunctionInputOverrideMergeAdapter::GetDataValueObject() const
{
	return DataValueObject;
}

TSharedPtr<FNiagaraStackFunctionMergeAdapter> FNiagaraStackFunctionInputOverrideMergeAdapter::GetDynamicValueFunction() const
{
	return DynamicValueFunction;
}

TOptional<FString> FNiagaraStackFunctionInputOverrideMergeAdapter::GetStaticSwitchValue() const
{
	return StaticSwitchValue;
}

FNiagaraStackFunctionMergeAdapter::FNiagaraStackFunctionMergeAdapter(const UNiagaraEmitter& InOwningEmitter, UNiagaraScript& InOwningScript, UNiagaraNodeFunctionCall& InFunctionCallNode, int32 InStackIndex)
{
	OwningScript = &InOwningScript;
	FunctionCallNode = &InFunctionCallNode;
	StackIndex = InStackIndex;

	bUsesScratchPadScript = InOwningEmitter.ScratchPadScripts.Contains(FunctionCallNode->FunctionScript) ||
		InOwningEmitter.ParentScratchPadScripts.Contains(FunctionCallNode->FunctionScript);

	FString UniqueEmitterName = InOwningEmitter.GetUniqueEmitterName();

	// Collect explicit overrides set via parameter map set nodes.
	TSet<FName> AliasedInputsAdded;
	UNiagaraNodeParameterMapSet* OverrideNode = FNiagaraStackGraphUtilities::GetStackFunctionOverrideNode(*FunctionCallNode);
	if (OverrideNode != nullptr)
	{
		for(UEdGraphPin* OverridePin : FNiagaraStackGraphUtilities::GetOverridePinsForFunction(*OverrideNode, *FunctionCallNode))
		{
			InputOverrides.Add(MakeShared<FNiagaraStackFunctionInputOverrideMergeAdapter>(InOwningEmitter, *OwningScript.Get(), *FunctionCallNode.Get(), *OverridePin));
			AliasedInputsAdded.Add(*OverridePin->PinName.ToString());
		}
	}

	// If we have a valid function script collect up the default values of the rapid iteration parameters so that default values in the parameter store
	// can be ignored since they're not actually overrides.  This is usually not an issue due to the PreparateRapidIterationParameters call in the emitter
	// editor, but modifications to modules can cause inconsistency here in the emitter.
	TArray<FNiagaraVariable> RapidIterationInputDefaultValues;
	const UEdGraphSchema_Niagara* NiagaraSchema = GetDefault<UEdGraphSchema_Niagara>();
	if (InFunctionCallNode.FunctionScript != nullptr)
	{
		TSet<const UEdGraphPin*> HiddenPins;
		FCompileConstantResolver Resolver(&InOwningEmitter, ENiagaraScriptUsage::Function);
		TArray<const UEdGraphPin*> FunctionInputPins;
		FNiagaraStackGraphUtilities::GetStackFunctionInputPins(*FunctionCallNode, FunctionInputPins, HiddenPins, Resolver, FNiagaraStackGraphUtilities::ENiagaraGetStackFunctionInputPinsOptions::ModuleInputsOnly, false);

		for (const UEdGraphPin* FunctionInputPin : FunctionInputPins)
		{
			FNiagaraVariable FunctionInputVariable = NiagaraSchema->PinToNiagaraVariable(FunctionInputPin);
			if (FunctionInputVariable.IsValid() && FNiagaraStackGraphUtilities::IsRapidIterationType(FunctionInputVariable.GetType()))
			{
				FCompileConstantResolver ConstantResolver(&InOwningEmitter, FNiagaraStackGraphUtilities::GetEmitterOutputNodeForStackNode(*FunctionCallNode)->GetUsage());
				UEdGraphPin* FunctionInputDefaultPin = FunctionCallNode->FindParameterMapDefaultValuePin(FunctionInputPin->PinName, OwningScript->GetUsage(), ConstantResolver);
				if (FunctionInputDefaultPin != nullptr)
				{
					// Try to get the default value from the default pin.
					FNiagaraVariable FunctionInputDefaultVariable = NiagaraSchema->PinToNiagaraVariable(FunctionInputDefaultPin);
					if (FunctionInputDefaultVariable.GetData() != nullptr)
					{
						FunctionInputVariable.SetData(FunctionInputDefaultVariable.GetData());
					}
				}

				if (FunctionInputVariable.GetData() == nullptr)
				{
					// If the pin didn't have a default value then use the type default.
					FNiagaraEditorUtilities::ResetVariableToDefaultValue(FunctionInputVariable);
				}

				if (FunctionInputVariable.GetData() != nullptr)
				{
					FNiagaraParameterHandle AliasedFunctionInputHandle = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(FNiagaraParameterHandle(FunctionInputVariable.GetName()), &InFunctionCallNode);
					FNiagaraVariable FunctionInputRapidIterationParameter =
						FNiagaraStackGraphUtilities::CreateRapidIterationParameter(UniqueEmitterName, OwningScript->GetUsage(), AliasedFunctionInputHandle.GetParameterHandleString(), FunctionInputVariable.GetType());
					FunctionInputRapidIterationParameter.SetData(FunctionInputVariable.GetData());
					RapidIterationInputDefaultValues.Add(FunctionInputRapidIterationParameter);
				}
			}
		}
	}

	// Collect rapid iteration parameters which aren't at the function default values.
	FString RapidIterationParameterNamePrefix = TEXT("Constants." + UniqueEmitterName + ".");
	TArray<FNiagaraVariable> RapidIterationParameters;
	OwningScript->RapidIterationParameters.GetParameters(RapidIterationParameters);
	for (const FNiagaraVariable& RapidIterationParameter : RapidIterationParameters)
	{
		FNiagaraParameterHandle AliasedInputHandle(*RapidIterationParameter.GetName().ToString().RightChop(RapidIterationParameterNamePrefix.Len()));
		if (AliasedInputHandle.GetNamespace().ToString() == FunctionCallNode->GetFunctionName())
		{
			// Currently rapid iteration parameters for assignment nodes in emitter scripts get double aliased which prevents their inputs from
			// being diffed correctly, so we need to un-mangle the names here so that the diffs are correct.
			if (FunctionCallNode->IsA<UNiagaraNodeAssignment>() &&
				(OwningScript->GetUsage() == ENiagaraScriptUsage::EmitterSpawnScript || OwningScript->GetUsage() == ENiagaraScriptUsage::EmitterUpdateScript))
			{
				FString InputName = AliasedInputHandle.GetName().ToString();
				if (InputName.StartsWith(UniqueEmitterName + TEXT(".")))
				{
					FString UnaliasedInputName = TEXT("Emitter") + InputName.RightChop(UniqueEmitterName.Len());
					AliasedInputHandle = FNiagaraParameterHandle(AliasedInputHandle.GetNamespace(), *UnaliasedInputName);
				}
			}

			if (AliasedInputsAdded.Contains(AliasedInputHandle.GetParameterHandleString()) == false)
			{
				// Check if the input is at the current default and if so it can be skipped.
				bool bMatchesDefault = false;
				FNiagaraVariable* RapidIterationInputDefaultValue = RapidIterationInputDefaultValues.FindByPredicate([RapidIterationParameter](const FNiagaraVariable& DefaultValue) 
					{ return DefaultValue.GetName() == RapidIterationParameter.GetName() && DefaultValue.GetType() == RapidIterationParameter.GetType(); });
				if (RapidIterationInputDefaultValue != nullptr)
				{
					const uint8* CurrentValueData = OwningScript->RapidIterationParameters.GetParameterData(RapidIterationParameter);
					if (CurrentValueData != nullptr)
					{
						bMatchesDefault = FMemory::Memcmp(CurrentValueData, RapidIterationInputDefaultValue->GetData(), RapidIterationParameter.GetSizeInBytes()) == 0;
					}
				}

				if (bMatchesDefault == false)
				{
					InputOverrides.Add(MakeShared<FNiagaraStackFunctionInputOverrideMergeAdapter>(*OwningScript.Get(), *FunctionCallNode.Get(), AliasedInputHandle.GetName().ToString(), RapidIterationParameter));
				}
			}
		}
	}

	TArray<UEdGraphPin*> StaticSwitchPins;
	TSet<UEdGraphPin*> StaticSwitchPinsHidden;
	FNiagaraStackGraphUtilities::GetStackFunctionStaticSwitchPins(*FunctionCallNode.Get(), StaticSwitchPins, StaticSwitchPinsHidden);
	for (UEdGraphPin* StaticSwitchPin : StaticSwitchPins)
	{
		// TODO: Only add static switch overrides when the current value is different from the default.  This requires a 
		// refactor of the static switch default storage to use the same data format as FNiagaraVariables.
		InputOverrides.Add(MakeShared<FNiagaraStackFunctionInputOverrideMergeAdapter>(StaticSwitchPin));
	}
}

UNiagaraNodeFunctionCall* FNiagaraStackFunctionMergeAdapter::GetFunctionCallNode() const
{
	return FunctionCallNode.Get();
}

int32 FNiagaraStackFunctionMergeAdapter::GetStackIndex() const
{
	return StackIndex;
}

bool FNiagaraStackFunctionMergeAdapter::GetUsesScratchPadScript() const
{
	return bUsesScratchPadScript;
}

const TArray<TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter>>& FNiagaraStackFunctionMergeAdapter::GetInputOverrides() const
{
	return InputOverrides;
}

TSharedPtr<FNiagaraStackFunctionInputOverrideMergeAdapter> FNiagaraStackFunctionMergeAdapter::GetInputOverrideByInputName(FString InputName) const
{
	for (TSharedPtr<FNiagaraStackFunctionInputOverrideMergeAdapter> InputOverride : InputOverrides)
	{
		if (InputOverride->GetInputName() == InputName)
		{
			return InputOverride;
		}
	}
	return TSharedPtr<FNiagaraStackFunctionInputOverrideMergeAdapter>();
}

void FNiagaraStackFunctionMergeAdapter::GatherFunctionCallNodes(TArray<UNiagaraNodeFunctionCall*>& OutFunctionCallNodes) const
{
	if (FunctionCallNode.IsValid())
	{
		OutFunctionCallNodes.Add(FunctionCallNode.Get());
	}

	for (TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter> InputOverride : InputOverrides)
	{
		if (InputOverride->GetDynamicValueFunction().IsValid())
		{
			InputOverride->GetDynamicValueFunction()->GatherFunctionCallNodes(OutFunctionCallNodes);
		}
	}
}

const TArray<FNiagaraStackMessage>& FNiagaraStackFunctionMergeAdapter::GetMessages() const
{
	return FunctionCallNode->GetCustomNotes();
}

FNiagaraScriptStackMergeAdapter::FNiagaraScriptStackMergeAdapter(const UNiagaraEmitter& InOwningEmitter, UNiagaraNodeOutput& InOutputNode, UNiagaraScript& InScript)
{
	OutputNode = &InOutputNode;
	InputNode.Reset();
	Script = &InScript;
	UniqueEmitterName = InOwningEmitter.GetUniqueEmitterName();

	TArray<FNiagaraStackGraphUtilities::FStackNodeGroup> StackGroups;
	FNiagaraStackGraphUtilities::GetStackNodeGroups(*OutputNode, StackGroups);

	if (StackGroups.Num() >= 2 && StackGroups[0].EndNode->IsA<UNiagaraNodeInput>())
	{
		InputNode = Cast<UNiagaraNodeInput>(StackGroups[0].EndNode);
	}

	if (StackGroups.Num() > 2 && StackGroups[0].EndNode->IsA<UNiagaraNodeInput>() && StackGroups.Last().EndNode->IsA<UNiagaraNodeOutput>())
	{
		for (int i = 1; i < StackGroups.Num() - 1; i++)
		{
			UNiagaraNodeFunctionCall* ModuleFunctionCallNode = Cast<UNiagaraNodeFunctionCall>(StackGroups[i].EndNode);
			if (ModuleFunctionCallNode != nullptr)
			{
				// The first stack node group is the input node, so we subtract one to get the index of the module.
				int32 StackIndex = i - 1;
				ModuleFunctions.Add(MakeShared<FNiagaraStackFunctionMergeAdapter>(InOwningEmitter, *Script.Get(), *ModuleFunctionCallNode, StackIndex));
			}
		}
	}
}

UNiagaraNodeInput* FNiagaraScriptStackMergeAdapter::GetInputNode() const
{
	return InputNode.Get();
}

UNiagaraNodeOutput* FNiagaraScriptStackMergeAdapter::GetOutputNode() const
{
	return OutputNode.Get();
}

UNiagaraScript* FNiagaraScriptStackMergeAdapter::GetScript() const
{
	return Script.Get();
}

FString FNiagaraScriptStackMergeAdapter::GetUniqueEmitterName() const
{
	return UniqueEmitterName;
}

const TArray<TSharedRef<FNiagaraStackFunctionMergeAdapter>>& FNiagaraScriptStackMergeAdapter::GetModuleFunctions() const
{
	return ModuleFunctions;
}

TSharedPtr<FNiagaraStackFunctionMergeAdapter> FNiagaraScriptStackMergeAdapter::GetModuleFunctionById(FGuid FunctionCallNodeId) const
{
	for (TSharedRef<FNiagaraStackFunctionMergeAdapter> Modulefunction : ModuleFunctions)
	{
		if (Modulefunction->GetFunctionCallNode()->NodeGuid == FunctionCallNodeId)
		{
			return Modulefunction;
		}
	}
	return TSharedPtr<FNiagaraStackFunctionMergeAdapter>();
}

void FNiagaraScriptStackMergeAdapter::GatherFunctionCallNodes(TArray<UNiagaraNodeFunctionCall*>& OutFunctionCallNodes) const
{
	for (TSharedRef<FNiagaraStackFunctionMergeAdapter> ModuleFunction : ModuleFunctions)
	{
		ModuleFunction->GatherFunctionCallNodes(OutFunctionCallNodes);
	}
}

FNiagaraEventHandlerMergeAdapter::FNiagaraEventHandlerMergeAdapter(const UNiagaraEmitter& InEmitter, const FNiagaraEventScriptProperties* InEventScriptProperties, UNiagaraNodeOutput* InOutputNode)
{
	Initialize(InEmitter, InEventScriptProperties, nullptr, InOutputNode);
}

FNiagaraEventHandlerMergeAdapter::FNiagaraEventHandlerMergeAdapter(const UNiagaraEmitter& InEmitter, FNiagaraEventScriptProperties* InEventScriptProperties, UNiagaraNodeOutput* InOutputNode)
{
	Initialize(InEmitter, InEventScriptProperties, InEventScriptProperties, InOutputNode);
}

FNiagaraEventHandlerMergeAdapter::FNiagaraEventHandlerMergeAdapter(const UNiagaraEmitter& InEmitter, UNiagaraNodeOutput* InOutputNode)
{
	Initialize(InEmitter, nullptr, nullptr, InOutputNode);
}

void FNiagaraEventHandlerMergeAdapter::Initialize(const UNiagaraEmitter& InEmitter, const FNiagaraEventScriptProperties* InEventScriptProperties, FNiagaraEventScriptProperties* InEditableEventScriptProperties, UNiagaraNodeOutput* InOutputNode)
{
	Emitter = MakeWeakObjectPtr(const_cast<UNiagaraEmitter*>(&InEmitter));

	EventScriptProperties = InEventScriptProperties;
	EditableEventScriptProperties = InEditableEventScriptProperties;

	OutputNode = InOutputNode;

	if (EventScriptProperties != nullptr && OutputNode != nullptr)
	{
		EventStack = MakeShared<FNiagaraScriptStackMergeAdapter>(*Emitter.Get(), *OutputNode.Get(), *EventScriptProperties->Script);
		InputNode = EventStack->GetInputNode();
	}
}

const UNiagaraEmitter* FNiagaraEventHandlerMergeAdapter::GetEmitter() const
{
	return Emitter.Get();
}

FGuid FNiagaraEventHandlerMergeAdapter::GetUsageId() const
{
	if (EventScriptProperties != nullptr)
	{
		return EventScriptProperties->Script->GetUsageId();
	}
	else
	{
		return OutputNode->GetUsageId();
	}
}

const FNiagaraEventScriptProperties* FNiagaraEventHandlerMergeAdapter::GetEventScriptProperties() const
{
	return EventScriptProperties;
}

FNiagaraEventScriptProperties* FNiagaraEventHandlerMergeAdapter::GetEditableEventScriptProperties() const
{
	return EditableEventScriptProperties;
}

UNiagaraNodeOutput* FNiagaraEventHandlerMergeAdapter::GetOutputNode() const
{
	return OutputNode.Get();
}

UNiagaraNodeInput* FNiagaraEventHandlerMergeAdapter::GetInputNode() const
{
	return InputNode.Get();
}

TSharedPtr<FNiagaraScriptStackMergeAdapter> FNiagaraEventHandlerMergeAdapter::GetEventStack() const
{
	return EventStack;
}

FNiagaraSimulationStageMergeAdapter::FNiagaraSimulationStageMergeAdapter(const UNiagaraEmitter& InEmitter, const UNiagaraSimulationStageBase* InSimulationStage, int32 InSimulationStageIndex, UNiagaraNodeOutput* InOutputNode)
{
	Initialize(InEmitter, InSimulationStage, nullptr, InSimulationStageIndex, InOutputNode);
}

FNiagaraSimulationStageMergeAdapter::FNiagaraSimulationStageMergeAdapter(const UNiagaraEmitter& InEmitter, UNiagaraSimulationStageBase* InSimulationStage, int32 InSimulationStageIndex, UNiagaraNodeOutput* InOutputNode)
{
	Initialize(InEmitter, InSimulationStage, InSimulationStage, InSimulationStageIndex, InOutputNode);
}

FNiagaraSimulationStageMergeAdapter::FNiagaraSimulationStageMergeAdapter(const UNiagaraEmitter& InEmitter, UNiagaraNodeOutput* InOutputNode)
{
	Initialize(InEmitter, nullptr, nullptr, INDEX_NONE, InOutputNode);
}

void FNiagaraSimulationStageMergeAdapter::Initialize(const UNiagaraEmitter& InEmitter, const UNiagaraSimulationStageBase* InSimulationStage, UNiagaraSimulationStageBase* InEditableSimulationStage, int32 InSimulationStageIndex, UNiagaraNodeOutput* InOutputNode)
{
	Emitter = MakeWeakObjectPtr(const_cast<UNiagaraEmitter*>(&InEmitter));

	SimulationStage = InSimulationStage;
	EditableSimulationStage = InEditableSimulationStage;

	OutputNode = InOutputNode;
	SimulationStageIndex = InSimulationStageIndex;

	if (SimulationStage != nullptr && OutputNode != nullptr)
	{
		SimulationStageStack = MakeShared<FNiagaraScriptStackMergeAdapter>(*Emitter.Get(), *OutputNode.Get(), *SimulationStage->Script);
		InputNode = SimulationStageStack->GetInputNode();
	}
}

const UNiagaraEmitter* FNiagaraSimulationStageMergeAdapter::GetEmitter() const
{
	return Emitter.Get();
}

FGuid FNiagaraSimulationStageMergeAdapter::GetUsageId() const
{
	if (SimulationStage != nullptr)
	{
		return SimulationStage->Script->GetUsageId();
	}
	else
	{
		return OutputNode->GetUsageId();
	}
}

const UNiagaraSimulationStageBase* FNiagaraSimulationStageMergeAdapter::GetSimulationStage() const
{
	return SimulationStage;
}

UNiagaraSimulationStageBase* FNiagaraSimulationStageMergeAdapter::GetEditableSimulationStage() const
{
	return EditableSimulationStage;
}

UNiagaraNodeOutput* FNiagaraSimulationStageMergeAdapter::GetOutputNode() const
{
	return OutputNode.Get();
}

UNiagaraNodeInput* FNiagaraSimulationStageMergeAdapter::GetInputNode() const
{
	return InputNode.Get();
}

int32 FNiagaraSimulationStageMergeAdapter::GetSimulationStageIndex() const
{
	return SimulationStageIndex;
}

TSharedPtr<FNiagaraScriptStackMergeAdapter> FNiagaraSimulationStageMergeAdapter::GetSimulationStageStack() const
{
	return SimulationStageStack;
}

FNiagaraRendererMergeAdapter::FNiagaraRendererMergeAdapter(UNiagaraRendererProperties& InRenderer)
{
	Renderer = &InRenderer;
}

UNiagaraRendererProperties* FNiagaraRendererMergeAdapter::GetRenderer()
{
	return Renderer.Get();
}

FNiagaraScratchPadMergeAdapter::FNiagaraScratchPadMergeAdapter()
	: TargetEmitter(nullptr)
	, InstanceEmitter(nullptr)
	, ParentEmitter(nullptr)
	, bIsInitialized(false)
{
}

FNiagaraScratchPadMergeAdapter::FNiagaraScratchPadMergeAdapter(UNiagaraEmitter* InTargetEmitter, UNiagaraEmitter* InInstanceEmitter, UNiagaraEmitter* InParentEmitter)
	: TargetEmitter(InTargetEmitter)
	, InstanceEmitter(InInstanceEmitter)
	, ParentEmitter(InParentEmitter)
	, bIsInitialized(false)
{
}

UNiagaraScript* FNiagaraScratchPadMergeAdapter::GetScratchPadScriptForFunctionId(FGuid FunctionId)
{
	if (bIsInitialized == false)
	{
		Initialize();
	}

	UNiagaraScript** ParentScratchPadScriptPtr = FunctionIdToScratchPadScript.Find(FunctionId);
	return ParentScratchPadScriptPtr != nullptr ? *ParentScratchPadScriptPtr : nullptr;
}

void FNiagaraScratchPadMergeAdapter::Initialize()
{
	if (TargetEmitter->ScratchPadScripts.Num() > 0 || TargetEmitter->ParentScratchPadScripts.Num() > 0)
	{
		// Collect the parent emitters in order
		TArray<UNiagaraEmitter*> ParentEmitters;
		UNiagaraEmitter* CurrentParent = ParentEmitter;
		while (CurrentParent != nullptr)
		{
			ParentEmitters.Insert(CurrentParent, 0);
			CurrentParent = CurrentParent->GetParent();
		}

		// Create a mapping from the actual parent scratch pad scripts to their copies in the target emitter.
		TMap<UNiagaraScript*, UNiagaraScript*> SourceScratchPadScriptToTargetCopyScratchPadScript;
		int32 TargetCopyIndex = 0;
		for (UNiagaraEmitter* CurrentParentEmitter : ParentEmitters)
		{
			TArray<UNiagaraScript*> ParentScratchPadScripts;
			if (CurrentParentEmitter->GetParent() == nullptr)
			{
				ParentScratchPadScripts.Append(CurrentParentEmitter->ParentScratchPadScripts);
			}
			ParentScratchPadScripts.Append(CurrentParentEmitter->ScratchPadScripts);

			for (UNiagaraScript* ParentScratchPadScript : ParentScratchPadScripts)
			{
				UNiagaraScript* TargetCopy;
				if (ensureMsgf(TargetCopyIndex < TargetEmitter->ParentScratchPadScripts.Num(), TEXT("Parent scratch pad script was missing from the Target's copies")))
				{
					TargetCopy = TargetEmitter->ParentScratchPadScripts[TargetCopyIndex];
				}
				else
				{
					TargetCopy = nullptr;
				}
				SourceScratchPadScriptToTargetCopyScratchPadScript.Add(ParentScratchPadScript, TargetCopy);
				TargetCopyIndex++;
			}
		}

		// Create a mapping from the instance scratch pad scripts to their copies in the target emitter.
		if (InstanceEmitter != nullptr)
		{
			for (int32 InstanceScratchPadScriptIndex = 0; InstanceScratchPadScriptIndex < InstanceEmitter->ScratchPadScripts.Num(); InstanceScratchPadScriptIndex++)
			{
				UNiagaraScript* TargetCopy;
				if (ensureMsgf(InstanceScratchPadScriptIndex < TargetEmitter->ScratchPadScripts.Num(), TEXT("Instance scratch pad script was missing from the Target's copies.")))
				{
					TargetCopy = TargetEmitter->ScratchPadScripts[InstanceScratchPadScriptIndex];
				}
				else
				{
					TargetCopy = nullptr;
				}
				SourceScratchPadScriptToTargetCopyScratchPadScript.Add(InstanceEmitter->ScratchPadScripts[InstanceScratchPadScriptIndex], TargetCopy);
			}
		}

		// For each source emitter collect up the traversed function ids that use the source scratch pad scripts and cache the corresponding target copies so that
		// any usages encountered when applying the diff can be hooked up correctly.
		TArray<UNiagaraEmitter*> SourceEmitters;
		SourceEmitters.Append(ParentEmitters);
		if (InstanceEmitter != nullptr)
		{
			SourceEmitters.Add(InstanceEmitter);
		}
		for (UNiagaraEmitter* SourceEmitter : SourceEmitters)
		{
			if (SourceEmitter->ScratchPadScripts.Num() > 0 || (SourceEmitter->GetParent() == nullptr && SourceEmitter->ParentScratchPadScripts.Num() > 0))
			{
				UNiagaraScriptSource* ScriptSource = Cast<UNiagaraScriptSource>(SourceEmitter->GraphSource);
				if (ScriptSource != nullptr)
				{
					TArray<UNiagaraNodeOutput*> OutputNodes;
					ScriptSource->NodeGraph->GetNodesOfClass(OutputNodes);
					for (UNiagaraNodeOutput* OutputNode : OutputNodes)
					{
						TArray<UNiagaraNode*> TraversedNodes;
						UNiagaraGraph::BuildTraversal(TraversedNodes, OutputNode, false);
						for (UNiagaraNode* TraversedNode : TraversedNodes)
						{
							UNiagaraNodeFunctionCall* TraversedFunctionNode = Cast<UNiagaraNodeFunctionCall>(TraversedNode);
							if (TraversedFunctionNode != nullptr &&
								TraversedFunctionNode->FunctionScript != nullptr &&
								TraversedFunctionNode->FunctionScript->IsAsset() == false &&
								TraversedFunctionNode->GetClass() == UNiagaraNodeFunctionCall::StaticClass())
							{
								UNiagaraScript* ScratchPadScript = TraversedFunctionNode->FunctionScript;
								if (SourceEmitter->ScratchPadScripts.Contains(ScratchPadScript) ||
									(SourceEmitter->GetParent() == nullptr && SourceEmitter->ParentScratchPadScripts.Contains(ScratchPadScript)))
								{
									FunctionIdToScratchPadScript.Add(TraversedFunctionNode->NodeGuid, SourceScratchPadScriptToTargetCopyScratchPadScript[TraversedFunctionNode->FunctionScript]);
								}
							}
						}
					}
				}
			}
		}
	}

	bIsInitialized = true;
}

FNiagaraEmitterMergeAdapter::FNiagaraEmitterMergeAdapter(const UNiagaraEmitter& InEmitter)
{
	Initialize(InEmitter, nullptr);
}

FNiagaraEmitterMergeAdapter::FNiagaraEmitterMergeAdapter(UNiagaraEmitter& InEmitter)
{
	Initialize(InEmitter, &InEmitter);
}

void FNiagaraEmitterMergeAdapter::Initialize(const UNiagaraEmitter& InEmitter, UNiagaraEmitter* InEditableEmitter)
{
	Emitter = &InEmitter;
	EditableEmitter = InEditableEmitter;
	UNiagaraScriptSource* EmitterScriptSource = Cast<UNiagaraScriptSource>(Emitter->GraphSource);
	UNiagaraGraph* Graph = EmitterScriptSource->NodeGraph;
	TArray<UNiagaraNodeOutput*> OutputNodes;
	Graph->GetNodesOfClass<UNiagaraNodeOutput>(OutputNodes);

	TArray<UNiagaraNodeOutput*> EventOutputNodes;
	TArray<UNiagaraNodeOutput*> SimulationStageOutputNodes;
	for (UNiagaraNodeOutput* OutputNode : OutputNodes)
	{
		if (UNiagaraScript::IsEquivalentUsage(OutputNode->GetUsage(), ENiagaraScriptUsage::EmitterSpawnScript))
		{
			EmitterSpawnStack = MakeShared<FNiagaraScriptStackMergeAdapter>(*Emitter.Get(), *OutputNode, *Emitter->EmitterSpawnScriptProps.Script);
		}
		else if (UNiagaraScript::IsEquivalentUsage(OutputNode->GetUsage(), ENiagaraScriptUsage::EmitterUpdateScript))
		{
			EmitterUpdateStack = MakeShared<FNiagaraScriptStackMergeAdapter>(*Emitter.Get(), *OutputNode, *Emitter->EmitterUpdateScriptProps.Script);
		}
		else if (UNiagaraScript::IsEquivalentUsage(OutputNode->GetUsage(), ENiagaraScriptUsage::ParticleSpawnScript))
		{
			ParticleSpawnStack = MakeShared<FNiagaraScriptStackMergeAdapter>(*Emitter.Get(), *OutputNode, *Emitter->SpawnScriptProps.Script);
		}
		else if (UNiagaraScript::IsEquivalentUsage(OutputNode->GetUsage(), ENiagaraScriptUsage::ParticleUpdateScript))
		{
			ParticleUpdateStack = MakeShared<FNiagaraScriptStackMergeAdapter>(*Emitter.Get(), *OutputNode, *Emitter->UpdateScriptProps.Script);
		}
		else if(UNiagaraScript::IsEquivalentUsage(OutputNode->GetUsage(), ENiagaraScriptUsage::ParticleEventScript))
		{
			EventOutputNodes.Add(OutputNode);
		}
		else if (UNiagaraScript::IsEquivalentUsage(OutputNode->GetUsage(), ENiagaraScriptUsage::ParticleSimulationStageScript))
		{
			SimulationStageOutputNodes.Add(OutputNode);
		}
	}

	// Create an event handler adapter for each usage id even if it's missing an event script properties struct or an output node.  These
	// incomplete adapters will be caught if they are diffed.
	for (const FNiagaraEventScriptProperties& EventScriptProperties : Emitter->GetEventHandlers())
	{
		UNiagaraNodeOutput** MatchingOutputNodePtr = EventOutputNodes.FindByPredicate(
			[=](UNiagaraNodeOutput* EventOutputNode) { return EventOutputNode->GetUsageId() == EventScriptProperties.Script->GetUsageId(); });

		UNiagaraNodeOutput* MatchingOutputNode = MatchingOutputNodePtr != nullptr ? *MatchingOutputNodePtr : nullptr;

		if (EditableEmitter == nullptr)
		{
			EventHandlers.Add(MakeShared<FNiagaraEventHandlerMergeAdapter>(*Emitter.Get(), &EventScriptProperties, MatchingOutputNode));
		}
		else
		{
			FNiagaraEventScriptProperties* EditableEventScriptProperties = EditableEmitter->GetEventHandlerByIdUnsafe(EventScriptProperties.Script->GetUsageId());
			EventHandlers.Add(MakeShared<FNiagaraEventHandlerMergeAdapter>(*Emitter.Get(), EditableEventScriptProperties, MatchingOutputNode));
		}

		if (MatchingOutputNode != nullptr)
		{
			EventOutputNodes.Remove(MatchingOutputNode);
		}
	}

	for (UNiagaraNodeOutput* EventOutputNode : EventOutputNodes)
	{
		EventHandlers.Add(MakeShared<FNiagaraEventHandlerMergeAdapter>(*Emitter.Get(), EventOutputNode));
	}

	// Create an shader stage adapter for each usage id even if it's missing a shader stage object or an output node.  These
	// incomplete adapters will be caught if they are diffed.
	for (int32 SimulationStageIndex = 0; SimulationStageIndex < Emitter->GetSimulationStages().Num(); SimulationStageIndex++)
	{
		const UNiagaraSimulationStageBase* SimulationStage = Emitter->GetSimulationStages()[SimulationStageIndex];
	
		UNiagaraNodeOutput** MatchingOutputNodePtr = SimulationStageOutputNodes.FindByPredicate(
			[=](UNiagaraNodeOutput* SimulationStageOutputNode) { return SimulationStageOutputNode->GetUsageId() == SimulationStage->Script->GetUsageId(); });

		UNiagaraNodeOutput* MatchingOutputNode = MatchingOutputNodePtr != nullptr ? *MatchingOutputNodePtr : nullptr;

		if (EditableEmitter == nullptr)
		{
			SimulationStages.Add(MakeShared<FNiagaraSimulationStageMergeAdapter>(*Emitter.Get(), SimulationStage, SimulationStageIndex, MatchingOutputNode));
		}
		else
		{
			UNiagaraSimulationStageBase* EditableSimulationStage = EditableEmitter->GetSimulationStageById(SimulationStage->Script->GetUsageId());
			SimulationStages.Add(MakeShared<FNiagaraSimulationStageMergeAdapter>(*Emitter.Get(), EditableSimulationStage, SimulationStageIndex, MatchingOutputNode));
		}

		if (MatchingOutputNode != nullptr)
		{
			SimulationStageOutputNodes.Remove(MatchingOutputNode);
		}
	}

	for (UNiagaraNodeOutput* SimulationStageOutputNode : SimulationStageOutputNodes)
	{
		SimulationStages.Add(MakeShared<FNiagaraSimulationStageMergeAdapter>(*Emitter.Get(), SimulationStageOutputNode));
	}

	// Renderers
	for (UNiagaraRendererProperties* RendererProperties : Emitter->GetRenderers())
	{
		Renderers.Add(MakeShared<FNiagaraRendererMergeAdapter>(*RendererProperties));
	}

	EditorData = Cast<const UNiagaraEmitterEditorData>(Emitter->GetEditorData());
}

UNiagaraEmitter* FNiagaraEmitterMergeAdapter::GetEditableEmitter() const
{
	return EditableEmitter.Get();
}

TSharedPtr<FNiagaraScriptStackMergeAdapter> FNiagaraEmitterMergeAdapter::GetEmitterSpawnStack() const
{
	return EmitterSpawnStack;
}

TSharedPtr<FNiagaraScriptStackMergeAdapter> FNiagaraEmitterMergeAdapter::GetEmitterUpdateStack() const
{
	return EmitterUpdateStack;
}

TSharedPtr<FNiagaraScriptStackMergeAdapter> FNiagaraEmitterMergeAdapter::GetParticleSpawnStack() const
{
	return ParticleSpawnStack;
}

TSharedPtr<FNiagaraScriptStackMergeAdapter> FNiagaraEmitterMergeAdapter::GetParticleUpdateStack() const
{
	return ParticleUpdateStack;
}

const TArray<TSharedRef<FNiagaraEventHandlerMergeAdapter>> FNiagaraEmitterMergeAdapter::GetEventHandlers() const
{
	return EventHandlers;
}

const TArray<TSharedRef<FNiagaraSimulationStageMergeAdapter>> FNiagaraEmitterMergeAdapter::GetSimulationStages() const
{
	return SimulationStages;
}

const TArray<TSharedRef<FNiagaraRendererMergeAdapter>> FNiagaraEmitterMergeAdapter::GetRenderers() const
{
	return Renderers;
}

const UNiagaraEmitterEditorData* FNiagaraEmitterMergeAdapter::GetEditorData() const
{
	return EditorData.Get();
}

TSharedPtr<FNiagaraScriptStackMergeAdapter> FNiagaraEmitterMergeAdapter::GetScriptStack(ENiagaraScriptUsage Usage, FGuid ScriptUsageId)
{
	switch (Usage)
	{
	case ENiagaraScriptUsage::EmitterSpawnScript:
		return EmitterSpawnStack;
	case ENiagaraScriptUsage::EmitterUpdateScript:
		return EmitterUpdateStack;
	case ENiagaraScriptUsage::ParticleSpawnScript:
		return ParticleSpawnStack;
	case ENiagaraScriptUsage::ParticleUpdateScript:
		return ParticleUpdateStack;
	case ENiagaraScriptUsage::ParticleEventScript:
		for (TSharedPtr<FNiagaraEventHandlerMergeAdapter> EventHandler : EventHandlers)
		{
			if (EventHandler->GetUsageId() == ScriptUsageId)
			{
				return EventHandler->GetEventStack();
			}
		}
		break;
	case ENiagaraScriptUsage::ParticleSimulationStageScript:
		for (TSharedPtr<FNiagaraSimulationStageMergeAdapter> SimulationStage : SimulationStages)
		{
			if (SimulationStage->GetUsageId() == ScriptUsageId)
			{
				return SimulationStage->GetSimulationStageStack();
			}
		}
		break;
	default:
		checkf(false, TEXT("Unsupported usage"));
	}

	return TSharedPtr<FNiagaraScriptStackMergeAdapter>();
}

TSharedPtr<FNiagaraEventHandlerMergeAdapter> FNiagaraEmitterMergeAdapter::GetEventHandler(FGuid EventScriptUsageId)
{
	for (TSharedRef<FNiagaraEventHandlerMergeAdapter> EventHandler : EventHandlers)
	{
		if (EventHandler->GetUsageId() == EventScriptUsageId)
		{
			return EventHandler;
		}
	}
	return TSharedPtr<FNiagaraEventHandlerMergeAdapter>();
}

TSharedPtr<FNiagaraSimulationStageMergeAdapter> FNiagaraEmitterMergeAdapter::GetSimulationStage(FGuid SimulationStageUsageId)
{
	for (TSharedRef<FNiagaraSimulationStageMergeAdapter> SimulationStage : SimulationStages)
	{
		if (SimulationStage->GetUsageId() == SimulationStageUsageId)
		{
			return SimulationStage;
		}
	}
	return TSharedPtr<FNiagaraSimulationStageMergeAdapter>();
}

TSharedPtr<FNiagaraRendererMergeAdapter> FNiagaraEmitterMergeAdapter::GetRenderer(FGuid RendererMergeId)
{
	for (TSharedRef<FNiagaraRendererMergeAdapter> Renderer : Renderers)
	{
		if (Renderer->GetRenderer()->GetMergeId() == RendererMergeId)
		{
			return Renderer;
		}
	}
	return TSharedPtr<FNiagaraRendererMergeAdapter>();
}

void FNiagaraEmitterMergeAdapter::GatherFunctionCallNodes(TArray<UNiagaraNodeFunctionCall*>& OutFunctionCallNodes) const
{
	EmitterSpawnStack->GatherFunctionCallNodes(OutFunctionCallNodes);
	EmitterUpdateStack->GatherFunctionCallNodes(OutFunctionCallNodes);
	ParticleSpawnStack->GatherFunctionCallNodes(OutFunctionCallNodes);
	ParticleUpdateStack->GatherFunctionCallNodes(OutFunctionCallNodes);
	for (TSharedRef<FNiagaraEventHandlerMergeAdapter> EventHandler : EventHandlers)
	{
		EventHandler->GetEventStack()->GatherFunctionCallNodes(OutFunctionCallNodes);
	}
	for (TSharedRef<FNiagaraSimulationStageMergeAdapter> SimulationStage : SimulationStages)
	{
		SimulationStage->GetSimulationStageStack()->GatherFunctionCallNodes(OutFunctionCallNodes);
	}
}

FNiagaraScriptStackDiffResults::FNiagaraScriptStackDiffResults()
	: bIsValid(true)
{
}

bool FNiagaraScriptStackDiffResults::IsEmpty() const
{
	return
		RemovedBaseModules.Num() == 0 &&
		AddedOtherModules.Num() == 0 &&
		MovedBaseModules.Num() == 0 &&
		MovedOtherModules.Num() == 0 &&
		ChangedVersionBaseModules.Num() == 0 &&
		ChangedVersionOtherModules.Num() == 0 &&
		EnabledChangedBaseModules.Num() == 0 &&
		EnabledChangedOtherModules.Num() == 0 &&
		AddedOtherMessages.Num() == 0 &&
		RemovedBaseMessagesInOther.Num() == 0 &&
		RemovedBaseInputOverrides.Num() == 0 &&
		AddedOtherInputOverrides.Num() == 0 &&
		ModifiedOtherInputOverrides.Num() == 0 &&
		ChangedBaseUsage.IsSet() == false &&
		ChangedOtherUsage.IsSet() == false;
}

bool FNiagaraScriptStackDiffResults::IsValid() const
{
	return bIsValid;
}

void FNiagaraScriptStackDiffResults::AddError(FText ErrorMessage)
{
	ErrorMessages.Add(ErrorMessage);
	bIsValid = false;
}

const TArray<FText>& FNiagaraScriptStackDiffResults::GetErrorMessages() const
{
	return ErrorMessages;
}

FNiagaraEmitterDiffResults::FNiagaraEmitterDiffResults()
	: bScratchPadModified(false)
	, bIsValid(true)
{
}

bool FNiagaraEmitterDiffResults::IsValid() const
{
	bool bEventHandlerDiffsAreValid = true;
	for (const FNiagaraModifiedEventHandlerDiffResults& EventHandlerDiffResults : ModifiedEventHandlers)
	{
		if (EventHandlerDiffResults.ScriptDiffResults.IsValid() == false)
		{
			bEventHandlerDiffsAreValid = false;
			break;
		}
	}
	bool bSimulationStageDiffsAreValid = true;
	for (const FNiagaraModifiedSimulationStageDiffResults& SimulationStageDiffResults : ModifiedSimulationStages)
	{
		if (SimulationStageDiffResults.ScriptDiffResults.IsValid() == false)
		{
			bSimulationStageDiffsAreValid = false;
			break;
		}
	}
	return bIsValid &&
		bEventHandlerDiffsAreValid &&
		bSimulationStageDiffsAreValid &&
		EmitterSpawnDiffResults.IsValid() &&
		EmitterUpdateDiffResults.IsValid() &&
		ParticleSpawnDiffResults.IsValid() &&
		ParticleUpdateDiffResults.IsValid();
}

bool FNiagaraEmitterDiffResults::IsEmpty() const
{
	return DifferentEmitterProperties.Num() == 0 &&
		EmitterSpawnDiffResults.IsEmpty() &&
		EmitterUpdateDiffResults.IsEmpty() &&
		ParticleSpawnDiffResults.IsEmpty() &&
		ParticleUpdateDiffResults.IsEmpty() &&
		RemovedBaseEventHandlers.Num() == 0 &&
		AddedOtherEventHandlers.Num() == 0 &&
		ModifiedEventHandlers.Num() == 0 &&
		RemovedBaseSimulationStages.Num() == 0 &&
		AddedOtherSimulationStages.Num() == 0 &&
		ModifiedSimulationStages.Num() == 0 &&
		RemovedBaseRenderers.Num() == 0 &&
		AddedOtherRenderers.Num() == 0 &&
		ModifiedBaseRenderers.Num() == 0 &&
		ModifiedOtherRenderers.Num() == 0 &&
		ModifiedStackEntryDisplayNames.Num() == 0 &&
		bScratchPadModified == false;
}

void FNiagaraEmitterDiffResults::AddError(FText ErrorMessage)
{
	ErrorMessages.Add(ErrorMessage);
	bIsValid = false;
}

const TArray<FText>& FNiagaraEmitterDiffResults::GetErrorMessages() const
{
	return ErrorMessages;
}

FString FNiagaraEmitterDiffResults::GetErrorMessagesString() const
{
	TArray<FString> ErrorMessageStrings;
	for (FText ErrorMessage : ErrorMessages)
	{
		ErrorMessageStrings.Add(ErrorMessage.ToString());
	}
	return FString::Join(ErrorMessageStrings, TEXT("\n"));
}

bool FNiagaraEmitterDiffResults::HasVersionChanges() const
{
	for (auto& EventHandlerDiff : ModifiedEventHandlers)
	{
		if (EventHandlerDiff.ScriptDiffResults.ChangedVersionOtherModules.Num() > 0)
		{
			return true;
		}
	}
	for (auto& SimStageDiff : ModifiedSimulationStages)
	{
		if (SimStageDiff.ScriptDiffResults.ChangedVersionOtherModules.Num() > 0)
		{
			return true;
		}
	}
	return	EmitterSpawnDiffResults.ChangedVersionOtherModules.Num() > 0 ||
			EmitterUpdateDiffResults.ChangedVersionOtherModules.Num() > 0 ||
			ParticleSpawnDiffResults.ChangedVersionOtherModules.Num() > 0 ||
			ParticleUpdateDiffResults.ChangedVersionOtherModules.Num() > 0;
}

TSharedRef<FNiagaraScriptMergeManager> FNiagaraScriptMergeManager::Get()
{
	FNiagaraEditorModule& NiagaraEditorModule = FModuleManager::GetModuleChecked<FNiagaraEditorModule>("NiagaraEditor");
	return NiagaraEditorModule.GetScriptMergeManager();
}

void FNiagaraScriptMergeManager::GetForcedChangeIds(
	const TMap<FGuid, UNiagaraNodeFunctionCall*>& InParentFunctionIdToNodeMap,
	const TMap<FGuid, UNiagaraNodeFunctionCall*>& InParentAtLastMergeFunctionIdToNodeMap,
	const TMap<FGuid, UNiagaraNodeFunctionCall*>& InInstanceFunctionIdToNodeMap,
	TMap<FGuid, FGuid>& OutFunctionIdToForcedChangeId) const
{
	for(const TPair<FGuid, UNiagaraNodeFunctionCall*>& InstanceFunctionIdNodePair : InInstanceFunctionIdToNodeMap)
	{
		const FGuid& InstanceFunctionId = InstanceFunctionIdNodePair.Key;
		UNiagaraNodeFunctionCall* InstanceFunctionNode = InstanceFunctionIdNodePair.Value;

		UNiagaraNodeFunctionCall*const* MatchingParentFunctionNodePtr = InParentFunctionIdToNodeMap.Find(InstanceFunctionId);
		UNiagaraNodeFunctionCall*const* MatchingParentAtLastMergeFunctionNodePtr = InParentAtLastMergeFunctionIdToNodeMap.Find(InstanceFunctionId);

		if (MatchingParentFunctionNodePtr == nullptr && MatchingParentAtLastMergeFunctionNodePtr == nullptr)
		{
			// If neither the current parent or parent at last merge had a function node with this id, force the current change id of the node
			// since it only exists in the instance.
			OutFunctionIdToForcedChangeId.Add(InstanceFunctionId, InstanceFunctionNode->GetChangeId());
		}
		else if (MatchingParentFunctionNodePtr != nullptr && MatchingParentAtLastMergeFunctionNodePtr != nullptr)
		{
			if ((*MatchingParentFunctionNodePtr)->GetChangeId() == (*MatchingParentAtLastMergeFunctionNodePtr)->GetChangeId())
			{
				// If both the parent and parent at last merge agree on the change id, then the most recent changes will have happened in
				// the instance so we can force the instance change id.
				OutFunctionIdToForcedChangeId.Add(InstanceFunctionId, InstanceFunctionNode->GetChangeId());
			}
			else
			{
				if (InstanceFunctionNode->GetChangeId() == (*MatchingParentAtLastMergeFunctionNodePtr)->GetChangeId())
				{
					// If the parent and the parent at last merge have different change ids, and the instances change id matches
					// the parent at last merge change id, then the parent has changed, and the instance has not, so we can
					// force the id from the parent.
					OutFunctionIdToForcedChangeId.Add(InstanceFunctionId, (*MatchingParentFunctionNodePtr)->GetChangeId());
				}
			}
		}
		else if (MatchingParentAtLastMergeFunctionNodePtr != nullptr)
		{
			// If the parent did not have this function id but the parent at the last merge did have the id, it was deleted in the parent
			// so only the instance id is still relevant.
			OutFunctionIdToForcedChangeId.Add(InstanceFunctionId, InstanceFunctionNode->GetChangeId());
		}
		else
		{
			ensureMsgf(MatchingParentFunctionNodePtr == nullptr,
				TEXT("Error while merging changes for emitter instance function node: %s.  A node with this change id was not found in the last merge parent, but it was found on parent node: %s"),
				*InstanceFunctionNode->GetPathName(), *(*MatchingParentFunctionNodePtr)->GetPathName());
		}
	}
}

FNiagaraScriptMergeManager::FApplyDiffResults FNiagaraScriptMergeManager::ForceInstanceChangeIds(TSharedRef<FNiagaraEmitterMergeAdapter> MergedInstanceAdapter, UNiagaraEmitter& OriginalEmitterInstance, const TMap<FGuid, FGuid>& FunctionIdToForcedChangedId) const
{
	FApplyDiffResults DiffResults;

	if (FunctionIdToForcedChangedId.Num() != 0)
	{
		auto It = FunctionIdToForcedChangedId.CreateConstIterator();
		UNiagaraEmitter* Emitter = MergedInstanceAdapter->GetEditableEmitter();

		TArray<UNiagaraGraph*> Graphs;
		TArray<UNiagaraScript*> Scripts;
		Emitter->GetScripts(Scripts);

		TArray<UNiagaraScript*> OriginalScripts;
		OriginalEmitterInstance.GetScripts(OriginalScripts);

		// First gather all the graphs used by this emitter..
		for (UNiagaraScript* Script : Scripts)
		{
			if (Script != nullptr && Script->GetLatestSource() != nullptr)
			{
				UNiagaraScriptSource* ScriptSource = Cast<UNiagaraScriptSource>(Script->GetLatestSource());
				if (ScriptSource != nullptr)
				{
					Graphs.AddUnique(ScriptSource->NodeGraph);
				}
			}
		}

		// Now gather up all the nodes
		TArray<UNiagaraNode*> Nodes;
		for (UNiagaraGraph* Graph : Graphs)
		{
			Graph->GetNodesOfClass(Nodes);
		}

		// Now go through all the nodes and set the persistent change ids if we encounter a node that needs it's change id kept.
		bool bAnySet = false;
		while (It)
		{
			for (UNiagaraNode* Node : Nodes)
			{
				if (Node->NodeGuid == It.Key())
				{
					Node->ForceChangeId(It.Value(), false);
					bAnySet = true;
					break;
				}
			}
			++It;
		}

		if (bAnySet)
		{
			for (UNiagaraGraph* Graph : Graphs)
			{
				Graph->MarkGraphRequiresSynchronization(TEXT("Overwrote change id's within graph."));
			}
		}

		DiffResults.bModifiedGraph = bAnySet;

		if (bAnySet)
		{
			bool bAnyUpdated = false;
			TMap<FString, FString> RenameMap;
			RenameMap.Add(TEXT("Emitter"), TEXT("Emitter"));
			for (UNiagaraScript* Script : Scripts)
			{
				for (UNiagaraScript* OriginalScript : OriginalScripts)
				{
					if (Script->Usage == OriginalScript->Usage && Script->GetUsageId() == OriginalScript->GetUsageId())
					{
						bAnyUpdated |= Script->SynchronizeExecutablesWithMaster(OriginalScript, RenameMap);
					}
				}
			}

			if (bAnyUpdated)
			{
				//Emitter->OnPostCompile();
			}
		}
	}

	DiffResults.bSucceeded = true;
	return DiffResults;
}

void FNiagaraScriptMergeManager::UpdateModuleVersions(UNiagaraEmitter& Instance, const FNiagaraEmitterDiffResults& DiffResults) const
{
	// gather all the changes
	TArray<TSharedRef<FNiagaraStackFunctionMergeAdapter>> ChangedVersionBaseModules;
	ChangedVersionBaseModules.Append(DiffResults.EmitterSpawnDiffResults.ChangedVersionBaseModules);
	ChangedVersionBaseModules.Append(DiffResults.EmitterUpdateDiffResults.ChangedVersionBaseModules);
	ChangedVersionBaseModules.Append(DiffResults.ParticleSpawnDiffResults.ChangedVersionBaseModules);
	ChangedVersionBaseModules.Append(DiffResults.ParticleUpdateDiffResults.ChangedVersionBaseModules);
	for (auto& EventHandlerDiff : DiffResults.ModifiedEventHandlers)
	{
		ChangedVersionBaseModules.Append(EventHandlerDiff.ScriptDiffResults.ChangedVersionBaseModules);
	}
	for (auto& SimStageDiff : DiffResults.ModifiedSimulationStages)
	{
		ChangedVersionBaseModules.Append(SimStageDiff.ScriptDiffResults.ChangedVersionBaseModules);
	}

	TArray<TSharedRef<FNiagaraStackFunctionMergeAdapter>> ChangedVersionOtherModules;
	ChangedVersionOtherModules.Append(DiffResults.EmitterSpawnDiffResults.ChangedVersionOtherModules);
	ChangedVersionOtherModules.Append(DiffResults.EmitterUpdateDiffResults.ChangedVersionOtherModules);
	ChangedVersionOtherModules.Append(DiffResults.ParticleSpawnDiffResults.ChangedVersionOtherModules);
	ChangedVersionOtherModules.Append(DiffResults.ParticleUpdateDiffResults.ChangedVersionOtherModules);
	for (auto& EventHandlerDiff : DiffResults.ModifiedEventHandlers)
	{
		ChangedVersionOtherModules.Append(EventHandlerDiff.ScriptDiffResults.ChangedVersionOtherModules);
	}
	for (auto& SimStageDiff : DiffResults.ModifiedSimulationStages)
	{
		ChangedVersionOtherModules.Append(SimStageDiff.ScriptDiffResults.ChangedVersionOtherModules);
	}
	
	// the python update script needs a view model, so we need to create a temporary system here to instantiate the view models
	TSharedPtr<FNiagaraSystemViewModel> SystemViewModel;
	if (ChangedVersionOtherModules.Num() > 0)
	{
		if (!FSlateApplication::IsInitialized())
		{
			// it's possible that during the cook process we don't have slate initialized, but we need it set up for the view model
			FSlateApplication::Create();
			TSharedRef<FSlateRenderer> SlateRenderer = FModuleManager::Get().LoadModuleChecked<ISlateNullRendererModule>("SlateNullRenderer").CreateSlateNullRenderer();
			FSlateApplication::Get().InitializeRenderer(SlateRenderer);
		}
		UNiagaraSystem* System = NewObject<UNiagaraSystem>(GetTransientPackage(), NAME_None, RF_Transient | RF_Standalone);
		UNiagaraSystemFactoryNew::InitializeSystem(System, true);
		SystemViewModel = MakeShared<FNiagaraSystemViewModel>();
		FNiagaraSystemViewModelOptions SystemOptions;
		SystemOptions.bCanModifyEmittersFromTimeline = false;
		SystemOptions.bCanSimulate = false;
		SystemOptions.bCanAutoCompile = false;
		SystemOptions.bIsForDataProcessingOnly = true;
		SystemOptions.EditMode = ENiagaraSystemViewModelEditMode::EmitterDuringMerge;
		SystemOptions.MessageLogGuid = System->GetAssetGuid();
		SystemViewModel->Initialize(*System, SystemOptions);
		SystemViewModel->GetEditorData().SetOwningSystemIsPlaceholder(true, *System);
		SystemViewModel->AddEmitter(Instance);
	}

	// apply version changes
	for (int i = 0; i < ChangedVersionOtherModules.Num(); i++)
	{
		TSharedRef<FNiagaraStackFunctionMergeAdapter> BaseModule = ChangedVersionBaseModules[i];
		TSharedRef<FNiagaraStackFunctionMergeAdapter> ChangedModule = ChangedVersionOtherModules[i];
		FGuid NewScriptVersion = BaseModule->GetFunctionCallNode()->SelectedScriptVersion;

		// search for the stack item of the module node we are merging
		UNiagaraStackModuleItem* ModuleItem = nullptr;
		UNiagaraStackViewModel* EmitterStackViewModel = SystemViewModel->GetEmitterHandleViewModels()[0]->GetEmitterStackViewModel();
		TArray<UNiagaraStackEntry*> EntriesToCheck;
		EmitterStackViewModel->GetRootEntry()->GetUnfilteredChildren(EntriesToCheck);
		while (EntriesToCheck.Num() > 0)
		{
			UNiagaraStackEntry* Entry = EntriesToCheck.Pop();
			UNiagaraStackModuleItem* ModuleItemToCheck = Cast<UNiagaraStackModuleItem>(Entry);
			if (ModuleItemToCheck && ModuleItemToCheck->GetModuleNode().NodeGuid == ChangedModule->GetFunctionCallNode()->NodeGuid)
			{
				ModuleItem = ModuleItemToCheck;
				break;
			}
			Entry->GetUnfilteredChildren(EntriesToCheck);
		}
		
		FNiagaraScriptVersionUpgradeContext UpgradeContext;
		UpgradeContext.ConstantResolver = FCompileConstantResolver(&Instance, FNiagaraStackGraphUtilities::GetOutputNodeUsage(*ChangedModule->GetFunctionCallNode()));
		if (ModuleItem)
		{
			UpgradeContext.CreateClipboardCallback = [ModuleItem](UNiagaraClipboardContent* ClipboardContent)
			{
				ModuleItem->RefreshChildren();
				ModuleItem->Copy(ClipboardContent);
				if (ClipboardContent->Functions.Num() > 0)
				{
					ClipboardContent->FunctionInputs = ClipboardContent->Functions[0]->Inputs;
					ClipboardContent->Functions.Empty();
				}
			};
			UpgradeContext.ApplyClipboardCallback = [ModuleItem](UNiagaraClipboardContent* ClipboardContent, FText& OutWarning) { ModuleItem->Paste(ClipboardContent, OutWarning); };
		}
		ChangedModule->GetFunctionCallNode()->ChangeScriptVersion(NewScriptVersion, UpgradeContext, true);
	}
}

INiagaraMergeManager::FMergeEmitterResults FNiagaraScriptMergeManager::MergeEmitter(UNiagaraEmitter& Parent, UNiagaraEmitter* ParentAtLastMerge, UNiagaraEmitter& Instance) const
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_ScriptMergeManager_MergeEmitter);
	INiagaraMergeManager::FMergeEmitterResults MergeResults;
	const bool bNoParentAtLastMerge = (ParentAtLastMerge == nullptr);
	UNiagaraEmitter* FirstEmitterToDiffAgainst = bNoParentAtLastMerge ? &Parent : ParentAtLastMerge;
	FNiagaraEmitterDiffResults DiffResults = DiffEmitters(*FirstEmitterToDiffAgainst, Instance);
	
	if (DiffResults.IsValid() == false)
	{
		MergeResults.MergeResult = INiagaraMergeManager::EMergeEmitterResult::FailedToDiff;
		MergeResults.ErrorMessages = DiffResults.GetErrorMessages();

		auto ReportScriptStackDiffErrors = [](FMergeEmitterResults& EmitterMergeResults, const FNiagaraScriptStackDiffResults& ScriptStackDiffResults, FText ScriptName)
		{
			FText ScriptStackDiffInvalidFormat = LOCTEXT("ScriptStackDiffInvalidFormat", "Failed to diff {0} script stack.  {1} Errors:");
			if (ScriptStackDiffResults.IsValid() == false)
			{
				EmitterMergeResults.ErrorMessages.Add(FText::Format(ScriptStackDiffInvalidFormat, ScriptName, ScriptStackDiffResults.GetErrorMessages().Num()));
				for (const FText& ErrorMessage : ScriptStackDiffResults.GetErrorMessages())
				{
					EmitterMergeResults.ErrorMessages.Add(ErrorMessage);
				}
			}
		};

		ReportScriptStackDiffErrors(MergeResults, DiffResults.EmitterSpawnDiffResults, LOCTEXT("EmitterSpawnScriptName", "Emitter Spawn"));
		ReportScriptStackDiffErrors(MergeResults, DiffResults.EmitterUpdateDiffResults, LOCTEXT("EmitterUpdateScriptName", "Emitter Update"));
		ReportScriptStackDiffErrors(MergeResults, DiffResults.ParticleSpawnDiffResults, LOCTEXT("ParticleSpawnScriptName", "Particle Spawn"));
		ReportScriptStackDiffErrors(MergeResults, DiffResults.ParticleUpdateDiffResults, LOCTEXT("ParticleUpdateScriptName", "Particle Update"));

		for (const FNiagaraModifiedEventHandlerDiffResults& EventHandlerDiffResults : DiffResults.ModifiedEventHandlers)
		{
			FText EventHandlerName = FText::Format(LOCTEXT("EventHandlerScriptNameFormat", "Event Handler - {0}"), FText::FromName(EventHandlerDiffResults.BaseAdapter->GetEventScriptProperties()->SourceEventName));
			ReportScriptStackDiffErrors(MergeResults, EventHandlerDiffResults.ScriptDiffResults, EventHandlerName);
		}
	}
	else if (DiffResults.IsEmpty())
	{
		// If there were no changes made on the instance, check if the instance matches the parent.
		FNiagaraEmitterDiffResults DiffResultsFromParent = DiffEmitters(Parent, Instance);
		if (DiffResultsFromParent.IsValid() && DiffResultsFromParent.IsEmpty())
		{
			MergeResults.MergeResult = INiagaraMergeManager::EMergeEmitterResult::SucceededNoDifferences;
		}
		else
		{
			// If there were differences from the parent or the parent diff failed we can just return a copy of the parent as the merged instance since there
			// were no changes in the instance which need to be applied.
			MergeResults.MergeResult = INiagaraMergeManager::EMergeEmitterResult::SucceededDifferencesApplied;
			MergeResults.MergedInstance = Parent.DuplicateWithoutMerging(GetTransientPackage());
			MergeResults.MergedInstance->ParentScratchPadScripts.Append(MergeResults.MergedInstance->ScratchPadScripts);
			MergeResults.MergedInstance->ScratchPadScripts.Empty();
		}
	}
	else
	{
		UNiagaraEmitter* MergedInstance = Parent.DuplicateWithoutMerging(GetTransientPackage());
		TSharedRef<FNiagaraEmitterMergeAdapter> MergedInstanceAdapter = MakeShared<FNiagaraEmitterMergeAdapter>(*MergedInstance);

		// diff for version changes and apply them first. We need to upgrade both the current emitter and the parent at last merge
		// to the newest version and then diff them again. Otherwise we won't know which changes were made by the version upgrade
		// and which changes were made to the parent emitter.
		FNiagaraEmitterDiffResults VersionChangeDiffResults = DiffEmitters(Parent, Instance);
		if (VersionChangeDiffResults.HasVersionChanges())
		{
			// apply version upgrade to the merged emitter instance
			UpdateModuleVersions(Instance, VersionChangeDiffResults);

			// apply version upgrade to the parent at last merge
			UNiagaraEmitter* ParentAtLastMergeCopy = Cast<UNiagaraEmitter>(StaticDuplicateObject(FirstEmitterToDiffAgainst, GetTransientPackage()));
			ParentAtLastMergeCopy->ClearFlags(RF_Standalone | RF_Public);
			FNiagaraEmitterDiffResults ParentsDiffResults = DiffEmitters(Parent, *ParentAtLastMergeCopy);
			UpdateModuleVersions(*ParentAtLastMergeCopy, ParentsDiffResults);

			// now diff again
			DiffResults = DiffEmitters(*ParentAtLastMergeCopy, Instance);
			ensureMsgf(DiffResults.HasVersionChanges() == false, TEXT("Emitter still has pending version changes after merge! Asset %s"), *Instance.GetPathName());
		}

		TMap<FGuid, UNiagaraNodeFunctionCall*> ParentFunctionIdToNodeMap;
		TMap<FGuid, UNiagaraNodeFunctionCall*> LastMergedParentFunctionIdToNodeMap;
		TMap<FGuid, UNiagaraNodeFunctionCall*> InstanceFunctionIdToNodeMap;

		auto PopulateIdToNodeMap = [](TSharedRef<FNiagaraEmitterMergeAdapter> EmitterAdapter, TMap<FGuid, UNiagaraNodeFunctionCall*>& Map)
		{
			TArray<UNiagaraNodeFunctionCall*> EmitterFunctionCalls;
			EmitterAdapter->GatherFunctionCallNodes(EmitterFunctionCalls);
			for (UNiagaraNodeFunctionCall* EmitterFunctionCall : EmitterFunctionCalls)
			{
				Map.Add(EmitterFunctionCall->NodeGuid, EmitterFunctionCall);
			}
		};

		PopulateIdToNodeMap(MakeShared<FNiagaraEmitterMergeAdapter>(Parent), ParentFunctionIdToNodeMap);
		PopulateIdToNodeMap(DiffResults.BaseEmitterAdapter.ToSharedRef(), LastMergedParentFunctionIdToNodeMap);
		PopulateIdToNodeMap(DiffResults.OtherEmitterAdapter.ToSharedRef(), InstanceFunctionIdToNodeMap);

		TMap<FGuid, FGuid> FunctionIdToForcedChangeId;
		GetForcedChangeIds(ParentFunctionIdToNodeMap, LastMergedParentFunctionIdToNodeMap, InstanceFunctionIdToNodeMap, FunctionIdToForcedChangeId);

		MergedInstance->ParentScratchPadScripts.Append(MergedInstance->ScratchPadScripts);
		MergedInstance->ScratchPadScripts.Empty();
		CopyInstanceScratchPadScripts(*MergedInstance, Instance);
		TSharedRef<FNiagaraScratchPadMergeAdapter> ScratchPadAdapter = MakeShared<FNiagaraScratchPadMergeAdapter>(MergedInstance, &Instance, &Parent);

		MergeResults.MergeResult = INiagaraMergeManager::EMergeEmitterResult::SucceededDifferencesApplied;
		FApplyDiffResults EmitterSpawnResults = ApplyScriptStackDiff(MergedInstanceAdapter, MergedInstanceAdapter->GetEmitterSpawnStack().ToSharedRef(), ScratchPadAdapter, DiffResults.EmitterSpawnDiffResults, bNoParentAtLastMerge);
		if (EmitterSpawnResults.bSucceeded == false)
		{
			MergeResults.MergeResult = INiagaraMergeManager::EMergeEmitterResult::FailedToMerge;
		}
		MergeResults.bModifiedGraph |= EmitterSpawnResults.bModifiedGraph;
		MergeResults.ErrorMessages.Append(EmitterSpawnResults.ErrorMessages);

		FApplyDiffResults EmitterUpdateResults = ApplyScriptStackDiff(MergedInstanceAdapter, MergedInstanceAdapter->GetEmitterUpdateStack().ToSharedRef(), ScratchPadAdapter, DiffResults.EmitterUpdateDiffResults, bNoParentAtLastMerge);
		if (EmitterUpdateResults.bSucceeded == false)
		{
			MergeResults.MergeResult = INiagaraMergeManager::EMergeEmitterResult::FailedToMerge;
		}
		MergeResults.bModifiedGraph |= EmitterUpdateResults.bModifiedGraph;
		MergeResults.ErrorMessages.Append(EmitterUpdateResults.ErrorMessages);

		FApplyDiffResults ParticleSpawnResults = ApplyScriptStackDiff(MergedInstanceAdapter, MergedInstanceAdapter->GetParticleSpawnStack().ToSharedRef(), ScratchPadAdapter, DiffResults.ParticleSpawnDiffResults, bNoParentAtLastMerge);
		if (ParticleSpawnResults.bSucceeded == false)
		{
			MergeResults.MergeResult = INiagaraMergeManager::EMergeEmitterResult::FailedToMerge;
		}
		MergeResults.bModifiedGraph |= ParticleSpawnResults.bModifiedGraph;
		MergeResults.ErrorMessages.Append(ParticleSpawnResults.ErrorMessages);

		FApplyDiffResults ParticleUpdateResults = ApplyScriptStackDiff(MergedInstanceAdapter, MergedInstanceAdapter->GetParticleUpdateStack().ToSharedRef(), ScratchPadAdapter, DiffResults.ParticleUpdateDiffResults, bNoParentAtLastMerge);
		if (ParticleUpdateResults.bSucceeded == false)
		{
			MergeResults.MergeResult = INiagaraMergeManager::EMergeEmitterResult::FailedToMerge;
		}
		MergeResults.bModifiedGraph |= ParticleUpdateResults.bModifiedGraph;
		MergeResults.ErrorMessages.Append(ParticleUpdateResults.ErrorMessages);

		FApplyDiffResults EventHandlerResults = ApplyEventHandlerDiff(MergedInstanceAdapter, ScratchPadAdapter, DiffResults, bNoParentAtLastMerge);
		if (EventHandlerResults.bSucceeded == false)
		{
			MergeResults.MergeResult = INiagaraMergeManager::EMergeEmitterResult::FailedToMerge;
		}
		MergeResults.bModifiedGraph |= EventHandlerResults.bModifiedGraph;
		MergeResults.ErrorMessages.Append(EventHandlerResults.ErrorMessages);

		FApplyDiffResults SimulationStageResults = ApplySimulationStageDiff(MergedInstanceAdapter, ScratchPadAdapter, DiffResults, bNoParentAtLastMerge);
		if (SimulationStageResults.bSucceeded == false)
		{
			MergeResults.MergeResult = INiagaraMergeManager::EMergeEmitterResult::FailedToMerge;
		}
		MergeResults.bModifiedGraph |= SimulationStageResults.bModifiedGraph;
		MergeResults.ErrorMessages.Append(SimulationStageResults.ErrorMessages);

		FApplyDiffResults RendererResults = ApplyRendererDiff(*MergedInstance, DiffResults, bNoParentAtLastMerge);
		if (RendererResults.bSucceeded == false)
		{
			MergeResults.MergeResult = INiagaraMergeManager::EMergeEmitterResult::FailedToMerge;
		}
		MergeResults.bModifiedGraph |= RendererResults.bModifiedGraph;
		MergeResults.ErrorMessages.Append(RendererResults.ErrorMessages);

		CopyPropertiesToBase(MergedInstance, &Instance, DiffResults.DifferentEmitterProperties);

		FApplyDiffResults StackEntryDisplayNameDiffs = ApplyStackEntryDisplayNameDiffs(*MergedInstance, DiffResults);
		if (StackEntryDisplayNameDiffs.bSucceeded == false)
		{
			MergeResults.MergeResult = INiagaraMergeManager::EMergeEmitterResult::FailedToMerge;
		}
		MergeResults.bModifiedGraph |= StackEntryDisplayNameDiffs.bModifiedGraph;
		MergeResults.ErrorMessages.Append(StackEntryDisplayNameDiffs.ErrorMessages);

#if 0
		UE_LOG(LogNiagaraEditor, Log, TEXT("A"));
		//for (FNiagaraEmitterHandle& EmitterHandle : EmitterHandles)
		{
			//UE_LOG(LogNiagara, Log, TEXT("Emitter Handle: %s"), *EmitterHandle.GetUniqueInstanceName());
			UNiagaraScript* UpdateScript = MergedInstance->GetScript(ENiagaraScriptUsage::ParticleUpdateScript, FGuid());
			UNiagaraScript* SpawnScript = MergedInstance->GetScript(ENiagaraScriptUsage::ParticleSpawnScript, FGuid());
			UE_LOG(LogNiagaraEditor, Log, TEXT("Spawn Parameters"));
			SpawnScript->GetVMExecutableData().Parameters.DumpParameters();
			UE_LOG(LogNiagaraEditor, Log, TEXT("Spawn RI Parameters"));
			SpawnScript->RapidIterationParameters.DumpParameters();
			UE_LOG(LogNiagaraEditor, Log, TEXT("Update Parameters"));
			UpdateScript->GetVMExecutableData().Parameters.DumpParameters();
			UE_LOG(LogNiagaraEditor, Log, TEXT("Update RI Parameters"));
			UpdateScript->RapidIterationParameters.DumpParameters();
		}
#endif
		
		FApplyDiffResults ChangeIdResults = ForceInstanceChangeIds(MergedInstanceAdapter, Instance, FunctionIdToForcedChangeId);
		if (ChangeIdResults.bSucceeded == false)
		{
			MergeResults.MergeResult = INiagaraMergeManager::EMergeEmitterResult::FailedToMerge;
		}
		MergeResults.bModifiedGraph |= ChangeIdResults.bModifiedGraph;
		MergeResults.ErrorMessages.Append(ChangeIdResults.ErrorMessages);
		
#if 0
		UE_LOG(LogNiagaraEditor, Log, TEXT("B"));
		//for (FNiagaraEmitterHandle& EmitterHandle : EmitterHandles)
		{
			//UE_LOG(LogNiagara, Log, TEXT("Emitter Handle: %s"), *EmitterHandle.GetUniqueInstanceName());
			UNiagaraScript* UpdateScript = MergedInstance->GetScript(ENiagaraScriptUsage::ParticleUpdateScript, FGuid());
			UNiagaraScript* SpawnScript = MergedInstance->GetScript(ENiagaraScriptUsage::ParticleSpawnScript, FGuid());
			UE_LOG(LogNiagaraEditor, Log, TEXT("Spawn Parameters"));
			SpawnScript->GetVMExecutableData().Parameters.DumpParameters();
			UE_LOG(LogNiagaraEditor, Log, TEXT("Spawn RI Parameters"));
			SpawnScript->RapidIterationParameters.DumpParameters();
			UE_LOG(LogNiagaraEditor, Log, TEXT("Update Parameters"));
			UpdateScript->GetVMExecutableData().Parameters.DumpParameters();
			UE_LOG(LogNiagaraEditor, Log, TEXT("Update RI Parameters"));
			UpdateScript->RapidIterationParameters.DumpParameters();
		}
#endif

		FNiagaraStackGraphUtilities::CleanUpStaleRapidIterationParameters(*MergedInstance);

#if 0
		UE_LOG(LogNiagaraEditor, Log, TEXT("C"));
		//for (FNiagaraEmitterHandle& EmitterHandle : EmitterHandles)
		{
			//UE_LOG(LogNiagara, Log, TEXT("Emitter Handle: %s"), *EmitterHandle.GetUniqueInstanceName());
			UNiagaraScript* UpdateScript = MergedInstance->GetScript(ENiagaraScriptUsage::ParticleUpdateScript, FGuid());
			UNiagaraScript* SpawnScript = MergedInstance->GetScript(ENiagaraScriptUsage::ParticleSpawnScript, FGuid());
			UE_LOG(LogNiagaraEditor, Log, TEXT("Spawn Parameters"));
			SpawnScript->GetVMExecutableData().Parameters.DumpParameters();
			UE_LOG(LogNiagaraEditor, Log, TEXT("Spawn RI Parameters"));
			SpawnScript->RapidIterationParameters.DumpParameters();
			UE_LOG(LogNiagaraEditor, Log, TEXT("Update Parameters"));
			UpdateScript->GetVMExecutableData().Parameters.DumpParameters();
			UE_LOG(LogNiagaraEditor, Log, TEXT("Update RI Parameters"));
			UpdateScript->RapidIterationParameters.DumpParameters();
		}
#endif
		if (MergeResults.MergeResult == INiagaraMergeManager::EMergeEmitterResult::SucceededDifferencesApplied)
		{
			UNiagaraScriptSource* ScriptSource = Cast<UNiagaraScriptSource>(MergedInstance->GraphSource);
			FNiagaraStackGraphUtilities::RelayoutGraph(*ScriptSource->NodeGraph);
			MergeResults.MergedInstance = MergedInstance;
		}

		TMap<FGuid, FGuid> FinalChangeIds;
		FNiagaraEditorUtilities::GatherChangeIds(*MergedInstance, FinalChangeIds, TEXT("Final"));
	}

	return MergeResults;
}

bool FNiagaraScriptMergeManager::IsMergeableScriptUsage(ENiagaraScriptUsage ScriptUsage) const
{
	return ScriptUsage == ENiagaraScriptUsage::EmitterSpawnScript ||
		ScriptUsage == ENiagaraScriptUsage::EmitterUpdateScript ||
		ScriptUsage == ENiagaraScriptUsage::ParticleSpawnScript ||
		ScriptUsage == ENiagaraScriptUsage::ParticleUpdateScript ||
		ScriptUsage == ENiagaraScriptUsage::ParticleEventScript ||
		ScriptUsage == ENiagaraScriptUsage::ParticleSimulationStageScript;
}

bool FNiagaraScriptMergeManager::HasBaseModule(const UNiagaraEmitter& BaseEmitter, ENiagaraScriptUsage ScriptUsage, FGuid ScriptUsageId, FGuid ModuleId)
{
	TSharedRef<FNiagaraEmitterMergeAdapter> BaseEmitterAdapter = GetEmitterMergeAdapterUsingCache(BaseEmitter);
	TSharedPtr<FNiagaraScriptStackMergeAdapter> BaseScriptStackAdapter = BaseEmitterAdapter->GetScriptStack(ScriptUsage, ScriptUsageId);
	return BaseScriptStackAdapter.IsValid() && BaseScriptStackAdapter->GetModuleFunctionById(ModuleId).IsValid();
}

bool FNiagaraScriptMergeManager::IsModuleInputDifferentFromBase(UNiagaraEmitter& Emitter, const UNiagaraEmitter& BaseEmitter, ENiagaraScriptUsage ScriptUsage, FGuid ScriptUsageId, FGuid ModuleId, FString InputName)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_ScriptMergeManager_IsModuleInputDifferentFromBase);

	TSharedRef<FNiagaraEmitterMergeAdapter> EmitterAdapter = GetEmitterMergeAdapterUsingCache(Emitter);
	TSharedRef<FNiagaraEmitterMergeAdapter> BaseEmitterAdapter = GetEmitterMergeAdapterUsingCache(BaseEmitter);

	TSharedRef<FNiagaraScriptStackMergeAdapter> ScriptStackAdapter = EmitterAdapter->GetScriptStack(ScriptUsage, ScriptUsageId).ToSharedRef();
	TSharedPtr<FNiagaraScriptStackMergeAdapter> BaseScriptStackAdapter = BaseEmitterAdapter->GetScriptStack(ScriptUsage, ScriptUsageId);

	if (BaseScriptStackAdapter.IsValid() == false)
	{
		return false;
	}

	FNiagaraScriptStackDiffResults ScriptStackDiffResults;
	DiffScriptStacks(BaseScriptStackAdapter.ToSharedRef(), ScriptStackAdapter, ScriptStackDiffResults);

	if (ScriptStackDiffResults.IsValid() == false)
	{
		return true;
	}

	if (ScriptStackDiffResults.IsEmpty())
	{
		return false;
	}

	auto FindInputOverrideByInputName = [=](TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter> InputOverride)
	{
		return InputOverride->GetOwningFunctionCall()->NodeGuid == ModuleId && InputOverride->GetInputName() == InputName;
	};

	return
		ScriptStackDiffResults.RemovedBaseInputOverrides.FindByPredicate(FindInputOverrideByInputName) != nullptr ||
		ScriptStackDiffResults.AddedOtherInputOverrides.FindByPredicate(FindInputOverrideByInputName) != nullptr ||
		ScriptStackDiffResults.ModifiedOtherInputOverrides.FindByPredicate(FindInputOverrideByInputName) != nullptr;
}

FNiagaraScriptMergeManager::FApplyDiffResults FNiagaraScriptMergeManager::ResetModuleInputToBase(UNiagaraEmitter& Emitter, const UNiagaraEmitter& BaseEmitter, ENiagaraScriptUsage ScriptUsage, FGuid ScriptUsageId, FGuid ModuleId, FString InputName)
{
	TSharedRef<FNiagaraEmitterMergeAdapter> EmitterAdapter = GetEmitterMergeAdapterUsingCache(Emitter);
	TSharedRef<FNiagaraEmitterMergeAdapter> BaseEmitterAdapter = GetEmitterMergeAdapterUsingCache(BaseEmitter);
	TSharedRef<FNiagaraScratchPadMergeAdapter> ScratchPadMergeAdapter = GetScratchPadMergeAdapterUsingCache(Emitter);

	// Diff from the emitter to the base to create a diff which will reset the emitter back to the base.
	FNiagaraScriptStackDiffResults ResetDiffResults;
	DiffScriptStacks(EmitterAdapter->GetScriptStack(ScriptUsage, ScriptUsageId).ToSharedRef(), BaseEmitterAdapter->GetScriptStack(ScriptUsage, ScriptUsageId).ToSharedRef(), ResetDiffResults);

	if (ResetDiffResults.IsValid() == false)
	{
		FApplyDiffResults Results;
		Results.bSucceeded = false;
		Results.bModifiedGraph = false;
		Results.ErrorMessages.Add(FText::Format(LOCTEXT("ResetFailedBecauseOfDiffMessage", "Failed to reset input back to it's base value.  It couldn't be diffed successfully.  Emitter: {0}  Input:{1}"),
			FText::FromString(Emitter.GetPathName()), FText::FromString(InputName)));
		return Results;
	}

	if (ResetDiffResults.IsEmpty())
	{
		FApplyDiffResults Results;
		Results.bSucceeded = false;
		Results.bModifiedGraph = false;
		Results.ErrorMessages.Add(FText::Format(LOCTEXT("ResetFailedBecauseOfEmptyDiffMessage", "Failed to reset input back to it's base value.  It wasn't different from the base.  Emitter: {0}  Input:{1}"),
			FText::FromString(Emitter.GetPathName()), FText::FromString(InputName)));
		return Results;
	}

	if (Emitter.ParentScratchPadScripts.Num() != BaseEmitter.ParentScratchPadScripts.Num() + BaseEmitter.ScratchPadScripts.Num())
	{
		FApplyDiffResults Results;
		Results.bSucceeded = false;
		Results.bModifiedGraph = false;
		Results.ErrorMessages.Add(FText::Format(LOCTEXT("ResetFailedBecauseOfScratchPadScripts", "Failed to reset input back to it's base value.  Its scratch pad scripts were out of sync.  Emitter: {0}  Input:{1}"),
			FText::FromString(Emitter.GetPathName()), FText::FromString(InputName)));
	}
	
	// Remove items from the diff which are not relevant to this input.
	ResetDiffResults.RemovedBaseModules.Empty();
	ResetDiffResults.AddedOtherModules.Empty();

	auto FindUnrelatedInputOverrides = [=](TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter> InputOverride)
	{
		return InputOverride->GetOwningFunctionCall()->NodeGuid != ModuleId || InputOverride->GetInputName() != InputName;
	};

	ResetDiffResults.RemovedBaseInputOverrides.RemoveAll(FindUnrelatedInputOverrides);
	ResetDiffResults.AddedOtherInputOverrides.RemoveAll(FindUnrelatedInputOverrides);
	ResetDiffResults.ModifiedBaseInputOverrides.RemoveAll(FindUnrelatedInputOverrides);
	ResetDiffResults.ModifiedOtherInputOverrides.RemoveAll(FindUnrelatedInputOverrides);

	return ApplyScriptStackDiff(EmitterAdapter, EmitterAdapter->GetScriptStack(ScriptUsage, ScriptUsageId).ToSharedRef(), ScratchPadMergeAdapter, ResetDiffResults, false);
}

bool FNiagaraScriptMergeManager::HasBaseEventHandler(const UNiagaraEmitter& BaseEmitter, FGuid EventScriptUsageId)
{
	TSharedRef<FNiagaraEmitterMergeAdapter> BaseEmitterAdapter = GetEmitterMergeAdapterUsingCache(BaseEmitter);
	return BaseEmitterAdapter->GetEventHandler(EventScriptUsageId).IsValid();
}

bool FNiagaraScriptMergeManager::IsEventHandlerPropertySetDifferentFromBase(UNiagaraEmitter& Emitter, const UNiagaraEmitter& BaseEmitter, FGuid EventScriptUsageId)
{
	TSharedRef<FNiagaraEmitterMergeAdapter> EmitterAdapter = GetEmitterMergeAdapterUsingCache(Emitter);
	TSharedRef<FNiagaraEmitterMergeAdapter> BaseEmitterAdapter = GetEmitterMergeAdapterUsingCache(BaseEmitter);

	TSharedPtr<FNiagaraEventHandlerMergeAdapter> EventHandlerAdapter = EmitterAdapter->GetEventHandler(EventScriptUsageId);
	TSharedPtr<FNiagaraEventHandlerMergeAdapter> BaseEventHandlerAdapter = BaseEmitterAdapter->GetEventHandler(EventScriptUsageId);

	if (EventHandlerAdapter->GetEditableEventScriptProperties() == nullptr || BaseEventHandlerAdapter->GetEventScriptProperties() == nullptr)
	{
		return true;
	}

	TArray<FProperty*> DifferentProperties;
	DiffEditableProperties(BaseEventHandlerAdapter->GetEventScriptProperties(), EventHandlerAdapter->GetEventScriptProperties(), *FNiagaraEventScriptProperties::StaticStruct(), DifferentProperties);
	return DifferentProperties.Num() > 0;
}

void FNiagaraScriptMergeManager::ResetEventHandlerPropertySetToBase(UNiagaraEmitter& Emitter, const UNiagaraEmitter& BaseEmitter, FGuid EventScriptUsageId)
{
	TSharedRef<FNiagaraEmitterMergeAdapter> EmitterAdapter = GetEmitterMergeAdapterUsingCache(Emitter);
	TSharedRef<FNiagaraEmitterMergeAdapter> BaseEmitterAdapter = GetEmitterMergeAdapterUsingCache(BaseEmitter);

	TSharedPtr<FNiagaraEventHandlerMergeAdapter> EventHandlerAdapter = EmitterAdapter->GetEventHandler(EventScriptUsageId);
	TSharedPtr<FNiagaraEventHandlerMergeAdapter> BaseEventHandlerAdapter = BaseEmitterAdapter->GetEventHandler(EventScriptUsageId);

	if (EventHandlerAdapter->GetEditableEventScriptProperties() == nullptr || BaseEventHandlerAdapter->GetEventScriptProperties() == nullptr)
	{
		// TODO: Display an error to the user.
		return;
	}

	TArray<FProperty*> DifferentProperties;
	DiffEditableProperties(BaseEventHandlerAdapter->GetEventScriptProperties(), EventHandlerAdapter->GetEventScriptProperties(), *FNiagaraEventScriptProperties::StaticStruct(), DifferentProperties);
	CopyPropertiesToBase(EventHandlerAdapter->GetEditableEventScriptProperties(), BaseEventHandlerAdapter->GetEventScriptProperties(), DifferentProperties);
	Emitter.PostEditChange();
}

bool FNiagaraScriptMergeManager::HasBaseSimulationStage(const UNiagaraEmitter& BaseEmitter, FGuid SimulationStageScriptUsageId)
{
	TSharedRef<FNiagaraEmitterMergeAdapter> BaseEmitterAdapter = GetEmitterMergeAdapterUsingCache(BaseEmitter);
	return BaseEmitterAdapter->GetSimulationStage(SimulationStageScriptUsageId).IsValid();
}

bool FNiagaraScriptMergeManager::IsSimulationStagePropertySetDifferentFromBase(UNiagaraEmitter& Emitter, const UNiagaraEmitter& BaseEmitter, FGuid SimulationStageScriptUsageId)
{
	TSharedRef<FNiagaraEmitterMergeAdapter> EmitterAdapter = GetEmitterMergeAdapterUsingCache(Emitter);
	TSharedRef<FNiagaraEmitterMergeAdapter> BaseEmitterAdapter = GetEmitterMergeAdapterUsingCache(BaseEmitter);

	TSharedPtr<FNiagaraSimulationStageMergeAdapter> SimulationStageAdapter = EmitterAdapter->GetSimulationStage(SimulationStageScriptUsageId);
	TSharedPtr<FNiagaraSimulationStageMergeAdapter> BaseSimulationStageAdapter = BaseEmitterAdapter->GetSimulationStage(SimulationStageScriptUsageId);

	if (SimulationStageAdapter->GetEditableSimulationStage() == nullptr || BaseSimulationStageAdapter->GetSimulationStage() == nullptr)
	{
		return true;
	}

	TArray<FProperty*> DifferentProperties;
	DiffEditableProperties(BaseSimulationStageAdapter->GetSimulationStage(), SimulationStageAdapter->GetSimulationStage(), *BaseSimulationStageAdapter->GetSimulationStage()->GetClass(), DifferentProperties);
	return DifferentProperties.Num() > 0;
}

void FNiagaraScriptMergeManager::ResetSimulationStagePropertySetToBase(UNiagaraEmitter& Emitter, const UNiagaraEmitter& BaseEmitter, FGuid SimulationStageScriptUsageId)
{
	TSharedRef<FNiagaraEmitterMergeAdapter> EmitterAdapter = GetEmitterMergeAdapterUsingCache(Emitter);
	TSharedRef<FNiagaraEmitterMergeAdapter> BaseEmitterAdapter = GetEmitterMergeAdapterUsingCache(BaseEmitter);

	TSharedPtr<FNiagaraSimulationStageMergeAdapter> SimulationStageAdapter = EmitterAdapter->GetSimulationStage(SimulationStageScriptUsageId);
	TSharedPtr<FNiagaraSimulationStageMergeAdapter> BaseSimulationStageAdapter = BaseEmitterAdapter->GetSimulationStage(SimulationStageScriptUsageId);

	if (SimulationStageAdapter->GetEditableSimulationStage() == nullptr || BaseSimulationStageAdapter->GetSimulationStage() == nullptr)
	{
		// TODO: Display an error to the user.
		return;
	}

	TArray<FProperty*> DifferentProperties;
	DiffEditableProperties(BaseSimulationStageAdapter->GetSimulationStage(), SimulationStageAdapter->GetSimulationStage(), *BaseSimulationStageAdapter->GetSimulationStage()->GetClass(), DifferentProperties);
	CopyPropertiesToBase(SimulationStageAdapter->GetEditableSimulationStage(), BaseSimulationStageAdapter->GetSimulationStage(), DifferentProperties);
	Emitter.PostEditChange();
}

bool FNiagaraScriptMergeManager::HasBaseRenderer(const UNiagaraEmitter& BaseEmitter, FGuid RendererMergeId)
{
	TSharedRef<FNiagaraEmitterMergeAdapter> BaseEmitterAdapter = GetEmitterMergeAdapterUsingCache(BaseEmitter);
	return BaseEmitterAdapter->GetRenderer(RendererMergeId).IsValid();
}

bool FNiagaraScriptMergeManager::IsRendererDifferentFromBase(UNiagaraEmitter& Emitter, const UNiagaraEmitter& BaseEmitter, FGuid RendererMergeId)
{
	TSharedRef<FNiagaraEmitterMergeAdapter> EmitterAdapter = GetEmitterMergeAdapterUsingCache(Emitter);
	TSharedRef<FNiagaraEmitterMergeAdapter> BaseEmitterAdapter = GetEmitterMergeAdapterUsingCache(BaseEmitter);

	FNiagaraEmitterDiffResults DiffResults;
	DiffRenderers(BaseEmitterAdapter->GetRenderers(), EmitterAdapter->GetRenderers(), DiffResults);

	if (DiffResults.IsValid() == false)
	{
		return true;
	}

	if (DiffResults.ModifiedOtherRenderers.Num() == 0)
	{
		return false;
	}

	auto FindRendererByMergeId = [=](TSharedRef<FNiagaraRendererMergeAdapter> Renderer) { return Renderer->GetRenderer()->GetMergeId() == RendererMergeId; };
	return DiffResults.ModifiedOtherRenderers.FindByPredicate(FindRendererByMergeId) != nullptr;
}

void FNiagaraScriptMergeManager::ResetRendererToBase(UNiagaraEmitter& Emitter, const UNiagaraEmitter& BaseEmitter, FGuid RendererMergeId)
{
	TSharedRef<FNiagaraEmitterMergeAdapter> EmitterAdapter = GetEmitterMergeAdapterUsingCache(Emitter);
	TSharedRef<FNiagaraEmitterMergeAdapter> BaseEmitterAdapter = GetEmitterMergeAdapterUsingCache(BaseEmitter);

	// Diff from the current emitter to the base emitter to create a diff which will reset the emitter back to the base.
	FNiagaraEmitterDiffResults ResetDiffResults;
	DiffRenderers(EmitterAdapter->GetRenderers(), BaseEmitterAdapter->GetRenderers(), ResetDiffResults);

	auto FindUnrelatedRenderers = [=](TSharedRef<FNiagaraRendererMergeAdapter> Renderer)
	{
		return Renderer->GetRenderer()->GetMergeId() != RendererMergeId;
	};

	// Removed added and removed renderers, as well as changes to renderers with different ids from the one being reset.
	ResetDiffResults.RemovedBaseRenderers.Empty();
	ResetDiffResults.AddedOtherRenderers.Empty();
	ResetDiffResults.ModifiedBaseRenderers.RemoveAll(FindUnrelatedRenderers);
	ResetDiffResults.ModifiedOtherRenderers.RemoveAll(FindUnrelatedRenderers);

	ApplyRendererDiff(Emitter, ResetDiffResults, false);
}

bool FNiagaraScriptMergeManager::IsEmitterEditablePropertySetDifferentFromBase(const UNiagaraEmitter& Emitter, const UNiagaraEmitter& BaseEmitter)
{
	TArray<FProperty*> DifferentProperties;
	DiffEditableProperties(&BaseEmitter, &Emitter, *UNiagaraEmitter::StaticClass(), DifferentProperties);
	return DifferentProperties.Num() > 0;
}

void FNiagaraScriptMergeManager::ResetEmitterEditablePropertySetToBase(UNiagaraEmitter& Emitter, const UNiagaraEmitter& BaseEmitter)
{
	TArray<FProperty*> DifferentProperties;
	DiffEditableProperties(&BaseEmitter, &Emitter, *UNiagaraEmitter::StaticClass(), DifferentProperties);
	CopyPropertiesToBase(&Emitter, &BaseEmitter, DifferentProperties);
	Emitter.PostEditChange();
}

FNiagaraEmitterDiffResults FNiagaraScriptMergeManager::DiffEmitters(UNiagaraEmitter& BaseEmitter, UNiagaraEmitter& OtherEmitter) const
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_ScriptMergeManager_DiffEmitters);

	TSharedRef<FNiagaraEmitterMergeAdapter> BaseEmitterAdapter = MakeShared<FNiagaraEmitterMergeAdapter>(BaseEmitter);
	TSharedRef<FNiagaraEmitterMergeAdapter> OtherEmitterAdapter = MakeShared<FNiagaraEmitterMergeAdapter>(OtherEmitter);

	FNiagaraEmitterDiffResults EmitterDiffResults;
	if (BaseEmitterAdapter->GetEmitterSpawnStack().IsValid() && OtherEmitterAdapter->GetEmitterSpawnStack().IsValid())
	{
		DiffScriptStacks(BaseEmitterAdapter->GetEmitterSpawnStack().ToSharedRef(), OtherEmitterAdapter->GetEmitterSpawnStack().ToSharedRef(), EmitterDiffResults.EmitterSpawnDiffResults);
	}
	else
	{
		EmitterDiffResults.AddError(LOCTEXT("EmitterSpawnStacksInvalidMessage", "One of the emitter spawn script stacks was invalid."));
	}

	if (BaseEmitterAdapter->GetEmitterUpdateStack().IsValid() && OtherEmitterAdapter->GetEmitterUpdateStack().IsValid())
	{
		DiffScriptStacks(BaseEmitterAdapter->GetEmitterUpdateStack().ToSharedRef(), OtherEmitterAdapter->GetEmitterUpdateStack().ToSharedRef(), EmitterDiffResults.EmitterUpdateDiffResults);
	}
	else
	{
		EmitterDiffResults.AddError(LOCTEXT("EmitterUpdateStacksInvalidMessage", "One of the emitter update script stacks was invalid."));
	}

	if (BaseEmitterAdapter->GetParticleSpawnStack().IsValid() && OtherEmitterAdapter->GetParticleSpawnStack().IsValid())
	{
		DiffScriptStacks(BaseEmitterAdapter->GetParticleSpawnStack().ToSharedRef(), OtherEmitterAdapter->GetParticleSpawnStack().ToSharedRef(), EmitterDiffResults.ParticleSpawnDiffResults);
	}
	else
	{
		EmitterDiffResults.AddError(LOCTEXT("ParticleSpawnStacksInvalidMessage", "One of the particle spawn script stacks was invalid."));
	}

	if (BaseEmitterAdapter->GetParticleUpdateStack().IsValid() && OtherEmitterAdapter->GetParticleUpdateStack().IsValid())
	{
		DiffScriptStacks(BaseEmitterAdapter->GetParticleUpdateStack().ToSharedRef(), OtherEmitterAdapter->GetParticleUpdateStack().ToSharedRef(), EmitterDiffResults.ParticleUpdateDiffResults);
	}
	else
	{
		EmitterDiffResults.AddError(LOCTEXT("ParticleUpdateStacksInvalidMessage", "One of the particle update script stacks was invalid."));
	}

	DiffEventHandlers(BaseEmitterAdapter->GetEventHandlers(), OtherEmitterAdapter->GetEventHandlers(), EmitterDiffResults);
	DiffSimulationStages(BaseEmitterAdapter->GetSimulationStages(), OtherEmitterAdapter->GetSimulationStages(), EmitterDiffResults);
	DiffRenderers(BaseEmitterAdapter->GetRenderers(), OtherEmitterAdapter->GetRenderers(), EmitterDiffResults);
	DiffEditableProperties(&BaseEmitter, &OtherEmitter, *UNiagaraEmitter::StaticClass(), EmitterDiffResults.DifferentEmitterProperties);
	DiffStackEntryDisplayNames(BaseEmitterAdapter->GetEditorData(), OtherEmitterAdapter->GetEditorData(), EmitterDiffResults.ModifiedStackEntryDisplayNames);

	TArray<UNiagaraScript*> BaseScratchPadScripts;
	BaseScratchPadScripts.Append(BaseEmitter.ParentScratchPadScripts);
	BaseScratchPadScripts.Append(BaseEmitter.ScratchPadScripts);
	TArray<UNiagaraScript*> OtherScratchPadScripts;
	OtherScratchPadScripts.Append(OtherEmitter.ParentScratchPadScripts);
	OtherScratchPadScripts.Append(OtherEmitter.ScratchPadScripts);
	DiffScratchPadScripts(BaseScratchPadScripts, OtherScratchPadScripts, EmitterDiffResults);

	EmitterDiffResults.BaseEmitterAdapter = BaseEmitterAdapter;
	EmitterDiffResults.OtherEmitterAdapter = OtherEmitterAdapter;

	return EmitterDiffResults;
}

template<typename ValueType>
struct FCommonValuePair
{
	FCommonValuePair(ValueType InBaseValue, ValueType InOtherValue)
		: BaseValue(InBaseValue)
		, OtherValue(InOtherValue)
	{
	}

	ValueType BaseValue;
	ValueType OtherValue;
};

template<typename ValueType>
struct FListDiffResults
{
	TArray<ValueType> RemovedBaseValues;
	TArray<ValueType> AddedOtherValues;
	TArray<FCommonValuePair<ValueType>> CommonValuePairs;
};

template<typename ValueType, typename KeyType, typename KeyFromValueDelegate>
FListDiffResults<ValueType> DiffLists(const TArray<ValueType>& BaseList, const TArray<ValueType> OtherList, KeyFromValueDelegate KeyFromValue)
{
	FListDiffResults<ValueType> DiffResults;

	TMap<KeyType, ValueType> BaseKeyToValueMap;
	TSet<KeyType> BaseKeys;
	for (ValueType BaseValue : BaseList)
	{
		KeyType BaseKey = KeyFromValue(BaseValue);
		BaseKeyToValueMap.Add(BaseKey, BaseValue);
		BaseKeys.Add(BaseKey);
	}

	TMap<KeyType, ValueType> OtherKeyToValueMap;
	TSet<KeyType> OtherKeys;
	for (ValueType OtherValue : OtherList)
	{
		KeyType OtherKey = KeyFromValue(OtherValue);
		OtherKeyToValueMap.Add(OtherKey, OtherValue);
		OtherKeys.Add(OtherKey);
	}

	for (KeyType RemovedKey : BaseKeys.Difference(OtherKeys))
	{
		DiffResults.RemovedBaseValues.Add(BaseKeyToValueMap[RemovedKey]);
	}

	for (KeyType AddedKey : OtherKeys.Difference(BaseKeys))
	{
		DiffResults.AddedOtherValues.Add(OtherKeyToValueMap[AddedKey]);
	}

	for (KeyType CommonKey : BaseKeys.Intersect(OtherKeys))
	{
		DiffResults.CommonValuePairs.Add(FCommonValuePair<ValueType>(BaseKeyToValueMap[CommonKey], OtherKeyToValueMap[CommonKey]));
	}

	return DiffResults;
}

void FNiagaraScriptMergeManager::DiffEventHandlers(const TArray<TSharedRef<FNiagaraEventHandlerMergeAdapter>>& BaseEventHandlers, const TArray<TSharedRef<FNiagaraEventHandlerMergeAdapter>>& OtherEventHandlers, FNiagaraEmitterDiffResults& DiffResults) const
{
	FListDiffResults<TSharedRef<FNiagaraEventHandlerMergeAdapter>> EventHandlerListDiffResults = DiffLists<TSharedRef<FNiagaraEventHandlerMergeAdapter>, FGuid>(
		BaseEventHandlers,
		OtherEventHandlers,
		[](TSharedRef<FNiagaraEventHandlerMergeAdapter> EventHandler) { return EventHandler->GetUsageId(); });

	DiffResults.RemovedBaseEventHandlers.Append(EventHandlerListDiffResults.RemovedBaseValues);
	DiffResults.AddedOtherEventHandlers.Append(EventHandlerListDiffResults.AddedOtherValues);

	for (const FCommonValuePair<TSharedRef<FNiagaraEventHandlerMergeAdapter>>& CommonValuePair : EventHandlerListDiffResults.CommonValuePairs)
	{
		if (CommonValuePair.BaseValue->GetEventScriptProperties() == nullptr || CommonValuePair.BaseValue->GetOutputNode() == nullptr)
		{
			DiffResults.AddError(FText::Format(LOCTEXT("InvalidBaseEventHandlerDiffFailedFormat", "Failed to diff event handlers, the base event handler was invalid.  Script Usage Id: {0}"),
				FText::FromString(CommonValuePair.BaseValue->GetUsageId().ToString())));
		}
		else if(CommonValuePair.OtherValue->GetEventScriptProperties() == nullptr || CommonValuePair.OtherValue->GetOutputNode() == nullptr)
		{
			DiffResults.AddError(FText::Format(LOCTEXT("InvalidOtherEventHandlerDiffFailedFormat", "Failed to diff event handlers, the other event handler was invalid.  Script Usage Id: {0}"),
				FText::FromString(CommonValuePair.OtherValue->GetUsageId().ToString())));
		}
		else
		{
			TArray<FProperty*> DifferentProperties;
			DiffEditableProperties(CommonValuePair.BaseValue->GetEventScriptProperties(), CommonValuePair.OtherValue->GetEventScriptProperties(), *FNiagaraEventScriptProperties::StaticStruct(), DifferentProperties);

			FNiagaraScriptStackDiffResults EventHandlerScriptStackDiffResults;
			DiffScriptStacks(CommonValuePair.BaseValue->GetEventStack().ToSharedRef(), CommonValuePair.OtherValue->GetEventStack().ToSharedRef(), EventHandlerScriptStackDiffResults);

			if (DifferentProperties.Num() > 0 || EventHandlerScriptStackDiffResults.IsValid() == false || EventHandlerScriptStackDiffResults.IsEmpty() == false)
			{
				FNiagaraModifiedEventHandlerDiffResults ModifiedEventHandlerResults;
				ModifiedEventHandlerResults.BaseAdapter = CommonValuePair.BaseValue;
				ModifiedEventHandlerResults.OtherAdapter = CommonValuePair.OtherValue;
				ModifiedEventHandlerResults.ChangedProperties.Append(DifferentProperties);
				ModifiedEventHandlerResults.ScriptDiffResults = EventHandlerScriptStackDiffResults;
				DiffResults.ModifiedEventHandlers.Add(ModifiedEventHandlerResults);
			}

			if (EventHandlerScriptStackDiffResults.IsValid() == false)
			{
				for (const FText& EventHandlerScriptStackDiffErrorMessage : EventHandlerScriptStackDiffResults.GetErrorMessages())
				{
					DiffResults.AddError(EventHandlerScriptStackDiffErrorMessage);
				}
			}
		}
	}
}

void FNiagaraScriptMergeManager::DiffSimulationStages(const TArray<TSharedRef<FNiagaraSimulationStageMergeAdapter>>& BaseSimulationStages, const TArray<TSharedRef<FNiagaraSimulationStageMergeAdapter>>& OtherSimulationStages, FNiagaraEmitterDiffResults& DiffResults) const
{
	FListDiffResults<TSharedRef<FNiagaraSimulationStageMergeAdapter>> SimulationStageListDiffResults = DiffLists<TSharedRef<FNiagaraSimulationStageMergeAdapter>, FGuid>(
		BaseSimulationStages,
		OtherSimulationStages,
		[](TSharedRef<FNiagaraSimulationStageMergeAdapter> SimulationStage) { return SimulationStage->GetUsageId(); });

	// Sort the diff results for easier diff applying and testing.
	auto OrderSimulationStageByIndex = [](TSharedRef<FNiagaraSimulationStageMergeAdapter> SimulationStageA, TSharedRef<FNiagaraSimulationStageMergeAdapter> SimulationStageB)
	{
		return SimulationStageA->GetSimulationStageIndex() < SimulationStageB->GetSimulationStageIndex();
	};

	SimulationStageListDiffResults.RemovedBaseValues.Sort(OrderSimulationStageByIndex);
	SimulationStageListDiffResults.AddedOtherValues.Sort(OrderSimulationStageByIndex);

	auto OrderCommonSimulationStagePairByBaseIndex = [](
		const FCommonValuePair<TSharedRef<FNiagaraSimulationStageMergeAdapter>>& CommonValuesA,
		const FCommonValuePair<TSharedRef<FNiagaraSimulationStageMergeAdapter>>& CommonValuesB)
	{
		return CommonValuesA.BaseValue->GetSimulationStageIndex() < CommonValuesB.BaseValue->GetSimulationStageIndex();
	};

	SimulationStageListDiffResults.CommonValuePairs.Sort(OrderCommonSimulationStagePairByBaseIndex);

	// Populate results from the sorted diff.
	DiffResults.RemovedBaseSimulationStages.Append(SimulationStageListDiffResults.RemovedBaseValues);
	DiffResults.AddedOtherSimulationStages.Append(SimulationStageListDiffResults.AddedOtherValues);

	for (const FCommonValuePair<TSharedRef<FNiagaraSimulationStageMergeAdapter>>& CommonValuePair : SimulationStageListDiffResults.CommonValuePairs)
	{
		if (CommonValuePair.BaseValue->GetSimulationStage() == nullptr || CommonValuePair.BaseValue->GetOutputNode() == nullptr)
		{
			DiffResults.AddError(FText::Format(LOCTEXT("InvalidBaseSimulationStageDiffFailedFormat", "Failed to diff shader stages, the base shader stage was invalid.  Script Usage Id: {0}"),
				FText::FromString(CommonValuePair.BaseValue->GetUsageId().ToString())));
		}
		else if (CommonValuePair.OtherValue->GetSimulationStage() == nullptr || CommonValuePair.OtherValue->GetOutputNode() == nullptr)
		{
			DiffResults.AddError(FText::Format(LOCTEXT("InvalidOtherSimulationStageDiffFailedFormat", "Failed to diff shader stage, the other shader stage was invalid.  Script Usage Id: {0}"),
				FText::FromString(CommonValuePair.OtherValue->GetUsageId().ToString())));
		}
		else
		{
			TArray<FProperty*> DifferentProperties;
			DiffEditableProperties(CommonValuePair.BaseValue->GetSimulationStage(), CommonValuePair.OtherValue->GetSimulationStage(), *CommonValuePair.BaseValue->GetSimulationStage()->GetClass(), DifferentProperties);

			FNiagaraScriptStackDiffResults SimulationStageScriptStackDiffResults;
			DiffScriptStacks(CommonValuePair.BaseValue->GetSimulationStageStack().ToSharedRef(), CommonValuePair.OtherValue->GetSimulationStageStack().ToSharedRef(), SimulationStageScriptStackDiffResults);

			if (DifferentProperties.Num() > 0 || SimulationStageScriptStackDiffResults.IsValid() == false || SimulationStageScriptStackDiffResults.IsEmpty() == false)
			{
				FNiagaraModifiedSimulationStageDiffResults ModifiedSimulationStageResults;
				ModifiedSimulationStageResults.BaseAdapter = CommonValuePair.BaseValue;
				ModifiedSimulationStageResults.OtherAdapter = CommonValuePair.OtherValue;
				ModifiedSimulationStageResults.ChangedProperties.Append(DifferentProperties);
				ModifiedSimulationStageResults.ScriptDiffResults = SimulationStageScriptStackDiffResults;
				DiffResults.ModifiedSimulationStages.Add(ModifiedSimulationStageResults);
			}

			if (SimulationStageScriptStackDiffResults.IsValid() == false)
			{
				for (const FText& SimulationStageScriptStackDiffErrorMessage : SimulationStageScriptStackDiffResults.GetErrorMessages())
				{
					DiffResults.AddError(SimulationStageScriptStackDiffErrorMessage);
				}
			}
		}
	}
}

void FNiagaraScriptMergeManager::DiffRenderers(const TArray<TSharedRef<FNiagaraRendererMergeAdapter>>& BaseRenderers, const TArray<TSharedRef<FNiagaraRendererMergeAdapter>>& OtherRenderers, FNiagaraEmitterDiffResults& DiffResults) const
{
	FListDiffResults<TSharedRef<FNiagaraRendererMergeAdapter>> RendererListDiffResults = DiffLists<TSharedRef<FNiagaraRendererMergeAdapter>, FGuid>(
		BaseRenderers,
		OtherRenderers,
		[](TSharedRef<FNiagaraRendererMergeAdapter> Renderer) { return Renderer->GetRenderer()->GetMergeId(); });

	DiffResults.RemovedBaseRenderers.Append(RendererListDiffResults.RemovedBaseValues);
	DiffResults.AddedOtherRenderers.Append(RendererListDiffResults.AddedOtherValues);

	for (const FCommonValuePair<TSharedRef<FNiagaraRendererMergeAdapter>>& CommonValuePair : RendererListDiffResults.CommonValuePairs)
	{
		if (CommonValuePair.BaseValue->GetRenderer()->Equals(CommonValuePair.OtherValue->GetRenderer()) == false)
		{
			DiffResults.ModifiedBaseRenderers.Add(CommonValuePair.BaseValue);
			DiffResults.ModifiedOtherRenderers.Add(CommonValuePair.OtherValue);
		}
	}
}

void FNiagaraScriptMergeManager::DiffScriptStacks(TSharedRef<FNiagaraScriptStackMergeAdapter> BaseScriptStackAdapter, TSharedRef<FNiagaraScriptStackMergeAdapter> OtherScriptStackAdapter, FNiagaraScriptStackDiffResults& DiffResults) const
{
	// Diff the module lists.
	FListDiffResults<TSharedRef<FNiagaraStackFunctionMergeAdapter>> ModuleListDiffResults = DiffLists<TSharedRef<FNiagaraStackFunctionMergeAdapter>, FGuid>(
		BaseScriptStackAdapter->GetModuleFunctions(),
		OtherScriptStackAdapter->GetModuleFunctions(),
		[](TSharedRef<FNiagaraStackFunctionMergeAdapter> FunctionAdapter) { return FunctionAdapter->GetFunctionCallNode()->NodeGuid; });

	// Sort the diff results for easier diff applying and testing.
	auto OrderModuleByStackIndex = [](TSharedRef<FNiagaraStackFunctionMergeAdapter> ModuleA, TSharedRef<FNiagaraStackFunctionMergeAdapter> ModuleB)
	{
		return ModuleA->GetStackIndex() < ModuleB->GetStackIndex();
	};

	ModuleListDiffResults.RemovedBaseValues.Sort(OrderModuleByStackIndex);
	ModuleListDiffResults.AddedOtherValues.Sort(OrderModuleByStackIndex);

	auto OrderCommonModulePairByBaseStackIndex = [](
		const FCommonValuePair<TSharedRef<FNiagaraStackFunctionMergeAdapter>>& CommonValuesA,
		const FCommonValuePair<TSharedRef<FNiagaraStackFunctionMergeAdapter>>& CommonValuesB)
	{
		return CommonValuesA.BaseValue->GetStackIndex() < CommonValuesB.BaseValue->GetStackIndex();
	};

	ModuleListDiffResults.CommonValuePairs.Sort(OrderCommonModulePairByBaseStackIndex);

	// Populate results from the sorted diff.
	DiffResults.RemovedBaseModules.Append(ModuleListDiffResults.RemovedBaseValues);
	DiffResults.AddedOtherModules.Append(ModuleListDiffResults.AddedOtherValues);

	for (const FCommonValuePair<TSharedRef<FNiagaraStackFunctionMergeAdapter>>& CommonValuePair : ModuleListDiffResults.CommonValuePairs)
	{
		if (CommonValuePair.BaseValue->GetStackIndex() != CommonValuePair.OtherValue->GetStackIndex())
		{
			DiffResults.MovedBaseModules.Add(CommonValuePair.BaseValue);
			DiffResults.MovedOtherModules.Add(CommonValuePair.OtherValue);
		}

		if (CommonValuePair.BaseValue->GetFunctionCallNode()->IsNodeEnabled() != CommonValuePair.OtherValue->GetFunctionCallNode()->IsNodeEnabled())
		{
			DiffResults.EnabledChangedBaseModules.Add(CommonValuePair.BaseValue);
			DiffResults.EnabledChangedOtherModules.Add(CommonValuePair.OtherValue);
		}

		const TArray<FNiagaraStackMessage>& BaseMessages = CommonValuePair.BaseValue->GetMessages();
		const TArray<FNiagaraStackMessage>& OtherMessages = CommonValuePair.OtherValue->GetMessages();

		for(const FNiagaraStackMessage& OtherMessage : OtherMessages)
		{
			if(!BaseMessages.ContainsByPredicate([&](const FNiagaraStackMessage& Message)
			{
				return Message.Guid == OtherMessage.Guid;
			}))
			{
				DiffResults.AddedOtherMessages.Add({CommonValuePair.OtherValue, OtherMessage});
			}
		}

		for(const FNiagaraStackMessage& BaseMessage : BaseMessages)
		{
			if(!OtherMessages.ContainsByPredicate([&](const FNiagaraStackMessage& Message)
			{
				return Message.Guid == BaseMessage.Guid;
			}))
			{
				DiffResults.RemovedBaseMessagesInOther.Add({CommonValuePair.BaseValue, BaseMessage});
			}
		}

		if (CommonValuePair.BaseValue->GetFunctionCallNode()->SelectedScriptVersion != CommonValuePair.OtherValue->GetFunctionCallNode()->SelectedScriptVersion)
		{
			DiffResults.ChangedVersionBaseModules.Add(CommonValuePair.BaseValue);
			DiffResults.ChangedVersionOtherModules.Add(CommonValuePair.OtherValue);
		}

		UNiagaraScript* BaseFunctionScript = CommonValuePair.BaseValue->GetFunctionCallNode()->FunctionScript;
		UNiagaraScript* OtherFunctionScript = CommonValuePair.OtherValue->GetFunctionCallNode()->FunctionScript;
		bool bFunctionScriptsMatch = BaseFunctionScript == OtherFunctionScript;
		bool bFunctionScriptsAreNotAssets =
			BaseFunctionScript != nullptr && BaseFunctionScript->IsAsset() == false &&
			OtherFunctionScript != nullptr && OtherFunctionScript->IsAsset() == false;
		if (bFunctionScriptsMatch || bFunctionScriptsAreNotAssets)
		{
			DiffFunctionInputs(CommonValuePair.BaseValue, CommonValuePair.OtherValue, DiffResults);
		}
		else
		{
			FText ErrorMessage = FText::Format(LOCTEXT("FunctionScriptMismatchFormat", "Function scripts for function {0} did not match.  Parent: {1} Child: {2}.  This can be fixed by removing the module from the parent, merging the removal to the child, then removing it from the child, and then re-adding it to the parent and merging again."),
				FText::FromString(CommonValuePair.BaseValue->GetFunctionCallNode()->GetFunctionName()), 
				FText::FromString(CommonValuePair.BaseValue->GetFunctionCallNode()->FunctionScript != nullptr ? CommonValuePair.BaseValue->GetFunctionCallNode()->FunctionScript->GetPathName() : TEXT("(null)")),
				FText::FromString(CommonValuePair.OtherValue->GetFunctionCallNode()->FunctionScript != nullptr ? CommonValuePair.OtherValue->GetFunctionCallNode()->FunctionScript->GetPathName() : TEXT("(null)")));
			DiffResults.AddError(ErrorMessage);
		}
	}

	if (BaseScriptStackAdapter->GetScript()->GetUsage() != OtherScriptStackAdapter->GetScript()->GetUsage())
	{
		DiffResults.ChangedBaseUsage = BaseScriptStackAdapter->GetScript()->GetUsage();
		DiffResults.ChangedOtherUsage = OtherScriptStackAdapter->GetScript()->GetUsage();
	}
}

void FNiagaraScriptMergeManager::DiffFunctionInputs(TSharedRef<FNiagaraStackFunctionMergeAdapter> BaseFunctionAdapter, TSharedRef<FNiagaraStackFunctionMergeAdapter> OtherFunctionAdapter, FNiagaraScriptStackDiffResults& DiffResults) const
{
	FListDiffResults<TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter>> ListDiffResults = DiffLists<TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter>, FString>(
		BaseFunctionAdapter->GetInputOverrides(),
		OtherFunctionAdapter->GetInputOverrides(),
		[](TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter> InputOverrideAdapter) { return InputOverrideAdapter->GetInputName(); });

	DiffResults.RemovedBaseInputOverrides.Append(ListDiffResults.RemovedBaseValues);
	DiffResults.AddedOtherInputOverrides.Append(ListDiffResults.AddedOtherValues);

	for (const FCommonValuePair<TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter>>& CommonValuePair : ListDiffResults.CommonValuePairs)
	{
		TOptional<bool> FunctionMatch = DoFunctionInputOverridesMatch(CommonValuePair.BaseValue, CommonValuePair.OtherValue);
		if (FunctionMatch.IsSet())
		{
			if (FunctionMatch.GetValue() == false)
			{
				DiffResults.ModifiedBaseInputOverrides.Add(CommonValuePair.BaseValue);
				DiffResults.ModifiedOtherInputOverrides.Add(CommonValuePair.OtherValue);
			}
		}
		else
		{
			DiffResults.AddError(FText::Format(LOCTEXT("FunctionInputDiffFailedFormat", "Failed to diff function inputs.  Function name: {0}  Input Name: {1}"),
				FText::FromString(BaseFunctionAdapter->GetFunctionCallNode()->GetFunctionName()),
				FText::FromString(CommonValuePair.BaseValue->GetInputName())));
		}
	}
}

void FNiagaraScriptMergeManager::DiffEditableProperties(const void* BaseDataAddress, const void* OtherDataAddress, UStruct& Struct, TArray<FProperty*>& OutDifferentProperties) const
{
	for (TFieldIterator<FProperty> PropertyIterator(&Struct); PropertyIterator; ++PropertyIterator)
	{
		if (PropertyIterator->HasAllPropertyFlags(CPF_Edit) && PropertyIterator->HasMetaData("NiagaraNoMerge") == false)
		{
			if (PropertyIterator->Identical(
				PropertyIterator->ContainerPtrToValuePtr<void>(BaseDataAddress),
				PropertyIterator->ContainerPtrToValuePtr<void>(OtherDataAddress), PPF_DeepComparison) == false)
			{
				OutDifferentProperties.Add(*PropertyIterator);
			}
		}
	}
}

void FNiagaraScriptMergeManager::DiffStackEntryDisplayNames(const UNiagaraEmitterEditorData* BaseEditorData, const UNiagaraEmitterEditorData* OtherEditorData, TMap<FString, FText>& OutModifiedStackEntryDisplayNames) const
{
	if (BaseEditorData != nullptr && OtherEditorData != nullptr)
	{
		// find display names that have been added or changed in the instance
		const TMap<FString, FText>& OtherRenames = OtherEditorData->GetStackEditorData().GetAllStackEntryDisplayNames();
		for (auto& Pair : OtherRenames)
		{
			const FText* BaseDisplayName = BaseEditorData->GetStackEditorData().GetStackEntryDisplayName(Pair.Key);
			if (BaseDisplayName == nullptr || !BaseDisplayName->EqualTo(Pair.Value))
			{
				OutModifiedStackEntryDisplayNames.Add(Pair.Key, Pair.Value);
			}
		}
	}
}

void FNiagaraScriptMergeManager::DiffScratchPadScripts(const TArray<UNiagaraScript*>& BaseScratchPadScripts, const TArray<UNiagaraScript*>& OtherEmitterScratchPadScripts, FNiagaraEmitterDiffResults& DiffResults) const
{
	if (BaseScratchPadScripts.Num() != OtherEmitterScratchPadScripts.Num())
	{
		DiffResults.bScratchPadModified = true;
		return;
	}

	for (int32 ScratchPadScriptIndex = 0; ScratchPadScriptIndex < BaseScratchPadScripts.Num(); ScratchPadScriptIndex++)
	{
		UNiagaraScript* BaseScratchPadScript = BaseScratchPadScripts[ScratchPadScriptIndex];
		UNiagaraScript* OtherScratchPadScript = OtherEmitterScratchPadScripts[ScratchPadScriptIndex];
		if (BaseScratchPadScript->GetName() != OtherScratchPadScript->GetName() ||
			BaseScratchPadScript->GetBaseChangeID() != OtherScratchPadScript->GetBaseChangeID())
		{
			DiffResults.bScratchPadModified = true;
			return;
		}
	}
}

TOptional<bool> FNiagaraScriptMergeManager::DoFunctionInputOverridesMatch(TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter> BaseFunctionInputAdapter, TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter> OtherFunctionInputAdapter) const
{
	// Local String Value.
	if ((BaseFunctionInputAdapter->GetLocalValueString().IsSet() && OtherFunctionInputAdapter->GetLocalValueString().IsSet() == false) ||
		(BaseFunctionInputAdapter->GetLocalValueString().IsSet() == false && OtherFunctionInputAdapter->GetLocalValueString().IsSet()))
	{
		return false;
	}

	if (BaseFunctionInputAdapter->GetLocalValueString().IsSet() && OtherFunctionInputAdapter->GetLocalValueString().IsSet())
	{
		return BaseFunctionInputAdapter->GetLocalValueString().GetValue() == OtherFunctionInputAdapter->GetLocalValueString().GetValue();
	}

	// Local rapid iteration parameter value.
	if ((BaseFunctionInputAdapter->GetLocalValueRapidIterationParameter().IsSet() && OtherFunctionInputAdapter->GetLocalValueRapidIterationParameter().IsSet() == false) ||
		(BaseFunctionInputAdapter->GetLocalValueRapidIterationParameter().IsSet() == false && OtherFunctionInputAdapter->GetLocalValueRapidIterationParameter().IsSet()))
	{
		return false;
	}

	if (BaseFunctionInputAdapter->GetLocalValueRapidIterationParameter().IsSet() && OtherFunctionInputAdapter->GetLocalValueRapidIterationParameter().IsSet())
	{
		const uint8* BaseRapidIterationParameterValue = BaseFunctionInputAdapter->GetOwningScript()->RapidIterationParameters
			.GetParameterData(BaseFunctionInputAdapter->GetLocalValueRapidIterationParameter().GetValue());
		const uint8* OtherRapidIterationParameterValue = OtherFunctionInputAdapter->GetOwningScript()->RapidIterationParameters
			.GetParameterData(OtherFunctionInputAdapter->GetLocalValueRapidIterationParameter().GetValue());
		if (BaseRapidIterationParameterValue == nullptr || OtherRapidIterationParameterValue == nullptr)
		{
			return TOptional<bool>();
		}
		return FMemory::Memcmp(
			BaseRapidIterationParameterValue,
			OtherRapidIterationParameterValue,
			BaseFunctionInputAdapter->GetLocalValueRapidIterationParameter().GetValue().GetSizeInBytes()) == 0;
	}

	// Linked value
	if ((BaseFunctionInputAdapter->GetLinkedValueHandle().IsSet() && OtherFunctionInputAdapter->GetLinkedValueHandle().IsSet() == false) ||
		(BaseFunctionInputAdapter->GetLinkedValueHandle().IsSet() == false && OtherFunctionInputAdapter->GetLinkedValueHandle().IsSet()))
	{
		return false;
	}

	if (BaseFunctionInputAdapter->GetLinkedValueHandle().IsSet() && OtherFunctionInputAdapter->GetLinkedValueHandle().IsSet())
	{
		return BaseFunctionInputAdapter->GetLinkedValueHandle().GetValue() == OtherFunctionInputAdapter->GetLinkedValueHandle().GetValue();
	}

	// Data value
	if ((BaseFunctionInputAdapter->GetDataValueInputName().IsSet() && OtherFunctionInputAdapter->GetDataValueInputName().IsSet() == false) ||
		(BaseFunctionInputAdapter->GetDataValueInputName().IsSet() == false && OtherFunctionInputAdapter->GetDataValueInputName().IsSet()) ||
		(BaseFunctionInputAdapter->GetDataValueObject() != nullptr && OtherFunctionInputAdapter->GetDataValueObject() == nullptr) ||
		(BaseFunctionInputAdapter->GetDataValueObject() == nullptr && OtherFunctionInputAdapter->GetDataValueObject() != nullptr))
	{
		return false;
	}

	if (BaseFunctionInputAdapter->GetDataValueInputName().IsSet() && OtherFunctionInputAdapter->GetDataValueInputName().IsSet() &&
		BaseFunctionInputAdapter->GetDataValueObject() != nullptr && OtherFunctionInputAdapter->GetDataValueObject() != nullptr)
	{
		return 
			BaseFunctionInputAdapter->GetDataValueInputName().GetValue() == OtherFunctionInputAdapter->GetDataValueInputName().GetValue() &&
			BaseFunctionInputAdapter->GetDataValueObject()->Equals(OtherFunctionInputAdapter->GetDataValueObject());
	}

	// Dynamic value
	if ((BaseFunctionInputAdapter->GetDynamicValueFunction().IsValid() && OtherFunctionInputAdapter->GetDynamicValueFunction().IsValid() == false) ||
		(BaseFunctionInputAdapter->GetDynamicValueFunction().IsValid() == false && OtherFunctionInputAdapter->GetDynamicValueFunction().IsValid()))
	{
		return false;
	}

	if (BaseFunctionInputAdapter->GetDynamicValueFunction().IsValid() && OtherFunctionInputAdapter->GetDynamicValueFunction().IsValid())
	{
		TSharedRef<FNiagaraStackFunctionMergeAdapter> BaseDynamicValueFunction = BaseFunctionInputAdapter->GetDynamicValueFunction().ToSharedRef();
		TSharedRef<FNiagaraStackFunctionMergeAdapter> OtherDynamicValueFunction = OtherFunctionInputAdapter->GetDynamicValueFunction().ToSharedRef();

		UNiagaraNodeCustomHlsl* BaseCustomHlsl = Cast<UNiagaraNodeCustomHlsl>(BaseDynamicValueFunction->GetFunctionCallNode());
		UNiagaraNodeCustomHlsl* OtherCustomHlsl = Cast<UNiagaraNodeCustomHlsl>(OtherDynamicValueFunction->GetFunctionCallNode());
		if (BaseCustomHlsl != nullptr || OtherCustomHlsl != nullptr)
		{
			if ((BaseCustomHlsl != nullptr && OtherCustomHlsl == nullptr) ||
				(BaseCustomHlsl == nullptr && OtherCustomHlsl != nullptr))
			{
				return false;
			}

			if (BaseCustomHlsl->GetCustomHlsl() != OtherCustomHlsl->GetCustomHlsl() || BaseCustomHlsl->ScriptUsage != OtherCustomHlsl->ScriptUsage)
			{
				return false;
			}
		}
		else if (BaseDynamicValueFunction->GetUsesScratchPadScript() || OtherDynamicValueFunction->GetUsesScratchPadScript())
		{
			if ((BaseDynamicValueFunction->GetUsesScratchPadScript() && OtherDynamicValueFunction->GetUsesScratchPadScript() == false) ||
				(BaseDynamicValueFunction->GetUsesScratchPadScript() == false && OtherDynamicValueFunction->GetUsesScratchPadScript()))
			{
				return false;
			}

			if (BaseDynamicValueFunction->GetFunctionCallNode()->NodeGuid != OtherDynamicValueFunction->GetFunctionCallNode()->NodeGuid)
			{
				return false;
			}
		}
		else if (BaseDynamicValueFunction->GetFunctionCallNode()->FunctionScript != OtherDynamicValueFunction->GetFunctionCallNode()->FunctionScript)
		{
			return false;
		}

		FNiagaraScriptStackDiffResults FunctionDiffResults;
		DiffFunctionInputs(BaseDynamicValueFunction, OtherDynamicValueFunction, FunctionDiffResults);

		return
			FunctionDiffResults.RemovedBaseInputOverrides.Num() == 0 &&
			FunctionDiffResults.AddedOtherInputOverrides.Num() == 0 &&
			FunctionDiffResults.ModifiedOtherInputOverrides.Num() == 0;
	}

	// Static switch
	if (BaseFunctionInputAdapter->GetStaticSwitchValue().IsSet() && OtherFunctionInputAdapter->GetStaticSwitchValue().IsSet())
	{
		return BaseFunctionInputAdapter->GetStaticSwitchValue().GetValue() == OtherFunctionInputAdapter->GetStaticSwitchValue().GetValue();
	}

	return TOptional<bool>();
}

FNiagaraScriptMergeManager::FApplyDiffResults FNiagaraScriptMergeManager::AddModule(
	TSharedRef<FNiagaraEmitterMergeAdapter> BaseEmitterAdapter,
	TSharedRef<FNiagaraScratchPadMergeAdapter> ScratchPadAdapter,
	UNiagaraScript& OwningScript,
	UNiagaraNodeOutput& TargetOutputNode,
	TSharedRef<FNiagaraStackFunctionMergeAdapter> AddModule) const
{
	FApplyDiffResults Results;

	UNiagaraNodeFunctionCall* AddedModuleNode = nullptr;
	if (AddModule->GetFunctionCallNode()->IsA<UNiagaraNodeAssignment>())
	{
		UNiagaraNodeAssignment* AssignmentNode = CastChecked<UNiagaraNodeAssignment>(AddModule->GetFunctionCallNode());
		const TArray<FNiagaraVariable>& Targets = AssignmentNode->GetAssignmentTargets();
		const TArray<FString>& Defaults = AssignmentNode->GetAssignmentDefaults();
		AddedModuleNode = FNiagaraStackGraphUtilities::AddParameterModuleToStack(Targets, TargetOutputNode, AddModule->GetStackIndex(),Defaults);
		AddedModuleNode->NodeGuid = AddModule->GetFunctionCallNode()->NodeGuid;
		AddedModuleNode->RefreshFromExternalChanges();
		Results.bModifiedGraph = true;
	}
	else
	{
		UNiagaraScript* FunctionScript = nullptr;
		FGuid VersionGuid;
		if (AddModule->GetUsesScratchPadScript())
		{
			FunctionScript = ScratchPadAdapter->GetScratchPadScriptForFunctionId(AddModule->GetFunctionCallNode()->NodeGuid);
		}
		else
		{
			FunctionScript = AddModule->GetFunctionCallNode()->FunctionScript;
			VersionGuid = AddModule->GetFunctionCallNode()->SelectedScriptVersion;
		}

		AddedModuleNode = FNiagaraStackGraphUtilities::AddScriptModuleToStack(FunctionScript, TargetOutputNode, AddModule->GetStackIndex(), AddModule->GetFunctionCallNode()->GetFunctionName(), VersionGuid);
		AddedModuleNode->NodeGuid = AddModule->GetFunctionCallNode()->NodeGuid; // Synchronize the node Guid across runs so that the compile id's sync up.
		Results.bModifiedGraph = true;
	}

	if (AddedModuleNode != nullptr)
	{
		AddedModuleNode->NodeGuid = AddModule->GetFunctionCallNode()->NodeGuid; // Synchronize the node Guid across runs so that the compile id's sync up.

		AddedModuleNode->SetEnabledState(AddModule->GetFunctionCallNode()->GetDesiredEnabledState(), AddModule->GetFunctionCallNode()->HasUserSetTheEnabledState());
		for (TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter> InputOverride : AddModule->GetInputOverrides())
		{
			FApplyDiffResults AddInputResults = AddInputOverride(BaseEmitterAdapter, ScratchPadAdapter, OwningScript, *AddedModuleNode, InputOverride);
			Results.bSucceeded &= AddInputResults.bSucceeded;
			Results.bModifiedGraph |= AddInputResults.bModifiedGraph;
			Results.ErrorMessages.Append(AddInputResults.ErrorMessages);
		}
	}
	else
	{
		Results.bSucceeded = false;
		Results.ErrorMessages.Add(LOCTEXT("AddModuleFailed", "Failed to add module from diff."));
	}

	return Results;
}

FNiagaraScriptMergeManager::FApplyDiffResults FNiagaraScriptMergeManager::RemoveInputOverride(UNiagaraScript& OwningScript, TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter> OverrideToRemove) const
{
	FApplyDiffResults Results;
	Results.bSucceeded = true;
	Results.bModifiedGraph = false;

	// If there is a dynamic input we need to call remove recursively to make sure that all rapid iteration parameters are removed.
	if (OverrideToRemove->GetDynamicValueFunction().IsValid())
	{
		for (TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter> DynamicValueInputOverrideToRemove : OverrideToRemove->GetDynamicValueFunction()->GetInputOverrides())
		{
			FApplyDiffResults DynamicValueInputResults = RemoveInputOverride(OwningScript, DynamicValueInputOverrideToRemove);
			Results.bSucceeded &= DynamicValueInputResults.bSucceeded;
			Results.bModifiedGraph |= DynamicValueInputResults.bModifiedGraph;
			Results.ErrorMessages.Append(DynamicValueInputResults.ErrorMessages);
		}
	}

	if (OverrideToRemove->GetOverridePin() != nullptr && OverrideToRemove->GetOverrideNode() != nullptr)
	{
		FNiagaraStackGraphUtilities::RemoveNodesForStackFunctionInputOverridePin(*OverrideToRemove->GetOverridePin());
		OverrideToRemove->GetOverrideNode()->RemovePin(OverrideToRemove->GetOverridePin());
		Results.bModifiedGraph = true;
	}
	else if (OverrideToRemove->GetLocalValueRapidIterationParameter().IsSet())
	{
		OwningScript.Modify();
		OwningScript.RapidIterationParameters.RemoveParameter(OverrideToRemove->GetLocalValueRapidIterationParameter().GetValue());
	}
	else if (OverrideToRemove->GetStaticSwitchValue().IsSet())
	{
		// TODO: Static switches are always treated as overrides right now so removing them is a no-op.  This code should updated
		// so that removing a static switch override sets the value back to the module default.
	}
	else
	{
		Results.bSucceeded = false;
		Results.ErrorMessages.Add(LOCTEXT("RemoveInputOverrideFailed", "Failed to remove input override because it was invalid."));
	}

	return Results;
}

FNiagaraScriptMergeManager::FApplyDiffResults FNiagaraScriptMergeManager::AddInputOverride(
	TSharedRef<FNiagaraEmitterMergeAdapter> BaseEmitterAdapter,
	TSharedRef<FNiagaraScratchPadMergeAdapter> ScratchPadAdapter,
	UNiagaraScript& OwningScript,
	UNiagaraNodeFunctionCall& TargetFunctionCall,
	TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter> OverrideToAdd) const
{
	FApplyDiffResults Results;

	// If an assignment node, make sure that we have an assignment target for the input override.
	UNiagaraNodeAssignment* AssignmentNode = Cast<UNiagaraNodeAssignment>(&TargetFunctionCall);
	if (AssignmentNode)
	{
		FNiagaraParameterHandle FunctionInputHandle(FNiagaraConstants::ModuleNamespace, *OverrideToAdd->GetInputName());
		UNiagaraNodeAssignment* PreviousVersionAssignmentNode = Cast<UNiagaraNodeAssignment>(OverrideToAdd->GetOwningFunctionCall());
		bool bAnyAdded = false;
		for (int32 i = 0; i < PreviousVersionAssignmentNode->NumTargets(); i++)
		{
			const FNiagaraVariable& Var = PreviousVersionAssignmentNode->GetAssignmentTarget(i);

			int32 FoundVarIdx = AssignmentNode->FindAssignmentTarget(Var.GetName());
			if (FoundVarIdx == INDEX_NONE)
			{
				AssignmentNode->AddAssignmentTarget(Var, &PreviousVersionAssignmentNode->GetAssignmentDefaults()[i]);
				bAnyAdded = true;
			}
		}

		if (bAnyAdded)
		{
			AssignmentNode->RefreshFromExternalChanges();
		}
	}

	FNiagaraParameterHandle FunctionInputHandle(FNiagaraConstants::ModuleNamespace, *OverrideToAdd->GetInputName());
	FNiagaraParameterHandle AliasedFunctionInputHandle = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(FunctionInputHandle, &TargetFunctionCall);

	if (OverrideToAdd->GetOverridePin() != nullptr)
	{
		const UEdGraphSchema_Niagara* NiagaraSchema = GetDefault<UEdGraphSchema_Niagara>();
		FNiagaraTypeDefinition InputType = NiagaraSchema->PinToTypeDefinition(OverrideToAdd->GetOverridePin());

		UEdGraphPin& InputOverridePin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(TargetFunctionCall, AliasedFunctionInputHandle, InputType, OverrideToAdd->GetOverrideNode()->NodeGuid);
		if (InputOverridePin.LinkedTo.Num() != 0)
		{
			Results.bSucceeded = false;
			Results.ErrorMessages.Add(FText::Format(
				LOCTEXT("AddPinBasedInputOverrideFailedOverridePinStillLinkedFormat", "Failed to add input override because the target override pin was still linked to other nodes.  Target Script Usage: {0} Target Script Usage Id: {1} Target Node: {2} Target Input Handle: {3} Linked Node: {4} Linked Pin: {5}"),
				FNiagaraTypeDefinition::GetScriptUsageEnum()->GetDisplayNameTextByValue((int64)OwningScript.GetUsage()),
				FText::FromString(OwningScript.GetUsageId().ToString(EGuidFormats::DigitsWithHyphens)),
				FText::FromString(TargetFunctionCall.GetFunctionName()),
				FText::FromName(AliasedFunctionInputHandle.GetParameterHandleString()),
				InputOverridePin.LinkedTo[0] != nullptr && InputOverridePin.LinkedTo[0]->GetOwningNode() != nullptr
					? InputOverridePin.LinkedTo[0]->GetOwningNode()->GetNodeTitle(ENodeTitleType::ListView)
					: FText::FromString(TEXT("(null)")),
				InputOverridePin.LinkedTo[0] != nullptr
					? FText::FromName(InputOverridePin.LinkedTo[0]->PinName)
					: FText::FromString(TEXT("(null)"))));
		}
		else
		{
			if (OverrideToAdd->GetLocalValueString().IsSet())
			{
				InputOverridePin.DefaultValue = OverrideToAdd->GetLocalValueString().GetValue();
				Results.bSucceeded = true;
			}
			else if (OverrideToAdd->GetLinkedValueHandle().IsSet())
			{
				check(OverrideToAdd->GetOverrideNode() && OverrideToAdd->GetOverrideNode()->GetNiagaraGraph());
				UNiagaraScriptVariable* ScriptVar = OverrideToAdd->GetOverrideNode()->GetNiagaraGraph()->GetScriptVariable(FNiagaraVariable(InputType, OverrideToAdd->GetLinkedValueHandle().GetValue().GetParameterHandleString()));
				ENiagaraDefaultMode DesiredMode = ENiagaraDefaultMode::Value;
				if (ScriptVar)
					DesiredMode = ScriptVar->DefaultMode;
				if (GNiagaraForceFailIfPreviouslyNotSetOnMerge > 0)
					DesiredMode = ENiagaraDefaultMode::FailIfPreviouslyNotSet;
				FNiagaraStackGraphUtilities::SetLinkedValueHandleForFunctionInput(InputOverridePin, OverrideToAdd->GetLinkedValueHandle().GetValue(), DesiredMode, OverrideToAdd->GetOverrideNodeId());
				Results.bSucceeded = true;
			}
			else if (OverrideToAdd->GetDataValueInputName().IsSet() && OverrideToAdd->GetDataValueObject() != nullptr)
			{
				FName OverrideValueInputName = OverrideToAdd->GetDataValueInputName().GetValue();
				UNiagaraDataInterface* OverrideValueObject = OverrideToAdd->GetDataValueObject();
				UNiagaraDataInterface* NewOverrideValueObject;
				FNiagaraStackGraphUtilities::SetDataValueObjectForFunctionInput(InputOverridePin, OverrideToAdd->GetDataValueObject()->GetClass(), OverrideValueInputName.ToString(), NewOverrideValueObject, OverrideToAdd->GetOverrideNodeId());
				OverrideValueObject->CopyTo(NewOverrideValueObject);
				Results.bSucceeded = true;
			}
			else if (OverrideToAdd->GetDynamicValueFunction().IsValid())
			{
				UNiagaraNodeCustomHlsl* CustomHlslFunction = Cast<UNiagaraNodeCustomHlsl>(OverrideToAdd->GetDynamicValueFunction()->GetFunctionCallNode());
				if (CustomHlslFunction != nullptr)
				{
					UNiagaraNodeCustomHlsl* DynamicInputFunctionCall;
					FNiagaraStackGraphUtilities::SetCustomExpressionForFunctionInput(InputOverridePin, CustomHlslFunction->GetCustomHlsl(), DynamicInputFunctionCall, OverrideToAdd->GetOverrideNodeId());
					for (TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter> DynamicInputInputOverride : OverrideToAdd->GetDynamicValueFunction()->GetInputOverrides())
					{
						FApplyDiffResults AddResults = AddInputOverride(BaseEmitterAdapter, ScratchPadAdapter, OwningScript, *((UNiagaraNodeFunctionCall*)DynamicInputFunctionCall), DynamicInputInputOverride);
						Results.bSucceeded &= AddResults.bSucceeded;
						Results.bModifiedGraph |= AddResults.bModifiedGraph;
						Results.ErrorMessages.Append(AddResults.ErrorMessages);
					}
				}
				else if (OverrideToAdd->GetDynamicValueFunction()->GetFunctionCallNode())
				{
					UNiagaraScript* FunctionScript = nullptr;
					if (OverrideToAdd->GetDynamicValueFunction()->GetUsesScratchPadScript())
					{
						FunctionScript = ScratchPadAdapter->GetScratchPadScriptForFunctionId(OverrideToAdd->GetOverrideNodeId());
					}
					else
					{
						FunctionScript = OverrideToAdd->GetDynamicValueFunction()->GetFunctionCallNode()->FunctionScript;
					}

					UNiagaraNodeFunctionCall* DynamicInputFunctionCall;
					FNiagaraStackGraphUtilities::SetDynamicInputForFunctionInput(InputOverridePin, FunctionScript, 
						DynamicInputFunctionCall, OverrideToAdd->GetOverrideNodeId(), OverrideToAdd->GetDynamicValueFunction()->GetFunctionCallNode()->GetFunctionName());
					for (TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter> DynamicInputInputOverride : OverrideToAdd->GetDynamicValueFunction()->GetInputOverrides())
					{
						FApplyDiffResults AddResults = AddInputOverride(BaseEmitterAdapter, ScratchPadAdapter, OwningScript, *DynamicInputFunctionCall, DynamicInputInputOverride);
						Results.bSucceeded &= AddResults.bSucceeded;
						Results.bModifiedGraph |= AddResults.bModifiedGraph;
						Results.ErrorMessages.Append(AddResults.ErrorMessages);
					}
				}
			}
			else
			{
				Results.bSucceeded = false;
				Results.ErrorMessages.Add(LOCTEXT("AddPinBasedInputOverrideFailed", "Failed to add input override because it was invalid."));
			}
		}
		Results.bModifiedGraph = true;
	}
	else
	{
		if (OverrideToAdd->GetLocalValueRapidIterationParameter().IsSet())
		{
			FNiagaraVariable RapidIterationParameter = FNiagaraStackGraphUtilities::CreateRapidIterationParameter(
				BaseEmitterAdapter->GetEditableEmitter()->GetUniqueEmitterName(), OwningScript.GetUsage(), AliasedFunctionInputHandle.GetParameterHandleString(), OverrideToAdd->GetLocalValueRapidIterationParameter().GetValue().GetType());
			const uint8* SourceData = OverrideToAdd->GetOwningScript()->RapidIterationParameters.GetParameterData(OverrideToAdd->GetLocalValueRapidIterationParameter().GetValue());
			OwningScript.Modify();
			bool bAddParameterIfMissing = true;
			OwningScript.RapidIterationParameters.SetParameterData(SourceData, RapidIterationParameter, bAddParameterIfMissing);
			Results.bSucceeded = true;
		}
		else if (OverrideToAdd->GetStaticSwitchValue().IsSet())
		{
			TArray<UEdGraphPin*> StaticSwitchPins;
			TSet<UEdGraphPin*> StaticSwitchPinsHidden;
			FNiagaraStackGraphUtilities::GetStackFunctionStaticSwitchPins(TargetFunctionCall, StaticSwitchPins, StaticSwitchPinsHidden);
			UEdGraphPin** MatchingStaticSwitchPinPtr = StaticSwitchPins.FindByPredicate([&OverrideToAdd](UEdGraphPin* StaticSwitchPin) { return StaticSwitchPin->PinName == *OverrideToAdd->GetInputName(); });
			if (MatchingStaticSwitchPinPtr != nullptr)
			{
				UEdGraphPin* MatchingStaticSwitchPin = *MatchingStaticSwitchPinPtr;
				const UEdGraphSchema_Niagara* NiagaraSchema = GetDefault<UEdGraphSchema_Niagara>();
				FNiagaraTypeDefinition SwitchType = NiagaraSchema->PinToTypeDefinition(MatchingStaticSwitchPin);
				if (SwitchType == OverrideToAdd->GetType())
				{
					MatchingStaticSwitchPin->DefaultValue = OverrideToAdd->GetStaticSwitchValue().GetValue();
					Results.bSucceeded = true;
				}
				else
				{
					Results.bSucceeded = false;
					Results.ErrorMessages.Add(LOCTEXT("AddStaticInputOverrideFailedWrongType", "Failed to add static switch input override because a the type of the pin matched by name did not match."));
				}
			}
			else
			{
				Results.bSucceeded = false;
				Results.ErrorMessages.Add(LOCTEXT("AddStaticInputOverrideFailedNotFound", "Failed to add static switch input override because a matching pin could not be found."));
			}
		}
		else
		{
			Results.bSucceeded = false;
			Results.ErrorMessages.Add(LOCTEXT("AddParameterBasedInputOverrideFailed", "Failed to add input override because it was invalid."));
		}
		Results.bModifiedGraph = false;
	}
	return Results;
}

void FNiagaraScriptMergeManager::CopyPropertiesToBase(void* BaseDataAddress, const void* OtherDataAddress, TArray<FProperty*> PropertiesToCopy) const
{
	for (FProperty* PropertyToCopy : PropertiesToCopy)
	{
		PropertyToCopy->CopyCompleteValue(PropertyToCopy->ContainerPtrToValuePtr<void>(BaseDataAddress), PropertyToCopy->ContainerPtrToValuePtr<void>(OtherDataAddress));
	}
}

void FNiagaraScriptMergeManager::CopyInstanceScratchPadScripts(UNiagaraEmitter& MergedInstance, const UNiagaraEmitter& SourceInstance) const
{
	for (UNiagaraScript* SourceScratchPadScript : SourceInstance.ScratchPadScripts)
	{
		FName UniqueObjectName = FNiagaraEditorUtilities::GetUniqueObjectName<UNiagaraScript>(&MergedInstance, SourceScratchPadScript->GetName());
		UNiagaraScript* MergedInstanceScratchPadScript = CastChecked<UNiagaraScript>(StaticDuplicateObject(SourceScratchPadScript, &MergedInstance, UniqueObjectName));
		MergedInstance.ScratchPadScripts.Add(MergedInstanceScratchPadScript);
	}
}

FNiagaraScriptMergeManager::FApplyDiffResults FNiagaraScriptMergeManager::ApplyScriptStackDiff(
	TSharedRef<FNiagaraEmitterMergeAdapter> BaseEmitterAdapter,
	TSharedRef<FNiagaraScriptStackMergeAdapter> BaseScriptStackAdapter,
	TSharedRef<FNiagaraScratchPadMergeAdapter> ScratchPadAdapter,
	const FNiagaraScriptStackDiffResults& DiffResults,
	const bool bNoParentAtLastMerge) const
{
	FApplyDiffResults Results;

	if (DiffResults.IsEmpty())
	{
		Results.bSucceeded = true;
		Results.bModifiedGraph = false;
		return Results;
	}

	struct FAddInputOverrideActionData
	{
		UNiagaraNodeFunctionCall* TargetFunctionCall;
		TSharedPtr<FNiagaraStackFunctionInputOverrideMergeAdapter> OverrideToAdd;
	};

	// Collect the graph actions from the adapter and diff first.
	TArray<TSharedRef<FNiagaraStackFunctionMergeAdapter>> RemoveModules;
	TArray<TSharedRef<FNiagaraStackFunctionMergeAdapter>> AddModules;
	TArray<TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter>> RemoveInputOverrides;
	TArray<FAddInputOverrideActionData> AddInputOverrideActionDatas;
	TArray<TSharedRef<FNiagaraStackFunctionMergeAdapter>> EnableModules;
	TArray<TSharedRef<FNiagaraStackFunctionMergeAdapter>> DisableModules;

	for (TSharedRef<FNiagaraStackFunctionMergeAdapter> RemovedModule : DiffResults.RemovedBaseModules)
	{
		TSharedPtr<FNiagaraStackFunctionMergeAdapter> MatchingModuleAdapter = BaseScriptStackAdapter->GetModuleFunctionById(RemovedModule->GetFunctionCallNode()->NodeGuid);
		if (MatchingModuleAdapter.IsValid())
		{
			if (bNoParentAtLastMerge)
			{
				// If there is no last known parent we don't know if the module was removed in the child, or added in the parent, so
				// instead of removing the parent module we disable it in this case, since removing modules in child emitters isn't
				// supported through the UI.
				DisableModules.Add(MatchingModuleAdapter.ToSharedRef());
			}
			else
			{
				RemoveModules.Add(MatchingModuleAdapter.ToSharedRef());
			}
		}
	}

	AddModules.Append(DiffResults.AddedOtherModules);

	for (TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter> RemovedInputOverrideAdapter : DiffResults.RemovedBaseInputOverrides)
	{
		TSharedPtr<FNiagaraStackFunctionMergeAdapter> MatchingModuleAdapter = BaseScriptStackAdapter->GetModuleFunctionById(RemovedInputOverrideAdapter->GetOwningFunctionCall()->NodeGuid);
		if (MatchingModuleAdapter.IsValid())
		{
			TSharedPtr<FNiagaraStackFunctionInputOverrideMergeAdapter> MatchingInputOverrideAdapter = MatchingModuleAdapter->GetInputOverrideByInputName(RemovedInputOverrideAdapter->GetInputName());
			if (MatchingInputOverrideAdapter.IsValid())
			{
				RemoveInputOverrides.Add(MatchingInputOverrideAdapter.ToSharedRef());
			}
		}
	}

	for (TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter> AddedInputOverrideAdapter : DiffResults.AddedOtherInputOverrides)
	{
		TSharedPtr<FNiagaraStackFunctionMergeAdapter> MatchingModuleAdapter = BaseScriptStackAdapter->GetModuleFunctionById(AddedInputOverrideAdapter->GetOwningFunctionCall()->NodeGuid);
		if (MatchingModuleAdapter.IsValid())
		{
			TSharedPtr<FNiagaraStackFunctionInputOverrideMergeAdapter> MatchingInputOverrideAdapter = MatchingModuleAdapter->GetInputOverrideByInputName(AddedInputOverrideAdapter->GetInputName());
			if (MatchingInputOverrideAdapter.IsValid())
			{
				RemoveInputOverrides.AddUnique(MatchingInputOverrideAdapter.ToSharedRef());
			}

			FAddInputOverrideActionData AddInputOverrideActionData;
			AddInputOverrideActionData.TargetFunctionCall = MatchingModuleAdapter->GetFunctionCallNode();
			AddInputOverrideActionData.OverrideToAdd = AddedInputOverrideAdapter;
			AddInputOverrideActionDatas.Add(AddInputOverrideActionData);
		}
	}

	for (TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter> ModifiedInputOverrideAdapter : DiffResults.ModifiedOtherInputOverrides)
	{
		TSharedPtr<FNiagaraStackFunctionMergeAdapter> MatchingModuleAdapter = BaseScriptStackAdapter->GetModuleFunctionById(ModifiedInputOverrideAdapter->GetOwningFunctionCall()->NodeGuid);
		if (MatchingModuleAdapter.IsValid())
		{
			TSharedPtr<FNiagaraStackFunctionInputOverrideMergeAdapter> MatchingInputOverrideAdapter = MatchingModuleAdapter->GetInputOverrideByInputName(ModifiedInputOverrideAdapter->GetInputName());
			if (MatchingInputOverrideAdapter.IsValid())
			{
				RemoveInputOverrides.AddUnique(MatchingInputOverrideAdapter.ToSharedRef());
			}

			FAddInputOverrideActionData AddInputOverrideActionData;
			AddInputOverrideActionData.TargetFunctionCall = MatchingModuleAdapter->GetFunctionCallNode();
			AddInputOverrideActionData.OverrideToAdd = ModifiedInputOverrideAdapter;
			AddInputOverrideActionDatas.Add(AddInputOverrideActionData);
		}
	}

	for (TSharedRef<FNiagaraStackFunctionMergeAdapter> EnabledChangedModule : DiffResults.EnabledChangedOtherModules)
	{
		TSharedPtr<FNiagaraStackFunctionMergeAdapter> MatchingModuleAdapter = BaseScriptStackAdapter->GetModuleFunctionById(EnabledChangedModule->GetFunctionCallNode()->NodeGuid);
		if (MatchingModuleAdapter.IsValid())
		{
			if (EnabledChangedModule->GetFunctionCallNode()->IsNodeEnabled())
			{
				EnableModules.Add(MatchingModuleAdapter.ToSharedRef());
			}
			else
			{
				DisableModules.Add(MatchingModuleAdapter.ToSharedRef());
			}
		}
	}

	for(const FNiagaraStackFunctionMessageData& AddedMessage : DiffResults.AddedOtherMessages)
	{
		TSharedPtr<FNiagaraStackFunctionMergeAdapter> MatchingModuleAdapter = BaseScriptStackAdapter->GetModuleFunctionById(AddedMessage.Function->GetFunctionCallNode()->NodeGuid);

		if(MatchingModuleAdapter.IsValid())
		{
			MatchingModuleAdapter->GetFunctionCallNode()->AddCustomNote(AddedMessage.StackMessage);
		}
	}

	for(const FNiagaraStackFunctionMessageData& RemovedMessage : DiffResults.RemovedBaseMessagesInOther)
	{
		TSharedPtr<FNiagaraStackFunctionMergeAdapter> MatchingModuleAdapter = BaseScriptStackAdapter->GetModuleFunctionById(RemovedMessage.Function->GetFunctionCallNode()->NodeGuid);

		if(MatchingModuleAdapter.IsValid())
		{
			MatchingModuleAdapter->GetFunctionCallNode()->RemoveCustomNote(RemovedMessage.StackMessage.Guid);
		}
	}
	
	// Update the usage if different
	if (DiffResults.ChangedOtherUsage.IsSet())
	{
		BaseScriptStackAdapter->GetScript()->SetUsage(DiffResults.ChangedOtherUsage.GetValue());
		BaseScriptStackAdapter->GetOutputNode()->SetUsage(DiffResults.ChangedOtherUsage.GetValue());
	}

	// Apply the graph actions.
	for (TSharedRef<FNiagaraStackFunctionMergeAdapter> RemoveModule : RemoveModules)
	{
		bool bRemoveResults = FNiagaraStackGraphUtilities::RemoveModuleFromStack(*BaseScriptStackAdapter->GetScript(), *RemoveModule->GetFunctionCallNode());
		if (bRemoveResults == false)
		{
			Results.bSucceeded = false;
			Results.ErrorMessages.Add(LOCTEXT("RemoveModuleFailedMessage", "Failed to remove module while applying diff"));
		}
		else
		{
			Results.bModifiedGraph = true;
		}
	}

	for (TSharedRef<FNiagaraStackFunctionMergeAdapter> AddModuleAdapter : AddModules)
	{
		FApplyDiffResults AddModuleResults = AddModule(BaseEmitterAdapter, ScratchPadAdapter, *BaseScriptStackAdapter->GetScript(), *BaseScriptStackAdapter->GetOutputNode(), AddModuleAdapter);
		Results.bSucceeded &= AddModuleResults.bSucceeded;
		Results.bModifiedGraph |= AddModuleResults.bModifiedGraph;
		Results.ErrorMessages.Append(AddModuleResults.ErrorMessages);
	}

	for (TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter> RemoveInputOverrideItem : RemoveInputOverrides)
	{
		FApplyDiffResults RemoveInputOverrideResults = RemoveInputOverride(*BaseScriptStackAdapter->GetScript(), RemoveInputOverrideItem);
		Results.bSucceeded &= RemoveInputOverrideResults.bSucceeded;
		Results.bModifiedGraph |= RemoveInputOverrideResults.bModifiedGraph;
		Results.ErrorMessages.Append(RemoveInputOverrideResults.ErrorMessages);
	}

	for (const FAddInputOverrideActionData& AddInputOverrideActionData : AddInputOverrideActionDatas)
	{
		FApplyDiffResults AddInputOverrideResults = AddInputOverride(BaseEmitterAdapter, ScratchPadAdapter, *BaseScriptStackAdapter->GetScript(),
			*AddInputOverrideActionData.TargetFunctionCall, AddInputOverrideActionData.OverrideToAdd.ToSharedRef());
		Results.bSucceeded &= AddInputOverrideResults.bSucceeded;
		Results.bModifiedGraph |= AddInputOverrideResults.bModifiedGraph;
		Results.ErrorMessages.Append(AddInputOverrideResults.ErrorMessages);
	}

	// Apply enabled state last so that it applies to function calls added  from input overrides;
	for (TSharedRef<FNiagaraStackFunctionMergeAdapter> EnableModule : EnableModules)
	{
		FNiagaraStackGraphUtilities::SetModuleIsEnabled(*EnableModule->GetFunctionCallNode(), true);
	}
	for (TSharedRef<FNiagaraStackFunctionMergeAdapter> DisableModule : DisableModules)
	{
		FNiagaraStackGraphUtilities::SetModuleIsEnabled(*DisableModule->GetFunctionCallNode(), false);
	}

	return Results;
}

FNiagaraScriptMergeManager::FApplyDiffResults FNiagaraScriptMergeManager::ApplyEventHandlerDiff(
	TSharedRef<FNiagaraEmitterMergeAdapter> BaseEmitterAdapter,
	TSharedRef<FNiagaraScratchPadMergeAdapter> ScratchPadAdapter,
	const FNiagaraEmitterDiffResults& DiffResults,
	const bool bNoParentAtLastMerge) const
{
	FApplyDiffResults Results;
	if (DiffResults.RemovedBaseEventHandlers.Num() > 0)
	{
		// If this becomes supported, it needs to handle the bNoParentAtLastMerge case
		Results.bSucceeded = false;
		Results.bModifiedGraph = false;
		Results.ErrorMessages.Add(LOCTEXT("RemovedEventHandlersUnsupported", "Apply diff failed, removed event handlers are currently unsupported."));
		return Results;
	}

	// Apply the modifications first since adding new event handlers may invalidate the adapter.
	for (const FNiagaraModifiedEventHandlerDiffResults& ModifiedEventHandler : DiffResults.ModifiedEventHandlers)
	{
		if (ModifiedEventHandler.OtherAdapter->GetEventScriptProperties() == nullptr)
		{
			Results.bSucceeded = false;
			Results.ErrorMessages.Add(FText::Format(
				LOCTEXT("MissingModifiedEventPropertiesFormat", "Apply diff failed.  The modified event handler with id: {0} was missing it's event properties."),
				FText::FromString(ModifiedEventHandler.OtherAdapter->GetUsageId().ToString(EGuidFormats::DigitsWithHyphens))));
		}
		else if (ModifiedEventHandler.OtherAdapter->GetOutputNode() == nullptr)
		{
			Results.bSucceeded = false;
			Results.ErrorMessages.Add(FText::Format(
				LOCTEXT("MissingModifiedEventOutputNodeFormat", "Apply diff failed.  The modified event handler with id: {0} was missing it's output node."),
				FText::FromString(ModifiedEventHandler.OtherAdapter->GetUsageId().ToString(EGuidFormats::DigitsWithHyphens))));
		}
		else
		{
			TSharedPtr<FNiagaraEventHandlerMergeAdapter> MatchingBaseEventHandlerAdapter = BaseEmitterAdapter->GetEventHandler(ModifiedEventHandler.OtherAdapter->GetUsageId());
			if (MatchingBaseEventHandlerAdapter.IsValid())
			{
				if (ModifiedEventHandler.ChangedProperties.Num() > 0)
				{
					CopyPropertiesToBase(MatchingBaseEventHandlerAdapter->GetEditableEventScriptProperties(), ModifiedEventHandler.OtherAdapter->GetEditableEventScriptProperties(), ModifiedEventHandler.ChangedProperties);
				}
				if (ModifiedEventHandler.ScriptDiffResults.IsEmpty() == false)
				{
					FApplyDiffResults ApplyEventHandlerStackDiffResults = ApplyScriptStackDiff(
						BaseEmitterAdapter, MatchingBaseEventHandlerAdapter->GetEventStack().ToSharedRef(),	ScratchPadAdapter,
						ModifiedEventHandler.ScriptDiffResults, bNoParentAtLastMerge);
					Results.bSucceeded &= ApplyEventHandlerStackDiffResults.bSucceeded;
					Results.bModifiedGraph |= ApplyEventHandlerStackDiffResults.bModifiedGraph;
					Results.ErrorMessages.Append(ApplyEventHandlerStackDiffResults.ErrorMessages);
				}
			}
		}
	}

	UNiagaraScriptSource* EmitterSource = CastChecked<UNiagaraScriptSource>(BaseEmitterAdapter->GetEditableEmitter()->GraphSource);
	UNiagaraGraph* EmitterGraph = EmitterSource->NodeGraph;
	for (TSharedRef<FNiagaraEventHandlerMergeAdapter> AddedEventHandler : DiffResults.AddedOtherEventHandlers)
	{
		if (AddedEventHandler->GetEventScriptProperties() == nullptr)
		{
			Results.bSucceeded = false;
			Results.ErrorMessages.Add(FText::Format(
				LOCTEXT("MissingAddedEventPropertiesFormat", "Apply diff failed.  The added event handler with id: {0} was missing it's event properties."),
				FText::FromString(AddedEventHandler->GetUsageId().ToString(EGuidFormats::DigitsWithHyphens))));
		}
		else if (AddedEventHandler->GetOutputNode() == nullptr)
		{
			Results.bSucceeded = false;
			Results.ErrorMessages.Add(FText::Format(
				LOCTEXT("MissingAddedEventOutputNodeFormat", "Apply diff failed.  The added event handler with id: {0} was missing it's output node."),
				FText::FromString(AddedEventHandler->GetUsageId().ToString(EGuidFormats::DigitsWithHyphens))));
		}
		else
		{
			UNiagaraEmitter* BaseEmitter = BaseEmitterAdapter->GetEditableEmitter();
			FNiagaraEventScriptProperties AddedEventScriptProperties = *AddedEventHandler->GetEventScriptProperties();
			AddedEventScriptProperties.Script = NewObject<UNiagaraScript>(BaseEmitter, MakeUniqueObjectName(BaseEmitter, UNiagaraScript::StaticClass(), "EventScript"), EObjectFlags::RF_Transactional);
			AddedEventScriptProperties.Script->SetUsage(ENiagaraScriptUsage::ParticleEventScript);
			AddedEventScriptProperties.Script->SetUsageId(AddedEventHandler->GetUsageId());
			AddedEventScriptProperties.Script->SetLatestSource(EmitterSource);
			BaseEmitter->AddEventHandler(AddedEventScriptProperties);

			FGuid PreferredOutputNodeGuid = AddedEventHandler->GetOutputNode()->NodeGuid;
			FGuid PreferredInputNodeGuid = AddedEventHandler->GetInputNode()->NodeGuid;
			UNiagaraNodeOutput* EventOutputNode = FNiagaraStackGraphUtilities::ResetGraphForOutput(*EmitterGraph, ENiagaraScriptUsage::ParticleEventScript, AddedEventScriptProperties.Script->GetUsageId(), PreferredOutputNodeGuid, PreferredInputNodeGuid);
			for (TSharedRef<FNiagaraStackFunctionMergeAdapter> ModuleAdapter : AddedEventHandler->GetEventStack()->GetModuleFunctions())
			{
				FApplyDiffResults AddModuleResults = AddModule(BaseEmitterAdapter, ScratchPadAdapter, *AddedEventScriptProperties.Script, *EventOutputNode, ModuleAdapter);
				Results.bSucceeded &= AddModuleResults.bSucceeded;
				Results.ErrorMessages.Append(AddModuleResults.ErrorMessages);
			}

			// Force the base compile id of the new event handler to match the added instance event handler.
			UNiagaraScriptSource* AddedEventScriptSourceFromDiff = Cast<UNiagaraScriptSource>(AddedEventHandler->GetEventScriptProperties()->Script->GetLatestSource());
			UNiagaraGraph* AddedEventScriptGraphFromDiff = AddedEventScriptSourceFromDiff->NodeGraph;
			FGuid ScriptBaseIdFromDiff = AddedEventScriptGraphFromDiff->GetBaseId(ENiagaraScriptUsage::ParticleEventScript, AddedEventHandler->GetUsageId());
			UNiagaraScriptSource* AddedEventScriptSource = Cast<UNiagaraScriptSource>(AddedEventScriptProperties.Script->GetLatestSource());
			UNiagaraGraph* AddedEventScriptGraph = AddedEventScriptSource->NodeGraph;
			AddedEventScriptGraph->ForceBaseId(ENiagaraScriptUsage::ParticleEventScript, AddedEventHandler->GetUsageId(), ScriptBaseIdFromDiff);

			Results.bModifiedGraph = true;
		}
	}
	return Results;
}

FNiagaraScriptMergeManager::FApplyDiffResults FNiagaraScriptMergeManager::ApplySimulationStageDiff(
	TSharedRef<FNiagaraEmitterMergeAdapter> BaseEmitterAdapter,
	TSharedRef<FNiagaraScratchPadMergeAdapter> ScratchPadAdapter,
	const FNiagaraEmitterDiffResults& DiffResults,
	const bool bNoParentAtLastMerge) const
{
	FApplyDiffResults Results;
	if (DiffResults.RemovedBaseSimulationStages.Num() > 0)
	{
		Results.bSucceeded = false;
		Results.bModifiedGraph = false;
		// If this becomes supported, it needs to handle the bNoParentAtLastMerge case
		Results.ErrorMessages.Add(LOCTEXT("RemovedSimulationStagesUnsupported", "Apply diff failed, removed shader stages are currently unsupported."));
		return Results;
	}

	for (const FNiagaraModifiedSimulationStageDiffResults& ModifiedSimulationStage : DiffResults.ModifiedSimulationStages)
	{
		if (ModifiedSimulationStage.OtherAdapter->GetSimulationStage() == nullptr)
		{
			Results.bSucceeded = false;
			Results.ErrorMessages.Add(FText::Format(
				LOCTEXT("MissingModifiedSimulationStageObjectFormat", "Apply diff failed.  The modified shader stage with id: {0} was missing it's shader stage object."),
				FText::FromString(ModifiedSimulationStage.OtherAdapter->GetUsageId().ToString(EGuidFormats::DigitsWithHyphens))));
		}
		else if (ModifiedSimulationStage.OtherAdapter->GetOutputNode() == nullptr)
		{
			Results.bSucceeded = false;
			Results.ErrorMessages.Add(FText::Format(
				LOCTEXT("MissingModifiedSimulationStageOutputNodeFormat", "Apply diff failed.  The modified shader stage with id: {0} was missing it's output node."),
				FText::FromString(ModifiedSimulationStage.OtherAdapter->GetUsageId().ToString(EGuidFormats::DigitsWithHyphens))));
		}
		else
		{
			TSharedPtr<FNiagaraSimulationStageMergeAdapter> MatchingBaseSimulationStageAdapter = BaseEmitterAdapter->GetSimulationStage(ModifiedSimulationStage.OtherAdapter->GetUsageId());
			if (MatchingBaseSimulationStageAdapter.IsValid())
			{
				if (ModifiedSimulationStage.ChangedProperties.Num() > 0)
				{
					CopyPropertiesToBase(MatchingBaseSimulationStageAdapter->GetEditableSimulationStage(), ModifiedSimulationStage.OtherAdapter->GetEditableSimulationStage(), ModifiedSimulationStage.ChangedProperties);
				}
				if (ModifiedSimulationStage.ScriptDiffResults.IsEmpty() == false)
				{
					FApplyDiffResults ApplySimulationStageStackDiffResults = ApplyScriptStackDiff(
						BaseEmitterAdapter,	MatchingBaseSimulationStageAdapter->GetSimulationStageStack().ToSharedRef(), ScratchPadAdapter,
						ModifiedSimulationStage.ScriptDiffResults, bNoParentAtLastMerge);
					Results.bSucceeded &= ApplySimulationStageStackDiffResults.bSucceeded;
					Results.bModifiedGraph |= ApplySimulationStageStackDiffResults.bModifiedGraph;
					Results.ErrorMessages.Append(ApplySimulationStageStackDiffResults.ErrorMessages);
				}
			}
		}
	}

	UNiagaraScriptSource* EmitterSource = CastChecked<UNiagaraScriptSource>(BaseEmitterAdapter->GetEditableEmitter()->GraphSource);
	UNiagaraGraph* EmitterGraph = EmitterSource->NodeGraph;
	for (TSharedRef<FNiagaraSimulationStageMergeAdapter> AddedOtherSimulationStage : DiffResults.AddedOtherSimulationStages)
	{
		if (AddedOtherSimulationStage->GetSimulationStage() == nullptr)
		{
			Results.bSucceeded = false;
			Results.ErrorMessages.Add(FText::Format(
				LOCTEXT("MissingAddedSimulationStageObjectFormat", "Apply diff failed.  The added shader stage with id: {0} was missing it's shader stage object."),
				FText::FromString(AddedOtherSimulationStage->GetUsageId().ToString(EGuidFormats::DigitsWithHyphens))));
		}
		else if (AddedOtherSimulationStage->GetOutputNode() == nullptr)
		{
			Results.bSucceeded = false;
			Results.ErrorMessages.Add(FText::Format(
				LOCTEXT("MissingAddedSimulationStageOutputNodeFormat", "Apply diff failed.  The added shader stage with id: {0} was missing it's output node."),
				FText::FromString(AddedOtherSimulationStage->GetUsageId().ToString(EGuidFormats::DigitsWithHyphens))));
		}
		else
		{
			UNiagaraEmitter* BaseEmitter = BaseEmitterAdapter->GetEditableEmitter();
			UNiagaraSimulationStageBase* AddedSimulationStage = CastChecked<UNiagaraSimulationStageBase>(StaticDuplicateObject(AddedOtherSimulationStage->GetSimulationStage(), BaseEmitter));
			AddedSimulationStage->Script = NewObject<UNiagaraScript>(AddedSimulationStage, MakeUniqueObjectName(AddedSimulationStage, UNiagaraScript::StaticClass(), "SimulationStage"), EObjectFlags::RF_Transactional);
			AddedSimulationStage->Script->SetUsage(ENiagaraScriptUsage::ParticleSimulationStageScript);
			AddedSimulationStage->Script->SetUsageId(AddedOtherSimulationStage->GetUsageId());
			AddedSimulationStage->Script->SetLatestSource(EmitterSource);
			BaseEmitter->AddSimulationStage(AddedSimulationStage);

			int32 TargetIndex = FMath::Min(AddedOtherSimulationStage->GetSimulationStageIndex(), BaseEmitter->GetSimulationStages().Num() - 1);
			BaseEmitter->MoveSimulationStageToIndex(AddedSimulationStage, TargetIndex);

			FGuid PreferredOutputNodeGuid = AddedOtherSimulationStage->GetOutputNode()->NodeGuid;
			FGuid PreferredInputNodeGuid = AddedOtherSimulationStage->GetInputNode()->NodeGuid;
			UNiagaraNodeOutput* SimulationStageOutputNode = FNiagaraStackGraphUtilities::ResetGraphForOutput(*EmitterGraph, ENiagaraScriptUsage::ParticleSimulationStageScript, AddedSimulationStage->Script->GetUsageId(), PreferredOutputNodeGuid, PreferredInputNodeGuid);
			for (TSharedRef<FNiagaraStackFunctionMergeAdapter> ModuleAdapter : AddedOtherSimulationStage->GetSimulationStageStack()->GetModuleFunctions())
			{
				FApplyDiffResults AddModuleResults = AddModule(BaseEmitterAdapter, ScratchPadAdapter, *AddedSimulationStage->Script, *SimulationStageOutputNode, ModuleAdapter);
				Results.bSucceeded &= AddModuleResults.bSucceeded;
				Results.ErrorMessages.Append(AddModuleResults.ErrorMessages);
			}

			// Force the base compile id of the new shader stage to match the added instance shader stage.
			UNiagaraScriptSource* AddedSimulationStageSourceFromDiff = Cast<UNiagaraScriptSource>(AddedOtherSimulationStage->GetSimulationStage()->Script->GetLatestSource());
			UNiagaraGraph* AddedSimulationStageGraphFromDiff = AddedSimulationStageSourceFromDiff->NodeGraph;
			FGuid ScriptBaseIdFromDiff = AddedSimulationStageGraphFromDiff->GetBaseId(ENiagaraScriptUsage::ParticleSimulationStageScript, AddedOtherSimulationStage->GetUsageId());
			UNiagaraScriptSource* AddedSimulationStageSource = Cast<UNiagaraScriptSource>(AddedSimulationStage->Script->GetLatestSource());
			UNiagaraGraph* AddedSimulationStageGraph = AddedSimulationStageSource->NodeGraph;
			AddedSimulationStageGraph->ForceBaseId(ENiagaraScriptUsage::ParticleSimulationStageScript, AddedOtherSimulationStage->GetUsageId(), ScriptBaseIdFromDiff);

			Results.bModifiedGraph = true;
		}
	}
	return Results;
}

FNiagaraScriptMergeManager::FApplyDiffResults FNiagaraScriptMergeManager::ApplyRendererDiff(UNiagaraEmitter& BaseEmitter, const FNiagaraEmitterDiffResults& DiffResults, const bool bNoParentAtLastMerge) const
{
	TArray<UNiagaraRendererProperties*> RenderersToRemove;
	TArray<UNiagaraRendererProperties*> RenderersToAdd;
	TArray<UNiagaraRendererProperties*> RenderersToDisable;

	for (TSharedRef<FNiagaraRendererMergeAdapter> RemovedRenderer : DiffResults.RemovedBaseRenderers)
	{
		auto FindRendererByMergeId = [=](UNiagaraRendererProperties* Renderer) { return Renderer->GetMergeId() == RemovedRenderer->GetRenderer()->GetMergeId(); };
		UNiagaraRendererProperties* const* MatchingRendererPtr = BaseEmitter.GetRenderers().FindByPredicate(FindRendererByMergeId);
		if (MatchingRendererPtr != nullptr)
		{
			if (bNoParentAtLastMerge)
			{
				// If there is no last known parent we don't know if the renderer was removed in the child, or added in the parent, so
				// instead of removing the parent renderer we disable it in this case, since removing renderers in child emitters isn't
				// supported through the UI, and instead the user is expected to disable it.
				RenderersToDisable.Add(*MatchingRendererPtr);
			}
			else
			{
				RenderersToRemove.Add(*MatchingRendererPtr);
			}
		}
	}

	for (TSharedRef<FNiagaraRendererMergeAdapter> AddedRenderer : DiffResults.AddedOtherRenderers)
	{
		RenderersToAdd.Add(Cast<UNiagaraRendererProperties>(StaticDuplicateObject(AddedRenderer->GetRenderer(), &BaseEmitter)));
	}

	for (TSharedRef<FNiagaraRendererMergeAdapter> ModifiedRenderer : DiffResults.ModifiedOtherRenderers)
	{
		auto FindRendererByMergeId = [=](UNiagaraRendererProperties* Renderer) { return Renderer->GetMergeId() == ModifiedRenderer->GetRenderer()->GetMergeId(); };
		UNiagaraRendererProperties*const* MatchingRendererPtr = BaseEmitter.GetRenderers().FindByPredicate(FindRendererByMergeId);
		if (MatchingRendererPtr != nullptr)
		{
			RenderersToRemove.Add(*MatchingRendererPtr);
			RenderersToAdd.Add(Cast<UNiagaraRendererProperties>(StaticDuplicateObject(ModifiedRenderer->GetRenderer(), &BaseEmitter)));
		}
	}

	for (UNiagaraRendererProperties* RendererToRemove : RenderersToRemove)
	{
		BaseEmitter.RemoveRenderer(RendererToRemove);
	}

	for (UNiagaraRendererProperties* RendererToAdd : RenderersToAdd)
	{
		BaseEmitter.AddRenderer(RendererToAdd);
	}

	for (UNiagaraRendererProperties* RendererToDisable : RenderersToDisable)
	{
		RendererToDisable->bIsEnabled = false;
	}

	FApplyDiffResults Results;
	Results.bSucceeded = true;
	Results.bModifiedGraph = false;
	return Results;
}

FNiagaraScriptMergeManager::FApplyDiffResults FNiagaraScriptMergeManager::ApplyStackEntryDisplayNameDiffs(UNiagaraEmitter& Emitter, const FNiagaraEmitterDiffResults& DiffResults) const
{
	if (DiffResults.ModifiedStackEntryDisplayNames.Num() > 0)
	{
		UNiagaraEmitterEditorData* EditorData = Cast<UNiagaraEmitterEditorData>(Emitter.GetEditorData());
		if (EditorData == nullptr)
		{
			EditorData = NewObject<UNiagaraEmitterEditorData>(&Emitter, NAME_None, RF_Transactional);
			Emitter.SetEditorData(EditorData);
		}

		for (auto& Pair : DiffResults.ModifiedStackEntryDisplayNames)
		{
			EditorData->GetStackEditorData().SetStackEntryDisplayName(Pair.Key, Pair.Value);
		}
	}

	FApplyDiffResults Results;
	Results.bSucceeded = true;
	Results.bModifiedGraph = false;
	return Results;
}

FNiagaraScriptMergeManager::FCachedMergeAdapter* FNiagaraScriptMergeManager::FindOrAddMergeAdapterCacheForEmitter(const UNiagaraEmitter& Emitter)
{
	FCachedMergeAdapter* CachedMergeAdapter = CachedMergeAdapters.Find(FObjectKey(&Emitter));
	if (CachedMergeAdapter == nullptr)
	{
		CachedMergeAdapter = &CachedMergeAdapters.Add(FObjectKey(&Emitter));
	}
	else
	{
		if (CachedMergeAdapter->ChangeId != Emitter.GetChangeId())
		{
			CachedMergeAdapter->ChangeId = Emitter.GetChangeId();
			CachedMergeAdapter->EmitterMergeAdapter.Reset();
			CachedMergeAdapter->ScratchPadMergeAdapter.Reset();
		}
	}
	return CachedMergeAdapter;
}

TSharedRef<FNiagaraEmitterMergeAdapter> FNiagaraScriptMergeManager::GetEmitterMergeAdapterUsingCache(const UNiagaraEmitter& Emitter)
{
	FCachedMergeAdapter* CachedMergeAdapter = FindOrAddMergeAdapterCacheForEmitter(Emitter);

	if (CachedMergeAdapter->EmitterMergeAdapter.IsValid() == false ||
		CachedMergeAdapter->EmitterMergeAdapter->GetEditableEmitter() != nullptr)
	{
		CachedMergeAdapter->EmitterMergeAdapter = MakeShared<FNiagaraEmitterMergeAdapter>(Emitter);
	}

	return CachedMergeAdapter->EmitterMergeAdapter.ToSharedRef();
}

TSharedRef<FNiagaraEmitterMergeAdapter> FNiagaraScriptMergeManager::GetEmitterMergeAdapterUsingCache(UNiagaraEmitter& Emitter)
{
	FCachedMergeAdapter* CachedMergeAdapter = FindOrAddMergeAdapterCacheForEmitter(Emitter);

	if (CachedMergeAdapter->EmitterMergeAdapter.IsValid() == false ||
		CachedMergeAdapter->EmitterMergeAdapter->GetEditableEmitter() == nullptr)
	{
		CachedMergeAdapter->EmitterMergeAdapter = MakeShared<FNiagaraEmitterMergeAdapter>(Emitter);
	}

	return CachedMergeAdapter->EmitterMergeAdapter.ToSharedRef();
}

TSharedRef<FNiagaraScratchPadMergeAdapter> FNiagaraScriptMergeManager::GetScratchPadMergeAdapterUsingCache(UNiagaraEmitter& Emitter)
{
	FCachedMergeAdapter* CachedMergeAdapter = FindOrAddMergeAdapterCacheForEmitter(Emitter);

	if (CachedMergeAdapter->ScratchPadMergeAdapter.IsValid() == false)
	{
		CachedMergeAdapter->ScratchPadMergeAdapter = MakeShared<FNiagaraScratchPadMergeAdapter>(&Emitter, nullptr, Emitter.GetParent());
	}

	return CachedMergeAdapter->ScratchPadMergeAdapter.ToSharedRef();
}


#undef LOCTEXT_NAMESPACE
