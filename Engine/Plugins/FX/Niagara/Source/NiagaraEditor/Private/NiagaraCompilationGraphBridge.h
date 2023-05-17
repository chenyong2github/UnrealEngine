// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EdGraphSchema_Niagara.h"
#include "NiagaraCompilationPrivate.h"
#include "NiagaraConstants.h"
#include "NiagaraModule.h"
#include "NiagaraNodeConvert.h"
#include "NiagaraNodeCustomHlsl.h"
#include "NiagaraNodeEmitter.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeIf.h"
#include "NiagaraNodeInput.h"
#include "NiagaraNodeOp.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraNodeParameterMapFor.h"
#include "NiagaraNodeParameterMapGet.h"
#include "NiagaraNodeParameterMapSet.h"
#include "NiagaraNodeSelect.h"
#include "NiagaraScriptVariable.h"

// Defines an interface for UNiagaraGraph, their nodes & pins and all related support structures.
// Intended as a temporary stopgap measure to allow us to reuse code like the hlsl translator and
// parameter map history builders that can traverse different ways of representing the niagara graphs
struct FNiagaraCompilationGraphBridge
{
	using FGraph = UNiagaraGraph;
	using FPin = UEdGraphPin;
	using FScopedPin = FModuleScopedPin;
	using FCustomHlslNode = UNiagaraNodeCustomHlsl;
	using FNode = UNiagaraNode;
	using FOutputNode = UNiagaraNodeOutput;
	using FInputNode = UNiagaraNodeInput;
	using FOpNode = UNiagaraNodeOp;
	using FEmitterNode = UNiagaraNodeEmitter;
	using FIfNode = UNiagaraNodeIf;
	using FConvertNode = UNiagaraNodeConvert;
	using FSelectNode = UNiagaraNodeSelect;
	using FFunctionCallNode = UNiagaraNodeFunctionCall;
	using FParamMapGetNode = UNiagaraNodeParameterMapGet;
	using FParamMapSetNode = UNiagaraNodeParameterMapSet;
	using FParamMapForNode = UNiagaraNodeParameterMapFor;
	using FStaticSwitchNode = UNiagaraNodeStaticSwitch;
	using FInputPin = UEdGraphPin;
	using FOutputPin = UEdGraphPin;
	using FParamMapHistory = FNiagaraParameterMapHistory;
	using FParamMapHistoryBuilder = FNiagaraParameterMapHistoryBuilder;
	using FPrecompileData = FNiagaraCompileRequestData;
	using FCompilationCopy = FNiagaraCompileRequestDuplicateData;
	using FModuleScopedPin = FModuleScopedPin;
	using FGraphTraversalHandle = FGraphTraversalHandle;
	using FGraphFunctionAliasContext = FNiagaraGraphFunctionAliasContext;
	using FConvertConnection = FNiagaraConvertConnection;

	static const FGraph* GetGraph(const FCompilationCopy* CompilationCopy)
	{
		return CompilationCopy->NodeGraphDeepCopy.Get();
	}

	static const FNode* GetOwningNode(const FPin* Pin)
	{
		return Cast<const FNode>(Pin->GetOwningNode());
	}

	static FNode* GetMutableOwningNode(const FPin* Pin)
	{
		return Cast<FNode>(Pin->GetOwningNode());
	}

	static const FGraph* GetOwningGraph(const FNode* Node)
	{
		if (const UNiagaraNode* NiagaraNode = Cast<const UNiagaraNode>(Node))
		{
			return NiagaraNode->GetNiagaraGraph();
		}
		return nullptr;
	}

	static bool CustomHlslReferencesTokens(const FCustomHlslNode* CustomNode, TConstArrayView<FStringView> TokenStrings)
	{
		TArray<FString> StringTokens;
		UNiagaraNodeCustomHlsl::GetTokensFromString(CustomNode->GetCustomHlsl(), StringTokens, false, false);

		for (FStringView Token : TokenStrings)
		{
			const bool TokenMatched = StringTokens.ContainsByPredicate([&Token](const FString& HlslToken) -> bool
			{
				return HlslToken.Contains(Token);
			});

			if (TokenMatched)
			{
				return true;
			}
		}

		return false;
	}

