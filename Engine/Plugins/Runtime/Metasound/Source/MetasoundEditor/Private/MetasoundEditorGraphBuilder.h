// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MetasoundFrontend.h"

// Forward Declarations
class UEdGraphNode;
class UMetasound;
class UMetasoundEditorGraphNode;

namespace Metasound
{
	namespace Frontend
	{
		// Forward Declarations
		class FNodeHandle;
		struct FNodeClassInfo;
	} // namespace Frontend

	namespace Editor
	{
		class FGraphBuilder
		{
		public:
			static const FName PinPrimitiveBoolean;
			static const FName PinPrimitiveFloat;
			static const FName PinPrimitiveInteger;
			static const FName PinPrimitiveString;
			static const FName PinPrimitiveUObject;
			static const FName PinPrimitiveUObjectArray;

			// Adds a node to the editor graph that corresponds to the provided node handle.
			static UEdGraphNode* AddNode(UObject& InMetasound, const FVector2D& Location, Frontend::FNodeHandle& InNodeHandle, bool bInSelectNewNode = true);

			// Adds a node with the given class info to both the editor and document graphs
			static UEdGraphNode* AddNode(UObject& InMetasound, const FVector2D& Location, const Frontend::FNodeClassInfo& InClassInfo, bool bInSelectNewNode = true);

			// Adds a node handle with the given class info
			static Frontend::FNodeHandle AddNodeHandle(UObject& InMetasound, const Frontend::FNodeClassInfo& InClassInfo);

			static FString GenerateUniqueInputName(const UObject& InMetasound, const FName InBaseName);

			static FString GenerateUniqueOutputName(const UObject& InMetasound, const FName InBaseName);

			static FString GetDataTypeDisplayName(const FName& InDataTypeName);

			static TArray<FString> GetDataTypeNameCategories(const FName& InDataTypeName);

			static UEdGraphNode* AddInput(UObject& InMetasound, const FVector2D& Location, const FString& InName, const FName InTypeName, const FText& InToolTip, bool bInSelectNewNode = true);

			// Adds an input node handle with the given class info
			static Frontend::FNodeHandle AddInputNodeHandle(UObject& InMetasound, const FString& InName, const FName InTypeName, const FText& InToolTip);

			static UEdGraphNode* AddOutput(UObject& InMetasound, const FVector2D& Location, const FString& InName, const FName InTypeName, const FText& InToolTip, bool bInSelectNewNode = true);

			// Adds an output node handle with the given class info
			static Frontend::FNodeHandle AddOutputNodeHandle(UObject& InMetasound, const FString& InName, const FName InTypeName, const FText& InToolTip);

			static void DeleteNode(UMetasoundEditorGraphNode& InNode, bool bInRecordTransaction = true);

			static void RebuildGraph(UObject& InMetasound);

			static void RebuildNodePins(UMetasoundEditorGraphNode& InGraphNode, Frontend::FNodeHandle& InNodeHandle, bool bInRecordTransaction = true);
		};
	} // namespace Editor
} // namespace Metasound
