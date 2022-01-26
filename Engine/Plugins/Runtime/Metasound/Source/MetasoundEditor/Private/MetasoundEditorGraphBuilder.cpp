// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundEditorGraphBuilder.h"

#include "Algo/Sort.h"
#include "Algo/Transform.h"
#include "Algo/AnyOf.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Framework/Notifications/NotificationManager.h"
#include "GraphEditor.h"
#include "Metasound.h"
#include "MetasoundAssetBase.h"
#include "MetasoundEditor.h"
#include "MetasoundEditorGraph.h"
#include "MetasoundEditorGraphBuilder.h"
#include "MetasoundEditorGraphInputNode.h"
#include "MetasoundEditorGraphMemberDefaults.h"
#include "MetasoundEditorGraphNode.h"
#include "MetasoundEditorGraphValidation.h"
#include "MetasoundEditorModule.h"
#include "MetasoundEditorSettings.h"
#include "MetasoundFrontendDataTypeRegistry.h"
#include "MetasoundFrontendDocumentAccessPtr.h"
#include "MetasoundFrontendQuery.h"
#include "MetasoundFrontendQuerySteps.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundFrontendSearchEngine.h"
#include "MetasoundFrontendTransform.h"
#include "MetasoundLiteral.h"
#include "MetasoundUObjectRegistry.h"
#include "MetasoundVariableNodes.h"
#include "MetasoundVertex.h"
#include "Modules/ModuleManager.h"
#include "Templates/Tuple.h"
#include "Toolkits/ToolkitManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "MetaSoundEditor"


namespace Metasound
{
	namespace Editor
	{
		const FName FGraphBuilder::PinCategoryAudio = "audio";
		const FName FGraphBuilder::PinCategoryBoolean = "bool";
		//const FName FGraphBuilder::PinCategoryDouble = "double";
		const FName FGraphBuilder::PinCategoryFloat = "float";
		const FName FGraphBuilder::PinCategoryInt32 = "int";
		//const FName FGraphBuilder::PinCategoryInt64 = "int64";
		const FName FGraphBuilder::PinCategoryObject = "object";
		const FName FGraphBuilder::PinCategoryString = "string";
		const FName FGraphBuilder::PinCategoryTrigger = "trigger";

		const FName FGraphBuilder::PinSubCategoryTime = "time";

		namespace GraphBuilderPrivate
		{
			void DeleteNode(UObject& InMetaSound, Frontend::FNodeHandle InNodeHandle)
			{
				if (InNodeHandle->IsValid())
				{
					Frontend::FGraphHandle GraphHandle = InNodeHandle->GetOwningGraph();
					if (GraphHandle->IsValid())
					{
						GraphHandle->RemoveNode(*InNodeHandle);
					}
				}
			}

			FName GenerateUniqueName(const TArray<FName>& InExistingNames, const FString& InBaseName)
			{
				int32 PostFixInt = 0;
				FString NewName = InBaseName;

				while (InExistingNames.Contains(*NewName))
				{
					PostFixInt++;
					NewName = FString::Format(TEXT("{0} {1}"), { InBaseName, PostFixInt });
				}

				return FName(*NewName);
			}
		} // namespace GraphBuilderPrivate

		FText FGraphBuilder::GetDisplayName(const Frontend::INodeController& InFrontendNode, bool bInIncludeNamespace)
		{
			FName Namespace;
			FName ParameterName;
			Audio::FParameterPath::SplitName(InFrontendNode.GetNodeName(), Namespace, ParameterName);

			FText DisplayName = InFrontendNode.GetDisplayName();
			if (DisplayName.IsEmptyOrWhitespace())
			{
				DisplayName = FText::FromName(ParameterName);
			}

			if (bInIncludeNamespace)
			{
				if (!Namespace.IsNone())
				{
					return FText::Format(LOCTEXT("MemberDisplayNameWithNamespaceFormat", "{0} ({1})"), DisplayName, FText::FromName(Namespace));
				}
			}

			return DisplayName;
		}

		FText FGraphBuilder::GetDisplayName(const Frontend::IInputController& InFrontendInput)
		{
			FText DisplayName = InFrontendInput.GetDisplayName();
			if (DisplayName.IsEmptyOrWhitespace())
			{
				DisplayName = FText::FromName(InFrontendInput.GetName());
			}
			return DisplayName;
		}

		FText FGraphBuilder::GetDisplayName(const Frontend::IOutputController& InFrontendOutput)
		{
			FText DisplayName = InFrontendOutput.GetDisplayName();
			if (DisplayName.IsEmptyOrWhitespace())
			{
				DisplayName = FText::FromName(InFrontendOutput.GetName());
			}
			return DisplayName;
		}

		FText FGraphBuilder::GetDisplayName(const Frontend::IVariableController& InFrontendVariable)
		{
			FText DisplayName = InFrontendVariable.GetDisplayName();
			if (DisplayName.IsEmptyOrWhitespace())
			{
				DisplayName = FText::FromName(InFrontendVariable.GetName());
			}
			return DisplayName;
		}

		FName FGraphBuilder::GetPinName(const Frontend::IOutputController& InFrontendOutput)
		{
			Frontend::FConstNodeHandle OwningNode = InFrontendOutput.GetOwningNode();
			EMetasoundFrontendClassType OwningNodeClassType = OwningNode->GetClassMetadata().GetType();

			switch (OwningNodeClassType)
			{
				case EMetasoundFrontendClassType::Variable:
				case EMetasoundFrontendClassType::VariableAccessor:
				case EMetasoundFrontendClassType::VariableDeferredAccessor:
				case EMetasoundFrontendClassType::VariableMutator:
				{
					// All variables nodes use the same pin name for user-modifiable node
					// inputs and outputs and the editor does not display the pin's name. The
					// editor instead displays the variable's name in place of the pin name to
					// maintain a consistent look and behavior to input and output nodes.
					return VariableNames::GetOutputDataName();
				}
				case EMetasoundFrontendClassType::Input:
				case EMetasoundFrontendClassType::Output:
				{
					return OwningNode->GetNodeName();
				}

				default:
				{
					return InFrontendOutput.GetName();
				}
			}
		}

		FName FGraphBuilder::GetPinName(const Frontend::IInputController& InFrontendInput)
		{
			Frontend::FConstNodeHandle OwningNode = InFrontendInput.GetOwningNode();
			EMetasoundFrontendClassType OwningNodeClassType = OwningNode->GetClassMetadata().GetType();

			switch (OwningNodeClassType)
			{
				case EMetasoundFrontendClassType::Variable:
				case EMetasoundFrontendClassType::VariableAccessor:
				case EMetasoundFrontendClassType::VariableDeferredAccessor:
				case EMetasoundFrontendClassType::VariableMutator:
				{
					// All variables nodes use the same pin name for user-modifiable node
					// inputs and outputs and the editor does not display the pin's name. The
					// editor instead displays the variable's name in place of the pin name to
					// maintain a consistent look and behavior to input and output nodes.
					return VariableNames::GetInputDataName();
				}

				case EMetasoundFrontendClassType::Input:
				case EMetasoundFrontendClassType::Output:
				{
					return OwningNode->GetNodeName();
				}

				default:
				{
					return InFrontendInput.GetName();
				}
			}
		}

		UMetasoundEditorGraphExternalNode* FGraphBuilder::AddExternalNode(UObject& InMetaSound, Frontend::FNodeHandle& InNodeHandle, FVector2D InLocation, bool bInSelectNewNode)
		{
			using namespace Frontend;

			UMetasoundEditorGraphExternalNode* NewGraphNode = nullptr;
			if (!ensure(InNodeHandle->GetClassMetadata().GetType() == EMetasoundFrontendClassType::External))
			{
				return nullptr;
			}

			FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSound);
			check(MetaSoundAsset);
			UEdGraph& Graph = MetaSoundAsset->GetGraphChecked();
			FGraphNodeCreator<UMetasoundEditorGraphExternalNode> NodeCreator(Graph);

			NewGraphNode = NodeCreator.CreateNode(bInSelectNewNode);

			const FNodeRegistryKey RegistryKey = NodeRegistryKey::CreateKey(InNodeHandle->GetClassMetadata());
			NewGraphNode->bIsClassNative = FMetasoundFrontendRegistryContainer::Get()->IsNodeNative(RegistryKey);
			NewGraphNode->ClassName = InNodeHandle->GetClassMetadata().GetClassName();

			NodeCreator.Finalize();
			InitGraphNode(InNodeHandle, NewGraphNode, InMetaSound);

			SynchronizeNodeLocation(InLocation, InNodeHandle, *NewGraphNode);

			// Adding external node may introduce referenced asset so rebuild referenced keys.
			MetaSoundAsset->RebuildReferencedAssetClassKeys();

			return NewGraphNode;
		}

		UMetasoundEditorGraphExternalNode* FGraphBuilder::AddExternalNode(UObject& InMetaSound, const FMetasoundFrontendClassMetadata& InMetadata, FVector2D InLocation, bool bInSelectNewNode)
		{
			FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSound);
			check(MetaSoundAsset);

