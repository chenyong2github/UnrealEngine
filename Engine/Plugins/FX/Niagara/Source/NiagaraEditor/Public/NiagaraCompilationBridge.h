// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraCommon.h"

class FCompileConstantResolver;
class FNiagaraCompileRequestData;
class FNiagaraCompileRequestDuplicateData;
class UEdGraphPin;
class UNiagaraGraph;
class UNiagaraNode;
class UNiagaraNodeConvert;
class UNiagaraNodeCustomHlsl;
class UNiagaraNodeEmitter;
class UNiagaraNodeFunctionCall;
class UNiagaraNodeIf;
class UNiagaraNodeInput;
class UNiagaraNodeOp;
class UNiagaraNodeOutput;
class UNiagaraNodeParameterMapFor;
class UNiagaraNodeParameterMapGet;
class UNiagaraNodeParameterMapSet;
class UNiagaraNodeSelect;
class UNiagaraNodeStaticSwitch;
class UNiagaraParameterCollection;
struct FGraphTraversalHandle;
struct FNiagaraConvertConnection;
struct FNiagaraCustomHlslInclude;
struct FNiagaraFindInputNodeOptions;
struct FNiagaraGraphFunctionAliasContext;
struct FNiagaraStaticVariableSearchContext;
template<typename GraphBridge> class TNiagaraParameterMapHistoryBuilder;
template<typename GraphBridge> struct TNiagaraParameterMapHistory;
template<typename PinType> struct TModuleScopedPin;

struct FNiagaraCompilationGraphBridge
{
	// base types
	using FGraph = UNiagaraGraph;
	using FPin = UEdGraphPin;
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
	using FParamMapHistory = TNiagaraParameterMapHistory<FNiagaraCompilationGraphBridge>;
	using FParamMapHistoryBuilder = TNiagaraParameterMapHistoryBuilder<FNiagaraCompilationGraphBridge>;
	using FPrecompileData = FNiagaraCompileRequestData;
	using FCompilationCopy = FNiagaraCompileRequestDuplicateData;
	using FModuleScopedPin = TModuleScopedPin<FPin>;
	using FGraphTraversalHandle = FGraphTraversalHandle;
	using FGraphFunctionAliasContext = FNiagaraGraphFunctionAliasContext;
	using FConvertConnection = FNiagaraConvertConnection;
	using FParameterCollection = UNiagaraParameterCollection*;
	using FConstantResolver = FCompileConstantResolver;

	// additional data for extending the ParameterMapHistoryBuilder
	struct FParameterCollectionStore
	{
		void Append(const FParameterCollectionStore& Other);
		void Add(UNiagaraParameterCollection* Collection);

		TArray<UNiagaraParameterCollection*> Collections;
		/** Cached off contents of used parameter collections, in case they change during threaded compilation. */
		TArray<TArray<FNiagaraVariable>> CollectionVariables;
		/** Cached off contents of used parameter collections, in case they change during threaded compilation. */
		TArray<FString> CollectionNamespaces;
	};

	struct FAvailableParameterCollections
	{
		UNiagaraParameterCollection* FindCollection(const FNiagaraVariable& Variable) const;
		UNiagaraParameterCollection* FindMatchingCollection(FName VariableName, bool bAllowPartialMatch, FNiagaraVariable& OutVar) const;
	};

	class FBuilderExtraData
	{
	public:
		FBuilderExtraData();
		TUniquePtr<FAvailableParameterCollections> AvailableCollections;
	};

	static const FGraph* GetGraph(const FCompilationCopy* CompilationCopy);
	static const FNode* GetOwningNode(const FPin* Pin);
	static FNode* GetMutableOwningNode(const FPin* Pin);
	static const FGraph* GetOwningGraph(const FNode* Node);
	static bool CustomHlslReferencesTokens(const FCustomHlslNode* CustomNode, TConstArrayView<FStringView> TokenStrings);
	static ENiagaraScriptUsage GetCustomHlslUsage(const FCustomHlslNode* CustomNode);
	static FString GetCustomHlslString(const FCustomHlslNode* CustomNode);
	static void GetCustomHlslIncludePaths(const FCustomHlslNode* CustomNode, TArray<FNiagaraCustomHlslInclude>& Includes);
	static const TArray<FConvertConnection>& GetConvertConnections(const FConvertNode* ConvertNode);

	// various cast functions
	static const FFunctionCallNode* AsFunctionCallNode(const FNode* Node);
	static const FInputNode* AsInputNode(const FNode* Node);
	static const FParamMapGetNode* AsParamMapGetNode(const FNode* Node);
	static const FCustomHlslNode* AsCustomHlslNode(const FNode* Node);
	static const FParamMapSetNode* AsParamMapSetNode(const FNode* Node);