	static ENiagaraScriptUsage GetCustomHlslUsage(const FCustomHlslNode* CustomNode)
	{
		return CustomNode->ScriptUsage;
	}

	static FString GetCustomHlslString(const FCustomHlslNode* CustomNode)
	{
		return CustomNode->GetCustomHlsl();
	}

	static TArray<FNiagaraCustomHlslInclude> GetCustomHlslIncludePaths(const FCustomHlslNode* CustomNode)
	{
		TArray<FNiagaraCustomHlslInclude> Includes;
		CustomNode->GetIncludeFilePaths(Includes);
		return Includes;
	}

	static const TArray<FConvertConnection>& GetConvertConnections(const FConvertNode* ConvertNode)
	{
		return ConvertNode->GetConnections();
	}

	template<typename NodeType>
	static const NodeType* AsNodeType(const FNode* Node)
	{
		return Cast<const NodeType>(Node);
	}

	static bool GraphHasParametersOfType(const FGraph* Graph, const FNiagaraTypeDefinition& TypeDef)
	{
		TArray<FNiagaraVariable> Inputs;
		TArray<FNiagaraVariable> Outputs;
		Graph->GetParameters(Inputs, Outputs);

		auto ContainsVariableOfType = [TypeDef](const FNiagaraVariable& Variable) -> bool
		{
			return Variable.GetType() == TypeDef;
		};

		return Inputs.ContainsByPredicate(ContainsVariableOfType)
			|| Outputs.ContainsByPredicate(ContainsVariableOfType);
	}

	static TArray<FNiagaraVariableBase> GraphGetStaticSwitchInputs(const FGraph* Graph)
	{
		TArray<FNiagaraVariableBase> StaticSwitchInputs;
		const TArray<FNiagaraVariable>& Variables = Graph->FindStaticSwitchInputs();

		StaticSwitchInputs.Reserve(Variables.Num());
		Algo::Transform(Variables, StaticSwitchInputs, [](const FNiagaraVariable& InVar) -> FNiagaraVariableBase
		{
			return InVar;
		});

		return StaticSwitchInputs;
	}

	static void FindOutputNodes(const FGraph* Graph, ENiagaraScriptUsage ScriptUsage, TArray<const FOutputNode*>& OutputNodes)
	{
		TArray<FOutputNode*> MutableOutputNodes;
		Graph->FindOutputNodes(ScriptUsage, MutableOutputNodes);
		OutputNodes.Reserve(MutableOutputNodes.Num());
		Algo::Transform(MutableOutputNodes, OutputNodes, [](FOutputNode* OutputNode) -> const FOutputNode*
		{
			return OutputNode;
		});
	}

	static void FindOutputNodes(const FGraph* Graph, TArray<const FOutputNode*>& OutputNodes)
	{
		TArray<FOutputNode*> MutableOutputNodes;
		Graph->FindOutputNodes(MutableOutputNodes);
		OutputNodes.Reserve(MutableOutputNodes.Num());
		Algo::Transform(MutableOutputNodes, OutputNodes, [](FOutputNode* OutputNode) -> const FOutputNode*
		{
			return OutputNode;
		});
	}

	static void BuildTraversal(const FGraph* Graph, const FNode* OutputNode, TArray<const FNode*>& TraversedNodes)
	{
		TArray<UNiagaraNode*> MutableTraversedNodes;
		Graph->BuildTraversal(MutableTraversedNodes, const_cast<FNode*>(OutputNode), true);
		TraversedNodes.Reserve(MutableTraversedNodes.Num());
		Algo::Transform(MutableTraversedNodes, TraversedNodes, [](UNiagaraNode* Node) -> const FNode*
		{
			return Node;
		});
	}

	static const FGraph* GetEmitterGraph(const FEmitterNode* EmitterNode)
	{
		return EmitterNode->GetCalledGraph();
	}

	static FString GetEmitterUniqueName(const FEmitterNode* EmitterNode)
	{
		return EmitterNode->GetEmitterUniqueName();
	}

	static ENiagaraScriptUsage GetEmitterUsage(const FEmitterNode* EmitterNode)
	{
		return EmitterNode->GetUsage();
	}

	static FString GetEmitterName(const FEmitterNode* EmitterNode)
	{
		return EmitterNode->GetName();
	}

	static FString GetEmitterPathName(const FEmitterNode* EmitterNode)
	{
		return EmitterNode->GetFullName();
	}

