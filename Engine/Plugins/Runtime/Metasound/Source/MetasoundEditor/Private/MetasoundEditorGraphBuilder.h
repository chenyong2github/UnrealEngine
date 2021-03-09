// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "MetasoundEditorGraphInputNodes.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendController.h"
#include "MetasoundFrontendDocument.h"


// Forward Declarations
class UEdGraphNode;
class UEdGraphPin;
class UMetasound;
class UMetasoundEditorGraphNode;

struct FMetasoundFrontendNodeStyle;


namespace Metasound
{
	// Forward Declarations
	struct FLiteral;

	namespace Editor
	{
		class FGraphBuilder
		{
			static void InitGraphNode(Frontend::FNodeHandle& InNodeHandle, UMetasoundEditorGraphNode* NewGraphNode, UObject& InMetasound);

		public:
			static const FName PinCategoryAudio;
			static const FName PinCategoryBoolean;
			static const FName PinCategoryDouble;
			static const FName PinCategoryFloat;
			static const FName PinCategoryInt32;
			static const FName PinCategoryInt64;
			static const FName PinCategoryObject;
			static const FName PinCategoryString;
			static const FName PinCategoryTrigger;

			// Custom pin-related styles for non-literal types (ex. wire color, pin heads, etc.)
			static const FName PinSubCategoryTime; // Time type

			static const FText ConvertMenuName;
			static const FText FunctionMenuName;

			// Adds an EdGraph node to mirror the provided FNodeHandle.
			static UEdGraphNode* AddNode(UObject& InMetasound, Frontend::FNodeHandle InNodeHandle, bool bInSelectNewNode = true);

			// Adds a node handle to mirror the provided graph node and binds to it.  Does *NOT* mirror existing EdGraph connections
			// nor does it remove existing bound Frontend Node (if set) from associated Frontend Graph.
			static Frontend::FNodeHandle AddNodeHandle(UObject& InMetasound, UMetasoundEditorGraphNode& InGraphNode);

			// Adds an output node to the editor graph that corresponds to the provided node handle.
			// Generates analogous FNodeHandle.
			static UMetasoundEditorGraphInputNode* AddInputNode(UObject& InMetasound, const FString& InName, const FName InTypeName, const FMetasoundFrontendNodeStyle& InNodeStyle, const FText& InToolTip, bool bInSelectNewNode = true);

			// Adds a corresponding UMetasoundEditorGraphInputNode for the provided node handle.
			static UMetasoundEditorGraphInputNode* AddInputNode(UObject& InMetasound, Frontend::FNodeHandle InNodeHandle, bool bInSelectNewNode = true);

			// Generates FNodeHandle for the given external node data. Does not bind or create EdGraph representation of given node.
			static Frontend::FNodeHandle AddInputNodeHandle(UObject& InMetasound, const FString& InName, const FName InTypeName, const FMetasoundFrontendNodeStyle& InNodeStyle, const FText& InToolTip, const FMetasoundFrontendLiteral* InDefaultValue = nullptr);

			// Adds a corresponding UMetasoundEditorGraphExternalNode for the provided node handle.
			static UMetasoundEditorGraphExternalNode* AddExternalNode(UObject& InMetasound, Frontend::FNodeHandle& InNodeHandle, bool bInSelectNewNode = true);

			// Adds an externally-defined node with the given class info to both the editor and document graphs.
			// Generates analogous FNodeHandle.
			static UMetasoundEditorGraphExternalNode* AddExternalNode(UObject& InMetasound, const Frontend::FNodeClassInfo& InClassInfo, const FMetasoundFrontendNodeStyle& InNodeStyle, bool bInSelectNewNode = true);

			// Generates FNodeHandle for the given external node data. Does not bind or create EdGraph representation of given node.
			static Frontend::FNodeHandle AddExternalNodeHandle(UObject& InMetasound, const Frontend::FNodeClassInfo& InClassInfo, const FMetasoundFrontendNodeStyle& InNodeStyle);

			// Adds an output node to the editor graph that corresponds to the provided node handle.
			static UMetasoundEditorGraphOutputNode* AddOutputNode(UObject& InMetasound, Frontend::FNodeHandle& InNodeHandle, bool bInSelectNewNode = true);

			// Adds an output node to the editor graph that corresponds to the provided node handle.
			// Generates analogous FNodeHandle.
			static UMetasoundEditorGraphOutputNode* AddOutputNode(UObject& InMetasound, const FString& InName, const FName InTypeName, const FMetasoundFrontendNodeStyle& InNodeStyle, const FText& InToolTip, bool bInSelectNewNode = true);

			// Generates analogous FNodeHandle for the given internal node data. Does not bind nor create EdGraph representation of given node.
			static Frontend::FNodeHandle AddOutputNodeHandle(UObject& InMetasound, const FString& InName, const FName InTypeName, const FMetasoundFrontendNodeStyle& InNodeStyle, const FText& InToolTip);

