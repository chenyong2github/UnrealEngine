// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundEditorGraphBuilder.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Metasound.h"
#include "MetasoundEditorGraph.h"
#include "MetasoundEditorGraphNode.h"
#include "MetasoundFrontend.h"
#include "ScopedTransaction.h"
#include "Templates/Tuple.h"
#include "MetasoundEditorModule.h"
#include "Modules/ModuleManager.h"


#define LOCTEXT_NAMESPACE "MetasoundEditor"


namespace Metasound
{
	namespace Editor
	{
		const FName FGraphBuilder::PinPrimitiveBoolean = "Boolean";
		const FName FGraphBuilder::PinPrimitiveFloat = "Float";
		const FName FGraphBuilder::PinPrimitiveInteger = "Int";
		const FName FGraphBuilder::PinPrimitiveString = "String";

		UEdGraphNode* FGraphBuilder::AddNode(UMetasound& InMetasound, const FVector2D& Location, Frontend::FNodeHandle& InNodeHandle, bool bInSelectNewNode)
		{
			const FScopedTransaction Transaction(LOCTEXT("AddMetasoundGraphNode", "Add Metasound Node"));

			const FString EdNodeName = InNodeHandle.GetNodeClassName() + TEXT("_") + FString::FromInt(InNodeHandle.GetNodeID());

			UEdGraph& Graph = InMetasound.GetGraphChecked();
			FGraphNodeCreator<UMetasoundEditorGraphNode> NodeCreator(Graph);
			UMetasoundEditorGraphNode* NewGraphNode = NodeCreator.CreateNode(bInSelectNewNode);
			NodeCreator.Finalize();

			NewGraphNode->SetNodeID(InNodeHandle.GetNodeID());
			NewGraphNode->CreateNewGuid();
			NewGraphNode->NodePosX = Location.X;
			NewGraphNode->NodePosY = Location.Y;

			RebuildNodePins(*NewGraphNode, InNodeHandle);

			InMetasound.PostEditChange();
			InMetasound.MarkPackageDirty();

			return NewGraphNode;
		}

		UEdGraphNode* FGraphBuilder::AddNode(UMetasound& InMetasound, const FVector2D& Location, const Frontend::FNodeClassInfo& InClassInfo, bool bInSelectNewNode)
		{
			Frontend::FNodeHandle NodeHandle = InMetasound.GetRootGraphHandle().AddNewNode(InClassInfo);
			return AddNode(InMetasound, Location, NodeHandle, bInSelectNewNode);
		}

		UEdGraphNode* FGraphBuilder::AddInput(UMetasound& InMetasound, const FVector2D& Location, const FString& InName, const FName InTypeName, const FText& InToolTip, bool bInSelectNewNode)
		{
			FMetasoundInputDescription Description;
			Description.Name = InName;
			Description.TypeName = InTypeName;
			Description.ToolTip = InToolTip;

			Frontend::FNodeHandle NodeHandle = InMetasound.GetRootGraphHandle().AddNewInput(Description);
			return AddNode(InMetasound, Location, NodeHandle, bInSelectNewNode);
		}

		UEdGraphNode* FGraphBuilder::AddOutput(UMetasound& InMetasound, const FVector2D& Location, const FString& InName, const FName InTypeName, const FText& InToolTip, bool bInSelectNewNode)
		{
			FMetasoundOutputDescription Description;
			Description.Name = InName;
			Description.TypeName = InTypeName;
			Description.ToolTip = InToolTip;

			Frontend::FNodeHandle NodeHandle = InMetasound.GetRootGraphHandle().AddNewOutput(Description);
			return AddNode(InMetasound, Location, NodeHandle, bInSelectNewNode);
		}

		void FGraphBuilder::DeleteNode(UMetasoundEditorGraphNode& InNode, bool bInRecordTransaction)
		{
			const FScopedTransaction Transaction(LOCTEXT("DeleteMetasoundGraphNode", "Delete Metasound Node"), bInRecordTransaction);

			UMetasoundEditorGraph* Graph = CastChecked<UMetasoundEditorGraph>(InNode.GetGraph());
			if (InNode.CanUserDeleteNode() && Graph->RemoveNode(&InNode))
			{
				Graph->PostEditChange();
				Graph->MarkPackageDirty();
			}

			const Frontend::FNodeHandle& NodeHandle = InNode.GetNodeHandle();
			Frontend::FGraphHandle GraphHandle = Graph->GetMetasoundChecked().GetRootGraphHandle();
			if (GraphHandle.IsValid() && NodeHandle.IsValid())
			{
				GraphHandle.RemoveNode(NodeHandle, true /* bEvenIfInputOrOutputNode */);
			}

			InNode.PostEditChange();
			InNode.MarkPackageDirty();
		}