	static FString GetEmitterHandleIdString(const FEmitterNode* EmitterNode)
	{
		return EmitterNode->GetEmitterHandleId().ToString(EGuidFormats::Digits);
	}

	static const FGraph* GetFunctionNodeGraph(const FFunctionCallNode* FunctionCall)
	{
		return FunctionCall->GetCalledGraph();
	}

	static FString GetFunctionFullName(const FFunctionCallNode* FunctionCall)
	{
		return FunctionCall->FunctionScript ? FunctionCall->FunctionScript->GetFullName() : FString();
	}

	static FString GetFunctionScriptName(const FFunctionCallNode* FunctionCall)
	{
		return FunctionCall->FunctionScript ? FunctionCall->FunctionScript->GetName() : FString();
	}

	static FString GetFunctionName(const FFunctionCallNode* FunctionCall)
	{
		return FunctionCall ? FunctionCall->GetFunctionName() : FString();
	}

	static ENiagaraScriptUsage GetFunctionUsage(const FFunctionCallNode* FunctionCall)
	{
		return FunctionCall->GetCalledUsage();
	}

	static TOptional<ENiagaraDefaultMode> GetGraphDefaultMode(const FGraph* Graph, const FNiagaraVariable& Variable, FNiagaraScriptVariableBinding& Binding)
	{
		return Graph->GetDefaultMode(Variable, &Binding);
	}

	static const FInputPin* GetDefaultPin(const FParamMapGetNode* GetNode, const FOutputPin* OutputPin)
	{
		return GetNode->GetDefaultPin(const_cast<FOutputPin*>(OutputPin));
	}

	template<typename ArrayType>
	static void AppendCompilationPins(const FNode* Node, EEdGraphPinDirection PinDirection, ArrayType& OutPins)
	{
		const UNiagaraNodeWithDynamicPins* DynNode = Cast<const UNiagaraNodeWithDynamicPins>(Node);
		for (FPin* Pin : Node->Pins)
		{
			if (Pin->Direction == PinDirection)
			{
				if (Pin->bOrphanedPin)
				{
					continue;
				}

				if (DynNode != nullptr && DynNode->IsAddPin(Pin))
				{
					continue;
				}

				OutPins.Add(Pin);
			}
		}
	}

	// retrieves all input pins (excluding any add pins that may be present)
	static TArray<const FInputPin*> GetInputPins(const FNode* Node)
	{
		TArray<const FInputPin*> InputPins;
		AppendCompilationPins(Node, EEdGraphPinDirection::EGPD_Input, InputPins);
		return InputPins;
	}

	// retrieves all output pins (excluding both orphaned pins and add pins)
	static TArray<const FOutputPin*> GetOutputPins(const FNode* Node)
	{
		TArray<const FInputPin*> OutputPins;
		AppendCompilationPins(Node, EEdGraphPinDirection::EGPD_Output, OutputPins);
		return OutputPins;
	}

	// gets all pins assoicated with the node
	static TArray<const FPin*> GetPins(const FNode* Node)
	{
		TArray<const FPin*> Pins;
		Pins.Reserve(Node->Pins.Num());
		Algo::Transform(Node->Pins, Pins, [](FPin* InPin) -> const FPin*
		{
			return InPin;
		});
		return Pins;
	}

	static FNiagaraTypeDefinition GetPinType(const FPin* Pin, ENiagaraStructConversion Conversion)
	{
		return UEdGraphSchema_Niagara::PinToTypeDefinition(Pin, Conversion);
	}

	static FText GetPinFriendlyName(const FPin* Pin)
	{
		return Pin->PinFriendlyName;
	}

	static FText GetPinDisplayName(const FPin* Pin)
	{
		return Pin->GetDisplayName();
	}

	static FNiagaraVariable GetPinVariable(const FPin* Pin, bool bNeedsValue, ENiagaraStructConversion Conversion)
	{
		return UEdGraphSchema_Niagara::PinToNiagaraVariable(Pin, bNeedsValue, Conversion);
	}

	static const FInputPin* GetPinAsInput(const FPin* Pin)
	{
		if (Pin && Pin->Direction == EGPD_Input)
		{
			return Pin;
		}
		return nullptr;
	}

