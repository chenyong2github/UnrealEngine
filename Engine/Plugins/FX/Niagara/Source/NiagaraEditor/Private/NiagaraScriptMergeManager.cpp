// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraScriptMergeManager.h"
#include "NiagaraEmitter.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraShaderStageBase.h"
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
#include "NiagaraEditorModule.h"
#include "NiagaraEmitterEditorData.h"

#include "UObject/PropertyPortFlags.h"

#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "NiagaraScriptMergeManager"

DECLARE_CYCLE_STAT(TEXT("Niagara - ScriptMergeManager - DiffEmitters"), STAT_NiagaraEditor_ScriptMergeManager_DiffEmitters, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("Niagara - ScriptMergeManager - MergeEmitter"), STAT_NiagaraEditor_ScriptMergeManager_MergeEmitter, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("Niagara - ScriptMergeManager - IsModuleInputDifferentFromBase"), STAT_NiagaraEditor_ScriptMergeManager_IsModuleInputDifferentFromBase, STATGROUP_NiagaraEditor);

FNiagaraStackFunctionInputOverrideMergeAdapter::FNiagaraStackFunctionInputOverrideMergeAdapter(
	FString InUniqueEmitterName,
	UNiagaraScript& InOwningScript,
	UNiagaraNodeFunctionCall& InOwningFunctionCallNode,
	UEdGraphPin& InOverridePin
)
	: UniqueEmitterName(InUniqueEmitterName)
	, OwningScript(&InOwningScript)
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
			DataValueObject = DataInputNode->GetDataInterface();
		}
		else if (OverridePin->LinkedTo[0]->GetOwningNode()->IsA<UNiagaraNodeFunctionCall>())
		{
			DynamicValueFunction = MakeShared<FNiagaraStackFunctionMergeAdapter>(UniqueEmitterName, *OwningScript.Get(), *CastChecked<UNiagaraNodeFunctionCall>(OverridePin->LinkedTo[0]->GetOwningNode()), INDEX_NONE);
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
	FString InUniqueEmitterName,
	UNiagaraScript& InOwningScript,
	UNiagaraNodeFunctionCall& InOwningFunctionCallNode,
	FString InInputName,
	FNiagaraVariable InRapidIterationParameter
)
	: UniqueEmitterName(InUniqueEmitterName)
	, OwningScript(&InOwningScript)
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