	static bool GraphHasParametersOfType(const FGraph* Graph, const FNiagaraTypeDefinition& TypeDef);
	static TArray<FNiagaraVariableBase> GraphGetStaticSwitchInputs(const FGraph* Graph);
	static void FindOutputNodes(const FGraph* Graph, ENiagaraScriptUsage ScriptUsage, TArray<const FOutputNode*>& OutputNodes);
	static void FindOutputNodes(const FGraph* Graph, TArray<const FOutputNode*>& OutputNodes);
	static void BuildTraversal(const FGraph* Graph, const FNode* OutputNode, TArray<const FNode*>& TraversedNodes);
	static const FGraph* GetEmitterGraph(const FEmitterNode* EmitterNode);
	static FString GetEmitterUniqueName(const FEmitterNode* EmitterNode);
	static ENiagaraScriptUsage GetEmitterUsage(const FEmitterNode* EmitterNode);
	static FString GetEmitterName(const FEmitterNode* EmitterNode);
	static FString GetEmitterPathName(const FEmitterNode* EmitterNode);
	static FString GetEmitterHandleIdString(const FEmitterNode* EmitterNode);
	static const FGraph* GetFunctionNodeGraph(const FFunctionCallNode* FunctionCall);
	static FString GetFunctionFullName(const FFunctionCallNode* FunctionCall);
	static FString GetFunctionScriptName(const FFunctionCallNode* FunctionCall);
	static FString GetFunctionName(const FFunctionCallNode* FunctionCall);
	static ENiagaraScriptUsage GetFunctionUsage(const FFunctionCallNode* FunctionCall);
	static TOptional<ENiagaraDefaultMode> GetGraphDefaultMode(const FGraph* Graph, const FNiagaraVariable& Variable, FNiagaraScriptVariableBinding& Binding);
	static const FInputPin* GetDefaultPin(const FParamMapGetNode* GetNode, const FOutputPin* OutputPin);
	static bool IsStaticPin(const FPin* Pin);
	// retrieves all input pins (excluding any add pins that may be present)
	static TArray<const FInputPin*> GetInputPins(const FNode* Node);
	// retrieves all output pins (excluding both orphaned pins and add pins)
	static TArray<const FOutputPin*> GetOutputPins(const FNode* Node);
	// gets all pins assoicated with the node
	static TArray<const FPin*> GetPins(const FNode* Node);
	static FNiagaraTypeDefinition GetPinType(const FPin* Pin, ENiagaraStructConversion Conversion);
	static FText GetPinFriendlyName(const FPin* Pin);
	static FText GetPinDisplayName(const FPin* Pin);
	static FNiagaraVariable GetPinVariable(const FPin* Pin, bool bNeedsValue, ENiagaraStructConversion Conversion);
	static const FInputPin* GetPinAsInput(const FPin* Pin);
	static FNiagaraVariable GetInputVariable(const FInputNode* InputNode);
	static const TArray<FNiagaraVariable>& GetOutputVariables(const FOutputNode* OutputNode);
	static TArray<FNiagaraVariable> GetGraphOutputNodeVariables(const FGraph* Graph, ENiagaraScriptUsage Usage);
	static TArray<const FInputNode*> GetGraphInputNodes(const FGraph* Graph, const FNiagaraFindInputNodeOptions& Options);
	static const FOutputPin* GetLinkedOutputPin(const FInputPin* InputPin);
	static bool CanCreateConnection(const FOutputPin* OutputPin, const FInputPin* InputPin, FText& FailureMessage);
	static ENiagaraScriptUsage GetOutputNodeUsage(const FOutputNode* OutputNode);
	static FGuid GetOutputNodeUsageId(const FOutputNode* OutputNode);
	static ENiagaraScriptUsage GetOutputNodeScriptType(const FOutputNode* OutputNode);
	static FGuid GetOutputNodeScriptTypeId(const FOutputNode* OutputNode);
	static bool IsGraphEmpty(const FGraph* Graph);
	static void AddCollectionPaths(const FParamMapHistory& History, TArray<FString>& Paths);
	static bool NodeIsEnabled(const FNode* Node);
	static TOptional<ENiagaraDefaultMode> GraphGetDefaultMode(const FGraph* Graph, const FNiagaraVariableBase& Variable, FNiagaraScriptVariableBinding& Binding);
	static const FOutputPin* GetSelectOutputPin(const FSelectNode* SelectNode, const FNiagaraVariableBase& Variable);
	static FString GetNodeName(const FNode* Node);
	static FString GetNodeTitle(const FNode* Node);
	static const FInputPin* GetInputPin(const FNode* Node, int32 PinIndex);
	static int32 GetPinIndexById(TConstArrayView<const FPin*> Pins, const FGuid& PinId);
	static FString GetCollectionFullName(FParameterCollection Collection);
	static bool IsCollectionValid(FParameterCollection Collection);
	static UNiagaraDataInterface* GetCollectionDataInterface(FParameterCollection Collection, const FNiagaraVariable& Variable);
	static UObject* GetCollectionUObject(FParameterCollection Collection, const FNiagaraVariable& Variable);

	static const FOutputNode* AsOutputNode(const FNode* Node);
	static bool IsParameterMapGet(const FNode* Node);
	static TOptional<FName> GetOutputNodeStackContextOverride(const FOutputNode* OutputNode);
	static FString GetNodeClassName(const FNode* Node);
	static bool IsParameterMapPin(const FPin* Pin);
	static bool GetGraphReferencesStaticVariables(const FGraph* Graph, FNiagaraStaticVariableSearchContext& StaticVariableContext);
	static const FEmitterNode* GetNodeAsEmitter(const FNode* Node);
};
