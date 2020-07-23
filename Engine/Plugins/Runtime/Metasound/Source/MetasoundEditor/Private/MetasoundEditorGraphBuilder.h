// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

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

			// Adds a node to the editor graph that corresponds to the provided node handle.
			static UEdGraphNode* AddNode(UMetasound& InMetasound, const FVector2D& Location, Frontend::FNodeHandle& InNodeHandle, bool bInSelectNewNode = true);

			// Adds a node with the given class info to both the editor and document graphs
			static UEdGraphNode* AddNode(UMetasound& InMetasound, const FVector2D& Location, const Frontend::FNodeClassInfo& InClassInfo, bool bInSelectNewNode = true);

			static UEdGraphNode* AddInput(UMetasound& InMetasound, const FVector2D& Location, const FString& InName, const FName InTypeName, const FText& InToolTip, bool bInSelectNewNode = true);

			static UEdGraphNode* AddOutput(UMetasound& InMetasound, const FVector2D& Location, const FString& InName, const FName InTypeName, const FText& InToolTip, bool bInSelectNewNode = true);

			static void DeleteNode(UMetasoundEditorGraphNode& InNode, bool bInRecordTransaction = true);

			static void RebuildGraph(UMetasound& InMetasound);

			static void RebuildNodePins(UMetasoundEditorGraphNode& InGraphNode, Frontend::FNodeHandle& InNodeHandle, bool bInRecordTransaction = true);
		};
	} // namespace Editor
} // namespace Metasound
