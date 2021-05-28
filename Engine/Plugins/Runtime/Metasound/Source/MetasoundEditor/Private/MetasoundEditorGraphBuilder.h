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
class UMetaSound;
class UMetasoundEditorGraphNode;

struct FEdGraphPinType;
struct FMetasoundFrontendNodeStyle;


namespace Metasound
{
	// Forward Declarations
	struct FLiteral;

	namespace Editor
	{
		// Forward Declarations
		class FEditor;

		class FGraphBuilder
		{
			static void InitGraphNode(Frontend::FNodeHandle& InNodeHandle, UMetasoundEditorGraphNode* NewGraphNode, UObject& InMetasound);

		public:
			static const FName PinCategoryAudio;
			static const FName PinCategoryBoolean;
			//static const FName PinCategoryDouble;
			static const FName PinCategoryFloat;
			static const FName PinCategoryInt32;
			//static const FName PinCategoryInt64;
			static const FName PinCategoryObject;
			static const FName PinCategoryString;
			static const FName PinCategoryTrigger;

			// Custom pin-related styles for non-literal types (ex. wire color, pin heads, etc.)
			static const FName PinSubCategoryTime; // Time type

			static const FText ConvertMenuName;
			static const FText FunctionMenuName;

			// Adds an EdGraph node to mirror the provided FNodeHandle.
			static UMetasoundEditorGraphNode* AddNode(UObject& InMetasound, Frontend::FNodeHandle InNodeHandle, FVector2D InLocation, bool bInSelectNewNode = true);

			// Convenience functions for retrieving the editor for the given Metasound/EdGraph
			static TSharedPtr<FEditor> GetEditorForGraph(const UObject& Metasound);
			static TSharedPtr<FEditor> GetEditorForGraph(const UEdGraph& EdGraph);

			// Adds a node handle to mirror the provided graph node and binds to it.  Does *NOT* mirror existing EdGraph connections
			// nor does it remove existing bound Frontend Node (if set) from associated Frontend Graph.
			static Frontend::FNodeHandle AddNodeHandle(UObject& InMetasound, UMetasoundEditorGraphNode& InGraphNode);

			// Adds a corresponding UMetasoundEditorGraphInputNode for the provided node handle.
			static UMetasoundEditorGraphInputNode* AddInputNode(UObject& InMetasound, Frontend::FNodeHandle InNodeHandle, FVector2D InLocation, bool bInSelectNewNode = true);

			// Generates FNodeHandle for the given external node data. Does not bind or create EdGraph representation of given node.
			static Frontend::FNodeHandle AddInputNodeHandle(
				UObject& InMetasound,
				const FName InTypeName,
				const FText& InToolTip,
				const FMetasoundFrontendLiteral* InDefaultValue = nullptr);

			// Adds a corresponding UMetasoundEditorGraphExternalNode for the provided node handle.
			static UMetasoundEditorGraphExternalNode* AddExternalNode(UObject& InMetasound, Frontend::FNodeHandle& InNodeHandle, FVector2D InLocation, bool bInSelectNewNode = true);

			// Adds an externally-defined node with the given class info to both the editor and document graphs.
			// Generates analogous FNodeHandle.
			static UMetasoundEditorGraphExternalNode* AddExternalNode(UObject& InMetasound, const FMetasoundFrontendClassMetadata& InMetadata, FVector2D InLocation, bool bInSelectNewNode = true);

			// Synchronizes node location data
			static void SynchronizeNodeLocation(FVector2D InLocation, Frontend::FNodeHandle InNodeHandle, UMetasoundEditorGraphNode& InNode);

			// Adds an output node to the editor graph that corresponds to the provided node handle.
			static UMetasoundEditorGraphOutputNode* AddOutputNode(UObject& InMetasound, Frontend::FNodeHandle& InNodeHandle, FVector2D InLocation, bool bInSelectNewNode = true);

			// Generates analogous FNodeHandle for the given internal node data. Does not bind nor create EdGraph representation of given node.
			static Frontend::FNodeHandle AddOutputNodeHandle(UObject& InMetasound, const FName InTypeName, const FText& InToolTip);

			// Attempts to connect Frontend node counterparts together for provided pins.  Returns true if succeeded,
			// and breaks pin link and returns false if failed.  If bConnectEdPins is set, will attempt to connect
			// the Editor Graph representation of the pins.
			static bool ConnectNodes(UEdGraphPin& InInputPin, UEdGraphPin& InOutputPin, bool bInConnectEdPins);

			// Disconnects pin from any linked input or output nodes, and reflects change
			// in the Frontend graph.  (Handles generation of literal inputs where needed).
			// If bAddLiteralInputs true, will attempt to connect literal inputs where
			// applicable post disconnection.
			static void DisconnectPin(UEdGraphPin& InPin, bool bAddLiteralInputs = true);

			// Generates a unique input display name for the given MetaSound object
			static FText GenerateUniqueInputDisplayName(const UObject& InMetaSound, const FText* InBaseName = nullptr);

			// Generates a unique output name for the given MetaSound object
			static FText GenerateUniqueOutputDisplayName(const UObject& InMetaSound, const FText* InBaseName = nullptr);

