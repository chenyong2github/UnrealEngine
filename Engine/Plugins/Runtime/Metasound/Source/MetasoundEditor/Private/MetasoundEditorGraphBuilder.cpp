// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundEditorGraphBuilder.h"

#include "Algo/Sort.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Metasound.h"
#include "MetasoundAssetBase.h"
#include "MetasoundEditorGraph.h"
#include "MetasoundEditorGraphNode.h"
#include "MetasoundEditorGraphInputNodes.h"
#include "MetasoundEditorModule.h"
#include "MetasoundFrontendQuery.h"
#include "MetasoundFrontendQuerySteps.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundLiteral.h"
#include "MetasoundUObjectRegistry.h"
#include "Modules/ModuleManager.h"
#include "Templates/Tuple.h"
#include "Widgets/Notifications/SNotificationList.h"


#define LOCTEXT_NAMESPACE "MetasoundEditor"


namespace Metasound
{
	namespace Editor
	{
		const FName FGraphBuilder::PinCategoryAudio = "audio";
		const FName FGraphBuilder::PinCategoryBoolean = "bool";
		const FName FGraphBuilder::PinCategoryDouble = "double";
		const FName FGraphBuilder::PinCategoryFloat = "float";
		const FName FGraphBuilder::PinCategoryInt32 = "int";
		const FName FGraphBuilder::PinCategoryInt64 = "int64";
		const FName FGraphBuilder::PinCategoryObject = "object";
		const FName FGraphBuilder::PinCategoryString = "string";
		const FName FGraphBuilder::PinCategoryTrigger = "trigger";

		const FName FGraphBuilder::PinSubCategoryTime = "time";

		const FText FGraphBuilder::ConvertMenuName = LOCTEXT("MetasoundConversionsMenu", "Conversions");
		const FText FGraphBuilder::FunctionMenuName = LOCTEXT("MetasoundFunctionsMenu", "Functions");

		namespace GraphBuilderPrivate
		{
			void DeleteNode(UObject& InMetasound, Frontend::FNodeHandle InNodeHandle)
			{
				if (InNodeHandle->IsValid())
				{
					Frontend::FGraphHandle GraphHandle = InNodeHandle->GetOwningGraph();
					if (GraphHandle->IsValid())
					{
						GraphHandle->RemoveNode(*InNodeHandle);
					}
				}

				InMetasound.MarkPackageDirty();
			}
		}

		UMetasoundEditorGraphExternalNode* FGraphBuilder::AddExternalNode(UObject& InMetasound, Frontend::FNodeHandle& InNodeHandle, bool bInSelectNewNode)
		{
			UMetasoundEditorGraphExternalNode* NewGraphNode = nullptr;
			if (!ensure(InNodeHandle->GetClassType() == EMetasoundFrontendClassType::External))
			{
				return nullptr;
			}

			FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetasound);
			check(MetasoundAsset);
			UEdGraph& Graph = MetasoundAsset->GetGraphChecked();
			FGraphNodeCreator<UMetasoundEditorGraphExternalNode> NodeCreator(Graph);

			NewGraphNode = NodeCreator.CreateNode(bInSelectNewNode);
			NewGraphNode->ClassName = InNodeHandle->GetClassName();

			NodeCreator.Finalize();
			InitGraphNode(InNodeHandle, NewGraphNode, InMetasound);

			return NewGraphNode;
		}

		UMetasoundEditorGraphOutputNode* FGraphBuilder::AddOutputNode(UObject& InMetasound, Frontend::FNodeHandle& InNodeHandle, bool bInSelectNewNode)
		{
			using namespace Frontend;

			UMetasoundEditorGraphOutputNode* NewGraphNode = nullptr;
			if (!ensure(InNodeHandle->GetClassType() == EMetasoundFrontendClassType::Output))
			{
				return nullptr;
			}

			TArray<FConstInputHandle> Inputs = InNodeHandle->GetConstInputs();
			if (!ensure(!Inputs.IsEmpty()))
			{
				return nullptr;
			}

			FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetasound);
			check(MetasoundAsset);
			UEdGraph& Graph = MetasoundAsset->GetGraphChecked();
			FGraphNodeCreator<UMetasoundEditorGraphOutputNode> NodeCreator(Graph);
			NewGraphNode = NodeCreator.CreateNode(bInSelectNewNode);

			NodeCreator.Finalize();
			InitGraphNode(InNodeHandle, NewGraphNode, InMetasound);

