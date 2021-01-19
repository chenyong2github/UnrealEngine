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
#include "MetasoundFrontendController.h"
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
		const FName FGraphBuilder::PinAudioFormat = "Format";
		const FName FGraphBuilder::PinAudioNumeric = "Numeric";
		const FName FGraphBuilder::PinPrimitiveBoolean = "Boolean";
		const FName FGraphBuilder::PinPrimitiveFloat = "Float";
		const FName FGraphBuilder::PinPrimitiveInt32 = "Int32";
		const FName FGraphBuilder::PinPrimitiveInt64 = "Int64";
		const FName FGraphBuilder::PinPrimitiveString = "String";
		const FName FGraphBuilder::PinPrimitiveTrigger = "Trigger";
		const FName FGraphBuilder::PinPrimitiveUObject = "UObject";
		const FName FGraphBuilder::PinPrimitiveUObjectArray = "UObjectArray";

		UEdGraphNode* FGraphBuilder::AddNode(UObject& InMetasound, const FVector2D& Location, Frontend::FNodeHandle& InNodeHandle, bool bInSelectNewNode)
		{
			const FScopedTransaction Transaction(LOCTEXT("AddMetasoundGraphNode", "Add Metasound Node"));

			FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetasound);
			check(MetasoundAsset);

			UEdGraph& Graph = MetasoundAsset->GetGraphChecked();
			FGraphNodeCreator<UMetasoundEditorGraphNode> NodeCreator(Graph);
			UMetasoundEditorGraphNode* NewGraphNode = NodeCreator.CreateNode(bInSelectNewNode);
			NodeCreator.Finalize();

			NewGraphNode->SetNodeID(InNodeHandle->GetID());
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

			return MetasoundAsset->GetRootGraphHandle()->AddNode(InClassInfo);
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

		FString FGraphBuilder::GenerateUniqueInputName(const UObject& InMetasound, const FName InBaseName)
		{
			FString NameBase = GetDataTypeDisplayName(InBaseName);
			const FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetasound);
			check(MetasoundAsset);

			const Frontend::FConstGraphHandle GraphHandle = MetasoundAsset->GetRootGraphHandle();

			int32 i = 1;
			FString NewInputName = NameBase + FString::Printf(TEXT("_%02d"), i);
			while (GraphHandle->ContainsInputVertexWithName(NewInputName))
			{
				NewInputName = NameBase + FString::Printf(TEXT("_%02d"), ++i);
			}

			return NewInputName;
		}

		FString FGraphBuilder::GenerateUniqueOutputName(const UObject& InMetasound, const FName InBaseName)
		{
			FString NameBase = GetDataTypeDisplayName(InBaseName);
			const FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetasound);
			check(MetasoundAsset);

			const Frontend::FConstGraphHandle GraphHandle = MetasoundAsset->GetRootGraphHandle();

			int32 i = 1;
			FString NewOutputName = NameBase + FString::Printf(TEXT("_%02d"), i);
			while (GraphHandle->ContainsOutputVertexWithName(NewOutputName))
			{
				NewOutputName = NameBase + FString::Printf(TEXT("_%02d"), ++i);
			}

			return NewOutputName;
		}

		UEdGraphNode* FGraphBuilder::AddInput(UObject& InMetasound, const FVector2D& Location, const FString& InName, const FName InTypeName, const FText& InToolTip, bool bInSelectNewNode)
		{
			Frontend::FNodeHandle NodeHandle = AddInputNodeHandle(InMetasound, InName, InTypeName, InToolTip);
			return AddNode(InMetasound, Location, NodeHandle, bInSelectNewNode);
		}

		Frontend::FNodeHandle FGraphBuilder::AddInputNodeHandle(UObject& InMetasound, const FString& InName, const FName InTypeName, const FText& InToolTip)
		{

			FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetasound);
			check(MetasoundAsset);

			Frontend::FGraphHandle GraphHandle = MetasoundAsset->GetRootGraphHandle();

			FMetasoundFrontendClassInput Description;

			Description.Name = InName;
			Description.TypeName = InTypeName;
			Description.Metadata.Description = InToolTip;
			int32 PointID = GraphHandle->GetNewPointID();

			Description.PointIDs.Add(PointID);
			FMetasoundFrontendVertexLiteral DefaultValue;
			DefaultValue.PointID = PointID;
			Description.Defaults.Add(DefaultValue);

			Frontend::FNodeHandle NodeHandle = GraphHandle->AddInputVertex(Description);
			if (!NodeHandle->IsValid())
			{
				return NodeHandle;
			}

			IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetasoundEditor");
			FEditorDataType DataType = EditorModule.FindDataType(InTypeName);

			Metasound::FLiteral LiteralParam = Frontend::GetDefaultParamForDataType(InTypeName);
			if (!ensureAlways(LiteralParam.IsValid()))
			{
				return NodeHandle;
			}

			switch (LiteralParam.GetType())
			{
				case ELiteralType::Boolean:
				{
					GraphHandle->SetDefaultInputToLiteral(InName, PointID, false);
				}
				break;

				case ELiteralType::Float:
				{
					GraphHandle->SetDefaultInputToLiteral(InName, PointID, 0.0f);
				}
				break;

				case ELiteralType::Integer:
				{
					GraphHandle->SetDefaultInputToLiteral(InName, PointID, 0);
				}
				break;

				case ELiteralType::String:
				{
					GraphHandle->SetDefaultInputToLiteral(InName, PointID, FString(TEXT("")));
				}
				break;

				case ELiteralType::UObjectProxy:
				{
					UClass* ClassToUse = FMetasoundFrontendRegistryContainer::Get()->GetLiteralUClassForDataType(InTypeName);
					if (ClassToUse)
					{
						GraphHandle->SetDefaultInputToLiteral(InName, PointID, ClassToUse->ClassDefaultObject);
					}
				}
				break;

				case ELiteralType::UObjectProxyArray:
				{
					UClass* ClassToUse = FMetasoundFrontendRegistryContainer::Get()->GetLiteralUClassForDataType(InTypeName);
					if (ClassToUse)
					{
						TArray<UObject*> ObjectArray;
						ObjectArray.Add(ClassToUse->ClassDefaultObject);
						GraphHandle->SetDefaultInputToLiteral(InName, PointID, ObjectArray);
					}
					GraphHandle->SetDefaultInputToLiteral(InName, PointID, FString(TEXT("")));
				}
				break;

				case ELiteralType::Invalid:
				case ELiteralType::None:
				default:
				{
					static_assert(static_cast<int32>(ELiteralType::Invalid) == 7, "Possible missing ELiteralType case coverage");
				}
				break;
			}

			GraphHandle->SetInputDisplayName(InName, FText::FromString(InName));
			return NodeHandle;
		}

		UEdGraphNode* FGraphBuilder::AddOutput(UObject& InMetasound, const FVector2D& Location, const FString& InName, const FName InTypeName, const FText& InToolTip, bool bInSelectNewNode)
		{
			Frontend::FNodeHandle NodeHandle = AddOutputNodeHandle(InMetasound, InName, InTypeName, InToolTip);
			return AddNode(InMetasound, Location, NodeHandle, bInSelectNewNode);
		}

		Frontend::FNodeHandle FGraphBuilder::AddOutputNodeHandle(UObject& InMetasound, const FString& InName, const FName InTypeName, const FText& InToolTip)
		{
			FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetasound);
			check(MetasoundAsset);

			Frontend::FGraphHandle GraphHandle = MetasoundAsset->GetRootGraphHandle();


			FMetasoundFrontendClassOutput Description;
			Description.Name = InName;
			Description.TypeName = InTypeName;
			Description.Metadata.Description = InToolTip;
			Description.PointIDs.Add(GraphHandle->GetNewPointID());

			Frontend::FNodeHandle NodeHandle = GraphHandle->AddOutputVertex(Description);

			GraphHandle->SetOutputDisplayName(InName, FText::FromString(InName));

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
			if (GraphHandle->IsValid() && NodeHandle->IsValid())
			{
				GraphHandle->RemoveNode(*NodeHandle);
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
				FNodeHandle NodeHandle = INodeController::GetInvalidHandle();
				UEdGraphNode* GraphNode = nullptr;
			};

			TMap<uint32, FNodePair> NewIdNodeMap;
			TArray<FNodeHandle> NodeHandles = GraphHandle->GetNodes();
			for (FNodeHandle& NodeHandle : NodeHandles)
			{
				const EMetasoundFrontendClassType NodeType = NodeHandle->GetClassType();
				FVector2D Location;
				if (NodeType == EMetasoundFrontendClassType::Input)
				{
					Location = InputNodeLocation;
					InputNodeLocation.Y += 100.0f;
				}
				else if (NodeType == EMetasoundFrontendClassType::Output)
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
				NewIdNodeMap.Add(NodeHandle->GetID(), FNodePair { NodeHandle, NewNode });
			}

			for (const TPair<uint32, FNodePair>& IdNodePair : NewIdNodeMap)
			{
				UEdGraphNode* GraphNode = IdNodePair.Value.GraphNode;
				check(GraphNode);

				FNodeHandle NodeHandle = IdNodePair.Value.NodeHandle;
				TArray<UEdGraphPin*> Pins = GraphNode->GetAllPins();

				const TArray<FInputHandle> NodeInputs = NodeHandle->GetInputs();

				int32 InputIndex = 0;
				for (UEdGraphPin* Pin : Pins)
				{
					switch (Pin->Direction)
					{
						case EEdGraphPinDirection::EGPD_Input:
						{
							FOutputHandle OutputHandle = NodeInputs[InputIndex]->GetCurrentlyConnectedOutput();
							if (OutputHandle->IsValid())
							{
								UEdGraphNode* OutputGraphNode = NewIdNodeMap.FindChecked(OutputHandle->GetOwningNodeID()).GraphNode;
								UEdGraphPin* OutputPin = OutputGraphNode->FindPinChecked(OutputHandle->GetName(), EEdGraphPinDirection::EGPD_Output);
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

		void FGraphBuilder::RebuildNodePins(UMetasoundEditorGraphNode& InGraphNode, Frontend::FNodeHandle InNodeHandle, bool bInRecordTransaction)
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

			TArray<Frontend::FInputHandle> InputHandles = InNodeHandle->GetInputs();
			for (int32 i = 0; i < InputHandles.Num(); ++i)
			{
				AddPinToNode(InGraphNode, InputHandles[i]);
			}

			TArray<Frontend::FOutputHandle> OutputHandles = InNodeHandle->GetOutputs();
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
				if (InEditorPin.GetName() == InInputHandle->GetName())
				{
					IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetasoundEditor");

					if (InEditorPin.PinType == EditorModule.FindDataType(InInputHandle->GetDataType()).PinType)
					{
						UEdGraphNode* EditorNode = InEditorPin.GetOwningNode();
						UMetasoundEditorGraphNode* MetasoundEditorNode = Cast<UMetasoundEditorGraphNode>(EditorNode);

						if (nullptr != MetasoundEditorNode)
						{
							if  (MetasoundEditorNode->GetNodeHandle()->GetID() == InInputHandle->GetOwningNodeID())
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
				if (InEditorPin.GetName() == InOutputHandle->GetName())
				{
					IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetasoundEditor");

					if (InEditorPin.PinType == EditorModule.FindDataType(InOutputHandle->GetDataType()).PinType)
					{
						UEdGraphNode* EditorNode = InEditorPin.GetOwningNode();
						UMetasoundEditorGraphNode* MetasoundEditorNode = Cast<UMetasoundEditorGraphNode>(EditorNode);

						if (nullptr != MetasoundEditorNode)
						{
							if  (MetasoundEditorNode->GetNodeHandle()->GetID() == InOutputHandle->GetOwningNodeID())
							{
								return true;
							}
						}
					}
				}
			}
			return false;
		}

		UEdGraphPin* FGraphBuilder::AddPinToNode(UMetasoundEditorGraphNode& InEditorNode, Frontend::FInputHandle InInputHandle)
		{
			IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetasoundEditor");

			FEdGraphPinType PinType("MetasoundNode", NAME_None, nullptr, EPinContainerType::None, false, FEdGraphTerminalType());
			UEdGraphPin* NewPin = InEditorNode.CreatePin(EGPD_Input, PinType, FName(*InInputHandle->GetName()));

			if (ensureAlways(NewPin))
			{
				NewPin->PinToolTip = InInputHandle->GetTooltip().ToString();
				NewPin->PinType = EditorModule.FindDataType(InInputHandle->GetDataType()).PinType;
			}

			return NewPin;
		}

		UEdGraphPin* FGraphBuilder::AddPinToNode(UMetasoundEditorGraphNode& InEditorNode, Frontend::FOutputHandle InOutputHandle)
		{
			IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetasoundEditor");
			FText DisplayName;

			FEdGraphPinType PinType("MetasoundNode", NAME_None, nullptr, EPinContainerType::None, false, FEdGraphTerminalType());
			UEdGraphPin* NewPin = InEditorNode.CreatePin(EGPD_Output, PinType, FName(*InOutputHandle->GetName()));

			if (ensureAlways(NewPin))
			{
				NewPin->PinToolTip = InOutputHandle->GetTooltip().ToString();
				NewPin->PinType = EditorModule.FindDataType(InOutputHandle->GetDataType()).PinType;
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
			TArray<FNodeHandle> Nodes = GraphHandle->GetNodes();

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
				FNodeHandle Node = INodeController::GetInvalidHandle();;
			};
			TMap<int32, FNodePair> PairedNodes;

			// Reverse iterate so paired nodes can safely be removed from the array.
			for (int32 i = Nodes.Num() - 1; i >= 0; i--)
			{
				FNodeHandle Node = Nodes[i];
				auto IsEditorNodeWithSameNodeID = [&](const UMetasoundEditorGraphNode* InEditorNode)
				{
					return InEditorNode->GetNodeID() == Node->GetID();
				};

				UMetasoundEditorGraphNode* EditorNode = nullptr;
				if (UMetasoundEditorGraphNode** PointerEditorNode = EditorNodes.FindByPredicate(IsEditorNodeWithSameNodeID))
				{
					EditorNode = *PointerEditorNode;
				}

				if (nullptr != EditorNode)
				{
					PairedNodes.Add(Node->GetID(), { EditorNode, Node });
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
				PairedNodes.Add(Node->GetID(), {Cast<UMetasoundEditorGraphNode>(NewNode), Node});
			}

			// Synchronize pins on node pairs.
			for (const TPair<int32, FNodePair>& IdNodePair : PairedNodes)
			{
				UMetasoundEditorGraphNode* EditorNode = IdNodePair.Value.EditorNode;
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

			TArray<Frontend::FInputHandle> InputHandles = InNode->GetInputs();
			TArray<Frontend::FOutputHandle> OutputHandles = InNode->GetOutputs();
			TArray<UEdGraphPin*> EditorPins = InEditorNode.Pins;

			// Filter out pins which are not paired.
			for (int32 i = EditorPins.Num() - 1; i >= 0; i--)
			{
				UEdGraphPin* Pin = EditorPins[i];

				auto IsMatchingInputHandle = [&](const Frontend::FInputHandle& InputHandle) -> bool
				{
					return IsMatchingInputHandleAndPin(InputHandle, *Pin);
				};

				auto IsMatchingOutputHandle = [&](const Frontend::FOutputHandle& OutputHandle) -> bool
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
				TArray<Frontend::FInputHandle> NodeInputs = Node->GetInputs();

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

						Frontend::FOutputHandle OutputHandle = NodeInput->GetCurrentlyConnectedOutput();
						if (OutputHandle->IsValid())
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
								UMetasoundEditorGraphNode* OutputEditorNode = EditorNodesByID[OutputHandle->GetOwningNodeID()];
								UEdGraphPin* OutputPin = OutputEditorNode->FindPinChecked(OutputHandle->GetName(), EEdGraphPinDirection::EGPD_Output);
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
