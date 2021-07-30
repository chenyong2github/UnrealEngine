// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundEditorGraphBuilder.h"

#include "Algo/Sort.h"
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
#include "MetasoundEditorGraphNode.h"
#include "MetasoundEditorGraphInputNodes.h"
#include "MetasoundEditorGraphSchema.h"
#include "MetasoundEditorGraphValidation.h"
#include "MetasoundEditorModule.h"
#include "MetasoundEditorSettings.h"
#include "MetasoundFrontendQuery.h"
#include "MetasoundFrontendQuerySteps.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundFrontendSearchEngine.h"
#include "MetasoundFrontendTransform.h"
#include "MetasoundLiteral.h"
#include "MetasoundUObjectRegistry.h"
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

		const FText FGraphBuilder::ConvertMenuName = LOCTEXT("MetasoundConversionsMenu", "Conversions");
		const FText FGraphBuilder::FunctionMenuName = LOCTEXT("MetasoundFunctionsMenu", "Functions");

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

			bool InitializeGraph(UObject& InMetaSound)
			{
				FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSound);
				check(MetaSoundAsset);

				// Initial graph generation is not something to be managed by the transaction stack, so don't track dirty state until
				// after initial setup if necessary.
				if (!MetaSoundAsset->GetGraph())
				{
					UMetasoundEditorGraph* Graph = NewObject<UMetasoundEditorGraph>(&InMetaSound, FName(), RF_Transactional);
					Graph->Schema = UMetasoundEditorGraphSchema::StaticClass();
					MetaSoundAsset->SetGraph(Graph);

					return true;
				}

				return false;
			}
		}

		UMetasoundEditorGraphExternalNode* FGraphBuilder::AddExternalNode(UObject& InMetaSound, Frontend::FNodeHandle& InNodeHandle, FVector2D InLocation, bool bInSelectNewNode)
		{
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
			NewGraphNode->ClassName = InNodeHandle->GetClassMetadata().GetClassName();

			NodeCreator.Finalize();
			InitGraphNode(InNodeHandle, NewGraphNode, InMetaSound);

			SynchronizeNodeLocation(InLocation, InNodeHandle, *NewGraphNode);

			return NewGraphNode;
		}

		UMetasoundEditorGraphExternalNode* FGraphBuilder::AddExternalNode(UObject& InMetaSound, const FMetasoundFrontendClassMetadata& InMetadata, FVector2D InLocation, bool bInSelectNewNode)
		{
			FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSound);
			check(MetaSoundAsset);

			Frontend::FNodeHandle NodeHandle = MetaSoundAsset->GetRootGraphHandle()->AddNode(InMetadata);
			MetaSoundAsset->RebuildReferencedAssets(UMetaSoundAssetSubsystem::Get());

			return AddExternalNode(InMetaSound, NodeHandle, InLocation, bInSelectNewNode);
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

		bool FGraphBuilder::ValidateGraph(UObject& InMetaSoundToUpdate, bool bInAutoUpdate)
		{
			using namespace Frontend;
			using namespace GraphBuilderPrivate;

			SynchronizeGraph(InMetaSoundToUpdate);

			TSharedPtr<SGraphEditor> GraphEditor;
			TSharedPtr<FEditor> MetaSoundEditor = GetEditorForMetasound(InMetaSoundToUpdate);
			if (MetaSoundEditor.IsValid())
			{
				GraphEditor = MetaSoundEditor->GetGraphEditor();
			}

			FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSoundToUpdate);
			check(MetaSoundAsset);

			// Run initial validation pass before updating to capture upgrade message state prior to auto-update
			UMetasoundEditorGraph& Graph = *CastChecked<UMetasoundEditorGraph>(&MetaSoundAsset->GetGraphChecked());
			FGraphValidationResults Results;
			Graph.ValidateInternal(Results);

			bool bAutoUpdated = false;
			if (Results.IsValid() && bInAutoUpdate)
			{
				check(MetaSoundAsset);
				if (MetaSoundAsset->AutoUpdate(UMetaSoundAssetSubsystem::Get(), true /* bMarkDirty*/, true /* bUpdateReferencedAssets*/))
				{
					SynchronizeGraph(InMetaSoundToUpdate);

					Graph.ValidateInternal(Results, false /* bClearUpgradeMessaging */);
					for (const FGraphNodeValidationResult& Result : Results.GetResults())
					{
						if (GraphEditor.IsValid())
						{
							check(Result.Node);
							GraphEditor->RefreshNode(*Result.Node);
							MetaSoundEditor->RefreshInterface();
						}
					}

					InMetaSoundToUpdate.MarkPackageDirty();
				}
			}

			return Results.IsValid();
		}

		void FGraphBuilder::InitGraphNode(Frontend::FNodeHandle& InNodeHandle, UMetasoundEditorGraphNode* NewGraphNode, UObject& InMetaSound)
		{
			NewGraphNode->CreateNewGuid();
			NewGraphNode->SetNodeID(InNodeHandle->GetID());

			RebuildNodePins(*NewGraphNode);
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

		FText FGraphBuilder::GenerateUniqueInputDisplayName(const UObject& InMetaSound, const FText* InBaseName)
		{
			static const FText DefaultBaseName = FText::FromString(TEXT("Input"));
			const FText& NameBase = InBaseName ? *InBaseName : DefaultBaseName;
			return GenerateUniqueNameByFilter(InMetaSound, NameBase, [](const Frontend::FConstGraphHandle& GraphHandle, const FText& NewName)
			{
				bool bIsNameInvalid = false;
				GraphHandle->IterateConstNodes([&](Frontend::FConstNodeHandle Node)
				{
					if (NewName.CompareToCaseIgnored(Node->GetDisplayName()) == 0)
					{
						bIsNameInvalid = true;
					}
				}, EMetasoundFrontendClassType::Input);
				return !bIsNameInvalid;
			});
		}

		FText FGraphBuilder::GenerateUniqueOutputDisplayName(const UObject& InMetaSound, const FText* InBaseName)
		{
			static const FText DefaultBaseName = FText::FromString(TEXT("Output"));
			const FText& NameBase = InBaseName ? *InBaseName : DefaultBaseName;
			return GenerateUniqueNameByFilter(InMetaSound, NameBase, [](const Frontend::FConstGraphHandle& GraphHandle, const FText& NewName)
			{
				bool bIsNameInvalid = false;
				GraphHandle->IterateConstNodes([&](Frontend::FConstNodeHandle Node)
				{
					if (NewName.CompareToCaseIgnored(Node->GetDisplayName()) == 0)
					{
						bIsNameInvalid = true;
					}
				}, EMetasoundFrontendClassType::Output);
				return !bIsNameInvalid;
			});
		}

		FText FGraphBuilder::GenerateUniqueNameByFilter(const UObject& InMetaSound, const FText& InBaseText, TFunctionRef<bool(const Frontend::FConstGraphHandle&, const FText&)> InNameIsValidFilter)
		{
			const FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSound);
			check(MetaSoundAsset);

			const Frontend::FConstGraphHandle GraphHandle = MetaSoundAsset->GetRootGraphHandle();

			if (InNameIsValidFilter(GraphHandle, InBaseText))
			{
				return InBaseText;
			}

			FNumberFormattingOptions NumberFormatOptions;
			NumberFormatOptions.MinimumIntegralDigits = 2;

			int32 i = 1;
			FText NewName;
			do
			{
				static const FText VariableUniqueNameFormat = LOCTEXT("GenerateUniqueVariableNameFormat", "{0} {1}");
				NewName = FText::Format(VariableUniqueNameFormat, InBaseText, FText::AsNumber(++i, &NumberFormatOptions));
			}
			while (!InNameIsValidFilter(GraphHandle, NewName));

			return NewName;
		}

		TSharedPtr<FEditor> FGraphBuilder::GetEditorForMetasound(const UObject& Metasound)
		{
			TSharedPtr<IToolkit> FoundAssetEditor = FToolkitManager::Get().FindEditorForAsset(&Metasound);
			return StaticCastSharedPtr<FEditor, IToolkit>(FoundAssetEditor);
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

							const FString ClassName = Class->GetName();

							// Remove class prefix if included in default value path
							FString ObjectPath = InInputPin.DefaultValue;
							ObjectPath.RemoveFromStart(ClassName + TEXT(" "));

							FARFilter Filter;
							Filter.bRecursiveClasses = false;
							Filter.ObjectPaths.Add(*ObjectPath);
							Filter.ClassNames.Add(Class->GetFName());

							TArray<FAssetData> AssetData;
							AssetRegistryModule.Get().GetAssets(Filter, AssetData);
							if (!AssetData.IsEmpty())
							{
								OutDefaultLiteral.Set(AssetData[0].GetAsset());
								bObjectFound = true;
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
						NodeHandle = AddInputNodeHandle(InMetaSound, Input->TypeName, InGraphNode.GetTooltipText());
						NodeHandle->SetDisplayName(FText::FromString(Pin->GetName()));
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
						NodeHandle = FGraphBuilder::AddOutputNodeHandle(InMetaSound, Output->TypeName, InGraphNode.GetTooltipText());
						NodeHandle->SetDisplayName(FText::FromString(Pin->GetName()));
					}
				}
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

					MetaSoundAsset->RebuildReferencedAssets(UMetaSoundAssetSubsystem::Get());
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

		Frontend::FNodeHandle FGraphBuilder::AddInputNodeHandle(UObject& InMetaSound, const FName InTypeName, const FText& InToolTip, const FMetasoundFrontendLiteral* InDefaultValue)
		{
			FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSound);
			check(MetaSoundAsset);

			const FText DisplayName = GenerateUniqueInputDisplayName(InMetaSound);
			return MetaSoundAsset->GetRootGraphHandle()->AddInputNode(InTypeName, InToolTip, InDefaultValue, &DisplayName);
		}

		Frontend::FNodeHandle FGraphBuilder::AddOutputNodeHandle(UObject& InMetaSound, const FName InTypeName, const FText& InToolTip)
		{
			FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSound);
			check(MetaSoundAsset);

			const FText DisplayName = GenerateUniqueOutputDisplayName(InMetaSound);
			return MetaSoundAsset->GetRootGraphHandle()->AddOutputNode(InTypeName, InToolTip, &DisplayName);
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

				case EMetasoundFrontendClassType::Invalid:
				case EMetasoundFrontendClassType::Graph:
				case EMetasoundFrontendClassType::Variable:
				default:
				{
					checkNoEntry();
					static_assert(static_cast<int32>(EMetasoundFrontendClassType::Invalid) == 5, "Possible missing FMetasoundFrontendClassType case coverage");
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

		void FGraphBuilder::DisconnectPin(UEdGraphPin& InPin, bool bAddLiteralInputs)
		{
			using namespace Editor;
			using namespace Frontend;

			TArray<FInputHandle> InputHandles;
			TArray<UEdGraphPin*> InputPins;

			UObject& Metasound = CastChecked<UMetasoundEditorGraphNode>(InPin.GetOwningNode())->GetMetasoundChecked();

			if (InPin.Direction == EGPD_Input)
			{
				FNodeHandle NodeHandle = CastChecked<UMetasoundEditorGraphNode>(InPin.GetOwningNode())->GetNodeHandle();
				InputHandles = NodeHandle->GetInputsWithVertexName(InPin.GetName());
				InputPins.Add(&InPin);
			}
			else
			{
				check(InPin.Direction == EGPD_Output);
				for (UEdGraphPin* Pin : InPin.LinkedTo)
				{
					FNodeHandle NodeHandle = CastChecked<UMetasoundEditorGraphNode>(Pin->GetOwningNode())->GetNodeHandle();
					InputHandles.Append(NodeHandle->GetInputsWithVertexName(Pin->GetName()));
					InputPins.Add(Pin);
				}
			}

			IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetaSoundEditor");
			for (int32 i = 0; i < InputHandles.Num(); ++i)
			{
				FInputHandle InputHandle = InputHandles[i];
				FConstOutputHandle OutputHandle = InputHandle->GetConnectedOutput();
				const FMetasoundFrontendNodeStyle& Style = OutputHandle->GetOwningNode()->GetNodeStyle();

				InputHandle->Disconnect();

				if (bAddLiteralInputs)
				{
					FNodeHandle NodeHandle = InputHandle->GetOwningNode();
					SynchronizePinLiteral(*InputPins[i]);
				}
			}
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

			MetaSoundAsset->ConformDocumentToArchetype();

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

			const FMetasoundAssetBase* ReferencedAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSoundReferenced);
			check(ReferencedAsset);
			FRebuildPresetRootGraph(ReferencedAsset->GetDocumentHandle()).Transform(PresetAsset->GetDocumentHandle());
		}

		void FGraphBuilder::DeleteVariableNodeHandle(UMetasoundEditorGraphVariable& InVariable)
		{
			using namespace Frontend;

			FNodeHandle NodeHandle = InVariable.GetNodeHandle();
			TArray<UMetasoundEditorGraphNode*> Nodes = InVariable.GetNodes();
			for (UMetasoundEditorGraphNode* Node : Nodes)
			{
				if (ensure(Node))
				{
					// Remove the give node's location from the frontend node
					FMetasoundFrontendNodeStyle Style = NodeHandle->GetNodeStyle();
					Style.Display.Locations.Remove(Node->NodeGuid);
					NodeHandle->SetNodeStyle(Style);

					FGraphBuilder::DeleteNode(*Node);
				}
			}

			const FString NodeName = NodeHandle->GetNodeName();
			const FText NodeDisplayName = NodeHandle->GetDisplayName();
			FGraphHandle GraphHandle = NodeHandle->GetOwningGraph();
			GraphHandle->RemoveNode(*NodeHandle);
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

			FNodeHandle NodeHandle = Node->GetNodeHandle();
			if (!ensure(NodeHandle->IsValid()))
			{
				return false;
			}

			// Remove connects only to pins associated with this EdGraph node
			// only (Iterate pins and not Frontend representation to preserve
			// other input/output EditorGraph reference node associations)
			Node->IteratePins([](UEdGraphPin& Pin, int32 Index)
			{
				// Only add literal inputs for output pins as adding when disconnecting
				// inputs would immediately orphan them on EditorGraph node removal below.
				const bool bAddLiteralInputs = Pin.Direction == EGPD_Output;
				FGraphBuilder::DisconnectPin(Pin, bAddLiteralInputs);
			});

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
					case EMetasoundFrontendClassType::Variable:
					case EMetasoundFrontendClassType::External:
					default:
					{
						static_assert(static_cast<int32>(EMetasoundFrontendClassType::Invalid) == 5, "Possible missing MetasoundFrontendClassType switch case coverage.");

						if (ensure(GraphHandle->RemoveNode(*NodeHandle)))
						{
							GraphHandle->GetOwningDocument()->SynchronizeDependencies();
						}
					}
					break;
				}
			}

			const bool bSuccess = ensure(Graph->RemoveNode(&InNode));

			// Sync the graph after nodes containing errors are deleted to ensure that
			// the graph is not malformed once all errors are addressed by the user.
			if (bSuccess && bWasErroredNode)
			{
				SynchronizeGraph(Graph->GetMetasoundChecked());
			}
			// SynchronizeGraph calls rebuild references, but it needs to be
			// called to remove the given reference irrespective of error state.
			else
			{
				FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&Graph->GetMetasoundChecked());
				check(MetaSoundAsset);

				MetaSoundAsset->RebuildReferencedAssets(UMetaSoundAssetSubsystem::Get());
			}

			return bSuccess;
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

			// Only add input pins of the node is not an input node. Input nodes
			// have their input pins hidden because they cannot be connected internal
			// to the graph.
			if (EMetasoundFrontendClassType::Input != NodeHandle->GetClassMetadata().GetType())
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
			if (EMetasoundFrontendClassType::Output != NodeHandle->GetClassMetadata().GetType())
			{
				TArray<FOutputHandle> OutputHandles = NodeHandle->GetOutputs();
				OutputHandles = NodeHandle->GetOutputStyle().SortDefaults(OutputHandles);
				for (const FOutputHandle& OutputHandle : OutputHandles)
				{
					AddPinToNode(InGraphNode, OutputHandle);
				}
			}
		}

		void FGraphBuilder::RefreshPinMetadata(UEdGraphPin& InPin, const FMetasoundFrontendVertexMetadata& InMetadata)
		{
			InPin.PinToolTip = InMetadata.Description.ToString();
			InPin.bAdvancedView = InMetadata.bIsAdvancedDisplay;
			if (InPin.bAdvancedView)
			{
				UEdGraphNode* OwningNode = InPin.GetOwningNode();
				check(OwningNode);
				if (OwningNode->AdvancedPinDisplay == ENodeAdvancedPins::NoPins)
				{
					OwningNode->AdvancedPinDisplay = ENodeAdvancedPins::Hidden;
				}
			}
		}

		void FGraphBuilder::RegisterGraphWithFrontend(UObject& InMetaSound)
		{
			FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSound);
			check(MetaSoundAsset);

			MetaSoundAsset->RegisterGraphWithFrontend();

			if (GEditor)
			{
				TArray<UObject*> EditedAssets = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->GetAllEditedAssets();
				for (UObject* Asset : EditedAssets)
				{
					if (Asset != &InMetaSound && IMetasoundUObjectRegistry::Get().IsRegisteredClass(Asset))
					{
						ValidateGraph(*Asset, true /* bAutoUpdate */);
					}
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

			MetaSoundAsset->UnregisterGraphWithFrontend();

			if (GEditor)
			{
				TArray<UObject*> EditedAssets = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->GetAllEditedAssets();
				for (UObject* Asset : EditedAssets)
				{
					if (Asset != &InMetaSound && IMetasoundUObjectRegistry::Get().IsRegisteredClass(Asset))
					{
						ValidateGraph(*Asset, true /* bAutoUpdate */);
					}
				}
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

			IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetaSoundEditor");
			const FEdGraphPinType PinType = EditorModule.FindDataType(InInputHandle->GetDataType()).PinType;

			UEdGraphPin* NewPin = InEditorNode.CreatePin(EGPD_Input, PinType, FName(*InInputHandle->GetName()));
			if (ensure(NewPin))
			{
				RefreshPinMetadata(*NewPin, InInputHandle->GetMetadata());
				SynchronizePinLiteral(*NewPin);
			}

			return NewPin;
		}

		UEdGraphPin* FGraphBuilder::AddPinToNode(UMetasoundEditorGraphNode& InEditorNode, Frontend::FConstOutputHandle InOutputHandle)
		{
			IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetaSoundEditor");
			FEdGraphPinType	PinType = EditorModule.FindDataType(InOutputHandle->GetDataType()).PinType;

			UEdGraphPin* NewPin = InEditorNode.CreatePin(EGPD_Output, PinType, FName(*InOutputHandle->GetName()));
			if (ensure(NewPin))
			{
				NewPin->PinToolTip = InOutputHandle->GetTooltip().ToString();
				NewPin->bAdvancedView = InOutputHandle->GetMetadata().bIsAdvancedDisplay;
				if (NewPin->bAdvancedView)
				{
					if (InEditorNode.AdvancedPinDisplay == ENodeAdvancedPins::NoPins)
					{
						InEditorNode.AdvancedPinDisplay = ENodeAdvancedPins::Hidden;
					}
				}
			}

			return NewPin;
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

				FNodeHandle Node = EditorNode->GetNodeHandle();

				if (EMetasoundFrontendClassType::Input == Node->GetClassMetadata().GetType())
				{
					// Skip this node if it is an input node. Input pins on input 
					// nodes are not connected internal to the graph.
					continue;
				}

				TArray<UEdGraphPin*> Pins = EditorNode->GetAllPins();
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

					FOutputHandle OutputHandle = NodeInput->GetConnectedOutput();
					if (OutputHandle->IsValid())
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
							const FGuid NodeID = OutputHandle->GetOwningNodeID();
							TArray<UMetasoundEditorGraphNode*>* OutputEditorNode = EditorNodesByFrontendID.Find(NodeID);
							if (ensure(OutputEditorNode))
							{
								if (ensure(!OutputEditorNode->IsEmpty()))
								{
									UEdGraphPin* OutputPin = (*OutputEditorNode)[0]->FindPinChecked(OutputHandle->GetName(), EEdGraphPinDirection::EGPD_Output);
									const FText& OwningNodeName = NodeInput->GetOwningNode()->GetDisplayName();
									UE_LOG(LogMetasoundEditor, Display, TEXT("Synchronizing Node '%s' Connection: Linking Pin '%s' to '%s'"), *OwningNodeName.ToString(), *MatchingPin->GetName(), *OutputPin->GetName());
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
							const FText& OwningNodeName = NodeInput->GetOwningNode()->GetDisplayName();
							UE_LOG(LogMetasoundEditor, Display, TEXT("Synchronizing Node '%s' Connection: Breaking all pin links to '%s'"), *OwningNodeName.ToString(), *NodeInput->GetDisplayName().ToString());
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
			using namespace Frontend;

			GraphBuilderPrivate::InitializeGraph(InMetaSound);

			bool bIsEditorGraphDirty = SynchronizeVariables(InMetaSound);

			FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSound);
			check(MetaSoundAsset);

			// Get all external nodes from Frontend graph.  Input and output references will only be added/synchronized
			// if required when synchronizing connections (as they are not required to inhabit editor graph).
			FGraphHandle GraphHandle = MetaSoundAsset->GetRootGraphHandle();
			TArray<FNodeHandle> FrontendNodes = GraphHandle->GetNodes();

			// Get all editor nodes from editor graph (some nodes on graph may *NOT* be metasound ed nodes,
			// such as comment boxes, etc, so just get nodes of class UMetasoundEditorGraph).
			UMetasoundEditorGraph* EditorGraph = CastChecked<UMetasoundEditorGraph>(MetaSoundAsset->GetGraph());
			TArray<UMetasoundEditorGraphNode*> EditorNodes;
			EditorGraph->GetNodesOfClass(EditorNodes);

			// Do not synchronize with errors present as the graph is expected to be malformed.
			for (UMetasoundEditorGraphNode* Node : EditorNodes)
			{
				if (Node->ErrorType == EMessageSeverity::Error)
				{
					return true;
				}
			}

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

			// FrontendNodes contains nodes which need to be added to the editor graph.
			// EditorNodes contains nodes that need to be removed from the editor graph.
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

			// Synchronize connections.
			bIsEditorGraphDirty |= SynchronizeConnections(InMetaSound);

			MetaSoundAsset->RebuildReferencedAssets(UMetaSoundAssetSubsystem::Get());
			return bIsEditorGraphDirty;
		}

		bool FGraphBuilder::SynchronizeNodePins(UMetasoundEditorGraphNode& InEditorNode, Frontend::FConstNodeHandle InNode, bool bRemoveUnusedPins, bool bLogChanges)
		{
			bool bIsNodeDirty = false;

			TArray<Frontend::FConstInputHandle> InputHandles = InNode->GetConstInputs();
			TArray<Frontend::FConstOutputHandle> OutputHandles = InNode->GetConstOutputs();
			TArray<UEdGraphPin*> EditorPins = InEditorNode.Pins;

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

			// Remove any unused editor pins.
			if (bRemoveUnusedPins)
			{
				bIsNodeDirty |= !EditorPins.IsEmpty();
				for (UEdGraphPin* Pin : EditorPins)
				{
					if (bLogChanges)
					{
						UE_LOG(LogMetasoundEditor, Display, TEXT("Synchronizing Node '%s' Pins: Removing Excess Editor Pin '%s'"), *InNode->GetDisplayName().ToString(), *Pin->GetName());
					}
					InEditorNode.RemovePin(Pin);
				}
			}

			const EMetasoundFrontendClassType ClassType = InNode->GetClassMetadata().GetType();

			// Only add input pins of the node is not an input node. Input nodes
			// have their input pins hidden because they cannot be connected internal
			// to the graph.
			if (EMetasoundFrontendClassType::Input != ClassType)
			{
				if (!InputHandles.IsEmpty())
				{
					bIsNodeDirty = true;
					InputHandles = InNode->GetInputStyle().SortDefaults(InputHandles);
					for (Frontend::FConstInputHandle& InputHandle : InputHandles)
					{
						if (bLogChanges)
						{
							UE_LOG(LogMetasoundEditor, Display, TEXT("Synchronizing Node '%s' Pins: Adding missing Editor Input Pin '%s'"), *InNode->GetDisplayName().ToString(), *InputHandle->GetDisplayName().ToString());
						}
						AddPinToNode(InEditorNode, InputHandle);
					}
				}
			}

			// Only add output pins of the node is not an output node. Output nodes
			// have their output pins hidden because they cannot be connected internal
			// to the graph.
			if (EMetasoundFrontendClassType::Output != ClassType)
			{
				if (!OutputHandles.IsEmpty())
				{
					bIsNodeDirty = true;
					OutputHandles = InNode->GetOutputStyle().SortDefaults(OutputHandles);
					for (Frontend::FConstOutputHandle& OutputHandle : OutputHandles)
					{
						if (bLogChanges)
						{
							UE_LOG(LogMetasoundEditor, Display, TEXT("Synchronizing Node '%s' Pins: Adding missing Editor Output Pin '%s'"), *InNode->GetDisplayName().ToString(), *OutputHandle->GetDisplayName().ToString());
						}
						AddPinToNode(InEditorNode, OutputHandle);
					}
				}
			}

			return bIsNodeDirty;
		}

		bool FGraphBuilder::SynchronizePinLiteral(UEdGraphPin& InPin)
		{
			using namespace Metasound::Frontend;

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
			DefaultLiteral.SetFromLiteral(Frontend::GetDefaultParamForDataType(InputHandle->GetDataType()));
			InPin.DefaultValue = DefaultLiteral.ToString();
			return OldValue != InPin.DefaultValue;
		}

		bool FGraphBuilder::SynchronizeVariables(UObject& InMetaSound)
		{
			using namespace Frontend;

			bool bIsEditorGraphDirty = false;

			FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InMetaSound);
			check(MetaSoundAsset);
			UMetasoundEditorGraph* Graph = CastChecked<UMetasoundEditorGraph>(MetaSoundAsset->GetGraph());

			IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetaSoundEditor");

			TSet<UMetasoundEditorGraphInput*> Inputs;
			TSet<UMetasoundEditorGraphOutput*> Outputs;

			FGraphHandle GraphHandle = MetaSoundAsset->GetRootGraphHandle();
			GraphHandle->IterateNodes([&](FNodeHandle NodeHandle)
			{
				if (UMetasoundEditorGraphInput* Input = Graph->FindInput(NodeHandle->GetID()))
				{
					Inputs.Add(Input);
					return;
				}

				if (!ensure(NodeHandle->GetNumInputs() == 1))
				{
					return;
				}

				Inputs.Add(Graph->FindOrAddInput(NodeHandle));
				UE_LOG(LogMetasoundEditor, Display, TEXT("Synchronizing Inputs: Added missing input '%s'."), *NodeHandle->GetDisplayName().ToString());
				bIsEditorGraphDirty = true;
			}, EMetasoundFrontendClassType::Input);

			GraphHandle->IterateNodes([&](FNodeHandle NodeHandle)
			{
				if (UMetasoundEditorGraphOutput* Output = Graph->FindOutput(NodeHandle->GetID()))
				{
					Outputs.Add(Output);
					return;
				}
				if (!ensure(NodeHandle->GetNumOutputs() == 1))
				{
					return;
				}

				Outputs.Add(Graph->FindOrAddOutput(NodeHandle));
				UE_LOG(LogMetasoundEditor, Display, TEXT("Synchronizing Outputs: Added missing output '%s'."), *NodeHandle->GetDisplayName().ToString());
				bIsEditorGraphDirty = true;
			}, EMetasoundFrontendClassType::Output);

			TArray<UMetasoundEditorGraphVariable*> ToRemove;
			Graph->IterateInputs([&](UMetasoundEditorGraphInput& Input)
			{
				if (!Inputs.Contains(&Input))
				{
					UE_LOG(LogMetasoundEditor, Display, TEXT("Synchronizing Inputs: Removing stale input '%s'."), *Input.GetName());
					ToRemove.Add(&Input);
				}
			});
			Graph->IterateOutputs([&](UMetasoundEditorGraphOutput& Output)
			{
				if (!Outputs.Contains(&Output))
				{
					UE_LOG(LogMetasoundEditor, Display, TEXT("Synchronizing Outputs: Removing stale output '%s'."), *Output.GetName());
					ToRemove.Add(&Output);
				}
			});

			bIsEditorGraphDirty |= !ToRemove.IsEmpty();
			for (UMetasoundEditorGraphVariable* Variable : ToRemove)
			{
				Graph->RemoveVariable(*Variable);
			}

			return bIsEditorGraphDirty;
		}
	} // namespace Editor
} // namespace Metasound
#undef LOCTEXT_NAMESPACE
