// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
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
		public:
			static const FName PinCategoryAudioFormat;  // Audio formats (ex. Buffers, Mono, Stereo)
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

			// Adds a node to the editor graph that corresponds to the provided node handle.
			static UEdGraphNode* AddNode(UObject& InMetasound, Frontend::FNodeHandle& InNodeHandle, bool bInSelectNewNode = true);

			// Adds a node with the given class info to both the editor and document graphs
			static UEdGraphNode* AddNode(UObject& InMetasound, const Frontend::FNodeClassInfo& InClassInfo, const FMetasoundFrontendNodeStyle& InNodeStyle, bool bInSelectNewNode = true);

			// Adds a node handle with the given class and style info
			static Frontend::FNodeHandle AddNodeHandle(UObject& InMetasound, const Frontend::FNodeClassInfo& InClassInfo, const FMetasoundFrontendNodeStyle& InNodeStyle);

			// Attempts to connect graph nodes together.  Returns true if succeeded, breaks pin link and returns false if failed.
			// If bConnectEdPins is set, will attempt to connect the Editor Graph representation of the pins.
			static bool ConnectNodes(UEdGraphPin& InInputPin, UEdGraphPin& InOutputPin, bool bConnectEdPins);

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

			static UEdGraphNode* AddInput(UObject& InMetasound, const FString& InName, const FName InTypeName, const FMetasoundFrontendNodeStyle& InNodeStyle, const FText& InToolTip, bool bInSelectNewNode = true);

			// Adds or updates a hidden input that is set to the default literal value.  If the literal input is added, value defaults to
			// 1. what is set on the input vertex of the node's inputer vertex interface, and if not defined there,
			// 2. the pin's DataType literal default.
			static void AddOrUpdateLiteralInput(UObject& InMetasound, Frontend::FNodeHandle InNodeHandle, UEdGraphPin& InInputPin);

			// Adds an input node handle with the given class info
			static Frontend::FNodeHandle AddInputNodeHandle(UObject& InMetasound, const FString& InName, const FName InTypeName, const FMetasoundFrontendNodeStyle& InNodeStyle, const FText& InToolTip, const FMetasoundFrontendLiteral* InDefaultValue = nullptr);

			static UEdGraphNode* AddOutput(UObject& InMetasound, const FString& InName, const FName InTypeName, const FMetasoundFrontendNodeStyle& InNodeStyle, const FText& InToolTip, bool bInSelectNewNode = true);

			// Adds an output node handle with the given class info
			static Frontend::FNodeHandle AddOutputNodeHandle(UObject& InMetasound, const FString& InName, const FName InTypeName, const FMetasoundFrontendNodeStyle& InNodeStyle, const FText& InToolTip);

			// Constructs graph with default inputs & outputs.
			static void ConstructGraph(UObject& InMetasound);

			static void RebuildNodePins(UMetasoundEditorGraphNode& InGraphNode, Frontend::FNodeHandle InNodeHandle);

			// Removes all literal inputs connected to the given node
			static void DeleteLiteralInputs(UEdGraphNode& InNode);

			// Deletes both the editor graph & frontend nodes from respective graphs
			static bool DeleteNode(UEdGraphNode& InNode, bool bInRecordTransaction);

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
			static bool SynchronizeNodePins(UMetasoundEditorGraphNode& InEditorNode, Frontend::FNodeHandle InNode);

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