			FConstInputHandle InputHandle = Inputs[0];
			NewGraphNode->OutputTypeName = InputHandle->GetDataType();
			return NewGraphNode;
		}

		void FGraphBuilder::InitGraphNode(Frontend::FNodeHandle& InNodeHandle, UMetasoundEditorGraphNode* NewGraphNode, UObject& InMetasound)
		{
			const FMetasoundFrontendNodeStyle& Style = InNodeHandle->GetNodeStyle();

			NewGraphNode->CreateNewGuid();

			NewGraphNode->NodeID = InNodeHandle->GetID();
			NewGraphNode->NodePosX = Style.Display.Location.X;
			NewGraphNode->NodePosY = Style.Display.Location.Y;

			RebuildNodePins(*NewGraphNode);

			InMetasound.PostEditChange();
			InMetasound.MarkPackageDirty();
		}

		UMetasoundEditorGraphExternalNode* FGraphBuilder::AddExternalNode(UObject& InMetasound, const Frontend::FNodeClassInfo& InClassInfo, const FMetasoundFrontendNodeStyle& InNodeStyle, bool bInSelectNewNode)
		{
			Frontend::FNodeHandle NodeHandle = AddExternalNodeHandle(InMetasound, InClassInfo, InNodeStyle);
			return AddExternalNode(InMetasound, NodeHandle, bInSelectNewNode);
		}

		Frontend::FNodeHandle FGraphBuilder::AddExternalNodeHandle(UObject& InMetasound, const Frontend::FNodeClassInfo& InClassInfo, const FMetasoundFrontendNodeStyle& InNodeStyle)
		{
			FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetasound);
			check(MetasoundAsset);

			Frontend::FNodeHandle NewNode = MetasoundAsset->GetRootGraphHandle()->AddNode(InClassInfo);
			NewNode->SetNodeStyle(InNodeStyle);
			return NewNode;
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

		Frontend::FInputHandle FGraphBuilder::GetInputHandleFromPin(const UEdGraphPin* InPin)
		{
			using namespace Frontend;

			if (InPin && ensure(InPin->Direction == EGPD_Input))
			{
				if (UMetasoundEditorGraphNode* EdNode = CastChecked<UMetasoundEditorGraphNode>(InPin->GetOwningNode()))
				{
					FNodeHandle NodeHandle = EdNode->GetNodeHandle();
					if (NodeHandle->IsValid())
					{
						TArray<FInputHandle> Inputs = NodeHandle->GetInputsWithVertexName(InPin->GetName());
						if (ensure(Inputs.Num() == 1))
						{
							return Inputs[0];
						}
					}
				}
			}

			return IInputController::GetInvalidHandle();
		}

		Frontend::FConstInputHandle FGraphBuilder::GetConstInputHandleFromPin(const UEdGraphPin* InPin)
		{
			return GetInputHandleFromPin(InPin);
		}

		Frontend::FOutputHandle FGraphBuilder::GetOutputHandleFromPin(const UEdGraphPin* InPin)
		{
			using namespace Frontend;

			if (InPin && ensure(InPin->Direction == EGPD_Output))
			{
				if (UMetasoundEditorGraphNode* EdNode = CastChecked<UMetasoundEditorGraphNode>(InPin->GetOwningNode()))
				{
					FNodeHandle NodeHandle = EdNode->GetNodeHandle();
					if (NodeHandle->IsValid())
					{
						TArray<FOutputHandle> Outputs = NodeHandle->GetOutputsWithVertexName(InPin->GetName());
						if (ensure(Outputs.Num() == 1))
						{
							return Outputs[0];
						}
					}
				}
			}

			return IOutputController::GetInvalidHandle();
		}

		Frontend::FConstOutputHandle FGraphBuilder::GetConstOutputHandleFromPin(const UEdGraphPin* InPin)
		{
			return GetOutputHandleFromPin(InPin);
		}

		UMetasoundEditorGraphInputNode* FGraphBuilder::AddInputNode(UObject& InMetasound, Frontend::FNodeHandle InNodeHandle, bool bInSelectNewNode)
		{
			using namespace Frontend;

			FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetasound);
			check(MetasoundAsset);

			UMetasoundEditorGraph* MetasoundGraph = Cast<UMetasoundEditorGraph>(MetasoundAsset->GetGraph());
			if (!ensure(MetasoundGraph))
			{
				return nullptr;
			}


			TArray<FConstOutputHandle> Outputs = InNodeHandle->GetConstOutputs();
			if (!ensure(!Outputs.IsEmpty()))
			{
				return nullptr;
			}

			FConstOutputHandle OutputHandle = Outputs[0];
			FGraphHandle Graph = InNodeHandle->GetOwningGraph();
			FGuid VertexID = Graph->GetVertexIDForInputVertex(OutputHandle->GetName());
			FMetasoundFrontendLiteral DefaultLiteral = Graph->GetDefaultInput(VertexID);
			EMetasoundFrontendLiteralType LiteralType = DefaultLiteral.GetType();
			UMetasoundEditorGraphInputNode* NewGraphNode = MetasoundGraph->CreateInputNode(LiteralType, nullptr, bInSelectNewNode);
			if (ensure(NewGraphNode))
			{
				NewGraphNode->ClassName = InNodeHandle->GetClassName();
				NewGraphNode->NodeID = InNodeHandle->GetID();
				NewGraphNode->InputTypeName = OutputHandle->GetDataType();

				RebuildNodePins(*NewGraphNode);
				return NewGraphNode;
			}

			return nullptr;
		}

		UMetasoundEditorGraphInputNode* FGraphBuilder::AddInputNode(UObject& InMetasound, const FString& InName, const FName InTypeName, const FMetasoundFrontendNodeStyle& InNodeStyle, const FText& InToolTip, bool bInSelectNewNode)
		{
			using namespace Frontend;

			FNodeHandle NodeHandle = AddInputNodeHandle(InMetasound, InName, InTypeName, InNodeStyle, InToolTip);
			UMetasoundEditorGraphInputNode* NewGraphNode = AddInputNode(InMetasound, NodeHandle, bInSelectNewNode);
			if (ensure(NewGraphNode))
			{
				const FMetasoundFrontendNodeStyle& Style = NodeHandle->GetNodeStyle();
				NewGraphNode->NodePosX = Style.Display.Location.X;
				NewGraphNode->NodePosY = Style.Display.Location.Y;
				return NewGraphNode;
			}
			else
			{
				GraphBuilderPrivate::DeleteNode(InMetasound, NodeHandle);
			}

			return nullptr;
		}

		void FGraphBuilder::AddOrUpdateLiteralInput(UObject& InMetasound, Frontend::FNodeHandle InNodeHandle, UEdGraphPin& InInputPin, bool bForcePinValueAsDefault)
		{
			using namespace Frontend;

			const FString InInputName = InInputPin.GetName();
			TArray<FInputHandle> InputHandles = InNodeHandle->GetInputsWithVertexName(InInputName);
			if (!ensure(InputHandles.Num() == 1))
			{
				return;
			}

			FInputHandle InputHandle = InputHandles[0];
			FMetasoundFrontendLiteral PinDataTypeDefaultLiteral;
			if (!GetPinDefaultLiteral(InInputPin, InputHandle, PinDataTypeDefaultLiteral))
			{
				return;
			}

			FOutputHandle OutputHandle = InputHandle->GetCurrentlyConnectedOutput();
			if (!OutputHandle->IsValid())
			{
				FMetasoundFrontendNodeStyle Style;
				Style.Display.Visibility = EMetasoundFrontendNodeStyleDisplayVisibility::Hidden;

				const FName TypeName = InputHandle->GetDataType();
				const FString NewInputName = GenerateUniqueInputName(InMetasound, "LiteralInput");
				Frontend::FNodeHandle NewInputNode = AddInputNodeHandle(InMetasound, NewInputName, TypeName, Style, FText::GetEmpty(), &PinDataTypeDefaultLiteral);
				NewInputNode->SetNodeStyle(Style);

				const FMetasoundFrontendLiteral* DefaultLiteral = InputHandle->GetDefaultLiteral();
				Frontend::FGraphHandle GraphHandle = InNodeHandle->GetOwningGraph();
				const FGuid VertexID = GraphHandle->GetVertexIDForInputVertex(NewInputNode->GetNodeName());
				if (bForcePinValueAsDefault)
				{
					GraphHandle->SetDefaultInput(VertexID, PinDataTypeDefaultLiteral);
				}
				else if (DefaultLiteral && DefaultLiteral->IsValid())
				{
					GraphHandle->SetDefaultInput(VertexID, *DefaultLiteral);
					InInputPin.DefaultValue = DefaultLiteral->ToString();
				}

				TArray<FOutputHandle> OutputHandles = NewInputNode->GetOutputs();
				if (ensure(OutputHandles.Num() == 1))
				{
					OutputHandle = OutputHandles[0];
				}

				ensure(InputHandle->Connect(*OutputHandle));

				InMetasound.PostEditChange();
				InMetasound.MarkPackageDirty();
			}
			else
			{
				FNodeHandle InputNode = OutputHandle->GetOwningNode();
				const FMetasoundFrontendNodeStyle& Style = InputNode->GetNodeStyle();
				if (InputNode->IsValid() && Style.Display.Visibility == EMetasoundFrontendNodeStyleDisplayVisibility::Hidden)
				{
					Frontend::FGraphHandle GraphHandle = InputNode->GetOwningGraph();
					const FGuid VertexID = GraphHandle->GetVertexIDForInputVertex(InputNode->GetNodeName());

					GraphHandle->SetDefaultInput(VertexID, PinDataTypeDefaultLiteral);

					InMetasound.PostEditChange();
					InMetasound.MarkPackageDirty();
				}
			}
		}

		bool FGraphBuilder::GetPinDefaultLiteral(UEdGraphPin& InInputPin, Frontend::FInputHandle InputHandle, FMetasoundFrontendLiteral& OutDefaultLiteral)
		{
			using namespace Frontend;

			IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetasoundEditor");

			const FString& InStringValue = InInputPin.DefaultValue;
			const FName TypeName = InputHandle->GetDataType();
			const FEditorDataType DataType = EditorModule.FindDataType(TypeName);
			switch (DataType.RegistryInfo.PreferredLiteralType)
			{
				case ELiteralType::Boolean:
				{
					OutDefaultLiteral.Set(FCString::ToBool(*InStringValue));
				}
				break;

				case ELiteralType::Float:
				{
					OutDefaultLiteral.Set(FCString::Atof(*InStringValue));
				}
				break;

				case ELiteralType::Integer:
				{
					OutDefaultLiteral.Set(FCString::Atoi(*InStringValue));
				}
				break;

				case ELiteralType::String:
				{
					OutDefaultLiteral.Set(InStringValue);
				}
				break;

				case ELiteralType::UObjectProxy:
				{
					bool bObjectFound = false;
					if (!InInputPin.DefaultValue.IsEmpty())
					{
						FMetasoundFrontendRegistryContainer* FrontendRegistry = FMetasoundFrontendRegistryContainer::Get();
						check(FrontendRegistry);

						if (UClass* Class = FrontendRegistry->GetLiteralUClassForDataType(TypeName))
						{
							FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
							FARFilter Filter;
							Filter.bRecursiveClasses = false;
							Filter.ObjectPaths.Add(FName(*InInputPin.DefaultValue));
							Filter.ClassNames.Add(Class->GetFName());
							TArray<FAssetData> AssetData;
							AssetRegistryModule.Get().GetAssets(Filter, AssetData);

							if (AssetData.Num() > 0)
							{
								OutDefaultLiteral.Set(AssetData[0].GetAsset());
							}
						}
					}
				}
				break;

				case ELiteralType::BooleanArray:
				case ELiteralType::FloatArray:
				case ELiteralType::IntegerArray:
				case ELiteralType::NoneArray:
				case ELiteralType::StringArray:
				case ELiteralType::UObjectProxyArray:
				case ELiteralType::None:
				{
					return false;
				}
				break;

				case ELiteralType::Invalid:
				default:
				{
					static_assert(static_cast<int32>(ELiteralType::COUNT) == 13, "Possible missing ELiteralType case coverage.");
					ensureMsgf(false, TEXT("Failed to set input node default: Literal type not supported"));
					return false;
				}
				break;
			}

			return true;
		}

		Frontend::FNodeHandle FGraphBuilder::AddNodeHandle(UObject& InMetasound, UMetasoundEditorGraphNode& InGraphNode)
		{
			FMetasoundFrontendNodeStyle Style;
			Style.Display.Location = FVector2D(InGraphNode.NodePosX, InGraphNode.NodePosY);

 			if (UMetasoundEditorGraphInputNode* InputNode = Cast<UMetasoundEditorGraphInputNode>(&InGraphNode))
 			{
 				const TArray<UEdGraphPin*>& Pins = InGraphNode.GetAllPins();
 				const UEdGraphPin* Pin = Pins.IsEmpty() ? nullptr : Pins[0];
 				if (ensure(Pin) && ensure(Pin->Direction == EGPD_Output))
 				{
 					return FGraphBuilder::AddInputNodeHandle(InMetasound, Pin->GetName(), InputNode->InputTypeName, Style, InGraphNode.GetTooltipText());
 				}
 			}

 			if (UMetasoundEditorGraphOutputNode* OutputNode = Cast<UMetasoundEditorGraphOutputNode>(&InGraphNode))
 			{
 				const TArray<UEdGraphPin*>& Pins = InGraphNode.GetAllPins();
 				const UEdGraphPin* Pin = Pins.IsEmpty() ? nullptr : Pins[0];
 				if (ensure(Pin) && ensure(Pin->Direction == EGPD_Input))
 				{
 					return FGraphBuilder::AddOutputNodeHandle(InMetasound, Pin->GetName(), OutputNode->OutputTypeName, Style, InGraphNode.GetTooltipText());
 				}
 			}

			if (UMetasoundEditorGraphExternalNode* ExternalNode = Cast<UMetasoundEditorGraphExternalNode>(&InGraphNode))
			{
				FFrontendQuery Query;
				const FFrontendQuerySelectionView Result = Query
					.AddStep<FGenerateAllAvailableNodeClasses>()
					.AddStep<FFilterClassesByClassName>(ExternalNode->ClassName)
					.ExecuteQuery();

				TArrayView<const FFrontendQueryEntry* const> Selection = Result.GetSelection();
				if (ensure(!Selection.IsEmpty()))
				{
					const FMetasoundFrontendClass* FrontendClass = Selection[0]->Value.TryGet<FMetasoundFrontendClass>();
					if (ensure(FrontendClass))
					{
						FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetasound);
						check(MetasoundAsset);

						Frontend::FNodeHandle NewNode = MetasoundAsset->GetRootGraphHandle()->AddNode(FrontendClass->Metadata);
						NewNode->SetNodeStyle(Style);

						InGraphNode.NodeID = NewNode->GetID();

						return NewNode;
					}
				}
			}

			// Cast failed so no handle created
			return Frontend::INodeController::GetInvalidHandle();
		}

		Frontend::FNodeHandle FGraphBuilder::AddInputNodeHandle(UObject& InMetasound, const FString& InName, const FName InTypeName, const FMetasoundFrontendNodeStyle& InNodeStyle, const FText& InToolTip, const FMetasoundFrontendLiteral* InDefaultValue)
		{
			FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetasound);
			check(MetasoundAsset);

			Frontend::FGraphHandle GraphHandle = MetasoundAsset->GetRootGraphHandle();
			const FGuid VertexID = GraphHandle->GetNewVertexID();

			FMetasoundFrontendClassInput Description;
			Description.Name = InName;
			Description.TypeName = InTypeName;
			Description.Metadata.Description = InToolTip;
			Description.VertexID = VertexID;

			Frontend::FNodeHandle NodeHandle = GraphHandle->AddInputVertex(Description);
			NodeHandle->SetNodeStyle(InNodeStyle);

			if (ensure(NodeHandle->IsValid()))
			{
				GraphHandle->SetInputDisplayName(InName, FText::FromString(InName));

				if (InDefaultValue)
				{
					if (InDefaultValue->GetType() != EMetasoundFrontendLiteralType::None && InDefaultValue->GetType() != EMetasoundFrontendLiteralType::Invalid)
					{
						GraphHandle->SetDefaultInput(VertexID, *InDefaultValue);
					}
				}
				else
				{
					GraphHandle->SetDefaultInputToDefaultLiteralOfType(VertexID);
				}
			}

			return NodeHandle;
		}

		UMetasoundEditorGraphOutputNode* FGraphBuilder::AddOutputNode(UObject& InMetasound, const FString& InName, const FName InTypeName, const FMetasoundFrontendNodeStyle& InNodeStyle, const FText& InToolTip, bool bInSelectNewNode)
		{
			Frontend::FNodeHandle NodeHandle = AddOutputNodeHandle(InMetasound, InName, InTypeName, InNodeStyle, InToolTip);
			return AddOutputNode(InMetasound, NodeHandle, bInSelectNewNode);
		}

		Frontend::FNodeHandle FGraphBuilder::AddOutputNodeHandle(UObject& InMetasound, const FString& InName, const FName InTypeName, const FMetasoundFrontendNodeStyle& InNodeStyle, const FText& InToolTip)
		{
			FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetasound);
			check(MetasoundAsset);

			Frontend::FGraphHandle GraphHandle = MetasoundAsset->GetRootGraphHandle();

			FMetasoundFrontendClassOutput Description;
			Description.Name = InName;
			Description.TypeName = InTypeName;
			Description.Metadata.Description = InToolTip;
			Description.VertexID = GraphHandle->GetNewVertexID();

			Frontend::FNodeHandle NodeHandle = GraphHandle->AddOutputVertex(Description);
			NodeHandle->SetNodeStyle(InNodeStyle);

			GraphHandle->SetOutputDisplayName(InName, FText::FromString(InName));

			return NodeHandle;
		}

		UEdGraphNode* FGraphBuilder::AddNode(UObject& InMetasound, Frontend::FNodeHandle InNodeHandle, bool bInSelectNewNode)
		{
			switch (InNodeHandle->GetClassType())
			{
				case EMetasoundFrontendClassType::Input:
				{
					return CastChecked<UEdGraphNode>(AddInputNode(InMetasound, InNodeHandle, bInSelectNewNode));
				}
				break;

				case EMetasoundFrontendClassType::External:
				{
					return CastChecked<UEdGraphNode>(AddExternalNode(InMetasound, InNodeHandle, bInSelectNewNode));
				}
				break;

				case EMetasoundFrontendClassType::Output:
				{
					return CastChecked<UEdGraphNode>(AddOutputNode(InMetasound, InNodeHandle, bInSelectNewNode));
				}
				break;

				case EMetasoundFrontendClassType::Invalid:
				case EMetasoundFrontendClassType::Graph:
				default:
				{
					checkNoEntry();
					static_assert(static_cast<int32>(EMetasoundFrontendClassType::Invalid) == 4, "Possible missing FMetasoundFrontendClassType case coverage");
				}
				break;
			}

			return nullptr;
		}

		bool FGraphBuilder::ConnectNodes(UEdGraphPin& InInputPin, UEdGraphPin& InOutputPin, bool bInConnectEdPins)
		{
			using namespace Frontend;

			// When true, will recursively call back into this function
			// from the schema if the editor pins are successfully connected
			if (bInConnectEdPins)
			{
				const UEdGraphSchema* Schema = InInputPin.GetSchema();
				if (ensure(Schema))
				{
					return Schema->TryCreateConnection(&InInputPin, &InOutputPin);
				}
				else
				{
					return false;
				}
			}

			FInputHandle InputHandle = GetInputHandleFromPin(&InInputPin);
			FOutputHandle OutputHandle = GetOutputHandleFromPin(&InOutputPin);
			if (!InputHandle->IsValid() || !OutputHandle->IsValid())
			{
				InInputPin.BreakLinkTo(&InOutputPin);
				return false;
			}

			FOutputHandle ExistingOutput = InputHandle->GetCurrentlyConnectedOutput();
			if (ExistingOutput->IsValid())
			{
				FNodeHandle NodeHandle = ExistingOutput->GetOwningNode();
				const FMetasoundFrontendNodeStyle& NodeStyle = NodeHandle->GetNodeStyle();
				if (NodeHandle->IsValid() && NodeStyle.Display.Visibility == EMetasoundFrontendNodeStyleDisplayVisibility::Hidden)
				{
					if (UMetasoundEditorGraph* Graph = Cast<UMetasoundEditorGraph>(InInputPin.GetOwningNode()->GetGraph()))
					{
						GraphBuilderPrivate::DeleteNode(Graph->GetMetasoundChecked(), NodeHandle);
					}
				}
			}

			if (!ensure(InputHandle->Connect(*OutputHandle)))
			{
				InInputPin.BreakLinkTo(&InOutputPin);
				return false;
			}

			return true;
		}

		void FGraphBuilder::ConstructGraph(UObject& InMetasound)
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

			TMap<FGuid, FNodePair> NewIdNodeMap;
			TArray<FNodeHandle> NodeHandles = GraphHandle->GetNodes();
			for (FNodeHandle& NodeHandle : NodeHandles)
			{
				const EMetasoundFrontendClassType NodeType = NodeHandle->GetClassType();
				FMetasoundFrontendNodeStyle Style;
				if (NodeType == EMetasoundFrontendClassType::Input)
				{
					Style.Display.Location = InputNodeLocation;
					InputNodeLocation.Y += 100.0f;
				}
				else if (NodeType == EMetasoundFrontendClassType::Output)
				{
					Style.Display.Location = OutputNodeLocation;
					OutputNodeLocation.Y += 100.0f;
				}
				else
				{
					Style.Display.Location = OpNodeLocation;
					OpNodeLocation.Y += 100.0f;
				}
				NodeHandle->SetNodeStyle(Style);

				UEdGraphNode* NewNode = AddNode(InMetasound, NodeHandle, false /* bInSelectNewNode */);
				NewIdNodeMap.Add(NodeHandle->GetID(), FNodePair { NodeHandle, NewNode });
			}

			for (const TPair<FGuid, FNodePair>& IdNodePair : NewIdNodeMap)
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
								const FMetasoundFrontendNodeStyle& Style = OutputHandle->GetOwningNode()->GetNodeStyle();
								if (Style.Display.Visibility == EMetasoundFrontendNodeStyleDisplayVisibility::Visible)
								{
									UEdGraphNode* OutputGraphNode = NewIdNodeMap.FindChecked(OutputHandle->GetOwningNodeID()).GraphNode;
									UEdGraphPin* OutputPin = OutputGraphNode->FindPinChecked(OutputHandle->GetName(), EEdGraphPinDirection::EGPD_Output);
									Pin->MakeLinkTo(OutputPin);
								}
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

		void FGraphBuilder::DeleteLiteralInputs(UEdGraphNode& InNode)
		{
			using namespace Frontend;

			FNodeHandle NodeHandle = Frontend::INodeController::GetInvalidHandle();
			if (UMetasoundEditorGraphNode* Node = Cast<UMetasoundEditorGraphNode>(&InNode))
			{
				NodeHandle = Node->GetNodeHandle();

				TArray<FInputHandle> Inputs = NodeHandle->GetInputs();
				for (FInputHandle& Input : Inputs)
				{
					FOutputHandle Output = Input->GetCurrentlyConnectedOutput();
					if (Output->IsValid())
					{
						FNodeHandle InputHandle = Output->GetOwningNode();
						if (InputHandle->GetNodeStyle().Display.Visibility == EMetasoundFrontendNodeStyleDisplayVisibility::Hidden)
						{
							FGraphHandle Graph = InputHandle->GetOwningGraph();
							Graph->RemoveNode(*InputHandle);
						}
					}
				}
			}
		}

		bool FGraphBuilder::DeleteNode(UEdGraphNode& InNode)
		{
			using namespace Frontend;

			FNodeHandle NodeHandle = Frontend::INodeController::GetInvalidHandle();
			if (UMetasoundEditorGraphNode* Node = Cast<UMetasoundEditorGraphNode>(&InNode))
			{
				NodeHandle = Node->GetNodeHandle();

				if (NodeHandle->GetClassType() == EMetasoundFrontendClassType::Input)
				{
					FConstDocumentHandle DocumentHandle = NodeHandle->GetOwningGraph()->GetOwningDocument();
					auto IsRequiredInput = [&](const Frontend::FConstInputHandle& InputHandle)
					{
						return DocumentHandle->IsRequiredInput(InputHandle->GetName());
					};
					TArray<Frontend::FConstInputHandle> NodeInputs = NodeHandle->GetConstInputs();

					if (Frontend::FConstInputHandle* InputHandle = NodeInputs.FindByPredicate(IsRequiredInput))
					{
						FNotificationInfo Info(FText::Format(LOCTEXT("Metasounds_CannotDeleteRequiredInput",
							"'Required Input '{0}' cannot be deleted."), (*InputHandle)->GetDisplayName()));
						Info.bFireAndForget = true;
						Info.ExpireDuration = 2.0f;
						Info.bUseThrobber = true;
						FSlateNotificationManager::Get().AddNotification(Info);
						return false;
					}
				}

				if (NodeHandle->GetClassType() == EMetasoundFrontendClassType::Output)
				{
					FConstDocumentHandle DocumentHandle = NodeHandle->GetOwningGraph()->GetOwningDocument();
					auto IsRequiredOutput = [&](const Frontend::FConstOutputHandle& OutputHandle)
					{
						return DocumentHandle->IsRequiredOutput(OutputHandle->GetName());
					};
					TArray<Frontend::FConstOutputHandle> NodeOutputs = NodeHandle->GetConstOutputs();

					if (Frontend::FConstOutputHandle* OutputHandle = NodeOutputs.FindByPredicate(IsRequiredOutput))
					{
						FNotificationInfo Info(FText::Format(LOCTEXT("Metasounds_CannotDeleteRequiredOutput",
							"'Required Output '{0}' cannot be deleted."), (*OutputHandle)->GetDisplayName()));
						Info.bFireAndForget = true;
						Info.ExpireDuration = 2.0f;
						Info.bUseThrobber = true;
						FSlateNotificationManager::Get().AddNotification(Info);
						return false;
					}
				}
			}

			DeleteLiteralInputs(InNode);

			UMetasoundEditorGraph* Graph = CastChecked<UMetasoundEditorGraph>(InNode.GetGraph());
			if (InNode.CanUserDeleteNode() && Graph->RemoveNode(&InNode))
			{
				Graph->PostEditChange();
				Graph->MarkPackageDirty();
			}

			if (NodeHandle->IsValid())
			{
				Frontend::FGraphHandle GraphHandle = NodeHandle->GetOwningGraph();
				if (GraphHandle->IsValid())
				{
					GraphHandle->RemoveNode(*NodeHandle);
				}
			}

			InNode.PostEditChange();
			InNode.MarkPackageDirty();
			return true;
		}

		void FGraphBuilder::RebuildNodePins(UMetasoundEditorGraphNode& InGraphNode)
		{
			using namespace Frontend;
		
			FNodeHandle NodeHandle = InGraphNode.GetNodeHandle();
			for (int32 i = InGraphNode.Pins.Num() - 1; i >= 0; i--)
			{
				DeleteLiteralInputs(InGraphNode);
				InGraphNode.RemovePin(InGraphNode.Pins[i]);
			}

			// Only add input pins of the node is not an input node. Input nodes
			// have their input pins hidden because they cannot be connected internal
			// to the graph.
			if (EMetasoundFrontendClassType::Input != NodeHandle->GetClassType())
			{
				TArray<FInputHandle> InputHandles = NodeHandle->GetInputs();
				InputHandles = NodeHandle->GetInputStyle().SortDefaults(InputHandles);
				for (const FInputHandle& InputHandle : InputHandles)
				{
					AddPinToNode(InGraphNode, InputHandle);
				}
			}

			// Only add output pins of the node is not an output node. Output nodes
			// have their output pins hidden because they cannot be connected internal
			// to the graph.
			if (EMetasoundFrontendClassType::Output != NodeHandle->GetClassType())
			{
				TArray<FOutputHandle> OutputHandles = NodeHandle->GetOutputs();
				OutputHandles = NodeHandle->GetOutputStyle().SortDefaults(OutputHandles);
				for (const FOutputHandle& OutputHandle : OutputHandles)
				{
					AddPinToNode(InGraphNode, OutputHandle);
				}
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
						if (UMetasoundEditorGraphNode* MetasoundEditorNode = Cast<UMetasoundEditorGraphNode>(EditorNode))
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

		bool FGraphBuilder::IsRequiredInput(Frontend::FNodeHandle InNodeHandle)
		{
			if (InNodeHandle->GetClassType() != EMetasoundFrontendClassType::Input)
			{
				return false;
			}

			Frontend::FConstDocumentHandle DocumentHandle = InNodeHandle->GetOwningGraph()->GetOwningDocument();
			return InNodeHandle->GetConstOutputs().ContainsByPredicate([&](const Frontend::FConstOutputHandle& OutputHandle)
			{
				return DocumentHandle->IsRequiredInput(OutputHandle->GetName());
			});
		}

		bool FGraphBuilder::IsRequiredOutput(Frontend::FNodeHandle InNodeHandle)
		{
			if (InNodeHandle->GetClassType() != EMetasoundFrontendClassType::Output)
			{
				return false;
			}

			Frontend::FConstDocumentHandle DocumentHandle = InNodeHandle->GetOwningGraph()->GetOwningDocument();
			return InNodeHandle->GetConstInputs().ContainsByPredicate([&](const Frontend::FConstInputHandle& InputHandle)
			{
				return DocumentHandle->IsRequiredOutput(InputHandle->GetName());
			});
		}

		UEdGraphPin* FGraphBuilder::AddPinToNode(UMetasoundEditorGraphNode& InEditorNode, Frontend::FInputHandle InInputHandle)
		{
			using namespace Frontend;

			IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetasoundEditor");
			FEdGraphPinType PinType("MetasoundNode", NAME_None, nullptr, EPinContainerType::None, false, FEdGraphTerminalType());
			UEdGraphPin* NewPin = InEditorNode.CreatePin(EGPD_Input, PinType, FName(*InInputHandle->GetName()));

			if (ensure(NewPin))
			{
				NewPin->PinToolTip = InInputHandle->GetTooltip().ToString();
				NewPin->PinType = EditorModule.FindDataType(InInputHandle->GetDataType()).PinType;

				FNodeHandle NodeHandle = InInputHandle->GetOwningNode();
				FGraphBuilder::AddOrUpdateLiteralInput(InEditorNode.GetMetasoundChecked(), NodeHandle, *NewPin);
			}

			return NewPin;
		}

		UEdGraphPin* FGraphBuilder::AddPinToNode(UMetasoundEditorGraphNode& InEditorNode, Frontend::FOutputHandle InOutputHandle)
		{
			IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetasoundEditor");
			FEdGraphPinType PinType("MetasoundNode", NAME_None, nullptr, EPinContainerType::None, false, FEdGraphTerminalType());
			UEdGraphPin* NewPin = InEditorNode.CreatePin(EGPD_Output, PinType, FName(*InOutputHandle->GetName()));

			if (ensure(NewPin))
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

			// Get all nodes from frontend graph
			FGraphHandle GraphHandle = MetasoundAsset->GetRootGraphHandle();
			TArray<FNodeHandle> FrontendNodes = GraphHandle->GetNodes();

			// Get all editor nodes from editor graph (some nodes on graph may *NOT* be metasound ed nodes,
			// such as comment boxes, etc, so just get nodes of class UMetasoundEditorGraph).
			UMetasoundEditorGraph* EditorGraph = CastChecked<UMetasoundEditorGraph>(MetasoundAsset->GetGraph());
			TArray<UMetasoundEditorGraphNode*> EditorNodes;
			EditorGraph->GetNodesOfClass(EditorNodes);

			// Find existing pairs of nodes and editor nodes
			struct FNodePair
			{
				UMetasoundEditorGraphNode* EditorNode = nullptr;
				FNodeHandle Node = INodeController::GetInvalidHandle();
			};
			TMap<FGuid, FNodePair> PairedNodes;

			// Reverse iterate so paired nodes can safely be removed from the array.
			for (int32 i = FrontendNodes.Num() - 1; i >= 0; i--)
			{
				FNodeHandle Node = FrontendNodes[i];
				auto IsEditorNodeWithSameNodeID = [&](const UMetasoundEditorGraphNode* InEditorNode)
				{
					return InEditorNode->GetNodeID() == Node->GetID();
				};

				UMetasoundEditorGraphNode* EditorNode = nullptr;
				if (UMetasoundEditorGraphNode** PointerEditorNode = EditorNodes.FindByPredicate(IsEditorNodeWithSameNodeID))
				{
					EditorNode = *PointerEditorNode;
				}

				if (EditorNode)
				{
					PairedNodes.Add(Node->GetID(), { EditorNode, Node });
					EditorNodes.RemoveSwap(EditorNode);
					FrontendNodes.RemoveAtSwap(i);
				}
			}

			// FrontendNodes contains nodes which need to be added.
			// EditorNodes contains nodes that need to be removed.
			// PairedNodes contains pairs which we have to check have synchronized pins

			// Add and remove nodes first in order to make sure correct editor nodes
			// exist before attempting to synchronize connections.
			for (UMetasoundEditorGraphNode* EditorNode : EditorNodes)
			{
				bIsEditorGraphDirty |= EditorGraph->RemoveNode(EditorNode);
			}

			// Add missing editor nodes marked as visible.
			for (FNodeHandle Node : FrontendNodes)
			{
				const FMetasoundFrontendNodeStyle& Style = Node->GetNodeStyle();
				if (Style.Display.Visibility == EMetasoundFrontendNodeStyleDisplayVisibility::Visible)
				{
					bIsEditorGraphDirty = true;
					UEdGraphNode* NewNode = AddNode(InMetasound, Node, false /* bInSelectNewNode */);
					if (ensure(NewNode))
					{
						PairedNodes.Add(Node->GetID(), { Cast<UMetasoundEditorGraphNode>(NewNode), Node });
					}
				}
			}

			// Synchronize pins on node pairs.
			for (const TPair<FGuid, FNodePair>& IdNodePair : PairedNodes)
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

		bool FGraphBuilder::SynchronizeNodePins(UMetasoundEditorGraphNode& InEditorNode, Frontend::FNodeHandle InNode, bool bRemoveUnusedPins)
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

			bIsNodeDirty |= !InputHandles.IsEmpty();
			bIsNodeDirty |= !OutputHandles.IsEmpty();
			bIsNodeDirty |= !EditorPins.IsEmpty();

			// Remove any unused editor pins.
			if (bRemoveUnusedPins)
			{
				for (UEdGraphPin* Pin : EditorPins)
				{
					InEditorNode.RemovePin(Pin);
				}
			}

			// Only add input pins of the node is not an input node. Input nodes
			// have their input pins hidden because they cannot be connected internal
			// to the graph.
			if (EMetasoundFrontendClassType::Input != InNode->GetClassType())
			{
				InputHandles = InNode->GetOutputStyle().SortDefaults(InputHandles);
				for (Frontend::FInputHandle& InputHandle : InputHandles)
				{
					AddPinToNode(InEditorNode, InputHandle);
				}
			}

			// Only add output pins of the node is not an output node. Output nodes
			// have their output pins hidden because they cannot be connected internal
			// to the graph.
			if (EMetasoundFrontendClassType::Output != InNode->GetClassType())
			{
				OutputHandles = InNode->GetOutputStyle().SortDefaults(OutputHandles);
				for (Frontend::FOutputHandle& OutputHandle : OutputHandles)
				{
					AddPinToNode(InEditorNode, OutputHandle);
				}
			}
			
			if (bIsNodeDirty)
			{
				InEditorNode.MarkPackageDirty();
			}

			return bIsNodeDirty;
		}

		bool FGraphBuilder::SynchronizeConnections(UObject& InMetasound)
		{
			using namespace Frontend;

			bool bIsGraphDirty = false;

			FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetasound);
			check(MetasoundAsset);

			FGraphHandle GraphHandle = MetasoundAsset->GetRootGraphHandle();

			UMetasoundEditorGraph* EditorGraph = CastChecked<UMetasoundEditorGraph>(MetasoundAsset->GetGraph());
			TArray<UMetasoundEditorGraphNode*> EditorNodes;

			// Cache map of editor nodes indexed by node id.
			TMap<FGuid, UMetasoundEditorGraphNode*> EditorNodesByID;
			for (UEdGraphNode* EdGraphNode : EditorGraph->Nodes)
			{
				if (UMetasoundEditorGraphNode* MetasoundEditorNode = Cast<UMetasoundEditorGraphNode>(EdGraphNode))
				{
					EditorNodes.Add(MetasoundEditorNode);
					EditorNodesByID.Add(MetasoundEditorNode->GetNodeID(), MetasoundEditorNode);
				}
			}

			// Iterate through all nodes in metasound editor graph and synchronize connections.
			for (UMetasoundEditorGraphNode* MetasoundEditorNode : EditorNodes)
			{
				bool bIsNodeDirty = false;

				FNodeHandle Node = MetasoundEditorNode->GetNodeHandle();

				if (EMetasoundFrontendClassType::Input == Node->GetClassType())
				{
					// Skip this node if it is an input node. Input pins on input 
					// nodes are not connected internal to the graph.
					continue;
				}

				TArray<UEdGraphPin*> Pins = MetasoundEditorNode->GetAllPins();
				TArray<FInputHandle> NodeInputs = Node->GetInputs();

				for (FInputHandle& NodeInput : NodeInputs)
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

					if (!ensure(MatchingPin))
					{
						continue;
					}

					// Remove pin so it isn't used twice.
					Pins.Remove(MatchingPin);

					bool bShowConnectionInEditor = true;
					bool bIsConnectedToLiteralInput = false;

					FOutputHandle OutputHandle = NodeInput->GetCurrentlyConnectedOutput();
					if (OutputHandle->IsValid())
					{
						FNodeHandle InputNodeHandle = OutputHandle->GetOwningNode();
						const FMetasoundFrontendNodeStyle& InputNodeStyle = InputNodeHandle->GetNodeStyle();
						bShowConnectionInEditor = InputNodeStyle.Display.Visibility == EMetasoundFrontendNodeStyleDisplayVisibility::Visible;
						bIsConnectedToLiteralInput = !bShowConnectionInEditor;
					}
					else
					{
						bShowConnectionInEditor = false;
					}
						
					if (bShowConnectionInEditor)
					{
						bool bAddLink = false;

						if (MatchingPin->LinkedTo.IsEmpty())
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
							const FGuid InputNodeID = OutputHandle->GetOwningNodeID();
							UMetasoundEditorGraphNode* OutputEditorNode = EditorNodesByID[InputNodeID];
							UEdGraphPin* OutputPin = OutputEditorNode->FindPinChecked(OutputHandle->GetName(), EEdGraphPinDirection::EGPD_Output);
							MatchingPin->MakeLinkTo(OutputPin);
							bIsNodeDirty = true;
						}
					}
					else
					{
						// No link should exist.
						if (!MatchingPin->LinkedTo.IsEmpty())
						{
							MatchingPin->BreakAllPinLinks();
							bIsNodeDirty = true;
						}

						// Hidden literal input Link exists, so match pin default value to literal input value
						if (bIsConnectedToLiteralInput)
						{
							FNodeHandle InputNode = OutputHandle->GetOwningNode();
							const FMetasoundFrontendNodeStyle& Style = InputNode->GetNodeStyle();
							const FGuid VertexID = GraphHandle->GetVertexIDForInputVertex(InputNode->GetNodeName());
							const FMetasoundFrontendLiteral DefaultLiteral = GraphHandle->GetDefaultInput(VertexID);
							const FString DefaultLiteralString = DefaultLiteral.ToString();
							if (MatchingPin->DefaultValue != DefaultLiteralString)
							{
								MatchingPin->DefaultValue = DefaultLiteralString;
								bIsNodeDirty = true;
							}
						}
					}
				}

				if (bIsNodeDirty)
				{
					MetasoundEditorNode->MarkPackageDirty();
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