FNiagaraStackFunctionMergeAdapter::FNiagaraStackFunctionMergeAdapter(FString InEmitterUniqueName, UNiagaraScript& InOwningScript, UNiagaraNodeFunctionCall& InFunctionCallNode, int32 InStackIndex)
{
	UniqueEmitterName = InEmitterUniqueName;
	OwningScript = &InOwningScript;
	FunctionCallNode = &InFunctionCallNode;
	StackIndex = InStackIndex;

	TSet<FString> AliasedInputsAdded;
	UNiagaraNodeParameterMapSet* OverrideNode = FNiagaraStackGraphUtilities::GetStackFunctionOverrideNode(*FunctionCallNode);
	if (OverrideNode != nullptr)
	{
		TArray<UEdGraphPin*> OverridePins;
		OverrideNode->GetInputPins(OverridePins);
		for (UEdGraphPin* OverridePin : OverridePins)
		{
			if (OverridePin->PinType.PinCategory != UEdGraphSchema_Niagara::PinCategoryMisc &&
				OverridePin->PinType.PinSubCategoryObject != FNiagaraTypeDefinition::GetParameterMapStruct())
			{
				FNiagaraParameterHandle InputHandle(OverridePin->PinName);
				if (InputHandle.GetNamespace().ToString() == FunctionCallNode->GetFunctionName())
				{
					InputOverrides.Add(MakeShared<FNiagaraStackFunctionInputOverrideMergeAdapter>(UniqueEmitterName, *OwningScript.Get(), *FunctionCallNode.Get(), *OverridePin));
					AliasedInputsAdded.Add(OverridePin->PinName.ToString());
				}
			}
		}
	}

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

			if (AliasedInputsAdded.Contains(AliasedInputHandle.GetParameterHandleString().ToString()) == false)
			{
				InputOverrides.Add(MakeShared<FNiagaraStackFunctionInputOverrideMergeAdapter>(UniqueEmitterName, *OwningScript.Get(), *FunctionCallNode.Get(), AliasedInputHandle.GetName().ToString(), RapidIterationParameter));
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

FNiagaraScriptStackMergeAdapter::FNiagaraScriptStackMergeAdapter(UNiagaraNodeOutput& InOutputNode, UNiagaraScript& InScript, FString InUniqueEmitterName)
{
	OutputNode = &InOutputNode;
	InputNode.Reset();
	Script = &InScript;
	UniqueEmitterName = InUniqueEmitterName;

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
				ModuleFunctions.Add(MakeShared<FNiagaraStackFunctionMergeAdapter>(UniqueEmitterName, *Script.Get(), *ModuleFunctionCallNode, StackIndex));
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
		EventStack = MakeShared<FNiagaraScriptStackMergeAdapter>(*OutputNode.Get(), *EventScriptProperties->Script, Emitter->GetUniqueEmitterName());
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

FNiagaraShaderStageMergeAdapter::FNiagaraShaderStageMergeAdapter(const UNiagaraEmitter& InEmitter, const UNiagaraShaderStageBase* InShaderStage, UNiagaraNodeOutput* InOutputNode)
{
	Initialize(InEmitter, InShaderStage, nullptr, InOutputNode);
}

FNiagaraShaderStageMergeAdapter::FNiagaraShaderStageMergeAdapter(const UNiagaraEmitter& InEmitter, UNiagaraShaderStageBase* InShaderStage, UNiagaraNodeOutput* InOutputNode)
{
	Initialize(InEmitter, InShaderStage, InShaderStage, InOutputNode);
}

FNiagaraShaderStageMergeAdapter::FNiagaraShaderStageMergeAdapter(const UNiagaraEmitter& InEmitter, UNiagaraNodeOutput* InOutputNode)
{
	Initialize(InEmitter, nullptr, nullptr, InOutputNode);
}

void FNiagaraShaderStageMergeAdapter::Initialize(const UNiagaraEmitter& InEmitter, const UNiagaraShaderStageBase* InShaderStage, UNiagaraShaderStageBase* InEditableShaderStage, UNiagaraNodeOutput* InOutputNode)
{
	Emitter = MakeWeakObjectPtr(const_cast<UNiagaraEmitter*>(&InEmitter));

	ShaderStage = InShaderStage;
	EditableShaderStage = InEditableShaderStage;

	OutputNode = InOutputNode;

	if (ShaderStage != nullptr && OutputNode != nullptr)
	{
		ShaderStageStack = MakeShared<FNiagaraScriptStackMergeAdapter>(*OutputNode.Get(), *ShaderStage->Script, Emitter->GetUniqueEmitterName());
		InputNode = ShaderStageStack->GetInputNode();
	}
}

const UNiagaraEmitter* FNiagaraShaderStageMergeAdapter::GetEmitter() const
{
	return Emitter.Get();
}

FGuid FNiagaraShaderStageMergeAdapter::GetUsageId() const
{
	if (ShaderStage != nullptr)
	{
		return ShaderStage->Script->GetUsageId();
	}
	else
	{
		return OutputNode->GetUsageId();
	}
}

const UNiagaraShaderStageBase* FNiagaraShaderStageMergeAdapter::GetShaderStage() const
{
	return ShaderStage;
}

UNiagaraShaderStageBase* FNiagaraShaderStageMergeAdapter::GetEditableShaderStage() const
{
	return EditableShaderStage;
}

UNiagaraNodeOutput* FNiagaraShaderStageMergeAdapter::GetOutputNode() const
{
	return OutputNode.Get();
}

UNiagaraNodeInput* FNiagaraShaderStageMergeAdapter::GetInputNode() const
{
	return InputNode.Get();
}

TSharedPtr<FNiagaraScriptStackMergeAdapter> FNiagaraShaderStageMergeAdapter::GetShaderStageStack() const
{
	return ShaderStageStack;
}

FNiagaraRendererMergeAdapter::FNiagaraRendererMergeAdapter(UNiagaraRendererProperties& InRenderer)
{
	Renderer = &InRenderer;
}

UNiagaraRendererProperties* FNiagaraRendererMergeAdapter::GetRenderer()
{
	return Renderer.Get();
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
	TArray<UNiagaraNodeOutput*> ShaderStageOutputNodes;
	for (UNiagaraNodeOutput* OutputNode : OutputNodes)
	{
		if (UNiagaraScript::IsEquivalentUsage(OutputNode->GetUsage(), ENiagaraScriptUsage::EmitterSpawnScript))
		{
			EmitterSpawnStack = MakeShared<FNiagaraScriptStackMergeAdapter>(*OutputNode, *Emitter->EmitterSpawnScriptProps.Script, Emitter->GetUniqueEmitterName());
		}
		else if (UNiagaraScript::IsEquivalentUsage(OutputNode->GetUsage(), ENiagaraScriptUsage::EmitterUpdateScript))
		{
			EmitterUpdateStack = MakeShared<FNiagaraScriptStackMergeAdapter>(*OutputNode, *Emitter->EmitterUpdateScriptProps.Script, Emitter->GetUniqueEmitterName());
		}
		else if (UNiagaraScript::IsEquivalentUsage(OutputNode->GetUsage(), ENiagaraScriptUsage::ParticleSpawnScript))
		{
			ParticleSpawnStack = MakeShared<FNiagaraScriptStackMergeAdapter>(*OutputNode, *Emitter->SpawnScriptProps.Script, Emitter->GetUniqueEmitterName());
		}
		else if (UNiagaraScript::IsEquivalentUsage(OutputNode->GetUsage(), ENiagaraScriptUsage::ParticleUpdateScript))
		{
			ParticleUpdateStack = MakeShared<FNiagaraScriptStackMergeAdapter>(*OutputNode, *Emitter->UpdateScriptProps.Script, Emitter->GetUniqueEmitterName());
		}
		else if(UNiagaraScript::IsEquivalentUsage(OutputNode->GetUsage(), ENiagaraScriptUsage::ParticleEventScript))
		{
			EventOutputNodes.Add(OutputNode);
		}
		else if (UNiagaraScript::IsEquivalentUsage(OutputNode->GetUsage(), ENiagaraScriptUsage::ParticleShaderStageScript))
		{
			ShaderStageOutputNodes.Add(OutputNode);
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
	for (const UNiagaraShaderStageBase* ShaderStage : Emitter->GetShaderStages())
	{
		UNiagaraNodeOutput** MatchingOutputNodePtr = ShaderStageOutputNodes.FindByPredicate(
			[=](UNiagaraNodeOutput* ShaderStageOutputNode) { return ShaderStageOutputNode->GetUsageId() == ShaderStage->Script->GetUsageId(); });

		UNiagaraNodeOutput* MatchingOutputNode = MatchingOutputNodePtr != nullptr ? *MatchingOutputNodePtr : nullptr;

		if (EditableEmitter == nullptr)
		{
			ShaderStages.Add(MakeShared<FNiagaraShaderStageMergeAdapter>(*Emitter.Get(), ShaderStage, MatchingOutputNode));
		}
		else
		{
			UNiagaraShaderStageBase* EditableShaderStage = EditableEmitter->GetShaderStageById(ShaderStage->Script->GetUsageId());
			ShaderStages.Add(MakeShared<FNiagaraShaderStageMergeAdapter>(*Emitter.Get(), EditableShaderStage, MatchingOutputNode));
		}

		if (MatchingOutputNode != nullptr)
		{
			ShaderStageOutputNodes.Remove(MatchingOutputNode);
		}
	}

	for (UNiagaraNodeOutput* ShaderStageOutputNode : ShaderStageOutputNodes)
	{
		ShaderStages.Add(MakeShared<FNiagaraShaderStageMergeAdapter>(*Emitter.Get(), ShaderStageOutputNode));
	}

	// Renderers
	for (UNiagaraRendererProperties* RendererProperties : Emitter->GetRenderers())
	{
		Renderers.Add(MakeShared<FNiagaraRendererMergeAdapter>(*RendererProperties));
	}
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

const TArray<TSharedRef<FNiagaraShaderStageMergeAdapter>> FNiagaraEmitterMergeAdapter::GetShaderStages() const
{
	return ShaderStages;
}

const TArray<TSharedRef<FNiagaraRendererMergeAdapter>> FNiagaraEmitterMergeAdapter::GetRenderers() const
{
	return Renderers;
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

TSharedPtr<FNiagaraShaderStageMergeAdapter> FNiagaraEmitterMergeAdapter::GetShaderStage(FGuid ShaderStageUsageId)
{
	for (TSharedRef<FNiagaraShaderStageMergeAdapter> ShaderStage : ShaderStages)
	{
		if (ShaderStage->GetUsageId() == ShaderStageUsageId)
		{
			return ShaderStage;
		}
	}
	return TSharedPtr<FNiagaraShaderStageMergeAdapter>();
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
		EnabledChangedBaseModules.Num() == 0 &&
		EnabledChangedOtherModules.Num() == 0 &&
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
	: bIsValid(true)
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
	bool bShaderStageDiffsAreValid = true;
	for (const FNiagaraModifiedShaderStageDiffResults& ShaderStageDiffResults : ModifiedShaderStages)
	{
		if (ShaderStageDiffResults.ScriptDiffResults.IsValid() == false)
		{
			bShaderStageDiffsAreValid = false;
			break;
		}
	}
	return bIsValid &&
		bEventHandlerDiffsAreValid &&
		bShaderStageDiffsAreValid &&
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
		RemovedBaseShaderStages.Num() == 0 &&
		AddedOtherShaderStages.Num() == 0 &&
		ModifiedShaderStages.Num() == 0 &&
		RemovedBaseRenderers.Num() == 0 &&
		AddedOtherRenderers.Num() == 0 &&
		ModifiedBaseRenderers.Num() == 0 &&
		ModifiedOtherRenderers.Num() == 0;
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

TSharedRef<FNiagaraScriptMergeManager> FNiagaraScriptMergeManager::Get()
{
	FNiagaraEditorModule& NiagaraEditorModule = FModuleManager::GetModuleChecked<FNiagaraEditorModule>("NiagaraEditor");
	return NiagaraEditorModule.GetScriptMergeManager();
}

void FNiagaraScriptMergeManager::DiffChangeIds(const TMap<FGuid, FGuid>& InSourceChangeIds, const TMap<FGuid, FGuid>& InLastMergedChangeIds, const TMap<FGuid, FGuid>& InInstanceChangeIds, TMap<FGuid, FGuid>& OutChangeIdsToKeepOnInstance) const
{
	auto It = InInstanceChangeIds.CreateConstIterator();
	while (It)
	{
		const FGuid* MatchingSourceChangeId = InSourceChangeIds.Find(It.Key());
		const FGuid* MatchingLastMergedChangeId = InLastMergedChangeIds.Find(It.Key());

		// If we don't have source from the original or from the last merged version of the original, then the instance originated the node and needs to keep it.
		if (MatchingSourceChangeId == nullptr && MatchingLastMergedChangeId == nullptr)
		{
			OutChangeIdsToKeepOnInstance.Add(It.Key(), It.Value());
		}
		else if (MatchingSourceChangeId != nullptr && MatchingLastMergedChangeId != nullptr)
		{
			if (*MatchingSourceChangeId == *MatchingLastMergedChangeId)
			{
				// If both had a copy of the node and both agree on the change id, then we should keep the change id of the instance as it will be the most accurate.
				OutChangeIdsToKeepOnInstance.Add(It.Key(), It.Value());
			}
			else
			{
				// If both had a copy of the node and they're different than the source has changed and it's change id should be used since it's the newer.
				OutChangeIdsToKeepOnInstance.Add(It.Key(), *MatchingSourceChangeId);
			}
		}
		else if (MatchingLastMergedChangeId != nullptr)
		{
			// If only the previous version had the matching key, then we may possibly keep this node around as a local node, in which case, we should apply the override. 
			OutChangeIdsToKeepOnInstance.Add(It.Key(), It.Value());
		}
		else if (MatchingSourceChangeId != nullptr)
		{
			// I'm not sure that there's a way for us to reach this situation, where the node exists on the source and the instance, but not the 
			// last merged version.
			check(false);
		}

		++It;
	}
}

FNiagaraScriptMergeManager::FApplyDiffResults FNiagaraScriptMergeManager::ResolveChangeIds(TSharedRef<FNiagaraEmitterMergeAdapter> MergedInstanceAdapter, UNiagaraEmitter& OriginalEmitterInstance, const TMap<FGuid, FGuid>& ChangeIdsThatNeedToBeReset) const
{
	FApplyDiffResults DiffResults;

	if (ChangeIdsThatNeedToBeReset.Num() != 0)
	{
		auto It = ChangeIdsThatNeedToBeReset.CreateConstIterator();
		UNiagaraEmitter* Emitter = MergedInstanceAdapter->GetEditableEmitter();

		TArray<UNiagaraGraph*> Graphs;
		TArray<UNiagaraScript*> Scripts;
		Emitter->GetScripts(Scripts);

		TArray<UNiagaraScript*> OriginalScripts;
		OriginalEmitterInstance.GetScripts(OriginalScripts);

		// First gather all the graphs used by this emitter..
		for (UNiagaraScript* Script : Scripts)
		{
			if (Script != nullptr && Script->GetSource() != nullptr)
			{
				UNiagaraScriptSource* ScriptSource = Cast<UNiagaraScriptSource>(Script->GetSource());
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

INiagaraMergeManager::FMergeEmitterResults FNiagaraScriptMergeManager::MergeEmitter(UNiagaraEmitter& Parent, UNiagaraEmitter& ParentAtLastMerge, UNiagaraEmitter& Instance) const
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_ScriptMergeManager_MergeEmitter);
	INiagaraMergeManager::FMergeEmitterResults MergeResults;

	FNiagaraEmitterDiffResults DiffResults = DiffEmitters(ParentAtLastMerge, Instance);

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
			MergeResults.MergedInstance = Parent.DuplicateWithoutMerging((UObject*)GetTransientPackage());
		}
	}
	else
	{
		UNiagaraEmitter* MergedInstance = Parent.DuplicateWithoutMerging((UObject*)GetTransientPackage());
		TSharedRef<FNiagaraEmitterMergeAdapter> MergedInstanceAdapter = MakeShared<FNiagaraEmitterMergeAdapter>(*MergedInstance);

		TMap<FGuid, FGuid> SourceChangeIds;
		TMap<FGuid, FGuid> PreviousSourceChangeIds;
		TMap<FGuid, FGuid> LastChangeIds;
		TMap<FGuid, FGuid> ChangeIdsThatNeedToBeReset;
		FNiagaraEditorUtilities::GatherChangeIds(Parent, SourceChangeIds, TEXT("Source"));
		FNiagaraEditorUtilities::GatherChangeIds(ParentAtLastMerge, PreviousSourceChangeIds, TEXT("MergeLast"));
		FNiagaraEditorUtilities::GatherChangeIds(Instance, LastChangeIds, TEXT("Instance"));
		DiffChangeIds(SourceChangeIds, PreviousSourceChangeIds, LastChangeIds, ChangeIdsThatNeedToBeReset);

		MergeResults.MergeResult = INiagaraMergeManager::EMergeEmitterResult::SucceededDifferencesApplied;

		FApplyDiffResults EmitterSpawnResults = ApplyScriptStackDiff(MergedInstanceAdapter->GetEmitterSpawnStack().ToSharedRef(), DiffResults.EmitterSpawnDiffResults);
		if (EmitterSpawnResults.bSucceeded == false)
		{
			MergeResults.MergeResult = INiagaraMergeManager::EMergeEmitterResult::FailedToMerge;
		}
		MergeResults.bModifiedGraph |= EmitterSpawnResults.bModifiedGraph;
		MergeResults.ErrorMessages.Append(EmitterSpawnResults.ErrorMessages);

		FApplyDiffResults EmitterUpdateResults = ApplyScriptStackDiff(MergedInstanceAdapter->GetEmitterUpdateStack().ToSharedRef(), DiffResults.EmitterUpdateDiffResults);
		if (EmitterUpdateResults.bSucceeded == false)
		{
			MergeResults.MergeResult = INiagaraMergeManager::EMergeEmitterResult::FailedToMerge;
		}
		MergeResults.bModifiedGraph |= EmitterUpdateResults.bModifiedGraph;
		MergeResults.ErrorMessages.Append(EmitterUpdateResults.ErrorMessages);

		FApplyDiffResults ParticleSpawnResults = ApplyScriptStackDiff(MergedInstanceAdapter->GetParticleSpawnStack().ToSharedRef(), DiffResults.ParticleSpawnDiffResults);
		if (ParticleSpawnResults.bSucceeded == false)
		{
			MergeResults.MergeResult = INiagaraMergeManager::EMergeEmitterResult::FailedToMerge;
		}
		MergeResults.bModifiedGraph |= ParticleSpawnResults.bModifiedGraph;
		MergeResults.ErrorMessages.Append(ParticleSpawnResults.ErrorMessages);

		FApplyDiffResults ParticleUpdateResults = ApplyScriptStackDiff(MergedInstanceAdapter->GetParticleUpdateStack().ToSharedRef(), DiffResults.ParticleUpdateDiffResults);
		if (ParticleUpdateResults.bSucceeded == false)
		{
			MergeResults.MergeResult = INiagaraMergeManager::EMergeEmitterResult::FailedToMerge;
		}
		MergeResults.bModifiedGraph |= ParticleUpdateResults.bModifiedGraph;
		MergeResults.ErrorMessages.Append(ParticleUpdateResults.ErrorMessages);

		FApplyDiffResults EventHandlerResults = ApplyEventHandlerDiff(MergedInstanceAdapter, DiffResults);
		if (EventHandlerResults.bSucceeded == false)
		{
			MergeResults.MergeResult = INiagaraMergeManager::EMergeEmitterResult::FailedToMerge;
		}
		MergeResults.bModifiedGraph |= EventHandlerResults.bModifiedGraph;
		MergeResults.ErrorMessages.Append(EventHandlerResults.ErrorMessages);

		FApplyDiffResults ShaderStageResults = ApplyShaderStageDiff(MergedInstanceAdapter, DiffResults);
		if (ShaderStageResults.bSucceeded == false)
		{
			MergeResults.MergeResult = INiagaraMergeManager::EMergeEmitterResult::FailedToMerge;
		}
		MergeResults.bModifiedGraph |= ShaderStageResults.bModifiedGraph;
		MergeResults.ErrorMessages.Append(ShaderStageResults.ErrorMessages);

		FApplyDiffResults RendererResults = ApplyRendererDiff(*MergedInstance, DiffResults);
		if (RendererResults.bSucceeded == false)
		{
			MergeResults.MergeResult = INiagaraMergeManager::EMergeEmitterResult::FailedToMerge;
		}
		MergeResults.bModifiedGraph |= RendererResults.bModifiedGraph;
		MergeResults.ErrorMessages.Append(RendererResults.ErrorMessages);

		CopyPropertiesToBase(MergedInstance, &Instance, DiffResults.DifferentEmitterProperties);
		if (Instance.EditorData != nullptr)
		{
			// We keep the instance editor data here so that the UI state in the system stays more consistent.
			MergedInstance->EditorData = Cast<UNiagaraEmitterEditorData>(StaticDuplicateObject(Instance.EditorData, MergedInstance));
		}

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
		
		FApplyDiffResults ChangeIdResults = ResolveChangeIds(MergedInstanceAdapter, Instance, ChangeIdsThatNeedToBeReset);
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
		ScriptUsage == ENiagaraScriptUsage::ParticleEventScript;
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

	return ApplyScriptStackDiff(EmitterAdapter->GetScriptStack(ScriptUsage, ScriptUsageId).ToSharedRef(), ResetDiffResults);
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

bool FNiagaraScriptMergeManager::HasBaseShaderStage(const UNiagaraEmitter& BaseEmitter, FGuid ShaderStageScriptUsageId)
{
	TSharedRef<FNiagaraEmitterMergeAdapter> BaseEmitterAdapter = GetEmitterMergeAdapterUsingCache(BaseEmitter);
	return BaseEmitterAdapter->GetShaderStage(ShaderStageScriptUsageId).IsValid();
}

bool FNiagaraScriptMergeManager::IsShaderStagePropertySetDifferentFromBase(UNiagaraEmitter& Emitter, const UNiagaraEmitter& BaseEmitter, FGuid ShaderStageScriptUsageId)
{
	TSharedRef<FNiagaraEmitterMergeAdapter> EmitterAdapter = GetEmitterMergeAdapterUsingCache(Emitter);
	TSharedRef<FNiagaraEmitterMergeAdapter> BaseEmitterAdapter = GetEmitterMergeAdapterUsingCache(BaseEmitter);

	TSharedPtr<FNiagaraShaderStageMergeAdapter> ShaderStageAdapter = EmitterAdapter->GetShaderStage(ShaderStageScriptUsageId);
	TSharedPtr<FNiagaraShaderStageMergeAdapter> BaseShaderStageAdapter = BaseEmitterAdapter->GetShaderStage(ShaderStageScriptUsageId);

	if (ShaderStageAdapter->GetEditableShaderStage() == nullptr || BaseShaderStageAdapter->GetShaderStage() == nullptr)
	{
		return true;
	}

	TArray<UProperty*> DifferentProperties;
	DiffEditableProperties(BaseShaderStageAdapter->GetShaderStage(), ShaderStageAdapter->GetShaderStage(), *BaseShaderStageAdapter->GetShaderStage()->GetClass(), DifferentProperties);
	return DifferentProperties.Num() > 0;
}

void FNiagaraScriptMergeManager::ResetShaderStagePropertySetToBase(UNiagaraEmitter& Emitter, const UNiagaraEmitter& BaseEmitter, FGuid ShaderStageScriptUsageId)
{
	TSharedRef<FNiagaraEmitterMergeAdapter> EmitterAdapter = GetEmitterMergeAdapterUsingCache(Emitter);
	TSharedRef<FNiagaraEmitterMergeAdapter> BaseEmitterAdapter = GetEmitterMergeAdapterUsingCache(BaseEmitter);

	TSharedPtr<FNiagaraShaderStageMergeAdapter> ShaderStageAdapter = EmitterAdapter->GetShaderStage(ShaderStageScriptUsageId);
	TSharedPtr<FNiagaraShaderStageMergeAdapter> BaseShaderStageAdapter = BaseEmitterAdapter->GetShaderStage(ShaderStageScriptUsageId);

	if (ShaderStageAdapter->GetEditableShaderStage() == nullptr || BaseShaderStageAdapter->GetShaderStage() == nullptr)
	{
		// TODO: Display an error to the user.
		return;
	}

	TArray<UProperty*> DifferentProperties;
	DiffEditableProperties(BaseShaderStageAdapter->GetShaderStage(), ShaderStageAdapter->GetShaderStage(), *BaseShaderStageAdapter->GetShaderStage()->GetClass(), DifferentProperties);
	CopyPropertiesToBase(ShaderStageAdapter->GetEditableShaderStage(), BaseShaderStageAdapter->GetShaderStage(), DifferentProperties);
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

	ApplyRendererDiff(Emitter, ResetDiffResults);
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
	DiffShaderStages(BaseEmitterAdapter->GetShaderStages(), OtherEmitterAdapter->GetShaderStages(), EmitterDiffResults);
	DiffRenderers(BaseEmitterAdapter->GetRenderers(), OtherEmitterAdapter->GetRenderers(), EmitterDiffResults);
	DiffEditableProperties(&BaseEmitter, &OtherEmitter, *UNiagaraEmitter::StaticClass(), EmitterDiffResults.DifferentEmitterProperties);

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

void FNiagaraScriptMergeManager::DiffShaderStages(const TArray<TSharedRef<FNiagaraShaderStageMergeAdapter>>& BaseShaderStages, const TArray<TSharedRef<FNiagaraShaderStageMergeAdapter>>& OtherShaderStages, FNiagaraEmitterDiffResults& DiffResults) const
{
	FListDiffResults<TSharedRef<FNiagaraShaderStageMergeAdapter>> ShaderStageListDiffResults = DiffLists<TSharedRef<FNiagaraShaderStageMergeAdapter>, FGuid>(
		BaseShaderStages,
		OtherShaderStages,
		[](TSharedRef<FNiagaraShaderStageMergeAdapter> ShaderStage) { return ShaderStage->GetUsageId(); });

	DiffResults.RemovedBaseShaderStages.Append(ShaderStageListDiffResults.RemovedBaseValues);
	DiffResults.AddedOtherShaderStages.Append(ShaderStageListDiffResults.AddedOtherValues);

	for (const FCommonValuePair<TSharedRef<FNiagaraShaderStageMergeAdapter>>& CommonValuePair : ShaderStageListDiffResults.CommonValuePairs)
	{
		if (CommonValuePair.BaseValue->GetShaderStage() == nullptr || CommonValuePair.BaseValue->GetOutputNode() == nullptr)
		{
			DiffResults.AddError(FText::Format(LOCTEXT("InvalidBaseShaderStageDiffFailedFormat", "Failed to diff shader stages, the base shader stage was invalid.  Script Usage Id: {0}"),
				FText::FromString(CommonValuePair.BaseValue->GetUsageId().ToString())));
		}
		else if (CommonValuePair.OtherValue->GetShaderStage() == nullptr || CommonValuePair.OtherValue->GetOutputNode() == nullptr)
		{
			DiffResults.AddError(FText::Format(LOCTEXT("InvalidOtherShaderStageDiffFailedFormat", "Failed to diff shader stage, the other shader stage was invalid.  Script Usage Id: {0}"),
				FText::FromString(CommonValuePair.OtherValue->GetUsageId().ToString())));
		}
		else
		{
			TArray<UProperty*> DifferentProperties;
			DiffEditableProperties(CommonValuePair.BaseValue->GetShaderStage(), CommonValuePair.OtherValue->GetShaderStage(), *CommonValuePair.BaseValue->GetShaderStage()->GetClass(), DifferentProperties);

			FNiagaraScriptStackDiffResults ShaderStageScriptStackDiffResults;
			DiffScriptStacks(CommonValuePair.BaseValue->GetShaderStageStack().ToSharedRef(), CommonValuePair.OtherValue->GetShaderStageStack().ToSharedRef(), ShaderStageScriptStackDiffResults);

			if (DifferentProperties.Num() > 0 || ShaderStageScriptStackDiffResults.IsValid() == false || ShaderStageScriptStackDiffResults.IsEmpty() == false)
			{
				FNiagaraModifiedShaderStageDiffResults ModifiedShaderStageResults;
				ModifiedShaderStageResults.BaseAdapter = CommonValuePair.BaseValue;
				ModifiedShaderStageResults.OtherAdapter = CommonValuePair.OtherValue;
				ModifiedShaderStageResults.ChangedProperties.Append(DifferentProperties);
				ModifiedShaderStageResults.ScriptDiffResults = ShaderStageScriptStackDiffResults;
				DiffResults.ModifiedShaderStages.Add(ModifiedShaderStageResults);
			}

			if (ShaderStageScriptStackDiffResults.IsValid() == false)
			{
				for (const FText& ShaderStageScriptStackDiffErrorMessage : ShaderStageScriptStackDiffResults.GetErrorMessages())
				{
					DiffResults.AddError(ShaderStageScriptStackDiffErrorMessage);
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

		if (CommonValuePair.BaseValue->GetFunctionCallNode()->FunctionScript == CommonValuePair.OtherValue->GetFunctionCallNode()->FunctionScript ||
			CommonValuePair.BaseValue->GetFunctionCallNode()->IsA<UNiagaraNodeAssignment>())
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
	if ((BaseFunctionInputAdapter->GetDataValueObject()!= nullptr && OtherFunctionInputAdapter->GetDataValueObject()== nullptr) ||
		(BaseFunctionInputAdapter->GetDataValueObject()== nullptr && OtherFunctionInputAdapter->GetDataValueObject()!= nullptr))
	{
		return false;
	}

	if (BaseFunctionInputAdapter->GetDataValueObject()!= nullptr && OtherFunctionInputAdapter->GetDataValueObject()!= nullptr)
	{
		return BaseFunctionInputAdapter->GetDataValueObject()->Equals(OtherFunctionInputAdapter->GetDataValueObject());
	}

	// Dynamic value
	if ((BaseFunctionInputAdapter->GetDynamicValueFunction().IsValid() && OtherFunctionInputAdapter->GetDynamicValueFunction().IsValid() == false) ||
		(BaseFunctionInputAdapter->GetDynamicValueFunction().IsValid() == false && OtherFunctionInputAdapter->GetDynamicValueFunction().IsValid()))
	{
		return false;
	}

	if (BaseFunctionInputAdapter->GetDynamicValueFunction().IsValid() && OtherFunctionInputAdapter->GetDynamicValueFunction().IsValid())
	{
		UNiagaraNodeCustomHlsl* BaseCustomHlsl = Cast<UNiagaraNodeCustomHlsl>(BaseFunctionInputAdapter->GetDynamicValueFunction()->GetFunctionCallNode());
		UNiagaraNodeCustomHlsl* OtherCustomHlsl = Cast<UNiagaraNodeCustomHlsl>(OtherFunctionInputAdapter->GetDynamicValueFunction()->GetFunctionCallNode());

		if (BaseCustomHlsl && OtherCustomHlsl)
		{
			if (BaseCustomHlsl->GetCustomHlsl() != OtherCustomHlsl->GetCustomHlsl() || BaseCustomHlsl->ScriptUsage != OtherCustomHlsl->ScriptUsage)
			{
				return false;
			}
		}
		else if (BaseFunctionInputAdapter->GetDynamicValueFunction()->GetFunctionCallNode()->FunctionScript != OtherFunctionInputAdapter->GetDynamicValueFunction()->GetFunctionCallNode()->FunctionScript)
		{
			return false;
		}

		FNiagaraScriptStackDiffResults FunctionDiffResults;
		DiffFunctionInputs(BaseFunctionInputAdapter->GetDynamicValueFunction().ToSharedRef(), OtherFunctionInputAdapter->GetDynamicValueFunction().ToSharedRef(), FunctionDiffResults);
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

FNiagaraScriptMergeManager::FApplyDiffResults FNiagaraScriptMergeManager::AddModule(FString UniqueEmitterName, UNiagaraScript& OwningScript, UNiagaraNodeOutput& TargetOutputNode, TSharedRef<FNiagaraStackFunctionMergeAdapter> AddModule) const
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
		if (AddModule->GetFunctionCallNode()->FunctionScript != nullptr)
		{
			AddedModuleNode = FNiagaraStackGraphUtilities::AddScriptModuleToStack(AddModule->GetFunctionCallNode()->FunctionScript, TargetOutputNode, AddModule->GetStackIndex());
			AddedModuleNode->NodeGuid = AddModule->GetFunctionCallNode()->NodeGuid; // Synchronize the node Guid across runs so that the compile id's synch up.
			Results.bModifiedGraph = true;
		}
		else
		{
			Results.bSucceeded = false;
			Results.ErrorMessages.Add(FText::Format(
				LOCTEXT("AddModuleFailedDueToMissingModuleScriptFormat", "Can not add module {0} because it's script was missing."),
				FText::FromString(AddModule->GetFunctionCallNode()->GetFunctionName())));
		}
	}

	if (AddedModuleNode != nullptr)
	{
		AddedModuleNode->NodeGuid = AddModule->GetFunctionCallNode()->NodeGuid; // Synchronize the node Guid across runs so that the compile id's synch up.

		AddedModuleNode->SetEnabledState(AddModule->GetFunctionCallNode()->GetDesiredEnabledState(), AddModule->GetFunctionCallNode()->HasUserSetTheEnabledState());
		for (TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter> InputOverride : AddModule->GetInputOverrides())
		{
			FApplyDiffResults AddInputResults = AddInputOverride(UniqueEmitterName, OwningScript, *AddedModuleNode, InputOverride);
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
	if (OverrideToRemove->GetOverridePin() != nullptr && OverrideToRemove->GetOverrideNode() != nullptr)
	{
		FNiagaraStackGraphUtilities::RemoveNodesForStackFunctionInputOverridePin(*OverrideToRemove->GetOverridePin());
		OverrideToRemove->GetOverrideNode()->RemovePin(OverrideToRemove->GetOverridePin());
		Results.bSucceeded = true;
		Results.bModifiedGraph = true;
	}
	else if (OverrideToRemove->GetLocalValueRapidIterationParameter().IsSet())
	{
		OwningScript.Modify();
		OwningScript.RapidIterationParameters.RemoveParameter(OverrideToRemove->GetLocalValueRapidIterationParameter().GetValue());
		Results.bSucceeded = true;
		Results.bModifiedGraph = false;
	}
	else if (OverrideToRemove->GetStaticSwitchValue().IsSet())
	{
		// TODO: Static switches are always treated as overrides right now so removing them is a no-op.  This code should updated
		// so that removing a static switch override sets the value back to the module default.
		Results.bSucceeded = true;
	}
	else
	{
		Results.bSucceeded = false;
		Results.bModifiedGraph = false;
		Results.ErrorMessages.Add(LOCTEXT("RemoveInputOverrideFailed", "Failed to remove input override because it was invalid."));
	}
	return Results;
}

FNiagaraScriptMergeManager::FApplyDiffResults FNiagaraScriptMergeManager::AddInputOverride(FString UniqueEmitterName, UNiagaraScript& OwningScript, UNiagaraNodeFunctionCall& TargetFunctionCall, TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter> OverrideToAdd) const
{
	FApplyDiffResults Results;

	// If an assignment node, make sure that we have an assignment target for the input override.
	UNiagaraNodeAssignment* AssignmentNode = Cast<UNiagaraNodeAssignment>(&TargetFunctionCall);
	if (AssignmentNode)
	{
		FNiagaraParameterHandle FunctionInputHandle(FNiagaraParameterHandle::ModuleNamespace, *OverrideToAdd->GetInputName());
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

	FNiagaraParameterHandle FunctionInputHandle(FNiagaraParameterHandle::ModuleNamespace, *OverrideToAdd->GetInputName());
	FNiagaraParameterHandle AliasedFunctionInputHandle = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(FunctionInputHandle, &TargetFunctionCall);

	if (OverrideToAdd->GetOverridePin() != nullptr)
	{
		const UEdGraphSchema_Niagara* NiagaraSchema = GetDefault<UEdGraphSchema_Niagara>();
		FNiagaraTypeDefinition InputType = NiagaraSchema->PinToTypeDefinition(OverrideToAdd->GetOverridePin());

		UEdGraphPin& InputOverridePin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(TargetFunctionCall, AliasedFunctionInputHandle, InputType, OverrideToAdd->GetOverrideNode()->NodeGuid);
		if (InputOverridePin.LinkedTo.Num() != 0)
		{
			Results.bSucceeded = false;
			Results.ErrorMessages.Add(LOCTEXT("AddPinBasedInputOverrideFailedOverridePinStillLinked", "Failed to add input override because the target override pin was still linked to other nodes."));
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
				FNiagaraStackGraphUtilities::SetLinkedValueHandleForFunctionInput(InputOverridePin, OverrideToAdd->GetLinkedValueHandle().GetValue(), OverrideToAdd->GetOverrideNodeId());
				Results.bSucceeded = true;
			}
			else if (OverrideToAdd->GetDataValueObject() != nullptr)
			{
				UNiagaraDataInterface* OverrideValueObject = OverrideToAdd->GetDataValueObject();
				UNiagaraDataInterface* NewOverrideValueObject;
				FNiagaraStackGraphUtilities::SetDataValueObjectForFunctionInput(InputOverridePin, OverrideToAdd->GetDataValueObject()->GetClass(), OverrideValueObject->GetName(), NewOverrideValueObject, OverrideToAdd->GetOverrideNodeId());
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
						FApplyDiffResults AddResults = AddInputOverride(UniqueEmitterName, OwningScript, *((UNiagaraNodeFunctionCall*)DynamicInputFunctionCall), DynamicInputInputOverride);
						Results.bSucceeded &= AddResults.bSucceeded;
						Results.bModifiedGraph |= AddResults.bModifiedGraph;
						Results.ErrorMessages.Append(AddResults.ErrorMessages);
					}
				}
				else if (OverrideToAdd->GetDynamicValueFunction()->GetFunctionCallNode()->FunctionScript != nullptr)
				{
					UNiagaraNodeFunctionCall* DynamicInputFunctionCall;
					FNiagaraStackGraphUtilities::SetDynamicInputForFunctionInput(InputOverridePin, OverrideToAdd->GetDynamicValueFunction()->GetFunctionCallNode()->FunctionScript,
						DynamicInputFunctionCall, OverrideToAdd->GetOverrideNodeId(), OverrideToAdd->GetDynamicValueFunction()->GetFunctionCallNode()->GetFunctionName());
					for (TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter> DynamicInputInputOverride : OverrideToAdd->GetDynamicValueFunction()->GetInputOverrides())
					{
						FApplyDiffResults AddResults = AddInputOverride(UniqueEmitterName, OwningScript, *DynamicInputFunctionCall, DynamicInputInputOverride);
						Results.bSucceeded &= AddResults.bSucceeded;
						Results.bModifiedGraph |= AddResults.bModifiedGraph;
						Results.ErrorMessages.Append(AddResults.ErrorMessages);
					}
				}
				else
				{
					Results.bSucceeded = false;
					Results.ErrorMessages.Add(LOCTEXT("AddPinBasedInputOverrideFailedInvalidDynamicInput", "Failed to add input override because it's dynamic function call's function script was null."));
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
				UniqueEmitterName, OwningScript.GetUsage(), AliasedFunctionInputHandle.GetParameterHandleString(), OverrideToAdd->GetLocalValueRapidIterationParameter().GetValue().GetType());
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

FNiagaraScriptMergeManager::FApplyDiffResults FNiagaraScriptMergeManager::ApplyScriptStackDiff(TSharedRef<FNiagaraScriptStackMergeAdapter> BaseScriptStackAdapter, const FNiagaraScriptStackDiffResults& DiffResults) const
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
			RemoveModules.Add(MatchingModuleAdapter.ToSharedRef());
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
		FApplyDiffResults AddModuleResults = AddModule(BaseScriptStackAdapter->GetUniqueEmitterName(), *BaseScriptStackAdapter->GetScript(), *BaseScriptStackAdapter->GetOutputNode(), AddModuleAdapter);
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
		FApplyDiffResults AddInputOverrideResults = AddInputOverride(BaseScriptStackAdapter->GetUniqueEmitterName(), *BaseScriptStackAdapter->GetScript(), *AddInputOverrideActionData.TargetFunctionCall, AddInputOverrideActionData.OverrideToAdd.ToSharedRef());
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

FNiagaraScriptMergeManager::FApplyDiffResults FNiagaraScriptMergeManager::ApplyEventHandlerDiff(TSharedRef<FNiagaraEmitterMergeAdapter> BaseEmitterAdapter, const FNiagaraEmitterDiffResults& DiffResults) const
{
	FApplyDiffResults Results;
	if (DiffResults.RemovedBaseEventHandlers.Num() > 0)
	{
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
					FApplyDiffResults ApplyEventHandlerStackDiffResults = ApplyScriptStackDiff(MatchingBaseEventHandlerAdapter->GetEventStack().ToSharedRef(), ModifiedEventHandler.ScriptDiffResults);
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
			AddedEventScriptProperties.Script->SetSource(EmitterSource);
			BaseEmitter->AddEventHandler(AddedEventScriptProperties);

			FGuid PreferredOutputNodeGuid = AddedEventHandler->GetOutputNode()->NodeGuid;
			FGuid PreferredInputNodeGuid = AddedEventHandler->GetInputNode()->NodeGuid;
			UNiagaraNodeOutput* EventOutputNode = FNiagaraStackGraphUtilities::ResetGraphForOutput(*EmitterGraph, ENiagaraScriptUsage::ParticleEventScript, AddedEventScriptProperties.Script->GetUsageId(), PreferredOutputNodeGuid, PreferredInputNodeGuid);
			for (TSharedRef<FNiagaraStackFunctionMergeAdapter> ModuleAdapter : AddedEventHandler->GetEventStack()->GetModuleFunctions())
			{
				FApplyDiffResults AddModuleResults = AddModule(BaseEmitter->GetUniqueEmitterName(), *AddedEventScriptProperties.Script, *EventOutputNode, ModuleAdapter);
				Results.bSucceeded &= AddModuleResults.bSucceeded;
				Results.ErrorMessages.Append(AddModuleResults.ErrorMessages);
			}

			// Force the base compile id of the new event handler to match the added instance event handler.
			UNiagaraScriptSource* AddedEventScriptSourceFromDiff = Cast<UNiagaraScriptSource>(AddedEventHandler->GetEventScriptProperties()->Script->GetSource());
			UNiagaraGraph* AddedEventScriptGraphFromDiff = AddedEventScriptSourceFromDiff->NodeGraph;
			FGuid ScriptBaseIdFromDiff = AddedEventScriptGraphFromDiff->GetBaseId(ENiagaraScriptUsage::ParticleEventScript, AddedEventHandler->GetUsageId());
			UNiagaraScriptSource* AddedEventScriptSource = Cast<UNiagaraScriptSource>(AddedEventScriptProperties.Script->GetSource());
			UNiagaraGraph* AddedEventScriptGraph = AddedEventScriptSource->NodeGraph;
			AddedEventScriptGraph->ForceBaseId(ENiagaraScriptUsage::ParticleEventScript, AddedEventHandler->GetUsageId(), ScriptBaseIdFromDiff);

			Results.bModifiedGraph = true;
		}
	}
	return Results;
}

FNiagaraScriptMergeManager::FApplyDiffResults FNiagaraScriptMergeManager::ApplyShaderStageDiff(TSharedRef<FNiagaraEmitterMergeAdapter> BaseEmitterAdapter, const FNiagaraEmitterDiffResults& DiffResults) const
{
	FApplyDiffResults Results;
	if (DiffResults.RemovedBaseShaderStages.Num() > 0)
	{
		Results.bSucceeded = false;
		Results.bModifiedGraph = false;
		Results.ErrorMessages.Add(LOCTEXT("RemovedShaderStagesUnsupported", "Apply diff failed, removed shader stages are currently unsupported."));
		return Results;
	}

	for (const FNiagaraModifiedShaderStageDiffResults& ModifiedShaderStage : DiffResults.ModifiedShaderStages)
	{
		if (ModifiedShaderStage.OtherAdapter->GetShaderStage() == nullptr)
		{
			Results.bSucceeded = false;
			Results.ErrorMessages.Add(FText::Format(
				LOCTEXT("MissingModifiedShaderStageObjectFormat", "Apply diff failed.  The modified shader stage with id: {0} was missing it's shader stage object."),
				FText::FromString(ModifiedShaderStage.OtherAdapter->GetUsageId().ToString(EGuidFormats::DigitsWithHyphens))));
		}
		else if (ModifiedShaderStage.OtherAdapter->GetOutputNode() == nullptr)
		{
			Results.bSucceeded = false;
			Results.ErrorMessages.Add(FText::Format(
				LOCTEXT("MissingModifiedShaderStageOutputNodeFormat", "Apply diff failed.  The modified shader stage with id: {0} was missing it's output node."),
				FText::FromString(ModifiedShaderStage.OtherAdapter->GetUsageId().ToString(EGuidFormats::DigitsWithHyphens))));
		}
		else
		{
			TSharedPtr<FNiagaraShaderStageMergeAdapter> MatchingBaseShaderStageAdapter = BaseEmitterAdapter->GetShaderStage(ModifiedShaderStage.OtherAdapter->GetUsageId());
			if (MatchingBaseShaderStageAdapter.IsValid())
			{
				if (ModifiedShaderStage.ChangedProperties.Num() > 0)
				{
					CopyPropertiesToBase(MatchingBaseShaderStageAdapter->GetEditableShaderStage(), ModifiedShaderStage.OtherAdapter->GetEditableShaderStage(), ModifiedShaderStage.ChangedProperties);
				}
				if (ModifiedShaderStage.ScriptDiffResults.IsEmpty() == false)
				{
					FApplyDiffResults ApplyShaderStageStackDiffResults = ApplyScriptStackDiff(MatchingBaseShaderStageAdapter->GetShaderStageStack().ToSharedRef(), ModifiedShaderStage.ScriptDiffResults);
					Results.bSucceeded &= ApplyShaderStageStackDiffResults.bSucceeded;
					Results.bModifiedGraph |= ApplyShaderStageStackDiffResults.bModifiedGraph;
					Results.ErrorMessages.Append(ApplyShaderStageStackDiffResults.ErrorMessages);
				}
			}
		}
	}

	UNiagaraScriptSource* EmitterSource = CastChecked<UNiagaraScriptSource>(BaseEmitterAdapter->GetEditableEmitter()->GraphSource);
	UNiagaraGraph* EmitterGraph = EmitterSource->NodeGraph;
	for (TSharedRef<FNiagaraShaderStageMergeAdapter> AddedOtherShaderStage : DiffResults.AddedOtherShaderStages)
	{
		if (AddedOtherShaderStage->GetShaderStage() == nullptr)
		{
			Results.bSucceeded = false;
			Results.ErrorMessages.Add(FText::Format(
				LOCTEXT("MissingAddedShaderStageObjectFormat", "Apply diff failed.  The added shader stage with id: {0} was missing it's shader stage object."),
				FText::FromString(AddedOtherShaderStage->GetUsageId().ToString(EGuidFormats::DigitsWithHyphens))));
		}
		else if (AddedOtherShaderStage->GetOutputNode() == nullptr)
		{
			Results.bSucceeded = false;
			Results.ErrorMessages.Add(FText::Format(
				LOCTEXT("MissingAddedShaderStageOutputNodeFormat", "Apply diff failed.  The added shader stage with id: {0} was missing it's output node."),
				FText::FromString(AddedOtherShaderStage->GetUsageId().ToString(EGuidFormats::DigitsWithHyphens))));
		}
		else
		{
			UNiagaraEmitter* BaseEmitter = BaseEmitterAdapter->GetEditableEmitter();
			UNiagaraShaderStageBase* AddedShaderStage = CastChecked<UNiagaraShaderStageBase>(StaticDuplicateObject(AddedOtherShaderStage->GetShaderStage(), BaseEmitter));
			AddedShaderStage->Script = NewObject<UNiagaraScript>(BaseEmitter, MakeUniqueObjectName(BaseEmitter, UNiagaraScript::StaticClass(), "ShaderStage"), EObjectFlags::RF_Transactional);
			AddedShaderStage->Script->SetUsage(ENiagaraScriptUsage::ParticleEventScript);
			AddedShaderStage->Script->SetUsageId(AddedOtherShaderStage->GetUsageId());
			AddedShaderStage->Script->SetSource(EmitterSource);
			BaseEmitter->AddShaderStage(AddedShaderStage);

			FGuid PreferredOutputNodeGuid = AddedOtherShaderStage->GetOutputNode()->NodeGuid;
			FGuid PreferredInputNodeGuid = AddedOtherShaderStage->GetInputNode()->NodeGuid;
			UNiagaraNodeOutput* ShaderStageOutputNode = FNiagaraStackGraphUtilities::ResetGraphForOutput(*EmitterGraph, ENiagaraScriptUsage::ParticleShaderStageScript, AddedShaderStage->Script->GetUsageId(), PreferredOutputNodeGuid, PreferredInputNodeGuid);
			for (TSharedRef<FNiagaraStackFunctionMergeAdapter> ModuleAdapter : AddedOtherShaderStage->GetShaderStageStack()->GetModuleFunctions())
			{
				FApplyDiffResults AddModuleResults = AddModule(BaseEmitter->GetUniqueEmitterName(), *AddedShaderStage->Script, *ShaderStageOutputNode, ModuleAdapter);
				Results.bSucceeded &= AddModuleResults.bSucceeded;
				Results.ErrorMessages.Append(AddModuleResults.ErrorMessages);
			}

			// Force the base compile id of the new shader stage to match the added instance shader stage.
			UNiagaraScriptSource* AddedShaderStageSourceFromDiff = Cast<UNiagaraScriptSource>(AddedOtherShaderStage->GetShaderStage()->Script->GetSource());
			UNiagaraGraph* AddedShaderStageGraphFromDiff = AddedShaderStageSourceFromDiff->NodeGraph;
			FGuid ScriptBaseIdFromDiff = AddedShaderStageGraphFromDiff->GetBaseId(ENiagaraScriptUsage::ParticleShaderStageScript, AddedOtherShaderStage->GetUsageId());
			UNiagaraScriptSource* AddedShaderStageSource = Cast<UNiagaraScriptSource>(AddedShaderStage->Script->GetSource());
			UNiagaraGraph* AddedShaderStageGraph = AddedShaderStageSource->NodeGraph;
			AddedShaderStageGraph->ForceBaseId(ENiagaraScriptUsage::ParticleShaderStageScript, AddedOtherShaderStage->GetUsageId(), ScriptBaseIdFromDiff);

			Results.bModifiedGraph = true;
		}
	}
	return Results;
}

FNiagaraScriptMergeManager::FApplyDiffResults FNiagaraScriptMergeManager::ApplyRendererDiff(UNiagaraEmitter& BaseEmitter, const FNiagaraEmitterDiffResults& DiffResults) const
{
	TArray<UNiagaraRendererProperties*> RenderersToRemove;
	TArray<UNiagaraRendererProperties*> RenderersToAdd;

	for (TSharedRef<FNiagaraRendererMergeAdapter> RemovedRenderer : DiffResults.RemovedBaseRenderers)
	{
		auto FindRendererByMergeId = [=](UNiagaraRendererProperties* Renderer) { return Renderer->GetMergeId() == RemovedRenderer->GetRenderer()->GetMergeId(); };
		UNiagaraRendererProperties*const* MatchingRendererPtr = BaseEmitter.GetRenderers().FindByPredicate(FindRendererByMergeId);
		if (MatchingRendererPtr != nullptr)
		{
			RenderersToRemove.Add(*MatchingRendererPtr);
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

	FApplyDiffResults Results;
	Results.bSucceeded = true;
	Results.bModifiedGraph = false;
	return Results;
}

TSharedRef<FNiagaraEmitterMergeAdapter> FNiagaraScriptMergeManager::GetEmitterMergeAdapterUsingCache(const UNiagaraEmitter& Emitter)
{
	FCachedMergeAdapter* CachedMergeAdapter = CachedMergeAdapters.Find(FObjectKey(&Emitter));
	if (CachedMergeAdapter == nullptr)
	{
		CachedMergeAdapter = &CachedMergeAdapters.Add(FObjectKey(&Emitter));
	}

	if (CachedMergeAdapter->EmitterMergeAdapter.IsValid() == false ||
		CachedMergeAdapter->EmitterMergeAdapter->GetEditableEmitter() != nullptr ||
		CachedMergeAdapter->ChangeId != Emitter.GetChangeId())
	{
		CachedMergeAdapter->EmitterMergeAdapter = MakeShared<FNiagaraEmitterMergeAdapter>(Emitter);
		CachedMergeAdapter->ChangeId = Emitter.GetChangeId();
	}

	return CachedMergeAdapter->EmitterMergeAdapter.ToSharedRef();
}

TSharedRef<FNiagaraEmitterMergeAdapter> FNiagaraScriptMergeManager::GetEmitterMergeAdapterUsingCache(UNiagaraEmitter& Emitter)
{
	FCachedMergeAdapter* CachedMergeAdapter = CachedMergeAdapters.Find(FObjectKey(&Emitter));
	if (CachedMergeAdapter == nullptr)
	{
		CachedMergeAdapter = &CachedMergeAdapters.Add(FObjectKey(&Emitter));
	}

	if (CachedMergeAdapter->EmitterMergeAdapter.IsValid() == false ||
		CachedMergeAdapter->EmitterMergeAdapter->GetEditableEmitter() == nullptr ||
		CachedMergeAdapter->ChangeId != Emitter.GetChangeId())
	{
		CachedMergeAdapter->EmitterMergeAdapter = MakeShared<FNiagaraEmitterMergeAdapter>(Emitter);
		CachedMergeAdapter->ChangeId = Emitter.GetChangeId();
	}

	return CachedMergeAdapter->EmitterMergeAdapter.ToSharedRef();
}


#undef LOCTEXT_NAMESPACE
