// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundEditorGraphBuilder.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Metasound.h"
#include "MetasoundAssetBase.h"
#include "MetasoundEditorGraph.h"
#include "MetasoundEditorGraphNode.h"
#include "MetasoundEditorModule.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundUObjectRegistry.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"
#include "Templates/Tuple.h"


#define LOCTEXT_NAMESPACE "MetasoundEditor"


namespace Metasound
{
	namespace Editor
	{
		const FName FGraphBuilder::PinPrimitiveBoolean = TEXT("Boolean");
		const FName FGraphBuilder::PinPrimitiveFloat = TEXT("Float");
		const FName FGraphBuilder::PinPrimitiveInteger = TEXT("Int");
		const FName FGraphBuilder::PinPrimitiveString = TEXT("String");
		const FName FGraphBuilder::PinPrimitiveUObject = TEXT("UObject");
		const FName FGraphBuilder::PinPrimitiveUObjectArray = TEXT("UObjectArray");

		UEdGraphNode* FGraphBuilder::AddNode(UObject& InMetasound, const FVector2D& Location, Frontend::FNodeHandle& InNodeHandle, bool bInSelectNewNode)
		{
			const FScopedTransaction Transaction(LOCTEXT("AddMetasoundGraphNode", "Add Metasound Node"));

			FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetasound);
			check(MetasoundAsset);

			UEdGraph& Graph = MetasoundAsset->GetGraphChecked();
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

		UEdGraphNode* FGraphBuilder::AddNode(UObject& InMetasound, const FVector2D& Location, const Frontend::FNodeClassInfo& InClassInfo, bool bInSelectNewNode)
		{
			Frontend::FNodeHandle NodeHandle = AddNodeHandle(InMetasound, InClassInfo);
			return AddNode(InMetasound, Location, NodeHandle, bInSelectNewNode);
		}

		Frontend::FNodeHandle FGraphBuilder::AddNodeHandle(UObject& InMetasound, const Frontend::FNodeClassInfo& InClassInfo)
		{
			FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetasound);
			check(MetasoundAsset);

			return MetasoundAsset->GetRootGraphHandle().AddNewNode(InClassInfo);
		}

		FString FGraphBuilder::GetDataTypeDisplayName(const FName& InDataTypeName)
		{
			FString CategoryString = InDataTypeName.ToString();
			int32 Index = 0;
			CategoryString.FindLastChar(':', Index);

			return CategoryString.RightChop(Index + 1);
		}

		TArray<FString> FGraphBuilder::GetDataTypeNameCategories(const FName& InDataTypeName)
		{
			FString CategoryString = InDataTypeName.ToString();

			TArray<FString> Categories;
			CategoryString.ParseIntoArray(Categories, TEXT(":"));

			if (Categories.Num() > 0)
			{
				// Remove name
				Categories.RemoveAt(Categories.Num() - 1);
			}

			return Categories;
		}

		FString FGraphBuilder::GenerateUniqueInputName(UObject& InMetasound, const FName InBaseName)
		{
			FString NameBase = GetDataTypeDisplayName(InBaseName);
			FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetasound);
			check(MetasoundAsset);

			// TODO: To achieve const correctness, this needs a FConstGraphHandle.
			const Frontend::FGraphHandle GraphHandle = MetasoundAsset->GetRootGraphHandle();

			int32 i = 1;
			FString NewNodeName = NameBase + FString::Printf(TEXT("_%02d"), i);
			while (GraphHandle.ContainsInputNodeWithName(NewNodeName))
			{
				NewNodeName = NameBase + FString::Printf(TEXT("_%02d"), ++i);
			}