			Frontend::FNodeHandle NodeHandle = MetaSoundAsset->GetRootGraphHandle()->AddNode(InMetadata);
			return AddExternalNode(InMetaSound, NodeHandle, InLocation, bInSelectNewNode);
		}

		UMetasoundEditorGraphVariableNode* FGraphBuilder::AddVariableNode(UObject& InMetaSound, Frontend::FNodeHandle& InNodeHandle, FVector2D InLocation, bool bInSelectNewNode)
		{
			using namespace Frontend;

			EMetasoundFrontendClassType ClassType = InNodeHandle->GetClassMetadata().GetType();
			const bool bIsSupportedClassType = (ClassType == EMetasoundFrontendClassType::VariableAccessor) 
				|| (ClassType == EMetasoundFrontendClassType::VariableDeferredAccessor)
				|| (ClassType == EMetasoundFrontendClassType::VariableMutator);

			if (!ensure(bIsSupportedClassType))
			{
				return nullptr;
			}

			FConstVariableHandle FrontendVariable = InNodeHandle->GetOwningGraph()->FindVariableContainingNode(InNodeHandle->GetID());
			if (!ensure(FrontendVariable->IsValid()))
			{
				return nullptr;
			}

			UMetasoundEditorGraphVariableNode* NewGraphNode = nullptr;
			FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSound);
			if (ensure(nullptr != MetaSoundAsset))
			{
				if (UMetasoundEditorGraph* MetasoundGraph = CastChecked<UMetasoundEditorGraph>(MetaSoundAsset->GetGraph()))
				{
					FGraphNodeCreator<UMetasoundEditorGraphVariableNode> NodeCreator(*MetasoundGraph);

					NewGraphNode = NodeCreator.CreateNode(bInSelectNewNode);
					NewGraphNode->ClassName = InNodeHandle->GetClassMetadata().GetClassName();
					NewGraphNode->ClassType = ClassType;
					NewGraphNode->Variable = MetasoundGraph->FindOrAddVariable(FrontendVariable);
					NodeCreator.Finalize();

					InitGraphNode(InNodeHandle, NewGraphNode, InMetaSound);

					SynchronizeNodeLocation(InLocation, InNodeHandle, *NewGraphNode);
				}
			}

			return NewGraphNode;
		}

		UMetasoundEditorGraphOutputNode* FGraphBuilder::AddOutputNode(UObject& InMetaSound, Frontend::FNodeHandle& InNodeHandle, FVector2D InLocation, bool bInSelectNewNode)
		{
			using namespace Frontend;

			UMetasoundEditorGraphOutputNode* NewGraphNode = nullptr;
			if (!ensure(InNodeHandle->GetClassMetadata().GetType() == EMetasoundFrontendClassType::Output))
			{
				return nullptr;
			}

			FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSound);
			check(MetaSoundAsset);
			UEdGraph& Graph = MetaSoundAsset->GetGraphChecked();
			FGraphNodeCreator<UMetasoundEditorGraphOutputNode> NodeCreator(Graph);

			NewGraphNode = NodeCreator.CreateNode(bInSelectNewNode);
			UMetasoundEditorGraph* MetasoundGraph = CastChecked<UMetasoundEditorGraph>(&Graph);
			NewGraphNode->Output = MetasoundGraph->FindOrAddOutput(InNodeHandle);

			NodeCreator.Finalize();
			InitGraphNode(InNodeHandle, NewGraphNode, InMetaSound);

			SynchronizeNodeLocation(InLocation, InNodeHandle, *NewGraphNode);

			return NewGraphNode;
		}

		void FGraphBuilder::InitGraphNode(Frontend::FNodeHandle& InNodeHandle, UMetasoundEditorGraphNode* NewGraphNode, UObject& InMetaSound)
		{
			NewGraphNode->CreateNewGuid();
			NewGraphNode->SetNodeID(InNodeHandle->GetID());

			RebuildNodePins(*NewGraphNode);
		}

		bool FGraphBuilder::ValidateGraph(UObject& InMetaSound)
		{
			using namespace Frontend;

			TSharedPtr<SGraphEditor> GraphEditor;
			TSharedPtr<FEditor> MetaSoundEditor = GetEditorForMetasound(InMetaSound);
			if (MetaSoundEditor.IsValid())
			{
				GraphEditor = MetaSoundEditor->GetGraphEditor();
			}

			FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSound);
			check(MetaSoundAsset);
			UMetasoundEditorGraph& Graph = *CastChecked<UMetasoundEditorGraph>(&MetaSoundAsset->GetGraphChecked());

			FGraphValidationResults Results;

			bool bMarkDirty = false;

			Graph.ValidateInternal(Results);
			for (const FGraphNodeValidationResult& Result : Results.GetResults())
			{
				bMarkDirty |= Result.bIsDirty;
				if (GraphEditor.IsValid())
				{
					check(Result.Node);
					if (Result.bIsDirty || Result.Node->bRefreshNode)
					{
						GraphEditor->RefreshNode(*Result.Node);
						Result.Node->bRefreshNode = false;
					}
				}
			}

			if (MetaSoundEditor.IsValid())
			{
				MetaSoundEditor->RefreshInterface();
			}

			if (bMarkDirty)
			{
				InMetaSound.MarkPackageDirty();
			}

			return Results.IsValid();
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

		FName FGraphBuilder::GenerateUniqueNameByClassType(const UObject& InMetaSound, EMetasoundFrontendClassType InClassType, const FString& InBaseName)
		{
			const FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSound);
			check(MetaSoundAsset);

			// Get existing names.
			TArray<FName> ExistingNames;
			MetaSoundAsset->GetRootGraphHandle()->IterateConstNodes([&](const Frontend::FConstNodeHandle& Node)
			{
				ExistingNames.Add(Node->GetNodeName());
			}, InClassType);

			return GraphBuilderPrivate::GenerateUniqueName(ExistingNames, InBaseName);
		}

		TSharedPtr<FEditor> FGraphBuilder::GetEditorForMetasound(const UObject& Metasound)
		{
			// TODO: FToolkitManager is deprecated. Replace with UAssetEditorSubsystem.
			if (TSharedPtr<IToolkit> FoundAssetEditor = FToolkitManager::Get().FindEditorForAsset(&Metasound))
			{
				if (FEditor::EditorName == FoundAssetEditor->GetToolkitFName())
				{
					return StaticCastSharedPtr<FEditor, IToolkit>(FoundAssetEditor);
				}
			}

			return TSharedPtr<FEditor>(nullptr);
		}

		TSharedPtr<FEditor> FGraphBuilder::GetEditorForGraph(const UEdGraph& EdGraph)
		{
			const UMetasoundEditorGraph* MetasoundGraph = CastChecked<const UMetasoundEditorGraph>(&EdGraph);
			return GetEditorForMetasound(MetasoundGraph->GetMetasoundChecked());
		}

		FLinearColor FGraphBuilder::GetPinCategoryColor(const FEdGraphPinType& PinType)
		{
			const UMetasoundEditorSettings* Settings = GetDefault<UMetasoundEditorSettings>();
			check(Settings);

			if (PinType.PinCategory == PinCategoryAudio)
			{
				return Settings->AudioPinTypeColor;
			}

			if (PinType.PinCategory == PinCategoryTrigger)
			{
				return Settings->TriggerPinTypeColor;
			}

			if (PinType.PinCategory == PinCategoryBoolean)
			{
				return Settings->BooleanPinTypeColor;
			}

			if (PinType.PinCategory == PinCategoryFloat)
			{
				if (PinType.PinSubCategory == PinSubCategoryTime)
				{
					return Settings->TimePinTypeColor;
				}
				return Settings->FloatPinTypeColor;
			}

			if (PinType.PinCategory == PinCategoryInt32)
			{
				return Settings->IntPinTypeColor;
			}

			//if (PinType.PinCategory == PinCategoryInt64)
			//{
			//	return Settings->Int64PinTypeColor;
			//}

			if (PinType.PinCategory == PinCategoryString)
			{
				return Settings->StringPinTypeColor;
			}

			//if (PinType.PinCategory == PinCategoryDouble)
			//{
			//	return Settings->DoublePinTypeColor;
			//}

			if (PinType.PinCategory == PinCategoryObject)
			{
				return Settings->ObjectPinTypeColor;
			}

			return Settings->DefaultPinTypeColor;
		}

		Frontend::FInputHandle FGraphBuilder::GetInputHandleFromPin(const UEdGraphPin* InPin)
		{
			using namespace Frontend;

			if (InPin && ensure(InPin->Direction == EGPD_Input))
			{
				if (UMetasoundEditorGraphVariableNode* EdVariableNode = Cast<UMetasoundEditorGraphVariableNode>(InPin->GetOwningNode()))
				{
					// UEdGraphPins on variable nodes use the variable's name for display
					// purposes instead of the underlying vertex's name. The frontend vertices
					// of a variable node have consistent names no matter what the 
					// variable is named.
					return EdVariableNode->GetNodeHandle()->GetInputWithVertexName(VariableNames::GetInputDataName());
				}
				else if (UMetasoundEditorGraphNode* EdNode = CastChecked<UMetasoundEditorGraphNode>(InPin->GetOwningNode()))
				{
					return EdNode->GetNodeHandle()->GetInputWithVertexName(InPin->GetFName());
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
				if (UMetasoundEditorGraphVariableNode* EdVariableNode = Cast<UMetasoundEditorGraphVariableNode>(InPin->GetOwningNode()))
				{
					// UEdGraphPins on variable nodes use the variable's name for display
					// purposes instead of the underlying vertex's name. The frontend vertices
					// of a variable node have consistent names no matter what the 
					// variable is named.
					return EdVariableNode->GetNodeHandle()->GetOutputWithVertexName(VariableNames::GetOutputDataName());
				}
				else if (UMetasoundEditorGraphNode* EdNode = CastChecked<UMetasoundEditorGraphNode>(InPin->GetOwningNode()))
				{
					return EdNode->GetNodeHandle()->GetOutputWithVertexName(InPin->GetFName());
				}
			}

			return IOutputController::GetInvalidHandle();
		}

		Frontend::FConstOutputHandle FGraphBuilder::GetConstOutputHandleFromPin(const UEdGraphPin* InPin)
		{
			return GetOutputHandleFromPin(InPin);
		}

		bool FGraphBuilder::GraphContainsErrors(const UObject& InMetaSound)
		{
			const FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSound);
			check(MetaSoundAsset);
			const UMetasoundEditorGraph* EditorGraph = CastChecked<UMetasoundEditorGraph>(MetaSoundAsset->GetGraph());

			// Get all editor nodes from editor graph (some nodes on graph may *NOT* be metasound ed nodes,
			// such as comment boxes, etc, so just get nodes of class UMetasoundEditorGraph).
			TArray<const UMetasoundEditorGraphNode*> EditorNodes;
			EditorGraph->GetNodesOfClass(EditorNodes);

			// Do not synchronize with errors present as the graph is expected to be malformed.
			for (const UMetasoundEditorGraphNode* Node : EditorNodes)
			{
				if (Node->ErrorType == EMessageSeverity::Error)
				{
					return true;
				}
			}

			return false;
		}

		void FGraphBuilder::SynchronizeNodeLocation(FVector2D InLocation, Frontend::FNodeHandle InNodeHandle, UMetasoundEditorGraphNode& InNode)
		{
			InNode.NodePosX = InLocation.X;
			InNode.NodePosY = InLocation.Y;

			FMetasoundFrontendNodeStyle Style = InNodeHandle->GetNodeStyle();
			Style.Display.Locations.FindOrAdd(InNode.NodeGuid) = InLocation;
			InNodeHandle->SetNodeStyle(Style);
		}

		UMetasoundEditorGraphInputNode* FGraphBuilder::AddInputNode(UObject& InMetaSound, Frontend::FNodeHandle InNodeHandle, FVector2D InLocation, bool bInSelectNewNode)
		{
			using namespace Frontend;

			FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSound);
			check(MetaSoundAsset);

			UMetasoundEditorGraph* MetasoundGraph = Cast<UMetasoundEditorGraph>(MetaSoundAsset->GetGraph());
			if (!ensure(MetasoundGraph))
			{
				return nullptr;
			}

			UMetasoundEditorGraphInputNode* NewGraphNode = MetasoundGraph->CreateInputNode(InNodeHandle, bInSelectNewNode);
			if (ensure(NewGraphNode))
			{
				SynchronizeNodeLocation(InLocation, InNodeHandle, *NewGraphNode);

				RebuildNodePins(*NewGraphNode);
				return NewGraphNode;
			}

			return nullptr;
		}

		bool FGraphBuilder::GetPinLiteral(UEdGraphPin& InInputPin, FMetasoundFrontendLiteral& OutDefaultLiteral)
		{
			using namespace Frontend;

			IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetaSoundEditor");

			FInputHandle InputHandle = GetInputHandleFromPin(&InInputPin);
			if (!ensure(InputHandle->IsValid()))
			{
				return false;
			}

			const FString& InStringValue = InInputPin.DefaultValue;
			const FName TypeName = InputHandle->GetDataType();
			const FEditorDataType DataType = EditorModule.FindDataTypeChecked(TypeName);
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
						if (UClass* Class = IDataTypeRegistry::Get().GetUClassForDataType(TypeName))
						{
							FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

							// Remove class prefix if included in default value path
							FString ObjectPath = InInputPin.DefaultValue;
							ObjectPath.RemoveFromStart(Class->GetName() + TEXT(" "));

							FARFilter Filter;
							Filter.bRecursiveClasses = false;
							Filter.ObjectPaths.Add(*ObjectPath);

							TArray<FAssetData> AssetData;
							AssetRegistryModule.Get().GetAssets(Filter, AssetData);
							if (!AssetData.IsEmpty())
							{
								if (UObject* AssetObject = AssetData.GetData()->GetAsset())
								{
									const UClass* AssetClass = AssetObject->GetClass();
									if (ensureAlways(AssetClass))
									{
										if (AssetClass->IsChildOf(Class))
										{
											Filter.ClassNames.Add(Class->GetFName());
											OutDefaultLiteral.Set(AssetObject);
											bObjectFound = true;
										}
									}
								}
							}
						}
					}
					
					if (!bObjectFound)
					{
						OutDefaultLiteral.Set(static_cast<UObject*>(nullptr));
					}
				}
				break;

				case ELiteralType::BooleanArray:
				{
					OutDefaultLiteral.Set(TArray<bool>());
				}
				break;

				case ELiteralType::FloatArray:
				{
					OutDefaultLiteral.Set(TArray<float>());
				}
				break;

				case ELiteralType::IntegerArray:
				{
					OutDefaultLiteral.Set(TArray<int32>());
				}
				break;

				case ELiteralType::NoneArray:
				{
					OutDefaultLiteral.Set(FMetasoundFrontendLiteral::FDefaultArray());
				}
				break;

				case ELiteralType::StringArray:
				{
					OutDefaultLiteral.Set(TArray<FString>());
				}
				break;

				case ELiteralType::UObjectProxyArray:
				{
					OutDefaultLiteral.Set(TArray<UObject*>());
				}
				break;

				case ELiteralType::None:
				{
					OutDefaultLiteral.Set(FMetasoundFrontendLiteral::FDefault());
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

		Frontend::FNodeHandle FGraphBuilder::AddNodeHandle(UObject& InMetaSound, UMetasoundEditorGraphNode& InGraphNode)
		{
			using namespace Frontend;

			FNodeHandle NodeHandle = INodeController::GetInvalidHandle();
			if (UMetasoundEditorGraphInputNode* InputNode = Cast<UMetasoundEditorGraphInputNode>(&InGraphNode))
			{
				const TArray<UEdGraphPin*>& Pins = InGraphNode.GetAllPins();
				const UEdGraphPin* Pin = Pins.IsEmpty() ? nullptr : Pins[0];
				if (ensure(Pin) && ensure(Pin->Direction == EGPD_Output))
				{
					UMetasoundEditorGraphInput* Input = InputNode->Input;
					if (ensure(Input))
					{
						const FName PinName = Pin->GetFName();
						NodeHandle = AddInputNodeHandle(InMetaSound, Input->GetDataType(), nullptr, &PinName);
						NodeHandle->SetDescription(InGraphNode.GetTooltipText());
					}
				}
			}

			else if (UMetasoundEditorGraphOutputNode* OutputNode = Cast<UMetasoundEditorGraphOutputNode>(&InGraphNode))
			{
				const TArray<UEdGraphPin*>& Pins = InGraphNode.GetAllPins();
				const UEdGraphPin* Pin = Pins.IsEmpty() ? nullptr : Pins[0];
				if (ensure(Pin) && ensure(Pin->Direction == EGPD_Input))
				{
					UMetasoundEditorGraphOutput* Output = OutputNode->Output;
					if (ensure(Output))
					{
						const FName PinName = Pin->GetFName();
						NodeHandle = FGraphBuilder::AddOutputNodeHandle(InMetaSound, Output->GetDataType(), &PinName);
						NodeHandle->SetDescription(InGraphNode.GetTooltipText());
					}
				}
			}
			else if (UMetasoundEditorGraphVariableNode* VariableNode = Cast<UMetasoundEditorGraphVariableNode>(&InGraphNode))
			{
				NodeHandle = FGraphBuilder::AddVariableNodeHandle(InMetaSound, VariableNode->Variable->GetVariableID(), VariableNode->GetClassName().ToNodeClassName());
			}
			else if (UMetasoundEditorGraphExternalNode* ExternalNode = Cast<UMetasoundEditorGraphExternalNode>(&InGraphNode))
			{
				FMetasoundFrontendClass FrontendClass;
				bool bDidFindClassWithName = ISearchEngine::Get().FindClassWithHighestVersion(ExternalNode->ClassName.ToNodeClassName(), FrontendClass);
				if (ensure(bDidFindClassWithName))
				{
					FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSound);
					check(MetaSoundAsset);

					Frontend::FNodeHandle NewNode = MetaSoundAsset->GetRootGraphHandle()->AddNode(FrontendClass.Metadata);
					ExternalNode->SetNodeID(NewNode->GetID());

					NodeHandle = NewNode;
				}
			}

			if (NodeHandle->IsValid())
			{
				FMetasoundFrontendNodeStyle Style = NodeHandle->GetNodeStyle();
				Style.Display.Locations.Add(InGraphNode.NodeGuid, FVector2D(InGraphNode.NodePosX, InGraphNode.NodePosY));
				NodeHandle->SetNodeStyle(Style);
			}

			return NodeHandle;
		}

		Frontend::FNodeHandle FGraphBuilder::AddInputNodeHandle(UObject& InMetaSound, const FName InTypeName, const FMetasoundFrontendLiteral* InDefaultValue, const FName* InNameBase)
		{
			FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSound);
			check(MetaSoundAsset);

			const FName NewName = GenerateUniqueNameByClassType(InMetaSound, EMetasoundFrontendClassType::Input, InNameBase ? InNameBase->ToString() : TEXT("Input"));
			return MetaSoundAsset->GetRootGraphHandle()->AddInputVertex(NewName, InTypeName, InDefaultValue);
		}

		Frontend::FNodeHandle FGraphBuilder::AddOutputNodeHandle(UObject& InMetaSound, const FName InTypeName, const FName* InNameBase)
		{
			FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSound);
			check(MetaSoundAsset);

			const FName NewName = GenerateUniqueNameByClassType(InMetaSound, EMetasoundFrontendClassType::Output, InNameBase ? InNameBase->ToString() : TEXT("Output"));
			return MetaSoundAsset->GetRootGraphHandle()->AddOutputVertex(NewName, InTypeName);
		}

		FName FGraphBuilder::GenerateUniqueVariableName(const Frontend::FConstGraphHandle& InFrontendGraph, const FString& InBaseName)
		{
			using namespace Frontend;

			TArray<FName> ExistingVariableNames;

			// Get all the names from the existing variables on the graph
			// and place into the ExistingVariableNames array.
			Algo::Transform(InFrontendGraph->GetVariables(), ExistingVariableNames, [](const FConstVariableHandle& Var) { return Var->GetName(); });

			return GraphBuilderPrivate::GenerateUniqueName(ExistingVariableNames, InBaseName);
		}

		Frontend::FVariableHandle FGraphBuilder::AddVariableHandle(UObject& InMetaSound, const FName& InTypeName)
		{
			using namespace Frontend;

			FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSound);
			check(MetaSoundAsset);

			FGraphHandle FrontendGraph = MetaSoundAsset->GetRootGraphHandle();

			FText BaseDisplayName = LOCTEXT("VariableDefaultDisplayName", "Variable");

			FString BaseName = BaseDisplayName.ToString();
			FName VariableName = GenerateUniqueVariableName(FrontendGraph, BaseName);
			FVariableHandle Variable = FrontendGraph->AddVariable(InTypeName);

			Variable->SetDisplayName(FText::GetEmpty());
			Variable->SetName(VariableName);

			return Variable;
		}

		Frontend::FNodeHandle FGraphBuilder::AddVariableNodeHandle(UObject& InMetaSound, const FGuid& InVariableID, const Metasound::FNodeClassName& InVariableNodeClassName)
		{
			using namespace Frontend;

			FNodeHandle FrontendNode = INodeController::GetInvalidHandle();

			FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSound);
			
			if (ensure(MetaSoundAsset))
			{
				FMetasoundFrontendClass FrontendClass;
				bool bDidFindClassWithName = ISearchEngine::Get().FindClassWithHighestVersion(InVariableNodeClassName, FrontendClass);
				if (ensure(bDidFindClassWithName))
				{
					FGraphHandle Graph = MetaSoundAsset->GetRootGraphHandle();

					switch (FrontendClass.Metadata.GetType())
					{
						case EMetasoundFrontendClassType::VariableDeferredAccessor:
							FrontendNode = Graph->AddVariableDeferredAccessorNode(InVariableID);
							break;

						case EMetasoundFrontendClassType::VariableAccessor:
							FrontendNode = Graph->AddVariableAccessorNode(InVariableID);
							break;

						case EMetasoundFrontendClassType::VariableMutator:
							{
								FConstVariableHandle Variable = Graph->FindVariable(InVariableID);
								FConstNodeHandle ExistingMutator = Variable->FindMutatorNode();
								if (!ExistingMutator->IsValid())
								{
									FrontendNode = Graph->FindOrAddVariableMutatorNode(InVariableID);
								}
								else
								{
									UE_LOG(LogMetaSound, Error, TEXT("Cannot add node because \"%s\" already exists for variable \"%s\""), *ExistingMutator->GetDisplayName().ToString(), *Variable->GetDisplayName().ToString());
								}
							}
							break;

						default:
							{
								checkNoEntry();
							}
					}
				}
			}

			return FrontendNode;
		}

		UMetasoundEditorGraphNode* FGraphBuilder::AddNode(UObject& InMetaSound, Frontend::FNodeHandle InNodeHandle, FVector2D InLocation, bool bInSelectNewNode)
		{
			switch (InNodeHandle->GetClassMetadata().GetType())
			{
				case EMetasoundFrontendClassType::Input:
				{
					return CastChecked<UMetasoundEditorGraphNode>(AddInputNode(InMetaSound, InNodeHandle, InLocation, bInSelectNewNode));
				}
				break;

				case EMetasoundFrontendClassType::External:
				{
					return CastChecked<UMetasoundEditorGraphNode>(AddExternalNode(InMetaSound, InNodeHandle, InLocation, bInSelectNewNode));
				}
				break;

				case EMetasoundFrontendClassType::Output:
				{
					return CastChecked<UMetasoundEditorGraphNode>(AddOutputNode(InMetaSound, InNodeHandle, InLocation, bInSelectNewNode));
				}
				break;

				case EMetasoundFrontendClassType::VariableMutator:
				case EMetasoundFrontendClassType::VariableAccessor:
				case EMetasoundFrontendClassType::VariableDeferredAccessor:
				case EMetasoundFrontendClassType::Variable:
				{
					return CastChecked<UMetasoundEditorGraphNode>(AddVariableNode(InMetaSound, InNodeHandle, InLocation, bInSelectNewNode));
				}
				break;

				case EMetasoundFrontendClassType::Invalid:
				case EMetasoundFrontendClassType::Graph:
				
				case EMetasoundFrontendClassType::Literal: // Not yet supported in editor
				
				default:
				{
					checkNoEntry();
					static_assert(static_cast<int32>(EMetasoundFrontendClassType::Invalid) == 9, "Possible missing FMetasoundFrontendClassType case coverage");
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
				return false;
			}

			if (!ensure(InputHandle->Connect(*OutputHandle)))
			{
				InInputPin.BreakLinkTo(&InOutputPin);
				return false;
			}

			return true;
		}

		void FGraphBuilder::DisconnectPinVertex(UEdGraphPin& InPin, bool bAddLiteralInputs)
		{
			using namespace Editor;
			using namespace Frontend;

			TArray<FInputHandle> InputHandles;
			TArray<UEdGraphPin*> InputPins;

			UMetasoundEditorGraphNode* Node = CastChecked<UMetasoundEditorGraphNode>(InPin.GetOwningNode());

			if (InPin.Direction == EGPD_Input)
			{
				const FName PinName = InPin.GetFName();

				FNodeHandle NodeHandle = Node->GetNodeHandle();
				FInputHandle InputHandle = NodeHandle->GetInputWithVertexName(PinName);

				// Input can be invalid if renaming a vertex member
				if (InputHandle->IsValid())
				{
					InputHandles.Add(InputHandle);
					InputPins.Add(&InPin);
				}
			}
			else
			{
				check(InPin.Direction == EGPD_Output);
				for (UEdGraphPin* Pin : InPin.LinkedTo)
				{
					check(Pin);
					FNodeHandle NodeHandle = CastChecked<UMetasoundEditorGraphNode>(Pin->GetOwningNode())->GetNodeHandle();
					FInputHandle InputHandle = NodeHandle->GetInputWithVertexName(Pin->GetFName());

					// Input can be invalid if renaming a vertex member
					if (InputHandle->IsValid())
					{
						InputHandles.Add(InputHandle);
						InputPins.Add(Pin);
					}
				}
			}

			for (int32 i = 0; i < InputHandles.Num(); ++i)
			{
				FInputHandle InputHandle = InputHandles[i];
				FConstOutputHandle OutputHandle = InputHandle->GetConnectedOutput();

				InputHandle->Disconnect();

				if (bAddLiteralInputs)
				{
					FNodeHandle NodeHandle = InputHandle->GetOwningNode();
					SynchronizePinLiteral(*InputPins[i]);
				}
			}

			UObject& MetaSound = Node->GetMetasoundChecked();
			FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&MetaSound);
			MetaSoundAsset->SetSynchronizationRequired();
		}

		void FGraphBuilder::InitMetaSound(UObject& InMetaSound, const FString& InAuthor)
		{
			using namespace Frontend;
			using namespace GraphBuilderPrivate;

			FMetasoundFrontendClassMetadata Metadata;

			// 1. Set default class Metadata
			Metadata.SetClassName(FMetasoundFrontendClassName(FName(), *FGuid::NewGuid().ToString(), FName()));
			Metadata.SetVersion({ 1, 0 });
			Metadata.SetDisplayName(FText::FromString(InMetaSound.GetName()));
			Metadata.SetType(EMetasoundFrontendClassType::Graph);
			Metadata.SetAuthor(FText::FromString(InAuthor));

			FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSound);
			check(MetaSoundAsset);
			MetaSoundAsset->SetMetadata(Metadata);

			// 2. Set default doc version Metadata
			FDocumentHandle DocumentHandle = MetaSoundAsset->GetDocumentHandle();
			FMetasoundFrontendDocumentMetadata DocMetadata = DocumentHandle->GetMetadata();
			DocMetadata.Version.Number = FVersionDocument::GetMaxVersion();
			DocumentHandle->SetMetadata(DocMetadata);

			MetaSoundAsset->AddDefaultInterfaces();

			FGraphHandle GraphHandle = MetaSoundAsset->GetRootGraphHandle();
			FVector2D InputNodeLocation = FVector2D::ZeroVector;
			FVector2D ExternalNodeLocation = InputNodeLocation + DisplayStyle::NodeLayout::DefaultOffsetX;
			FVector2D OutputNodeLocation = ExternalNodeLocation + DisplayStyle::NodeLayout::DefaultOffsetX;

			TArray<FNodeHandle> NodeHandles = GraphHandle->GetNodes();
			for (FNodeHandle& NodeHandle : NodeHandles)
			{
				const EMetasoundFrontendClassType NodeType = NodeHandle->GetClassMetadata().GetType();
				FVector2D NewLocation;
				if (NodeType == EMetasoundFrontendClassType::Input)
				{
					NewLocation = InputNodeLocation;
					InputNodeLocation += DisplayStyle::NodeLayout::DefaultOffsetY;
				}
				else if (NodeType == EMetasoundFrontendClassType::Output)
				{
					NewLocation = OutputNodeLocation;
					OutputNodeLocation += DisplayStyle::NodeLayout::DefaultOffsetY;
				}
				else
				{
					NewLocation = ExternalNodeLocation;
					ExternalNodeLocation += DisplayStyle::NodeLayout::DefaultOffsetY;
				}
				FMetasoundFrontendNodeStyle Style = NodeHandle->GetNodeStyle();
				// TODO: Find consistent location for controlling node locations.
				// Currently it is split between MetasoundEditor and MetasoundFrontend modules.
				Style.Display.Locations = {{FGuid::NewGuid(), NewLocation}};
				NodeHandle->SetNodeStyle(Style);
			}
		}

		void FGraphBuilder::InitMetaSoundPreset(UObject& InMetaSoundReferenced, UObject& InMetaSoundPreset)
		{
			using namespace Frontend;
			using namespace GraphBuilderPrivate;

			FMetasoundAssetBase* PresetAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSoundPreset);
			check(PresetAsset);

			// Mark preset as auto-update and non-editable
			FGraphHandle PresetGraphHandle = PresetAsset->GetRootGraphHandle();
			FMetasoundFrontendGraphStyle Style = PresetGraphHandle->GetGraphStyle();
			Style.bIsGraphEditable = false;
			PresetGraphHandle->SetGraphStyle(Style);

			FMetasoundFrontendClassMetadata Metadata = PresetGraphHandle->GetGraphMetadata();
			Metadata.SetAutoUpdateManagesInterface(true);
			PresetGraphHandle->SetGraphMetadata(Metadata);

			FGraphBuilder::RegisterGraphWithFrontend(InMetaSoundReferenced);

			const FMetasoundAssetBase* ReferencedAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSoundReferenced);
			check(ReferencedAsset);

			FRebuildPresetRootGraph(ReferencedAsset->GetDocumentHandle()).Transform(PresetAsset->GetDocumentHandle());
			PresetAsset->ConformObjectDataToInterfaces();
		}

		bool FGraphBuilder::DeleteNode(UEdGraphNode& InNode)
		{
			using namespace Frontend;

			if (!InNode.CanUserDeleteNode())
			{
				return false;
			}

			bool bWasErroredNode = InNode.ErrorType == EMessageSeverity::Error;

			// If node isn't a MetasoundEditorGraphNode, just remove and return (ex. comment nodes)
			UMetasoundEditorGraphNode* Node = Cast<UMetasoundEditorGraphNode>(&InNode);
			UMetasoundEditorGraph* Graph = CastChecked<UMetasoundEditorGraph>(InNode.GetGraph());
			if (!Node)
			{
				Graph->RemoveNode(&InNode);
				return true;
			}


			// Remove connects only to pins associated with this EdGraph node
			// only (Iterate pins and not Frontend representation to preserve
			// other input/output EditorGraph reference node associations)
			Node->IteratePins([](UEdGraphPin& Pin, int32 Index)
			{
				// Only add literal inputs for output pins as adding when disconnecting
				// inputs would immediately orphan them on EditorGraph node removal below.
				const bool bAddLiteralInputs = Pin.Direction == EGPD_Output;
				FGraphBuilder::DisconnectPinVertex(Pin, bAddLiteralInputs);
			});

			FNodeHandle NodeHandle = Node->GetNodeHandle();
			Frontend::FGraphHandle GraphHandle = NodeHandle->GetOwningGraph();
			if (GraphHandle->IsValid())
			{
				switch (NodeHandle->GetClassMetadata().GetType())
				{
					case EMetasoundFrontendClassType::Output:
					case EMetasoundFrontendClassType::Input:
					{
						// NodeHandle does not get removed in these cases as EdGraph Inputs/Outputs
						// merely reference their respective types set on the MetasoundGraph. It must
						// be removed from the location display data however for graph sync reasons.
						FMetasoundFrontendNodeStyle Style = NodeHandle->GetNodeStyle();
						Style.Display.Locations.Remove(InNode.NodeGuid);
						NodeHandle->SetNodeStyle(Style);
					}
					break;

					case EMetasoundFrontendClassType::Graph:
					case EMetasoundFrontendClassType::Literal:
					case EMetasoundFrontendClassType::VariableAccessor:
					case EMetasoundFrontendClassType::VariableDeferredAccessor:
					case EMetasoundFrontendClassType::VariableMutator:
					case EMetasoundFrontendClassType::Variable:
					case EMetasoundFrontendClassType::External:
					default:
					{
						static_assert(static_cast<int32>(EMetasoundFrontendClassType::Invalid) == 9, "Possible missing MetasoundFrontendClassType switch case coverage.");

						if (ensure(GraphHandle->RemoveNode(*NodeHandle)))
						{
							GraphHandle->GetOwningDocument()->RemoveUnreferencedDependencies();
						}
					}
					break;
				}
			}

			return ensure(Graph->RemoveNode(&InNode));
		}

		void FGraphBuilder::RebuildNodePins(UMetasoundEditorGraphNode& InGraphNode)
		{
			using namespace Frontend;
		
			for (int32 i = InGraphNode.Pins.Num() - 1; i >= 0; i--)
			{
				InGraphNode.RemovePin(InGraphNode.Pins[i]);
			}

			// TODO: Make this a utility in Frontend (ClearInputLiterals())
			FNodeHandle NodeHandle = InGraphNode.GetNodeHandle();
			TArray<FInputHandle> Inputs = NodeHandle->GetInputs();
			for (FInputHandle& Input : Inputs)
			{
				NodeHandle->ClearInputLiteral(Input->GetID());
			}

			TArray<FInputHandle> InputHandles = NodeHandle->GetInputs();
			InputHandles = NodeHandle->GetInputStyle().SortDefaults(InputHandles);
			for (const FInputHandle& InputHandle : InputHandles)
			{
				// Only add pins of the node if the connection is user modifiable. 
				// Connections which the user cannot modify are controlled elsewhere.
				if (InputHandle->IsConnectionUserModifiable())
				{
					AddPinToNode(InGraphNode, InputHandle);
				}
			}

			TArray<FOutputHandle> OutputHandles = NodeHandle->GetOutputs();
			OutputHandles = NodeHandle->GetOutputStyle().SortDefaults(OutputHandles);
			for (const FOutputHandle& OutputHandle : OutputHandles)
			{
				// Only add pins of the node if the connection is user modifiable. 
				// Connections which the user cannot modify are controlled elsewhere.
				if (OutputHandle->IsConnectionUserModifiable())
				{
					AddPinToNode(InGraphNode, OutputHandle);
				}
			}

			InGraphNode.bRefreshNode = true;
		}

		void FGraphBuilder::RefreshPinMetadata(UEdGraphPin& InPin, const FMetasoundFrontendVertexMetadata& InMetadata)
		{
			InPin.PinToolTip = InMetadata.GetDescription().ToString();
			InPin.bAdvancedView = InMetadata.bIsAdvancedDisplay;
			if (InPin.bAdvancedView)
			{
				UEdGraphNode* OwningNode = InPin.GetOwningNode();
				check(OwningNode);
				if (OwningNode->AdvancedPinDisplay == ENodeAdvancedPins::NoPins)
				{
					OwningNode->AdvancedPinDisplay = ENodeAdvancedPins::Hidden;
				}

				if (UMetasoundEditorGraphNode* MetaSoundNode = Cast<UMetasoundEditorGraphNode>(OwningNode))
				{
					MetaSoundNode->bRefreshNode = true;
				}
			}
		}

		void FGraphBuilder::RegisterGraphWithFrontend(UObject& InMetaSound)
		{
			using namespace Frontend;

			FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSound);
			check(MetaSoundAsset);

			TArray<FMetasoundAssetBase*> EditedReferencingMetaSounds;
			if (GEditor)
			{
				TArray<UObject*> EditedAssets = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->GetAllEditedAssets();
				for (UObject* Asset : EditedAssets)
				{
					if (Asset != &InMetaSound)
					{
						if (FMetasoundAssetBase* EditedMetaSound = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(Asset))
						{
							EditedMetaSound->RebuildReferencedAssetClassKeys();
							if (EditedMetaSound->IsReferencedAsset(*MetaSoundAsset))
							{
								EditedReferencingMetaSounds.Add(EditedMetaSound);
							}
						}
					}
				}
			}

			FMetaSoundAssetRegistrationOptions RegOptions;
			RegOptions.bForceReregister = true;
			RegOptions.bRegisterDependencies = true;

			// if EditedReferencingMetaSounds is empty, then no MetaSounds are open
			// that reference this MetaSound, so just register this asset. Otherwise,
			// this graph will recursively get updated when the open referencing graphs
			// are registered recursively via bRegisterDependencies flag.
			if (EditedReferencingMetaSounds.IsEmpty())
			{
				MetaSoundAsset->RegisterGraphWithFrontend(RegOptions);
			}
			else
			{
				for (FMetasoundAssetBase* MetaSound : EditedReferencingMetaSounds)
				{
					MetaSound->RegisterGraphWithFrontend(RegOptions);
				}
			}
		}

		void FGraphBuilder::UnregisterGraphWithFrontend(UObject& InMetaSound)
		{
			FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSound);
			if (!ensure(MetaSoundAsset))
			{
				return;
			}

			if (GEditor)
			{
				TArray<UObject*> EditedAssets = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->GetAllEditedAssets();
				for (UObject* Asset : EditedAssets)
				{
					if (Asset != &InMetaSound)
					{
						if (FMetasoundAssetBase* EditedMetaSound = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(Asset))
						{
							if (EditedMetaSound->IsReferencedAsset(*MetaSoundAsset))
							{
								EditedMetaSound->SetSynchronizationRequired();
							}
						}
					}
				}
			}

			MetaSoundAsset->UnregisterGraphWithFrontend();
		}

		void FGraphBuilder::MarkEditorNodesReferencingAssetForRefresh(UObject& InMetaSound)
		{
			using namespace Frontend;

			FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSound);
			if (!MetaSoundAsset)
			{
				return;
			}

			if (!GEditor)
			{
				return;
			}

			bool bGraphUpdated = false;

			FMetasoundFrontendClassMetadata AssetClassMetadata = MetaSoundAsset->GetRootGraphHandle()->GetGraphMetadata();
			AssetClassMetadata.SetType(EMetasoundFrontendClassType::External);
			const FNodeRegistryKey AssetClassKey = NodeRegistryKey::CreateKey(AssetClassMetadata);

			TArray<UObject*> EditedAssets = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->GetAllEditedAssets();
			for (UObject* EditedAsset : EditedAssets)
			{
				if (FMetasoundAssetBase* EditedMetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(EditedAsset))
				{
					const UEdGraph& Graph = EditedMetaSoundAsset->GetGraphChecked();
					TArray<UMetasoundEditorGraphExternalNode*> ExternalNodes;
					Graph.GetNodesOfClass<UMetasoundEditorGraphExternalNode>(ExternalNodes);
					for (UMetasoundEditorGraphExternalNode* Node : ExternalNodes)
					{
						FConstNodeHandle NodeHandle = Node->GetConstNodeHandle();
						const FMetasoundFrontendClassMetadata& ClassMetadata = NodeHandle->GetClassMetadata();
						const FNodeRegistryKey RegistryKey = NodeRegistryKey::CreateKey(ClassMetadata);

						if (AssetClassKey == RegistryKey)
						{
							bGraphUpdated = true;
							Node->bRefreshNode = true;
						}
					}
				}
			}

			if (bGraphUpdated)
			{
				MetaSoundAsset->SetSynchronizationRequired();
			}
		}

		bool FGraphBuilder::IsMatchingInputHandleAndPin(const Frontend::FConstInputHandle& InInputHandle, const UEdGraphPin& InEditorPin)
		{
			if (InEditorPin.Direction != EGPD_Input)
			{
				return false;
			}

			Frontend::FInputHandle PinInputHandle = GetInputHandleFromPin(&InEditorPin);
			if (PinInputHandle->GetID() == InInputHandle->GetID())
			{
				return true;
			}

			return false;
		}

		bool FGraphBuilder::IsMatchingOutputHandleAndPin(const Frontend::FConstOutputHandle& InOutputHandle, const UEdGraphPin& InEditorPin)
		{
			if (InEditorPin.Direction != EGPD_Output)
			{
				return false;
			}

			Frontend::FOutputHandle PinOutputHandle = GetOutputHandleFromPin(&InEditorPin);
			if (PinOutputHandle->GetID() == InOutputHandle->GetID())
			{
				return true;
			}

			return false;
		}

		void FGraphBuilder::DepthFirstTraversal(UEdGraphNode* InInitialNode, FDepthFirstVisitFunction InVisitFunction)
		{
			// Non recursive depth first traversal.
			TArray<UEdGraphNode*> Stack({InInitialNode});
			TSet<UEdGraphNode*> Visited;

			while (Stack.Num() > 0)
			{
				UEdGraphNode* CurrentNode = Stack.Pop();
				if (Visited.Contains(CurrentNode))
				{
					// Do not revisit a node that has already been visited. 
					continue;
				}

				TArray<UEdGraphNode*> Children = InVisitFunction(CurrentNode).Array();
				Stack.Append(Children);

				Visited.Add(CurrentNode);
			}
		}

		UEdGraphPin* FGraphBuilder::AddPinToNode(UMetasoundEditorGraphNode& InEditorNode, Frontend::FConstInputHandle InInputHandle)
		{
			using namespace Frontend;

			FEdGraphPinType PinType;
			FName DataTypeName = InInputHandle->GetDataType();

			IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetaSoundEditor");
			if (const FEditorDataType* EditorDataType = EditorModule.FindDataType(DataTypeName))
			{
				PinType = EditorDataType->PinType;
			}

			FName PinName = FGraphBuilder::GetPinName(*InInputHandle);
			UEdGraphPin* NewPin = InEditorNode.CreatePin(EGPD_Input, PinType, PinName);
			if (ensure(NewPin))
			{
				RefreshPinMetadata(*NewPin, InInputHandle->GetMetadata());
				SynchronizePinLiteral(*NewPin);
			}

			return NewPin;
		}

		UEdGraphPin* FGraphBuilder::AddPinToNode(UMetasoundEditorGraphNode& InEditorNode, Frontend::FConstOutputHandle InOutputHandle)
		{
			FEdGraphPinType PinType;
			FName DataTypeName = InOutputHandle->GetDataType();

			IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetaSoundEditor");
			if (const FEditorDataType* EditorDataType = EditorModule.FindDataType(DataTypeName))
			{
				PinType = EditorDataType->PinType;
			}

			FName PinName = FGraphBuilder::GetPinName(*InOutputHandle);
			UEdGraphPin* NewPin = InEditorNode.CreatePin(EGPD_Output, PinType, PinName);
			if (ensure(NewPin))
			{
				RefreshPinMetadata(*NewPin, InOutputHandle->GetMetadata());
			}

			InEditorNode.bRefreshNode = true;
			return NewPin;
		}

		bool FGraphBuilder::SynchronizePinType(const IMetasoundEditorModule& InEditorModule, UEdGraphPin& InPin, const FName InDataType)
		{
			FEdGraphPinType PinType;
			if (const FEditorDataType* EditorDataType = InEditorModule.FindDataType(InDataType))
			{
				PinType = EditorDataType->PinType;
			}

			if (InPin.PinType != PinType)
			{
				if (const UMetasoundEditorGraphNode* Node = Cast<UMetasoundEditorGraphNode>(InPin.GetOwningNodeUnchecked()))
				{
					const FString NodeName = Node->GetDisplayName().ToString();
					UE_LOG(LogMetasoundEditor, Verbose, TEXT("Synchronizing Pin '%s' on Node '%s': Type converted to '%s'"), *NodeName, *InPin.GetName(), *InDataType.ToString());
				}
				InPin.PinType = PinType;
				return true;
			}

			return false;
		}

		bool FGraphBuilder::SynchronizeConnections(UObject& InMetaSound)
		{
			using namespace Frontend;

			bool bIsGraphDirty = false;

			FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSound);
			check(MetaSoundAsset);

			FGraphHandle GraphHandle = MetaSoundAsset->GetRootGraphHandle();

			UMetasoundEditorGraph* EditorGraph = CastChecked<UMetasoundEditorGraph>(MetaSoundAsset->GetGraph());

			TArray<UMetasoundEditorGraphNode*> EditorNodes;
			EditorGraph->GetNodesOfClass(EditorNodes);

			TMap<FGuid, TArray<UMetasoundEditorGraphNode*>> EditorNodesByFrontendID;
			for (UMetasoundEditorGraphNode* EditorNode : EditorNodes)
			{
				EditorNodesByFrontendID.FindOrAdd(EditorNode->GetNodeID()).Add(EditorNode);
			}

			// Iterate through all nodes in metasound editor graph and synchronize connections.
			for (UMetasoundEditorGraphNode* EditorNode : EditorNodes)
			{
				bool bIsNodeDirty = false;

				FConstNodeHandle Node = EditorNode->GetNodeHandle();

				TArray<UEdGraphPin*> Pins = EditorNode->GetAllPins();
				TArray<FConstInputHandle> NodeInputs = Node->GetConstInputs();

				// Ignore connections which are not handled by the editor.
				NodeInputs.RemoveAll([](const FConstInputHandle& FrontendInput) { return !FrontendInput->IsConnectionUserModifiable(); });

				for (FConstInputHandle& NodeInput : NodeInputs)
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

					FConstOutputHandle OutputHandle = NodeInput->GetConnectedOutput();
					if (OutputHandle->IsValid())
					{
						// Both input and output handles be user modifiable for a
						// connection to be controlled by the editor.
						check(OutputHandle->IsConnectionUserModifiable());

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
							const FGuid NodeID = OutputHandle->GetOwningNodeID();
							TArray<UMetasoundEditorGraphNode*>* OutputEditorNode = EditorNodesByFrontendID.Find(NodeID);
							if (ensure(OutputEditorNode))
							{
								if (ensure(!OutputEditorNode->IsEmpty()))
								{
									UEdGraphPin* OutputPin = (*OutputEditorNode)[0]->FindPinChecked(OutputHandle->GetName(), EEdGraphPinDirection::EGPD_Output);
									const FText& OwningNodeName = EditorNode->GetDisplayName();

									UE_LOG(LogMetasoundEditor, Verbose, TEXT("Synchronizing Node '%s' Connection: Linking Pin '%s' to '%s'"), *OwningNodeName.ToString(), *MatchingPin->GetName(), *OutputPin->GetName());
									MatchingPin->MakeLinkTo(OutputPin);
									bIsNodeDirty = true;
								}
							}
						}
					}
					else
					{
						// No link should exist.
						if (!MatchingPin->LinkedTo.IsEmpty())
						{
							MatchingPin->BreakAllPinLinks();
							const FText OwningNodeName = EditorNode->GetDisplayName();
							const FText InputName = FGraphBuilder::GetDisplayName(*NodeInput);
							UE_LOG(LogMetasoundEditor, Verbose, TEXT("Synchronizing Node '%s' Connection: Breaking all pin links to '%s'"), *OwningNodeName.ToString(), *InputName.ToString());
							bIsNodeDirty = true;
						}
					}

					SynchronizePinLiteral(*MatchingPin);
				}

				bIsGraphDirty |= bIsNodeDirty;
			}

			return bIsGraphDirty;
		}

		bool FGraphBuilder::SynchronizeGraph(UObject& InMetaSound)
		{
			bool bIsEditorGraphDirty = SynchronizeGraphVertices(InMetaSound);
			bIsEditorGraphDirty |= SynchronizeNodeMembers(InMetaSound);
			bIsEditorGraphDirty |= SynchronizeNodes(InMetaSound);
			bIsEditorGraphDirty |= SynchronizeConnections(InMetaSound);

			if (bIsEditorGraphDirty)
			{
				InMetaSound.MarkPackageDirty();
			}

			const bool bIsValid = FGraphBuilder::ValidateGraph(InMetaSound);

			FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSound);
			check(MetaSoundAsset);
			MetaSoundAsset->ResetSynchronizationState();

			return bIsValid;
		}

		bool FGraphBuilder::SynchronizeNodeMembers(UObject& InMetaSound)
		{
			using namespace Frontend;

			bool bIsEditorGraphDirty = false;

			FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSound);
			check(MetaSoundAsset);
			FGraphHandle GraphHandle = MetaSoundAsset->GetRootGraphHandle();
			UMetasoundEditorGraph* EditorGraph = CastChecked<UMetasoundEditorGraph>(MetaSoundAsset->GetGraph());

			TArray<UMetasoundEditorGraphInputNode*> InputNodes;
			EditorGraph->GetNodesOfClassEx<UMetasoundEditorGraphInputNode>(InputNodes);
			for (UMetasoundEditorGraphInputNode* Node : InputNodes)
			{
				check(Node);
				FConstNodeHandle NodeHandle = Node->GetConstNodeHandle();
				if (!NodeHandle->IsValid())
				{
					for (UEdGraphPin* Pin : Node->Pins)
					{
						check(Pin);

						FConstClassInputAccessPtr ClassInputPtr = GraphHandle->FindClassInputWithName(Pin->PinName);
						if (const FMetasoundFrontendClassInput* Input = ClassInputPtr.Get())
						{
							const FGuid& InitialID = Node->GetNodeID();
							if (Node->GetNodeHandle()->GetID() != Input->NodeID)
							{
								Node->SetNodeID(Input->NodeID);

								// Requery handle as the id has been fixed up
								NodeHandle = Node->GetConstNodeHandle();
								FText InputDisplayName = Node->GetDisplayName();
								UE_LOG(LogMetasoundEditor, Verbose, TEXT("Editor Input Node '%s' interface versioned"), *InputDisplayName.ToString());

								bIsEditorGraphDirty = true;
							}
						}
					}
				}
			}

			TArray<UMetasoundEditorGraphOutputNode*> OutputNodes;
			EditorGraph->GetNodesOfClassEx<UMetasoundEditorGraphOutputNode>(OutputNodes);
			for (UMetasoundEditorGraphOutputNode* Node : OutputNodes)
			{
				FConstNodeHandle NodeHandle = Node->GetConstNodeHandle();
				if (!NodeHandle->IsValid())
				{
					for (UEdGraphPin* Pin : Node->Pins)
					{
						check(Pin);
						FConstClassOutputAccessPtr ClassOutputPtr = GraphHandle->FindClassOutputWithName(Pin->PinName);
						if (const FMetasoundFrontendClassOutput* Output = ClassOutputPtr.Get())
						{
							const FGuid& InitialID = Node->GetNodeID();
							if (Node->GetNodeHandle()->GetID() != Output->NodeID)
							{
								Node->SetNodeID(Output->NodeID);

								// Requery handle as the id has been fixed up
								NodeHandle = Node->GetConstNodeHandle();
								FText OutputDisplayName = Node->GetDisplayName();
								UE_LOG(LogMetasoundEditor, Verbose, TEXT("Editor Output Node '%s' interface versioned"), *OutputDisplayName.ToString());

								bIsEditorGraphDirty = true;
							}
						}
					}
				}
			}

			return bIsEditorGraphDirty;
		}

		bool FGraphBuilder::SynchronizeNodes(UObject& InMetaSound)
		{
			using namespace Frontend;

			bool bIsEditorGraphDirty = false;

			// Get all external nodes from Frontend graph.  Input and output references will only be added/synchronized
			// if required when synchronizing connections (as they are not required to inhabit editor graph).
			FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSound);
			check(MetaSoundAsset);
			FGraphHandle GraphHandle = MetaSoundAsset->GetRootGraphHandle();
			TArray<FNodeHandle> FrontendNodes = GraphHandle->GetNodes();
			UMetasoundEditorGraph* EditorGraph = CastChecked<UMetasoundEditorGraph>(MetaSoundAsset->GetGraph());
			TArray<UMetasoundEditorGraphNode*> EditorNodes;
			EditorGraph->GetNodesOfClass(EditorNodes);

			TMap<FGuid, UMetasoundEditorGraphNode*> EditorNodesByEdNodeGuid;
			for (UMetasoundEditorGraphNode* Node : EditorNodes)
			{
				EditorNodesByEdNodeGuid.Add(Node->NodeGuid, Node);
			}

			// Find existing array of editor nodes associated with Frontend node
			struct FAssociatedNodes
			{
				TArray<UMetasoundEditorGraphNode*> EditorNodes;
				FNodeHandle Node = Metasound::Frontend::INodeController::GetInvalidHandle();
			};
			TMap<FGuid, FAssociatedNodes> AssociatedNodes;

			// Reverse iterate so paired nodes can safely be removed from the array.
			for (int32 i = FrontendNodes.Num() - 1; i >= 0; i--)
			{
				FNodeHandle Node = FrontendNodes[i];
				auto IsEditorNodeWithSameNodeID = [&](const UMetasoundEditorGraphNode* InEditorNode)
				{
					return InEditorNode->GetNodeID() == Node->GetID();
				};

				bool bFoundEditorNode = false;
				for (int32 j = EditorNodes.Num() - 1; j >= 0; --j)
				{
					UMetasoundEditorGraphNode* EditorNode = EditorNodes[j];
					if (EditorNode->GetNodeID() == Node->GetID())
					{
						bFoundEditorNode = true;
						FAssociatedNodes& AssociatedNodeData = AssociatedNodes.FindOrAdd(Node->GetID());
						if (AssociatedNodeData.Node->IsValid())
						{
							ensure(AssociatedNodeData.Node == Node);
						}
						else
						{
							AssociatedNodeData.Node = Node;
						}

						AssociatedNodeData.EditorNodes.Add(EditorNode);
						EditorNodes.RemoveAtSwap(j, 1, false /* bAllowShrinking */);
					}
				}

				if (bFoundEditorNode)
				{
					FrontendNodes.RemoveAtSwap(i, 1, false /* bAllowShrinking */);
				}
			}

			// FrontendNodes now contains nodes which need to be added to the editor graph.
			// EditorNodes now contains nodes that need to be removed from the editor graph.
			// AssociatedNodes contains pairs which we have to check have synchronized pins

			// Add and remove nodes first in order to make sure correct editor nodes
			// exist before attempting to synchronize connections.
			for (UMetasoundEditorGraphNode* EditorNode : EditorNodes)
			{
				bIsEditorGraphDirty |= EditorGraph->RemoveNode(EditorNode);
			}

			// Add missing editor nodes marked as visible.
			for (FNodeHandle Node : FrontendNodes)
			{
				const FMetasoundFrontendNodeStyle& CurrentStyle = Node->GetNodeStyle();
				if (CurrentStyle.Display.Locations.IsEmpty())
				{
					continue;
				}

				FMetasoundFrontendNodeStyle NewStyle = CurrentStyle;
				bIsEditorGraphDirty = true;

				TArray<UMetasoundEditorGraphNode*> AddedNodes;
				for (const TPair<FGuid, FVector2D>& Location : NewStyle.Display.Locations)
				{
					UMetasoundEditorGraphNode* NewNode = AddNode(InMetaSound, Node, Location.Value, false /* bInSelectNewNode */);
					if (ensure(NewNode))
					{
						FAssociatedNodes& AssociatedNodeData = AssociatedNodes.FindOrAdd(Node->GetID());
						if (AssociatedNodeData.Node->IsValid())
						{
							ensure(AssociatedNodeData.Node == Node);
						}
						else
						{
							AssociatedNodeData.Node = Node;
						}

						AddedNodes.Add(NewNode);
						AssociatedNodeData.EditorNodes.Add(NewNode);
					}
				}

				NewStyle.Display.Locations.Reset();
				for (UMetasoundEditorGraphNode* EditorNode : AddedNodes)
				{
					NewStyle.Display.Locations.Add(EditorNode->NodeGuid, FVector2D(EditorNode->NodePosX, EditorNode->NodePosY));
				}
				Node->SetNodeStyle(NewStyle);
			}

			// Synchronize pins on node associations.
			for (const TPair<FGuid, FAssociatedNodes>& IdNodePair : AssociatedNodes)
			{
				for (UMetasoundEditorGraphNode* EditorNode : IdNodePair.Value.EditorNodes)
				{
					bIsEditorGraphDirty |= SynchronizeNodePins(*EditorNode, IdNodePair.Value.Node);
				}
			}

			return bIsEditorGraphDirty;
		}

		bool FGraphBuilder::SynchronizeNodePins(UMetasoundEditorGraphNode& InEditorNode, Frontend::FConstNodeHandle InNode, bool bRemoveUnusedPins, bool bLogChanges)
		{
			bool bIsNodeDirty = false;

			IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetaSoundEditor");

			TArray<Frontend::FConstInputHandle> InputHandles = InNode->GetConstInputs();
			TArray<Frontend::FConstOutputHandle> OutputHandles = InNode->GetConstOutputs();
			TArray<UEdGraphPin*> EditorPins = InEditorNode.Pins;


			// Remove input and output handles which are not user modifiable
			InputHandles.RemoveAll([](const Frontend::FConstInputHandle& FrontendInput) { return !FrontendInput->IsConnectionUserModifiable(); });
			OutputHandles.RemoveAll([](const Frontend::FConstOutputHandle& FrontendOutput) { return !FrontendOutput->IsConnectionUserModifiable(); });

			// Filter out pins which are not paired.
			for (int32 i = EditorPins.Num() - 1; i >= 0; i--)
			{
				UEdGraphPin* Pin = EditorPins[i];

				auto IsMatchingInputHandle = [&](const Frontend::FConstInputHandle& InputHandle) -> bool
				{
					return IsMatchingInputHandleAndPin(InputHandle, *Pin);
				};

				auto IsMatchingOutputHandle = [&](const Frontend::FConstOutputHandle& OutputHandle) -> bool
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
							bIsNodeDirty |= SynchronizePinType(EditorModule, *EditorPins[i], InputHandles[MatchingInputIndex]->GetDataType());
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
							bIsNodeDirty |= SynchronizePinType(EditorModule, *EditorPins[i], OutputHandles[MatchingOutputIndex]->GetDataType());
							OutputHandles.RemoveAtSwap(MatchingOutputIndex);
							EditorPins.RemoveAtSwap(i);
						}
					}
					break;
				}
			}

			// Remove any unused editor pins.
			if (bRemoveUnusedPins)
			{
				bIsNodeDirty |= !EditorPins.IsEmpty();
				InEditorNode.bRefreshNode |= !EditorPins.IsEmpty();
				for (UEdGraphPin* Pin : EditorPins)
				{
					if (bLogChanges)
					{
						constexpr bool bIncludeNamespace = true;
						const FText NodeDisplayName = FGraphBuilder::GetDisplayName(*InNode, bIncludeNamespace);
						UE_LOG(LogMetasoundEditor, Verbose, TEXT("Synchronizing Node '%s' Pins: Removing Excess Editor Pin '%s'"), *NodeDisplayName.ToString(), *Pin->GetName());
					}
					InEditorNode.RemovePin(Pin);
				}
			}


			if (!InputHandles.IsEmpty())
			{
				bIsNodeDirty = true;
				InputHandles = InNode->GetInputStyle().SortDefaults(InputHandles);
				for (Frontend::FConstInputHandle& InputHandle : InputHandles)
				{
					if (bLogChanges)
					{
						constexpr bool bIncludeNamespace = true;
						const FText NodeDisplayName = FGraphBuilder::GetDisplayName(*InNode, bIncludeNamespace);
						const FText InputDisplayName = FGraphBuilder::GetDisplayName(*InputHandle);
						UE_LOG(LogMetasoundEditor, Verbose, TEXT("Synchronizing Node '%s' Pins: Adding missing Editor Input Pin '%s'"), *NodeDisplayName.ToString(), *InputDisplayName.ToString());
					}
					AddPinToNode(InEditorNode, InputHandle);
				}
			}

			if (!OutputHandles.IsEmpty())
			{
				bIsNodeDirty = true;
				OutputHandles = InNode->GetOutputStyle().SortDefaults(OutputHandles);
				for (Frontend::FConstOutputHandle& OutputHandle : OutputHandles)
				{
					if (bLogChanges)
					{
						constexpr bool bIncludeNamespace = true;
						const FText NodeDisplayName = FGraphBuilder::GetDisplayName(*InNode, bIncludeNamespace);
						const FText OutputDisplayName = FGraphBuilder::GetDisplayName(*OutputHandle);
						UE_LOG(LogMetasoundEditor, Verbose, TEXT("Synchronizing Node '%s' Pins: Adding missing Editor Output Pin '%s'"), *NodeDisplayName.ToString(), *OutputDisplayName.ToString());
					}
					AddPinToNode(InEditorNode, OutputHandle);
				}
			}

			return bIsNodeDirty;
		}

		bool FGraphBuilder::SynchronizePinLiteral(UEdGraphPin& InPin)
		{
			using namespace Frontend;

			if (!ensure(InPin.Direction == EGPD_Input))
			{
				return false;
			}

			const FString OldValue = InPin.DefaultValue;

			FInputHandle InputHandle = GetInputHandleFromPin(&InPin);
			if (const FMetasoundFrontendLiteral* NodeDefaultLiteral = InputHandle->GetLiteral())
			{
				InPin.DefaultValue = NodeDefaultLiteral->ToString();
				return OldValue != InPin.DefaultValue;
			}

			if (const FMetasoundFrontendLiteral* ClassDefaultLiteral = InputHandle->GetClassDefaultLiteral())
			{
				InPin.DefaultValue = ClassDefaultLiteral->ToString();
				return OldValue != InPin.DefaultValue;
			}

			FMetasoundFrontendLiteral DefaultLiteral;
			DefaultLiteral.SetFromLiteral(IDataTypeRegistry::Get().CreateDefaultLiteral(InputHandle->GetDataType()));

			InPin.DefaultValue = DefaultLiteral.ToString();
			return OldValue != InPin.DefaultValue;
		}

		bool FGraphBuilder::SynchronizeGraphVertices(UObject& InMetaSound)
		{
			using namespace Frontend;

			bool bIsEditorGraphDirty = false;

			FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSound);
			check(MetaSoundAsset);
			UMetasoundEditorGraph* Graph = CastChecked<UMetasoundEditorGraph>(MetaSoundAsset->GetGraph());
			FGraphHandle GraphHandle = MetaSoundAsset->GetRootGraphHandle();

			TSet<UMetasoundEditorGraphInput*> Inputs;
			TSet<UMetasoundEditorGraphOutput*> Outputs;

			// Collect all editor graph inputs with corresponding frontend inputs. 
			GraphHandle->IterateNodes([&](FNodeHandle NodeHandle)
			{
				if (UMetasoundEditorGraphInput* Input = Graph->FindInput(NodeHandle->GetID()))
				{
					Inputs.Add(Input);
					return;
				}

				// Add an editor input if none exist for a frontend input.
				Inputs.Add(Graph->FindOrAddInput(NodeHandle));
				constexpr bool bIncludeNamespace = true;
				FText NodeDisplayName = FGraphBuilder::GetDisplayName(*NodeHandle, bIncludeNamespace);
				UE_LOG(LogMetasoundEditor, Verbose, TEXT("Synchronizing Inputs: Added missing input '%s'."), *NodeDisplayName.ToString());
				bIsEditorGraphDirty = true;
			}, EMetasoundFrontendClassType::Input);

			// Collect all editor graph outputs with corresponding frontend outputs. 
			GraphHandle->IterateNodes([&](FNodeHandle NodeHandle)
			{
				if (UMetasoundEditorGraphOutput* Output = Graph->FindOutput(NodeHandle->GetID()))
				{
					Outputs.Add(Output);
					return;
				}

				// Add an editor output if none exist for a frontend output.
				Outputs.Add(Graph->FindOrAddOutput(NodeHandle));
				constexpr bool bIncludeNamespace = true;
				FText NodeDisplayName = FGraphBuilder::GetDisplayName(*NodeHandle, bIncludeNamespace);
				UE_LOG(LogMetasoundEditor, Verbose, TEXT("Synchronizing Outputs: Added missing output '%s'."), *NodeDisplayName.ToString());
				bIsEditorGraphDirty = true;
			}, EMetasoundFrontendClassType::Output);

			// Collect editor inputs and outputs to remove which have no corresponding frontend input or output.
			TArray<UMetasoundEditorGraphMember*> ToRemove;
			Graph->IterateInputs([&](UMetasoundEditorGraphInput& Input)
			{
				if (!Inputs.Contains(&Input))
				{
					UE_LOG(LogMetasoundEditor, Verbose, TEXT("Synchronizing Inputs: Removing stale input '%s'."), *Input.GetName());
					ToRemove.Add(&Input);
				}
			});
			Graph->IterateOutputs([&](UMetasoundEditorGraphOutput& Output)
			{
				if (!Outputs.Contains(&Output))
				{
					UE_LOG(LogMetasoundEditor, Verbose, TEXT("Synchronizing Outputs: Removing stale output '%s'."), *Output.GetName());
					ToRemove.Add(&Output);
				}
			});

			// Remove stale inputs and outputs.
			bIsEditorGraphDirty |= !ToRemove.IsEmpty();
			for (UMetasoundEditorGraphMember* GraphMember: ToRemove)
			{
				Graph->RemoveMember(*GraphMember);
			}

			UMetasoundEditorGraphMember* Member = nullptr;

			auto SynchronizeMember = [](UMetasoundEditorGraphVertex& InVertex)
			{
				FConstNodeHandle NodeHandle = InVertex.GetConstNodeHandle();
				TArray<FConstInputHandle> InputHandles = NodeHandle->GetConstInputs();
				if (ensure(InputHandles.Num() == 1))
				{
					FConstInputHandle InputHandle = InputHandles.Last();
					const FName NewDataType = InputHandle->GetDataType();
					if (InVertex.GetDataType() != NewDataType)
					{
						constexpr bool bIncludeNamespace = true;
						FText NodeDisplayName = FGraphBuilder::GetDisplayName(*NodeHandle, bIncludeNamespace);
						UE_LOG(LogMetasoundEditor, Verbose, TEXT("Synchronizing Member '%s': Updating DataType to '%s'."), *NodeDisplayName.ToString(), *NewDataType.ToString());

						FMetasoundFrontendLiteral DefaultLiteral;
						DefaultLiteral.SetFromLiteral(IDataTypeRegistry::Get().CreateDefaultLiteral(NewDataType));
						if (const FMetasoundFrontendLiteral* InputLiteral = InputHandle->GetLiteral())
						{
							DefaultLiteral = *InputLiteral;
						}

						InVertex.ClassName = NodeHandle->GetClassMetadata().GetClassName();

						constexpr bool bPostTransaction = false;
						InVertex.SetDataType(NewDataType, bPostTransaction);

						if (DefaultLiteral.IsValid())
						{
							InVertex.GetLiteral()->SetFromLiteral(DefaultLiteral);
						}
					}
				}
			};

			// Synchronize data types of input nodes.
			GraphHandle->IterateNodes([&](FNodeHandle NodeHandle)
			{
				if (UMetasoundEditorGraphInput* Input = Graph->FindInput(NodeHandle->GetID()))
				{
					SynchronizeMember(*CastChecked<UMetasoundEditorGraphVertex>(Input));
				}
			}, EMetasoundFrontendClassType::Input);

			// Synchronize data types of output nodes.
			GraphHandle->IterateNodes([&](FNodeHandle NodeHandle)
			{
				if (UMetasoundEditorGraphOutput* Output = Graph->FindOutput(NodeHandle->GetID()))
				{
					SynchronizeMember(*CastChecked<UMetasoundEditorGraphVertex>(Output));
				}
			}, EMetasoundFrontendClassType::Output);



			return bIsEditorGraphDirty;
		}
	} // namespace Editor
} // namespace Metasound
#undef LOCTEXT_NAMESPACE