			// Generates a unique name by retrieving the provided MetaSound's associated GraphHandle and filtering by the provided function.
			static FText GenerateUniqueNameByFilter(const UObject& InMetaSound, const FText& InBaseText, TUniqueFunction<bool(const Frontend::FConstGraphHandle&, const FText&)> InIsValidNameFilter);

			static TArray<FString> GetDataTypeNameCategories(const FName& InDataTypeName);

			// Get the input handle from an input pin.  Ensures pin is an input pin.
			// TODO: use IDs to connect rather than names. Likely need an UMetasoundEditorGraphPin
			static Frontend::FInputHandle GetInputHandleFromPin(const UEdGraphPin* InPin);
			static Frontend::FConstInputHandle GetConstInputHandleFromPin(const UEdGraphPin* InPin);

			// Get the output handle from an output pin.  Ensures pin is an output pin.
			// TODO: use IDs to connect rather than names. Likely need an UMetasoundEditorGraphPin
			static Frontend::FOutputHandle GetOutputHandleFromPin(const UEdGraphPin* InPin);
			static Frontend::FConstOutputHandle GetConstOutputHandleFromPin(const UEdGraphPin* InPin);

			// Returns the default literal stored on the respective Frontend Node's Input.
			static bool GetPinLiteral(UEdGraphPin& InInputPin, FMetasoundFrontendLiteral& OutLiteralDefault);

			// Deletes Editor Graph Variable's associated Frontend node, as well as any
			// Editor Graph nodes referencing the given variable.
			static void DeleteVariableNodeHandle(UMetasoundEditorGraphVariable& InVariable);

			// Retrieves the proper pin color for the given PinType
			static FLinearColor GetPinCategoryColor(const FEdGraphPinType& PinType);

			// Initializes MetaSound with default inputs & outputs.
			static void InitMetaSound(UObject& InMetaSound, const FString& InAuthor);

			// Rebuilds all editor node pins based on the provided node handle's class definition.
			static void RebuildNodePins(UMetasoundEditorGraphNode& InGraphNode);

			// Deletes both the editor graph & frontend nodes from respective graphs
			static bool DeleteNode(UEdGraphNode& InNode);

			// Adds an Input UEdGraphPin to a UMetasoundEditorGraphNode
			static UEdGraphPin* AddPinToNode(UMetasoundEditorGraphNode& InEditorNode, Frontend::FInputHandle InInputHandle);

			// Adds an Output UEdGraphPin to a UMetasoundEditorGraphNode
			static UEdGraphPin* AddPinToNode(UMetasoundEditorGraphNode& InEditorNode, Frontend::FOutputHandle InOutputHandle);

			// Refreshes pin state from class FrontendClassVertexMetadata
			static void RefreshPinMetadata(UEdGraphPin& InPin, const FMetasoundFrontendVertexMetadata& InMetadata);

			// Adds and removes nodes, pins and connections so that the UEdGraph of the Metasound matches the FMetasoundFrontendDocumentModel
			//
			// @return True if the UEdGraph was altered, false otherwise. 
			static bool SynchronizeGraph(UObject& InMetasound);

			// Adds and removes pins so that the UMetasoundEditorGraphNode matches the InNode.
			//
			// @return True if the UMetasoundEditorGraphNode was altered. False otherwise.
			static bool SynchronizeNodePins(UMetasoundEditorGraphNode& InEditorNode, Frontend::FNodeHandle InNode, bool bRemoveUnusedPins = true, bool bLogChanges = true);

			// Adds and removes connections so that the UEdGraph of the metasound has the same
			// connections as the FMetasoundFrontendDocument graph.
			//
			// @return True if the UEdGraph was altered. False otherwise.
			static bool SynchronizeConnections(UObject& InMetasound);

			// Synchronizes literal for a given input with the EdGraph's pin value.
			static bool SynchronizePinLiteral(UEdGraphPin& InPin);

			// Synchronizes inputs and outputs for the given Metasound.
			//
			// @return True if the UEdGraph was altered. False otherwise.
			static bool SynchronizeVariables(UObject& InMetasound);

			// Returns true if the FInputHandle and UEdGraphPin match each other.
			static bool IsMatchingInputHandleAndPin(const Frontend::FInputHandle& InInputHandle, const UEdGraphPin& InEditorPin);

			// Returns true if the FOutputHandle and UEdGraphPin match each other.
			static bool IsMatchingOutputHandleAndPin(const Frontend::FOutputHandle& InOutputHandle, const UEdGraphPin& InEditorPin);

			// Function signature for visiting a node doing depth first traversal.
			//
			// Functions accept a UEdGraphNode* and return a TSet<UEdGraphNode*> which
			// represent all the children of the node. 
			using FDepthFirstVisitFunction = TFunctionRef<TSet<UEdGraphNode*> (UEdGraphNode*)>;

			// Traverse depth first starting at the InInitialNode and calling the InVisitFunction
			// for each node. 
			//
			// This implementation avoids recursive function calls to support deep
			// graphs.
			static void DepthFirstTraversal(UEdGraphNode* InInitialNode, FDepthFirstVisitFunction InVisitFunction);
		};
	} // namespace Editor
} // namespace Metasound