			return NewNodeName;
		}

		FString FGraphBuilder::GenerateUniqueOutputName(UObject& InMetasound, const FName InBaseName)
		{
			FString NameBase = GetDataTypeDisplayName(InBaseName);
			FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetasound);
			check(MetasoundAsset);

			// TODO: To achieve const correctness, this needs a FConstGraphHandle.
			const Frontend::FGraphHandle GraphHandle = MetasoundAsset->GetRootGraphHandle();

			int32 i = 1;
			FString NewNodeName = NameBase + FString::Printf(TEXT("_%02d"), i);
			while (GraphHandle.ContainsOutputNodeWithName(NewNodeName))
			{
				NewNodeName = NameBase + FString::Printf(TEXT("_%02d"), ++i);
			}

			return NewNodeName;
		}

		UEdGraphNode* FGraphBuilder::AddInput(UObject& InMetasound, const FVector2D& Location, const FString& InName, const FName InTypeName, const FText& InToolTip, bool bInSelectNewNode)
		{
			Frontend::FNodeHandle NodeHandle = AddInputNodeHandle(InMetasound, InName, InTypeName, InToolTip);
			return AddNode(InMetasound, Location, NodeHandle, bInSelectNewNode);
		}

		Frontend::FNodeHandle FGraphBuilder::AddInputNodeHandle(UObject& InMetasound, const FString& InName, const FName InTypeName, const FText& InToolTip)
		{
			FMetasoundInputDescription Description;
			Description.Name = InName;
			Description.TypeName = InTypeName;
			Description.ToolTip = InToolTip;

			FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetasound);
			check(MetasoundAsset);

			Frontend::FGraphHandle GraphHandle = MetasoundAsset->GetRootGraphHandle();
			Frontend::FNodeHandle NodeHandle = GraphHandle.AddNewInput(Description);
			if (!NodeHandle.IsValid())
			{
				return Frontend::FNodeHandle::InvalidHandle();
			}

			IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetasoundEditor");
			FEditorDataType DataType = EditorModule.FindDataType(InTypeName);

			Metasound::FDataTypeLiteralParam LiteralParam = Frontend::GetDefaultParamForDataType(InTypeName);
			if (!ensureAlways(LiteralParam.IsValid()))
			{
				return Frontend::FNodeHandle::InvalidHandle();
			}

			switch (LiteralParam.ConstructorArgType)
			{
				case ELiteralArgType::Boolean:
				{
					GraphHandle.SetInputToLiteral(InName, false);
				}
				break;

				case ELiteralArgType::Float:
				{
					GraphHandle.SetInputToLiteral(InName, 0.0f);
				}
				break;

				case ELiteralArgType::Integer:
				{
					GraphHandle.SetInputToLiteral(InName, 0);
				}
				break;

				case ELiteralArgType::String:
				{
					GraphHandle.SetInputToLiteral(InName, FString(TEXT("")));
				}
				break;

				case ELiteralArgType::UObjectProxy:
				{
					UClass* ClassToUse = FMetasoundFrontendRegistryContainer::Get()->GetLiteralUClassForDataType(InTypeName);
					if (ClassToUse)
					{
						GraphHandle.SetInputToLiteral(InName, ClassToUse->ClassDefaultObject);
					}
				}
				break;

				case ELiteralArgType::UObjectProxyArray:
				{
					UClass* ClassToUse = FMetasoundFrontendRegistryContainer::Get()->GetLiteralUClassForDataType(InTypeName);
					if (ClassToUse)
					{
						TArray<UObject*> ObjectArray;
						ObjectArray.Add(ClassToUse->ClassDefaultObject);
						GraphHandle.SetInputToLiteral(InName, ObjectArray);
					}
					GraphHandle.SetInputToLiteral(InName, FString(TEXT("")));
				}
				break;

				case ELiteralArgType::Invalid:
				case ELiteralArgType::None:
				default:
				{
					static_assert(static_cast<int32>(ELiteralArgType::Invalid) == 7, "Possible missing ELiteralArgType case coverage");
				}
				break;
			}

			GraphHandle.SetInputDisplayName(InName, FText::FromString(InName));
			return NodeHandle;
		}

		UEdGraphNode* FGraphBuilder::AddOutput(UObject& InMetasound, const FVector2D& Location, const FString& InName, const FName InTypeName, const FText& InToolTip, bool bInSelectNewNode)
		{
			Frontend::FNodeHandle NodeHandle = AddOutputNodeHandle(InMetasound, InName, InTypeName, InToolTip);
			return AddNode(InMetasound, Location, NodeHandle, bInSelectNewNode);
		}

		Frontend::FNodeHandle FGraphBuilder::AddOutputNodeHandle(UObject& InMetasound, const FString& InName, const FName InTypeName, const FText& InToolTip)
		{
			FMetasoundOutputDescription Description;
			Description.Name = InName;
			Description.TypeName = InTypeName;
			Description.ToolTip = InToolTip;

			FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetasound);
			check(MetasoundAsset);

			Frontend::FGraphHandle GraphHandle = MetasoundAsset->GetRootGraphHandle();
			Frontend::FNodeHandle NodeHandle = GraphHandle.AddNewOutput(Description);

			GraphHandle.SetOutputDisplayName(InName, FText::FromString(InName));

			return NodeHandle;
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

			FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(Graph->GetMetasound());
			check(MetasoundAsset);

			Frontend::FGraphHandle GraphHandle = MetasoundAsset->GetRootGraphHandle();
			if (GraphHandle.IsValid() && NodeHandle.IsValid())
			{
				switch (NodeHandle.GetNodeType())
				{
					case EMetasoundClassType::Input:
					{
						GraphHandle.RemoveInput(NodeHandle.GetNodeName());
					}
					break;

					case EMetasoundClassType::Output:
					{
						GraphHandle.RemoveOutput(NodeHandle.GetNodeName());
					}
					break;

					case EMetasoundClassType::External:
					{
						GraphHandle.RemoveNode(NodeHandle);
					}
					break;
				}
			}

			InNode.PostEditChange();
			InNode.MarkPackageDirty();
		}

		void FGraphBuilder::RebuildGraph(UObject& InMetasound)
		{
			using namespace Frontend;

			FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetasound);
			check(MetasoundAsset);

			FGraphHandle GraphHandle = MetasoundAsset->GetRootGraphHandle();
			UMetasoundEditorGraph* Graph = CastChecked<UMetasoundEditorGraph>(MetasoundAsset->GetGraph());

			Graph->Nodes.Reset();

			// TODO: Space graph nodes in a procedural and readable way
			FVector2D InputNodeLocation = FVector2D::ZeroVector;
			FVector2D OpNodeLocation = FVector2D(250.0f, 0.0f);
			FVector2D OutputNodeLocation = FVector2D(500.0f, 0.0f);

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
				FVector2D Location;
				if (ClassInfo.NodeType == EMetasoundClassType::Input)
				{
					Location = InputNodeLocation;
					InputNodeLocation.Y += 100.0f;
				}
				else if (ClassInfo.NodeType == EMetasoundClassType::Output)
				{
					Location = OutputNodeLocation;
					OutputNodeLocation.Y += 100.0f;
				}
				else
				{
					Location = OpNodeLocation;
					OpNodeLocation.Y += 100.0f;
				}
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

			for (int32 i = InGraphNode.Pins.Num() - 1; i >= 0; i--)
			{
				InGraphNode.RemovePin(InGraphNode.Pins[i]);
			}

			IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetasoundEditor");

			FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InGraphNode.GetMetasoundChecked());
			check(MetasoundAsset);

			Frontend::FGraphHandle GraphHandle = MetasoundAsset->GetRootGraphHandle();

			TArray<Frontend::FInputHandle> InputHandles = InNodeHandle.GetAllInputs();
			for (int32 i = 0; i < InputHandles.Num(); ++i)
			{
				AddPinToNode(InGraphNode, InputHandles[i]);
			}

			TArray<Frontend::FOutputHandle> OutputHandles = InNodeHandle.GetAllOutputs();
			for (int32 i = 0; i < OutputHandles.Num(); ++i)
			{
				AddPinToNode(InGraphNode, OutputHandles[i]);
			}

			InGraphNode.MarkPackageDirty();
		}

		bool FGraphBuilder::IsMatchingInputHandleAndPin(const Frontend::FInputHandle& InInputHandle, const UEdGraphPin& InEditorPin)
		{
			if (EEdGraphPinDirection::EGPD_Input == InEditorPin.Direction)
			{
				if (InEditorPin.GetName() == InInputHandle.GetInputName())
				{
					IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetasoundEditor");

					if (InEditorPin.PinType == EditorModule.FindDataType(InInputHandle.GetInputType()).PinType)
					{
						UEdGraphNode* EditorNode = InEditorPin.GetOwningNode();
						UMetasoundEditorGraphNode* MetasoundEditorNode = Cast<UMetasoundEditorGraphNode>(EditorNode);

						if (nullptr != MetasoundEditorNode)
						{
							if  (MetasoundEditorNode->GetNodeHandle().GetNodeID() == InInputHandle.GetOwningNodeID())
							{
								return true;
							}
						}
					}
				}
			}
			return false;
		}

		bool FGraphBuilder::IsMatchingOutputHandleAndPin(const Frontend::FOutputHandle& InOutputHandle, const UEdGraphPin& InEditorPin)
		{
			if (EEdGraphPinDirection::EGPD_Output == InEditorPin.Direction)
			{
				if (InEditorPin.GetName() == InOutputHandle.GetOutputName())
				{
					IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetasoundEditor");

					if (InEditorPin.PinType == EditorModule.FindDataType(InOutputHandle.GetOutputType()).PinType)
					{
						UEdGraphNode* EditorNode = InEditorPin.GetOwningNode();
						UMetasoundEditorGraphNode* MetasoundEditorNode = Cast<UMetasoundEditorGraphNode>(EditorNode);

						if (nullptr != MetasoundEditorNode)
						{
							if  (MetasoundEditorNode->GetNodeHandle().GetNodeID() == InOutputHandle.GetOwningNodeID())
							{
								return true;
							}
						}
					}
				}
			}
			return false;
		}

		UEdGraphPin* FGraphBuilder::AddPinToNode(UMetasoundEditorGraphNode& InEditorNode, Frontend::FInputHandle& InInputHandle)
		{
			IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetasoundEditor");

			FEdGraphPinType PinType("MetasoundNode", NAME_None, nullptr, EPinContainerType::None, false, FEdGraphTerminalType());
			UEdGraphPin* NewPin = InEditorNode.CreatePin(EGPD_Input, PinType, FName(*InInputHandle.GetInputName()));

			if (ensureAlways(NewPin))
			{
				NewPin->PinToolTip = InInputHandle.GetInputTooltip().ToString();
				NewPin->PinType = EditorModule.FindDataType(InInputHandle.GetInputType()).PinType;
			}

			return NewPin;
		}

		UEdGraphPin* FGraphBuilder::AddPinToNode(UMetasoundEditorGraphNode& InEditorNode, Frontend::FOutputHandle& InOutputHandle)
		{
			IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetasoundEditor");
			FText DisplayName;

			FEdGraphPinType PinType("MetasoundNode", NAME_None, nullptr, EPinContainerType::None, false, FEdGraphTerminalType());
			UEdGraphPin* NewPin = InEditorNode.CreatePin(EGPD_Output, PinType, FName(*InOutputHandle.GetOutputName()));

			if (ensureAlways(NewPin))
			{
				NewPin->PinToolTip = InOutputHandle.GetOutputTooltip().ToString();
				NewPin->PinType = EditorModule.FindDataType(InOutputHandle.GetOutputType()).PinType;
			}

			return NewPin;
		}

		bool FGraphBuilder::SynchronizeGraph(UObject& InMetasound)
		{
			using namespace Frontend;

			bool bIsEditorGraphDirty = false;

			FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetasound);
			check(MetasoundAsset);

			// Get all nodes from metasound graph
			FGraphHandle GraphHandle = MetasoundAsset->GetRootGraphHandle();
			TArray<FNodeHandle> Nodes = GraphHandle.GetAllNodes();

			// Get all nodes from metasound editor graph
			UMetasoundEditorGraph* EditorGraph = CastChecked<UMetasoundEditorGraph>(MetasoundAsset->GetGraph());
			TArray<UMetasoundEditorGraphNode*> EditorNodes;
			for (UEdGraphNode* EdGraphNode : EditorGraph->Nodes)
			{
				UMetasoundEditorGraphNode* EditorNode = Cast<UMetasoundEditorGraphNode>(EdGraphNode);
				if (ensure(nullptr != EditorNode))
				{
					EditorNodes.Add(EditorNode);
				}
			}

			// Find existing pairs of nodes and editor nodes
			struct FNodePair
			{
				UMetasoundEditorGraphNode* EditorNode = nullptr;
				FNodeHandle Node = FNodeHandle::InvalidHandle();;
			};
			TMap<int32, FNodePair> PairedNodes;

			// Reverse iterate so paired nodes can safely be removed from the array.
			for (int32 i = Nodes.Num() - 1; i >= 0; i--)
			{
				FNodeHandle Node = Nodes[i];
				auto IsEditorNodeWithSameNodeID = [&](const UMetasoundEditorGraphNode* InEditorNode)
				{
					return InEditorNode->GetNodeID() == Node.GetNodeID();
				};

				UMetasoundEditorGraphNode* EditorNode = nullptr;
				if (UMetasoundEditorGraphNode** PointerEditorNode = EditorNodes.FindByPredicate(IsEditorNodeWithSameNodeID))
				{
					EditorNode = *PointerEditorNode;
				}

				if (nullptr != EditorNode)
				{
					PairedNodes.Add(Node.GetNodeID(), { EditorNode, Node });
					EditorNodes.RemoveSwap(EditorNode);
					Nodes.RemoveAtSwap(i);
				}
			}

			// Nodes contains nodes which need to be added.
			// EditorNodes contains nodes that need to be removed. 
			// PairedNodes contains pairs which we have to check have synchronized pins

			// Add and remove nodes first in order to make sure correct editor nodes
			// exist before attempting to synchronize connections.
			for (UMetasoundEditorGraphNode* EditorNode : EditorNodes)
			{
				bIsEditorGraphDirty |= EditorGraph->RemoveNode(EditorNode);
			}

			// Add missing nodes. 
			bIsEditorGraphDirty |= (Nodes.Num() > 0);
			for (FNodeHandle Node : Nodes)
			{
				UEdGraphNode* NewNode = AddNode(InMetasound, EditorGraph->GetGoodPlaceForNewNode(), Node, false /* bInSelectNewNode */);
				PairedNodes.Add(Node.GetNodeID(), {Cast<UMetasoundEditorGraphNode>(NewNode), Node});
			}

			// Synchronize pins on node pairs.
			for (const TPair<int32, FNodePair>& IdNodePair : PairedNodes)
			{
				UMetasoundEditorGraphNode* EditorNode = IdNodePair.Value.EditorNode;
				FNodeHandle Node = IdNodePair.Value.Node;

				bIsEditorGraphDirty |= SynchronizeNodePins(*IdNodePair.Value.EditorNode, IdNodePair.Value.Node);
			}

			// Synchronize connections.
			bIsEditorGraphDirty |= SynchronizeConnections(InMetasound);

			if (bIsEditorGraphDirty)
			{
				InMetasound.PostEditChange();
				InMetasound.MarkPackageDirty();
			}

			return bIsEditorGraphDirty;
		}

		bool FGraphBuilder::SynchronizeNodePins(UMetasoundEditorGraphNode& InEditorNode, Frontend::FNodeHandle InNode)
		{
			bool bIsNodeDirty = false;

			TArray<Frontend::FInputHandle> InputHandles = InNode.GetAllInputs();
			TArray<Frontend::FOutputHandle> OutputHandles = InNode.GetAllOutputs();
			TArray<UEdGraphPin*> EditorPins = InEditorNode.Pins;

			// Filter out pins which are not paired.
			for (int32 i = EditorPins.Num() - 1; i >= 0; i--)
			{
				UEdGraphPin* Pin = EditorPins[i];

				auto IsMatchingInputHandle = [&](const Frontend::FInputHandle& InputHandle)
				{
					return IsMatchingInputHandleAndPin(InputHandle, *Pin);
				};

				auto IsMatchingOutputHandle = [&](const Frontend::FOutputHandle& OutputHandle)
				{
					return IsMatchingOutputHandleAndPin(OutputHandle, *Pin);
				};

				switch (Pin->Direction)
				{
					case EEdGraphPinDirection::EGPD_Input:
					{
						int32 MatchingInputIndex = InputHandles.FindLastByPredicate(IsMatchingInputHandle);
						if (INDEX_NONE != MatchingInputIndex)
						{
							InputHandles.RemoveAtSwap(MatchingInputIndex);
							EditorPins.RemoveAtSwap(i);
						}
					}
					break;

					case EEdGraphPinDirection::EGPD_Output:
					{
						int32 MatchingOutputIndex = OutputHandles.FindLastByPredicate(IsMatchingOutputHandle);
						if (INDEX_NONE != MatchingOutputIndex)
						{
							OutputHandles.RemoveAtSwap(MatchingOutputIndex);
							EditorPins.RemoveAtSwap(i);
						}
					}
					break;
				}
			}

			bIsNodeDirty |= (InputHandles.Num() > 0);
			bIsNodeDirty |= (OutputHandles.Num() > 0);
			bIsNodeDirty |= (EditorPins.Num() > 0);

			// Remove any unused editor pins.
			for (UEdGraphPin* Pin : EditorPins)
			{
				InEditorNode.RemovePin(Pin);
			}

			// Add unmatched input pins
			for (Frontend::FInputHandle& InputHandle : InputHandles)
			{
				AddPinToNode(InEditorNode, InputHandle);
			}

			for (Frontend::FOutputHandle& OutputHandle : OutputHandles)
			{
				AddPinToNode(InEditorNode, OutputHandle);
			}
			
			if (bIsNodeDirty)
			{
				InEditorNode.MarkPackageDirty();
			}

			return bIsNodeDirty;
		}

		bool FGraphBuilder::SynchronizeConnections(UObject& InMetasound)
		{
			bool bIsGraphDirty = false;

			FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetasound);
			check(MetasoundAsset);

			Frontend::FGraphHandle GraphHandle = MetasoundAsset->GetRootGraphHandle();

			UMetasoundEditorGraph* EditorGraph = CastChecked<UMetasoundEditorGraph>(MetasoundAsset->GetGraph());
			TArray<UMetasoundEditorGraphNode*> EditorNodes;

			// Cache map of editor nodes indexed by node id.
			TMap<int32, UMetasoundEditorGraphNode*> EditorNodesByID;
			for (UEdGraphNode* EdGraphNode : EditorGraph->Nodes)
			{
				UMetasoundEditorGraphNode* MetasoundEditorNode = Cast<UMetasoundEditorGraphNode>(EdGraphNode);
				if (nullptr != MetasoundEditorNode)
				{
					EditorNodes.Add(MetasoundEditorNode);
					EditorNodesByID.Add(MetasoundEditorNode->GetNodeID(), MetasoundEditorNode);
				}
			}

			// Iterate through all nodes in metasound editor graph and synchronize connections.
			for (UEdGraphNode* EdGraphNode : EditorGraph->Nodes)
			{
				bool bIsNodeDirty = false;

				UMetasoundEditorGraphNode* MetasoundEditorNode = Cast<UMetasoundEditorGraphNode>(EdGraphNode);
				Frontend::FNodeHandle Node = MetasoundEditorNode->GetNodeHandle();

				TArray<UEdGraphPin*> Pins = MetasoundEditorNode->GetAllPins();
				TArray<Frontend::FInputHandle> NodeInputs = Node.GetAllInputs();

				for (Frontend::FInputHandle& NodeInput : NodeInputs)
				{
					auto IsMatchingInputPin = [&](const UEdGraphPin* Pin) -> bool
					{
						return IsMatchingInputHandleAndPin(NodeInput, *Pin);
					};

					UEdGraphPin* MatchingPin = nullptr;
					if (UEdGraphPin** DoublePointer = Pins.FindByPredicate(IsMatchingInputPin))
					{
						MatchingPin = *DoublePointer;
					}

					if (ensure(nullptr != MatchingPin))
					{
						// Remove pin so it isn't used twice. 
						Pins.Remove(MatchingPin);

						Frontend::FOutputHandle OutputHandle = NodeInput.GetCurrentlyConnectedOutput();
						if (OutputHandle.IsValid())
						{
							bool bAddLink = false;

							if (MatchingPin->LinkedTo.Num() < 1)
							{
								// No link currently exists. Add the appropriate link.
								bAddLink = true;
							}
							else if (!IsMatchingOutputHandleAndPin(OutputHandle, *MatchingPin->LinkedTo[0]))
							{
								// The wrong link exists.
								MatchingPin->BreakAllPinLinks();
								bAddLink = true;
							}

							if (bAddLink)
							{
								UMetasoundEditorGraphNode* OutputEditorNode = EditorNodesByID[OutputHandle.GetOwningNodeID()];
								UEdGraphPin* OutputPin = OutputEditorNode->FindPinChecked(OutputHandle.GetOutputName(), EEdGraphPinDirection::EGPD_Output);
								MatchingPin->MakeLinkTo(OutputPin);
								bIsNodeDirty = true;
							}
						}
						else if (MatchingPin->LinkedTo.Num() > 0)
						{
							// No link should exist.
							MatchingPin->BreakAllPinLinks();
							bIsNodeDirty = true;
						}
					}
				}

				if (bIsNodeDirty)
				{
					EdGraphNode->MarkPackageDirty();
				}

				bIsGraphDirty |= bIsNodeDirty;
			}

			if (bIsGraphDirty)
			{
				EditorGraph->MarkPackageDirty();
			}

			return bIsGraphDirty;
		}
	} // namespace Editor
} // namespace Metasound
#undef LOCTEXT_NAMESPACE