			// Attempts to connect frontend node counterparts together for provided pines.  Returns true if succeeded,
			// and breaks pin link and returns false if failed.  If bConnectEdPins is set, will attempt to connect
			// the Editor Graph representation of the pins.
			static bool ConnectNodes(UEdGraphPin& InInputPin, UEdGraphPin& InOutputPin, bool bInConnectEdPins);

			static FString GenerateUniqueInputName(const UObject& InMetasound, const FName InBaseName);

			static FString GenerateUniqueOutputName(const UObject& InMetasound, const FName InBaseName);

			static FString GetDataTypeDisplayName(const FName& InDataTypeName);

			static TArray<FString> GetDataTypeNameCategories(const FName& InDataTypeName);

			// Get the input handle from an input pin.  Ensures pin is an input pin.
			// TODO: use IDs to connect rather than names. Likely need an UMetasoundEditorGraphPin
			static Frontend::FInputHandle GetInputHandleFromPin(const UEdGraphPin* InPin);
			static Frontend::FConstInputHandle GetConstInputHandleFromPin(const UEdGraphPin* InPin);

			// Get the output handle from an output pin.  Ensures pin is an output pin.
			// TODO: use IDs to connect rather than names. Likely need an UMetasoundEditorGraphPin
			static Frontend::FOutputHandle GetOutputHandleFromPin(const UEdGraphPin* InPin);
			static Frontend::FConstOutputHandle GetConstOutputHandleFromPin(const UEdGraphPin* InPin);

			// Adds or updates a hidden input that is set to the default literal value.  If the literal input is added, value defaults to
			// 1. what is set on the input vertex of the node's inputer vertex interface, and if not defined there,
			// 2. the pin's DataType literal default.
			static void AddOrUpdateLiteralInput(UObject& InMetasound, Frontend::FNodeHandle InNodeHandle, UEdGraphPin& InInputPin, bool bForcePinValueAsDefault = false);

			static bool GetPinDefaultLiteral(UEdGraphPin& InInputPin, Frontend::FInputHandle InputHandle, FMetasoundFrontendLiteral& OutLiteralDefault);

			static bool IsRequiredInput(Frontend::FNodeHandle InNodeHandle);
			static bool IsRequiredOutput(Frontend::FNodeHandle InNodeHandle);

			// Constructs graph with default inputs & outputs.
			static void ConstructGraph(UObject& InMetasound);

			// Rebuilds all editor node pins based on the provided node handle's class definition.
			static void RebuildNodePins(UMetasoundEditorGraphNode& InGraphNode);

			// Removes all literal inputs connected to the given node
			static void DeleteLiteralInputs(UEdGraphNode& InNode);

			// Deletes both the editor graph & frontend nodes from respective graphs
			static bool DeleteNode(UEdGraphNode& InNode);

			// Adds an Input UEdGraphPin to a UMetasoundEditorGraphNode
			static UEdGraphPin* AddPinToNode(UMetasoundEditorGraphNode& InEditorNode, Frontend::FInputHandle InInputHandle);

			// Adds an Output UEdGraphPin to a UMetasoundEditorGraphNode
			static UEdGraphPin* AddPinToNode(UMetasoundEditorGraphNode& InEditorNode, Frontend::FOutputHandle InOutputHandle);

			// Adds and removes nodes, pins and connections so that the UEdGraph of the Metasound matches the FMetasoundFrontendDocumentModel
			//
			// @return True if the UEdGraph was altered, false otherwise. 
			static bool SynchronizeGraph(UObject& InMetasound);

			// Adds and removes pins so that the UMetasoundEditorGraphNode matches the InNode.
			//
			// @return True if the UMetasoundEditorGraphNode was altered. False otherwise.
			static bool SynchronizeNodePins(UMetasoundEditorGraphNode& InEditorNode, Frontend::FNodeHandle InNode, bool bRemoveUnusedPins = true);

			// Adds and removes connections so that the UEdGraph of the metasound has the same
			// connections as the FMetasoundFrontendDocument graph.
			//
			// @return True if the UEdGraph was altered. False otherwise. 
			static bool SynchronizeConnections(UObject& InMetasound);

			// Returns true if the FInputHandle and UEdGraphPin match each other. 
			static bool IsMatchingInputHandleAndPin(const Frontend::FInputHandle& InInputHandle, const UEdGraphPin& InEditorPin);

			// Returns true if the FOutputHandle and UEdGraphPin match each other. 
			static bool IsMatchingOutputHandleAndPin(const Frontend::FOutputHandle& InOutputHandle, const UEdGraphPin& InEditorPin);
		};
	} // namespace Editor
} // namespace Metasound