		void FGraphBuilder::RebuildGraph(UMetasound& InMetasound)
		{
			using namespace Frontend;

			FGraphHandle GraphHandle = InMetasound.GetRootGraphHandle();
			UMetasoundEditorGraph* Graph = CastChecked<UMetasoundEditorGraph>(InMetasound.GetGraph());

			Graph->Nodes.Reset();

			// TODO: Space graph nodes in a readable way
			FVector2D Location = FVector2D::ZeroVector;

			struct FNodePair
			{
				FNodeHandle NodeHandle = FNodeHandle::InvalidHandle();
				UEdGraphNode* GraphNode = nullptr;
			};

			TMap<uint32, FNodePair> NewIdNodeMap;
			TArray<FNodeHandle> NodeHandles = GraphHandle.GetAllNodes();
			for (FNodeHandle& NodeHandle : NodeHandles)
			{
				const FNodeClassInfo ClassInfo = NodeHandle.GetClassInfo();
				UEdGraphNode* NewNode = AddNode(InMetasound, Location, NodeHandle, false /* bInSelectNewNode */);
				NewIdNodeMap.Add(NodeHandle.GetNodeID(), FNodePair { NodeHandle, NewNode });
			}

			for (const TPair<uint32, FNodePair>& IdNodePair : NewIdNodeMap)
			{
				UEdGraphNode* GraphNode = IdNodePair.Value.GraphNode;
				check(GraphNode);

				FNodeHandle NodeHandle = IdNodePair.Value.NodeHandle;
				TArray<UEdGraphPin*> Pins = GraphNode->GetAllPins();

				const TArray<FInputHandle> NodeInputs = NodeHandle.GetAllInputs();

				int32 InputIndex = 0;
				for (UEdGraphPin* Pin : Pins)
				{
					switch (Pin->Direction)
					{
						case EEdGraphPinDirection::EGPD_Input:
						{
							FOutputHandle OutputHandle = NodeInputs[InputIndex].GetCurrentlyConnectedOutput();
							if (OutputHandle.IsValid())
							{
								UEdGraphNode* OutputGraphNode = NewIdNodeMap.FindChecked(OutputHandle.GetOwningNodeID()).GraphNode;
								UEdGraphPin* OutputPin = OutputGraphNode->FindPinChecked(OutputHandle.GetOutputName(), EEdGraphPinDirection::EGPD_Output);
								Pin->MakeLinkTo(OutputPin);
							}

							InputIndex++;
						}
						break;

						case EEdGraphPinDirection::EGPD_Output:
							// Do nothing.  Connecting all inputs will naturally connect all outputs where required
						break;
					}
				}
			}

			InMetasound.PostEditChange();
			InMetasound.MarkPackageDirty();
		}

		void FGraphBuilder::RebuildNodePins(UMetasoundEditorGraphNode& InGraphNode, Frontend::FNodeHandle& InNodeHandle, bool bInRecordTransaction)
		{
			const FScopedTransaction Transaction(LOCTEXT("RebuildMetasoundGraphNodePins", "Rebuild Metasound Pins"), bInRecordTransaction);

			InGraphNode.Pins.Reset();

			IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetasoundEditor");

			TArray<Frontend::FInputHandle> InputHandles = InNodeHandle.GetAllInputs();
			for (int32 i = 0; i < InputHandles.Num(); ++i)
			{
				const Frontend::FInputHandle& InputHandle = InputHandles[i];
				FEdGraphPinType PinType("MetasoundNode", NAME_None, nullptr, EPinContainerType::None, false, FEdGraphTerminalType());
				UEdGraphPin* NewPin = InGraphNode.CreatePin(EGPD_Input, PinType, *InputHandle.GetInputName(), i);
				if (ensureAlways(NewPin))
				{
					NewPin->PinToolTip = InputHandle.GetInputTooltip().ToString();

					FEditorDataType DataType = EditorModule.FindDataType(InputHandle.GetInputType());
					NewPin->PinType = DataType.PinType;
				}
			}

			TArray<Frontend::FOutputHandle> OutputHandles = InNodeHandle.GetAllOutputs();
			for (int32 i = 0; i < OutputHandles.Num(); ++i)
			{
				const Frontend::FOutputHandle& OutputHandle = OutputHandles[i];
				FEdGraphPinType PinType("MetasoundNode", NAME_None, nullptr, EPinContainerType::None, false, FEdGraphTerminalType());
				UEdGraphPin* NewPin = InGraphNode.CreatePin(EGPD_Output, PinType, *OutputHandle.GetOutputName(), i);
				if (ensureAlways(NewPin))
				{
					NewPin->PinToolTip = OutputHandle.GetOutputTooltip().ToString();

					const FName OutputType = OutputHandle.GetOutputType();
					FEditorDataType DataType = EditorModule.FindDataType(OutputType);
					NewPin->PinType = DataType.PinType;
				}
			}

			InGraphNode.MarkPackageDirty();
		}
	} // namespace Editor
} // namespace Metasound
#undef LOCTEXT_NAMESPACE