	static FNiagaraVariable GetInputVariable(const FInputNode* InputNode)
	{
		return InputNode->Input;
	}

	static const TArray<FNiagaraVariable>& GetOutputVariables(const FOutputNode* OutputNode)
	{
		return OutputNode->GetOutputs();
	}

	static TArray<FNiagaraVariable> GetGraphOutputNodeVariables(const FGraph* Graph, ENiagaraScriptUsage Usage)
	{
		TArray<FNiagaraVariable> OutputNodeVariables;
		Graph->GetOutputNodeVariables(Usage, OutputNodeVariables);
		return OutputNodeVariables;
	}

	static TArray<const FInputNode*> GetGraphInputNodes(const FGraph* Graph, const UNiagaraGraph::FFindInputNodeOptions& Options)
	{
		TArray<FInputNode*> MutableInputNodes;
		Graph->FindInputNodes(MutableInputNodes, Options);

		TArray<const FInputNode*> InputNodes;
		InputNodes.Reserve(MutableInputNodes.Num());

		Algo::Transform(MutableInputNodes, InputNodes, [](FInputNode* InputNode) -> const FInputNode*
		{
			return InputNode;
		});

		return InputNodes;
	}

	static const FOutputPin* GetLinkedOutputPin(const FInputPin* InputPin)
	{
		if (InputPin && !InputPin->LinkedTo.IsEmpty())
		{
			return InputPin->LinkedTo[0];
		}
		return nullptr;
	}

	static FPinConnectionResponse CanCreateConnection(const FOutputPin* OutputPin, const FInputPin* InputPin)
	{
		return GetDefault<UEdGraphSchema_Niagara>()->CanCreateConnection(OutputPin, InputPin);
	}

	static FGuid GetOutputNodeUsageId(const FOutputNode* OutputNode)
	{
		return OutputNode->GetUsageId();
	}

	static ENiagaraScriptUsage GetOutputNodeScriptType(const FOutputNode* OutputNode)
	{
		return OutputNode->ScriptType;
	}

	static FGuid GetOutputNodeScriptTypeId(const FOutputNode* OutputNode)
	{
		return OutputNode->ScriptTypeId;
	}

	static bool IsGraphEmpty(const FGraph* Graph)
	{
		return Graph->IsEmpty();
	}

	static void AddCollectionPaths(const FParamMapHistory& History, TArray<FString>& Paths)
	{
		for (UNiagaraParameterCollection* Collection : History.ParameterCollections)
		{
			Paths.AddUnique(FSoftObjectPath(Collection).ToString());
		}
	}

	static bool NodeIsEnabled(const FNode* Node)
	{
		return Node->IsNodeEnabled();
	}

	static TOptional<ENiagaraDefaultMode> GraphGetDefaultMode(const FGraph* Graph, const FNiagaraVariableBase& Variable, FNiagaraScriptVariableBinding& Binding)
	{
		return Graph->GetDefaultMode(Variable, &Binding);
	}

	static const FOutputPin* GetSelectOutputPin(const FSelectNode* SelectNode, const FNiagaraVariableBase& Variable)
	{
		return SelectNode->GetOutputPin(Variable);
	}

	static FString GetNodeName(const FNode* Node)
	{
		return Node->GetName();
	}

	static FString GetNodeTitle(const FNode* Node)
	{
		return Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
	}

	static const FInputPin* GetInputPin(const FNode* Node, int32 PinIndex)
	{
		return Node->GetInputPin(PinIndex);
	}

	static int32 GetPinIndexById(TConstArrayView<const FPin*> Pins, const FGuid& PinId)
	{
		const int32 PinCount = Pins.Num();
		for (int32 PinIt = 0; PinIt < PinCount; ++PinIt)
		{
			if (Pins[PinIt]->PinId == PinId)
			{
				return PinIt;
			}
		}
		return INDEX_NONE;
	}

	static void GetCompilationOutputPins(const UNiagaraNode* Node, FPinCollectorArray& Pins)
	{
		AppendCompilationPins(Node, EEdGraphPinDirection::EGPD_Output, Pins);
	}

	static void GetCompilationInputPins(const UNiagaraNode* Node, FPinCollectorArray& Pins)
	{
		AppendCompilationPins(Node, EEdGraphPinDirection::EGPD_Input, Pins);
	}
};
