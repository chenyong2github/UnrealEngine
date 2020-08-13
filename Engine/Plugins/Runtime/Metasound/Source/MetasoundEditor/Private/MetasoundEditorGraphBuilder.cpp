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

			FMetasoundAssetBase* MetasoundAsset = Frontend::GetObjectAsAssetBase(&InMetasound);
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
			FMetasoundAssetBase* MetasoundAsset = Frontend::GetObjectAsAssetBase(&InMetasound);
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

		FString FGraphBuilder::GenerateUniqueInputName(const UObject& InMetasound, const FName InBaseName)
		{
			FString NameBase = GetDataTypeDisplayName(InBaseName);
			const FMetasoundAssetBase* MetasoundAsset = Frontend::GetObjectAsAssetBase(&InMetasound);
			check(MetasoundAsset);

			const Frontend::FGraphHandle GraphHandle = MetasoundAsset->GetRootGraphHandle();

			int32 i = 1;
			FString NewNodeName = NameBase + FString::Printf(TEXT("_%02d"), i);
			while (GraphHandle.ContainsInputNodeWithName(NewNodeName))
			{
				NewNodeName = NameBase + FString::Printf(TEXT("_%02d"), ++i);
			}

			return NewNodeName;
		}

		FString FGraphBuilder::GenerateUniqueOutputName(const UObject& InMetasound, const FName InBaseName)
		{
			FString NameBase = GetDataTypeDisplayName(InBaseName);
			const FMetasoundAssetBase* MetasoundAsset = Frontend::GetObjectAsAssetBase(&InMetasound);
			check(MetasoundAsset);

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

			FMetasoundAssetBase* MetasoundAsset = Frontend::GetObjectAsAssetBase(&InMetasound);
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

			FMetasoundAssetBase* MetasoundAsset = Frontend::GetObjectAsAssetBase(&InMetasound);
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

			FMetasoundAssetBase* MetasoundAsset = Frontend::GetObjectAsAssetBase(Graph->GetMetasound());
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

			FMetasoundAssetBase* MetasoundAsset = GetObjectAsAssetBase(&InMetasound);
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

			const FMetasoundAssetBase* MetasoundAsset = Frontend::GetObjectAsAssetBase(&InGraphNode.GetMetasoundChecked());
			check(MetasoundAsset);

			Frontend::FGraphHandle GraphHandle = MetasoundAsset->GetRootGraphHandle();

			TArray<Frontend::FInputHandle> InputHandles = InNodeHandle.GetAllInputs();
			for (int32 i = 0; i < InputHandles.Num(); ++i)
			{
				const Frontend::FInputHandle& InputHandle = InputHandles[i];

				FText DisplayName;
				if (InGraphNode.GetNodeHandle().GetNodeType() == EMetasoundClassType::Input)
				{
					DisplayName = GraphHandle.GetInputDisplayName(InputHandle.GetInputName());
				}
				else
				{
					DisplayName = FText::FromString(*InputHandle.GetInputName());
				}

				FEdGraphPinType PinType("MetasoundNode", NAME_None, nullptr, EPinContainerType::None, false, FEdGraphTerminalType());
				UEdGraphPin* NewPin = InGraphNode.CreatePin(EGPD_Input, PinType, *DisplayName.ToString(), i);
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

				FText DisplayName;
				if (InGraphNode.GetNodeHandle().GetNodeType() == EMetasoundClassType::Output)
				{
					DisplayName = GraphHandle.GetInputDisplayName(OutputHandle.GetOutputName());
				}
				else
				{
					DisplayName = FText::FromString(*OutputHandle.GetOutputName());
				}

				UEdGraphPin* NewPin = InGraphNode.CreatePin(EGPD_Output, PinType, *DisplayName.ToString(), i);
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