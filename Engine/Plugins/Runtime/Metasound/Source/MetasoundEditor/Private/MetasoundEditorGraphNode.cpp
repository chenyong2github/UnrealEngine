// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundEditorGraphNode.h"

#include "EdGraph/EdGraphPin.h"
#include "Editor/EditorEngine.h"
#include "Engine/Font.h"
#include "Framework/Commands/GenericCommands.h"
#include "GraphEditorActions.h"
#include "Logging/TokenizedMessage.h"
#include "Metasound.h"
#include "MetasoundEditorCommands.h"
#include "MetasoundEditorGraph.h"
#include "MetasoundEditorGraphBuilder.h"
#include "MetasoundEditorGraphSchema.h"
#include "MetasoundEditorGraphValidation.h"
#include "MetasoundEditorModule.h"
#include "MetasoundEditorSettings.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendDataTypeRegistry.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundFrontendSearchEngine.h"
#include "MetasoundLiteral.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundUObjectRegistry.h"
#include "ScopedTransaction.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "MetaSoundEditor"


namespace Metasound
{
	namespace Editor
	{
		namespace GraphNodePrivate
		{
			void SetGraphNodeMessage(UEdGraphNode& InNode, EMessageSeverity::Type InSeverity, const FString& InMessage)
			{
				InNode.bHasCompilerMessage = true;
				InNode.ErrorMsg = InMessage;
				InNode.ErrorType = InSeverity;

				if (InSeverity == EMessageSeverity::Error)
				{
					UE_LOG(LogMetasoundEditor, Error, TEXT("%s"), *InMessage);
				}
			}
		} // namespace GraphNodePrivate
	} // namespace Editor
} // namespace Metasound


UMetasoundEditorGraphNode::UMetasoundEditorGraphNode(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UMetasoundEditorGraphNode::PostLoad()
{
	Super::PostLoad();

	for (int32 Index = 0; Index < Pins.Num(); ++Index)
	{
		UEdGraphPin* Pin = Pins[Index];
		if (Pin->PinName.IsNone())
		{
			// Makes sure pin has a name for lookup purposes but user will never see it
			if (Pin->Direction == EGPD_Input)
			{
				Pin->PinName = CreateUniquePinName("Input");
			}
			else
			{
				Pin->PinName = CreateUniquePinName("Output");
			}
			Pin->PinFriendlyName = FText::GetEmpty();
		}
	}
}

void UMetasoundEditorGraphNode::CreateInputPin()
{
	// TODO: Implement for nodes supporting variadic inputs
	if (ensure(false))
	{
		return;
	}

	FString PinName; // get from UMetaSound
	UEdGraphPin* NewPin = CreatePin(EGPD_Input, TEXT("MetasoundEditorGraphNode"), *PinName);
	if (NewPin->PinName.IsNone())
	{
		// Pin must have a name for lookup purposes but is not user-facing
// 		NewPin->PinName = 
// 		NewPin->PinFriendlyName =
	}
}

int32 UMetasoundEditorGraphNode::EstimateNodeWidth() const
{
	const FString NodeTitle = GetNodeTitle(ENodeTitleType::FullTitle).ToString();
	if (const UFont* Font = GetDefault<UEditorEngine>()->EditorFont)
	{
		return Font->GetStringSize(*NodeTitle);
	}
	else
	{
		static const int32 EstimatedCharWidth = 6;
		return NodeTitle.Len() * EstimatedCharWidth;
	}
}

UObject& UMetasoundEditorGraphNode::GetMetasoundChecked()
{
	UMetasoundEditorGraph* EdGraph = CastChecked<UMetasoundEditorGraph>(GetGraph());
	return EdGraph->GetMetasoundChecked();
}

const UObject& UMetasoundEditorGraphNode::GetMetasoundChecked() const
{
	UMetasoundEditorGraph* EdGraph = CastChecked<UMetasoundEditorGraph>(GetGraph());
	return EdGraph->GetMetasoundChecked();
}

Metasound::Frontend::FConstGraphHandle UMetasoundEditorGraphNode::GetConstRootGraphHandle() const
{
	const FMetasoundAssetBase* ConstMetasoundAsset = Metasound::IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&GetMetasoundChecked());
	FMetasoundAssetBase* MetasoundAsset = const_cast<FMetasoundAssetBase*>(ConstMetasoundAsset);
	check(MetasoundAsset);

	return MetasoundAsset->GetRootGraphHandle();
}

Metasound::Frontend::FGraphHandle UMetasoundEditorGraphNode::GetRootGraphHandle() const
{
	const FMetasoundAssetBase* ConstMetasoundAsset = Metasound::IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&GetMetasoundChecked());
	FMetasoundAssetBase* MetasoundAsset = const_cast<FMetasoundAssetBase*>(ConstMetasoundAsset);
	check(MetasoundAsset);

	return MetasoundAsset->GetRootGraphHandle();
}

Metasound::Frontend::FConstNodeHandle UMetasoundEditorGraphNode::GetConstNodeHandle() const
{
	const FGuid NodeID = GetNodeID();
	return GetConstRootGraphHandle()->GetNodeWithID(NodeID);
}

Metasound::Frontend::FNodeHandle UMetasoundEditorGraphNode::GetNodeHandle() const
{
	const FGuid NodeID = GetNodeID();
	return GetRootGraphHandle()->GetNodeWithID(NodeID);
}

void UMetasoundEditorGraphNode::IteratePins(TUniqueFunction<void(UEdGraphPin& /* Pin */, int32 /* Index */)> InFunc, EEdGraphPinDirection InPinDirection)
{
	for (int32 PinIndex = 0; PinIndex < Pins.Num(); PinIndex++)
	{
		if (InPinDirection == EGPD_MAX || Pins[PinIndex]->Direction == InPinDirection)
		{
			InFunc(*Pins[PinIndex], PinIndex);
		}
	}
}

void UMetasoundEditorGraphNode::AllocateDefaultPins()
{
	using namespace Metasound;

	ensureAlways(Pins.IsEmpty());
	Editor::FGraphBuilder::RebuildNodePins(*this);
}

void UMetasoundEditorGraphNode::ReconstructNode()
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	// Don't remove unused pins here. Reconstruction can occur while duplicating or pasting nodes,
	// and subsequent steps clean-up unused pins.  This can be called mid-copy, which means the node
	// handle may be invalid.  Setting to remove unused causes premature removal and then default values
	// are lost.
	FConstNodeHandle NodeHandle = GetNodeHandle();
	if (NodeHandle->IsValid())
	{
		FGraphBuilder::SynchronizeNodePins(*this, NodeHandle, false /* bRemoveUnusedPins */, false /* bLogChanges */);
	}
}

void UMetasoundEditorGraphNode::AutowireNewNode(UEdGraphPin* FromPin)
{
	if (FromPin)
	{
		const UMetasoundEditorGraphSchema* Schema = CastChecked<UMetasoundEditorGraphSchema>(GetSchema());

		TSet<UEdGraphNode*> NodeList;

		// auto-connect from dragged pin to first compatible pin on the new node
		for (int32 i = 0; i < Pins.Num(); i++)
		{
			UEdGraphPin* Pin = Pins[i];
			check(Pin);
			FPinConnectionResponse Response = Schema->CanCreateConnection(FromPin, Pin);
			if (ECanCreateConnectionResponse::CONNECT_RESPONSE_MAKE == Response.Response) //-V1051
			{
				if (Schema->TryCreateConnection(FromPin, Pin))
				{
					NodeList.Add(FromPin->GetOwningNode());
					NodeList.Add(this);
				}
				break;
			}
			else if (ECanCreateConnectionResponse::CONNECT_RESPONSE_BREAK_OTHERS_A == Response.Response)
			{
				// TODO: Implement default connections in GraphBuilder
				break;
			}
		}

		// Send all nodes that received a new pin connection a notification
		for (auto It = NodeList.CreateConstIterator(); It; ++It)
		{
			UEdGraphNode* Node = (*It);
			Node->NodeConnectionListChanged();
		}
	}
}

bool UMetasoundEditorGraphNode::CanCreateUnderSpecifiedSchema(const UEdGraphSchema* Schema) const
{
	return Schema->IsA(UMetasoundEditorGraphSchema::StaticClass());
}

bool UMetasoundEditorGraphNode::CanUserDeleteNode() const
{
	return true;
}

FString UMetasoundEditorGraphNode::GetDocumentationLink() const
{
	return TEXT("Shared/GraphNodes/Metasound");
}

FText UMetasoundEditorGraphNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	using namespace Metasound::Frontend;

	FConstNodeHandle NodeHandle = GetNodeHandle();
	return NodeHandle->GetDisplayTitle();
}

void UMetasoundEditorGraphNode::GetPinHoverText(const UEdGraphPin& Pin, FString& OutHoverText) const
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	if (Pin.Direction == EGPD_Input)
	{
		FConstInputHandle InputHandle = FGraphBuilder::GetConstInputHandleFromPin(&Pin);
		OutHoverText = InputHandle->GetTooltip().ToString();
	}
	else // Pin.Direction == EGPD_Output
	{
		FConstOutputHandle OutputHandle = FGraphBuilder::GetConstOutputHandleFromPin(&Pin);
		OutHoverText = OutputHandle->GetTooltip().ToString();
	}
}

void UMetasoundEditorGraphNode::PinDefaultValueChanged(UEdGraphPin* Pin)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	if (Pin && Pin->Direction == EGPD_Input)
	{
		GetMetasoundChecked().Modify();

		FInputHandle InputHandle = FGraphBuilder::GetInputHandleFromPin(Pin);
		if (InputHandle->IsValid())
		{
			FMetasoundFrontendLiteral LiteralValue;
			if (FGraphBuilder::GetPinLiteral(*Pin, LiteralValue))
			{
				InputHandle->SetLiteral(LiteralValue);
			}
		}
	}
}

Metasound::Frontend::FDataTypeRegistryInfo UMetasoundEditorGraphNode::GetPinDataTypeInfo(const UEdGraphPin& InPin) const
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

 	Metasound::Frontend::FDataTypeRegistryInfo DataTypeInfo;

	FConstInputHandle Handle = FGraphBuilder::GetConstInputHandleFromPin(&InPin);
	ensure(IDataTypeRegistry::Get().GetDataTypeInfo(Handle->GetDataType(), DataTypeInfo));


	return DataTypeInfo;
}

TSet<FString> UMetasoundEditorGraphNode::GetDisallowedPinClassNames(const UEdGraphPin& InPin) const
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	const IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetaSoundEditor");

	const FDataTypeRegistryInfo DataTypeInfo = GetPinDataTypeInfo(InPin);
	if (DataTypeInfo.PreferredLiteralType != Metasound::ELiteralType::UObjectProxy)
	{
		return { };
	}

	UClass* ProxyGenClass = DataTypeInfo.ProxyGeneratorClass;
	if (!ProxyGenClass)
	{
		return { };
	}

	TSet<FString> DisallowedClasses;
	const FName ClassName = ProxyGenClass->GetFName();
	for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
	{
		UClass* Class = *ClassIt;
		if (!Class->IsNative())
		{
			continue;
		}

		if (Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			continue;
		}

		if (ClassIt->GetFName() == ClassName)
		{
			continue;
		}

		if (EditorModule.IsExplicitProxyClass(*Class) && Class->IsChildOf(ProxyGenClass))
		{
			DisallowedClasses.Add(ClassIt->GetName());
		}
	}

	return DisallowedClasses;
}

FString UMetasoundEditorGraphNode::GetPinMetaData(FName InPinName, FName InKey)
{
	if (InKey == "DisallowedClasses")
	{
		if (UEdGraphPin* Pin = FindPin(InPinName, EGPD_Input))
		{
			TSet<FString> DisallowedClasses = GetDisallowedPinClassNames(*Pin);
			return FString::Join(DisallowedClasses.Array(), TEXT(","));
		}

		return FString();
	}

	return Super::GetPinMetaData(InPinName, InKey);
}

void UMetasoundEditorGraphNode::PreSave(FObjectPreSaveContext InSaveContext)
{
	using namespace Metasound::Editor;

	Super::PreSave(InSaveContext);

	// Required to refresh upgrade nodes that are stale when saving.
	if (TSharedPtr<FEditor> MetaSoundEditor = FGraphBuilder::GetEditorForMetasound(GetMetasoundChecked()))
	{
		if (TSharedPtr<SGraphEditor> GraphEditor = MetaSoundEditor->GetGraphEditor())
		{
			GraphEditor->RefreshNode(*this);
		}
	}
}

void UMetasoundEditorGraphNode::PostEditImport()
{
}

void UMetasoundEditorGraphNode::PostEditUndo()
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	UEdGraphPin::ResolveAllPinReferences();

	// This can trigger and the handle is no longer valid if transaction
	// is being undone on a graph node that is orphaned.  If orphaned,
	// bail early.
	FNodeHandle NodeHandle = GetNodeHandle();
	if (!NodeHandle->IsValid())
	{
		return;
	}

	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin && Pin->Direction == EGPD_Input)
		{
			FGraphBuilder::SynchronizePinLiteral(*Pin);
		}
	}
}

void UMetasoundEditorGraphNode::UpdatePosition()
{
	GetMetasoundChecked().Modify();

	Metasound::Frontend::FNodeHandle NodeHandle = GetNodeHandle();
	FMetasoundFrontendNodeStyle Style = NodeHandle->GetNodeStyle();
	Style.Display.Locations.FindOrAdd(NodeGuid) = FVector2D(NodePosX, NodePosY);
	GetNodeHandle()->SetNodeStyle(Style);
}

void UMetasoundEditorGraphNode::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);

	if (!bDuplicateForPIE)
	{
		CreateNewGuid();
	}
}

void UMetasoundEditorGraphNode::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	using namespace Metasound::Editor;

	if (Context->Pin)
	{
		// If on an input that can be deleted, show option
		if (Context->Pin->Direction == EGPD_Input)
		{
			FToolMenuSection& Section = Menu->AddSection("MetasoundEditorGraphDeleteInput");
			Section.AddMenuEntry(FEditorCommands::Get().DeleteInput);
		}
	}
	else if (Context->Node)
	{
		{
			FToolMenuSection& Section = Menu->AddSection("MetasoundEditorGraphNodeAlignment");
			Section.AddSubMenu("Alignment", LOCTEXT("AlignmentHeader", "Alignment"), FText(), FNewToolMenuDelegate::CreateLambda([](UToolMenu* SubMenu)
			{
				{
					FToolMenuSection& SubMenuSection = SubMenu->AddSection("EdGraphSchemaAlignment", LOCTEXT("AlignHeader", "Align"));
					SubMenuSection.AddMenuEntry(FGraphEditorCommands::Get().AlignNodesTop);
					SubMenuSection.AddMenuEntry(FGraphEditorCommands::Get().AlignNodesMiddle);
					SubMenuSection.AddMenuEntry(FGraphEditorCommands::Get().AlignNodesBottom);
					SubMenuSection.AddMenuEntry(FGraphEditorCommands::Get().AlignNodesLeft);
					SubMenuSection.AddMenuEntry(FGraphEditorCommands::Get().AlignNodesCenter);
					SubMenuSection.AddMenuEntry(FGraphEditorCommands::Get().AlignNodesRight);
					SubMenuSection.AddMenuEntry(FGraphEditorCommands::Get().StraightenConnections);
				}

				{
					FToolMenuSection& SubMenuSection = SubMenu->AddSection("EdGraphSchemaDistribution", LOCTEXT("DistributionHeader", "Distribution"));
					SubMenuSection.AddMenuEntry(FGraphEditorCommands::Get().DistributeNodesHorizontally);
					SubMenuSection.AddMenuEntry(FGraphEditorCommands::Get().DistributeNodesVertically);
				}
			}));
		}
	}
}

FText UMetasoundEditorGraphNode::GetTooltipText() const
{
	return GetConstNodeHandle()->GetDescription();
}

FText UMetasoundEditorGraphNode::GetDisplayName() const
{
	constexpr bool bIncludeNamespace = true;
	return Metasound::Editor::FGraphBuilder::GetDisplayName(*GetConstNodeHandle(), bIncludeNamespace);
}

FString UMetasoundEditorGraphNode::GetDocumentationExcerptName() const
{
	// Default the node to searching for an excerpt named for the C++ node class name, including the U prefix.
	// This is done so that the excerpt name in the doc file can be found by find-in-files when searching for the full class name.
	return FString::Printf(TEXT("%s%s"), UMetaSound::StaticClass()->GetPrefixCPP(), *UMetaSound::StaticClass()->GetName());
}

void UMetasoundEditorGraphOutputNode::PinDefaultValueChanged(UEdGraphPin* InPin)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	if (InPin && InPin->Direction == EGPD_Input)
	{
		GetMetasoundChecked().Modify();

		FInputHandle InputHandle = FGraphBuilder::GetInputHandleFromPin(InPin);
		if (InputHandle->IsValid())
		{
			FMetasoundFrontendLiteral LiteralValue;
			if (FGraphBuilder::GetPinLiteral(*InPin, LiteralValue))
			{
				InputHandle->SetLiteral(LiteralValue);
			}

			if (Output)
			{
				UMetasoundEditorGraphMemberDefaultLiteral* Literal = Output->GetLiteral();
				if (ensure(Literal))
				{
					Literal->SetFromLiteral(LiteralValue);
				}

				constexpr bool bPostTransaction = false;
				Output->UpdateFrontendDefaultLiteral(bPostTransaction);
			}
		}
	}
}

bool UMetasoundEditorGraphOutputNode::EnableInteractWidgets() const
{
	using namespace Metasound::Frontend;

	bool bEnabled = true;
	GetConstNodeHandle()->IterateConstInputs([bIsEnabled = &bEnabled](FConstInputHandle InputHandle)
	{
		if (InputHandle->IsConnectionUserModifiable())
		{
			*bIsEnabled &= !InputHandle->IsConnected();
		}
	});
	return bEnabled;
}

FMetasoundFrontendClassName UMetasoundEditorGraphOutputNode::GetClassName() const
{
	if (ensure(Output))
	{
		return Output->ClassName;
	}
	return FMetasoundFrontendClassName();
}

FGuid UMetasoundEditorGraphOutputNode::GetNodeID() const
{
	if (Output)
	{
		return Output->NodeID;
	}
	return FGuid();
}

bool UMetasoundEditorGraphOutputNode::CanUserDeleteNode() const
{
	return !GetNodeHandle()->IsInterfaceMember();
}

void UMetasoundEditorGraphOutputNode::SetNodeID(FGuid InNodeID)
{
	if (ensure(Output))
	{
		Output->NodeID = InNodeID;
	}
}

FLinearColor UMetasoundEditorGraphOutputNode::GetNodeTitleColor() const
{
	if (const UMetasoundEditorSettings* EditorSettings = GetDefault<UMetasoundEditorSettings>())
	{
		return EditorSettings->OutputNodeTitleColor;
	}

	return Super::GetNodeTitleColor();
}

UMetasoundEditorGraphMember* UMetasoundEditorGraphOutputNode::GetMember() const
{
	return Output;
}

FSlateIcon UMetasoundEditorGraphOutputNode::GetNodeTitleIcon() const
{
	return FSlateIcon("MetaSoundStyle", "MetasoundEditor.Graph.Node.Class.Output");
}

bool UMetasoundEditorGraphExternalNode::RefreshPinMetadata()
{
	using namespace Metasound::Frontend;

	bool bModified = false;

	FConstNodeHandle NodeHandle = GetConstNodeHandle();
	const FMetasoundFrontendClassInterface& ClassInterface = NodeHandle->GetClassInterface();
	TArray<FMetasoundFrontendClassInput> Inputs = ClassInterface.Inputs;
	TArray<FMetasoundFrontendClassOutput> Outputs = ClassInterface.Outputs;

	for (UEdGraphPin* Pin : Pins)
	{
		check(Pin);

		if (Pin->Direction == EGPD_Input)
		{
			// Remove valid class inputs on iteration to avoid additional
			// incurred look-up cost on subsequent tooltip update.
			auto RemoveValidEntry = [&](const FMetasoundFrontendClassInput& Entry)
			{
				if (Entry.Name == Pin->GetFName())
				{
					Metasound::Editor::FGraphBuilder::RefreshPinMetadata(*Pin, Entry.Metadata);
					return true;
				}

				return false;
			};
			bModified |= Inputs.RemoveAllSwap(RemoveValidEntry) > 0;
		}

		if (Pin->Direction == EGPD_Output)
		{
			// Remove valid class inputs on iteration to avoid additional
			// incurred look-up cost on subsequent tooltip update.
			auto RemoveValidEntry = [&](const FMetasoundFrontendClassOutput& Entry)
			{
				if (Entry.Name == Pin->GetFName())
				{
					Metasound::Editor::FGraphBuilder::RefreshPinMetadata(*Pin, Entry.Metadata);
					return true;
				}

				return false;
			};
			bModified |= Outputs.RemoveAllSwap(RemoveValidEntry) > 0;
		}
	}

	return bModified;
}

FMetasoundFrontendVersionNumber UMetasoundEditorGraphExternalNode::FindHighestVersionInRegistry() const
{
	return GetConstNodeHandle()->FindHighestVersionInRegistry();
}

FMetasoundFrontendVersionNumber UMetasoundEditorGraphExternalNode::FindHighestMinorVersionInRegistry() const
{
	return GetConstNodeHandle()->FindHighestMinorVersionInRegistry();
}

bool UMetasoundEditorGraphExternalNode::CanAutoUpdate() const
{
	using namespace Metasound::Frontend;

	return GetConstNodeHandle()->CanAutoUpdate();
}

UMetasoundEditorGraphExternalNode* UMetasoundEditorGraphExternalNode::UpdateToVersion(const FMetasoundFrontendVersionNumber& InNewVersion, bool bInPropagateErrorMessages)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	FNodeHandle InitNodeHandle = GetNodeHandle();
	const FGuid InitNodeID = GetNodeID();
	const FMetasoundFrontendVersionNumber InitVersion = InitNodeHandle->GetClassMetadata().GetVersion();

	FNodeHandle ReplacementNode = InitNodeHandle->ReplaceWithVersion(InNewVersion);
	const FGuid ReplacementNodeID = ReplacementNode->GetID();

	if (!ensure(InitNodeID != ReplacementNodeID))
	{
		return this;
	}

	const FMetasoundFrontendNodeStyle& Style = ReplacementNode->GetNodeStyle();
	FVector2D NodeLocation = FVector2D::ZeroVector;
	const FVector2D* StyleLocation = Style.Display.Locations.Find(NodeGuid);
	if (ensure(StyleLocation))
	{
		NodeLocation = *StyleLocation;
	}

	UObject& MetaSound = GetMetasoundChecked();
	UMetasoundEditorGraphExternalNode* ReplacementEdNode = FGraphBuilder::AddExternalNode(MetaSound, ReplacementNode, NodeLocation, false /* bInSelectNewNode */);

	if (!ensure(ReplacementEdNode))
	{
		return this;
	}

	GetGraph()->RemoveNode(this);
	MetaSound.Modify();

	if (InitNodeID != ReplacementNodeID)
	{
		if (bInPropagateErrorMessages)
		{
			ReplacementEdNode->bHasCompilerMessage = bHasCompilerMessage;
			ReplacementEdNode->ErrorMsg = ErrorMsg;
			ReplacementEdNode->ErrorType = ErrorType;
		}
	}

	bRefreshNode = true;
	return ReplacementEdNode;
}

bool UMetasoundEditorGraphExternalNode::Validate(Metasound::Editor::FGraphNodeValidationResult& OutResult)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	OutResult = FGraphNodeValidationResult(*this);

	// 1. Reset ed node validation state
	if (ErrorType != EMessageSeverity::Info)
	{
		ErrorType = EMessageSeverity::Info;
		OutResult.bIsDirty = true;
	}

	if (bHasCompilerMessage)
	{
		bHasCompilerMessage = false;
		ErrorMsg.Reset();
		OutResult.bIsDirty = true;
	}

	// 2. Check if node is invalid, version is missing and cache if interface changes exist between the document's records and the registry
	FNodeHandle NodeHandle = GetNodeHandle();
	const FMetasoundFrontendClassMetadata& Metadata = NodeHandle->GetClassMetadata();

	FClassInterfaceUpdates InterfaceUpdates;
	if (!NodeHandle->DiffAgainstRegistryInterface(InterfaceUpdates, false /* bUseHighestMinorVersion */))
	{
		if (NodeHandle->IsValid())
		{
			const FText* PromptIfMissing = nullptr;
			FString FormattedClassName;
			if (bIsClassNative)
			{
				PromptIfMissing = &Metadata.GetPromptIfMissing();
				FormattedClassName = FString::Format(TEXT("{0} {1} ({2})"), { *Metadata.GetDisplayName().ToString(), *Metadata.GetVersion().ToString(), *Metadata.GetClassName().ToString() });
			}
			else
			{
				static const FText AssetPromptIfMissing = LOCTEXT("PromptIfAssetMissing", "Asset may have not been saved, deleted or is not loaded (ex. in an unloaded plugin).");
				PromptIfMissing = &AssetPromptIfMissing;
				FormattedClassName = FString::Format(TEXT("{0} {1} ({2})"), { *Metadata.GetDisplayName().ToString(), *Metadata.GetVersion().ToString(), *Metadata.GetClassName().Name.ToString() });
			}

			const FString NewErrorMsg = FString::Format(TEXT("Class definition '{0}' not found: {1}"),
			{
				*FormattedClassName,
				*PromptIfMissing->ToString()
			});

			GraphNodePrivate::SetGraphNodeMessage(*this, EMessageSeverity::Error, NewErrorMsg);
		}
		else
		{
			if (bIsClassNative)
			{
				GraphNodePrivate::SetGraphNodeMessage(*this, EMessageSeverity::Error, FString::Format(
					TEXT("Class '{0}' definition missing for last known natively defined node."),
					{ *ClassName.ToString() }));
			}
			else
			{
				GraphNodePrivate::SetGraphNodeMessage(*this, EMessageSeverity::Error,
					FString::Format(TEXT("Class definition missing for asset with guid '{0}': Asset is either missing or invalid"),
					{ *ClassName.Name.ToString() }));
			}
		}

		OutResult.bIsDirty = true;
		OutResult.bIsInvalid = true;
	}

	// 3. Report if node was nativized
	const FNodeRegistryKey RegistryKey = NodeRegistryKey::CreateKey(Metadata);
	bool bNewIsClassNative = FMetasoundFrontendRegistryContainer::Get()->IsNodeNative(RegistryKey);
	if (bIsClassNative != bNewIsClassNative)
	{
		if (bNewIsClassNative)
		{
			NodeUpgradeMessage = FText::Format(LOCTEXT("MetaSoundNode_NativizedMessage", "Class '{0}' has been nativized."), Metadata.GetDisplayName());
		}

		OutResult.bIsDirty = true;
		bIsClassNative = bNewIsClassNative;
	}

	// 4. Report if node was auto-updated
	FMetasoundFrontendNodeStyle Style = NodeHandle->GetNodeStyle();
	if (Style.bMessageNodeUpdated)
	{
		NodeUpgradeMessage = FText::Format(LOCTEXT("MetaSoundNode_UpgradedMessage", "Node class '{0}' updated to version {1}"),
			Metadata.GetDisplayName(),
			FText::FromString(Metadata.GetVersion().ToString())
		);
		OutResult.bIsDirty = true;
	}

	// 5. Reset pin state (if pin was orphaned or clear if no longer orphaned)
	for (UEdGraphPin* Pin : Pins)
	{
		bool bWasRemoved = false;
		if (Pin->Direction == EGPD_Input)
		{
			FInputHandle Input = FGraphBuilder::GetInputHandleFromPin(Pin);
			bWasRemoved |= InterfaceUpdates.RemovedInputs.ContainsByPredicate([&](const FMetasoundFrontendClassInput* ClassInput)
			{
				return Input->GetName() == ClassInput->Name && Input->GetDataType() == ClassInput->TypeName;
			});
		}

		if (Pin->Direction == EGPD_Output)
		{
			FOutputHandle Output = FGraphBuilder::GetOutputHandleFromPin(Pin);
			bWasRemoved |= InterfaceUpdates.RemovedOutputs.ContainsByPredicate([&](const FMetasoundFrontendClassOutput* ClassOutput)
			{
				return Output->GetName() == ClassOutput->Name && Output->GetDataType() == ClassOutput->TypeName;
			});
		}

		if (Pin->bOrphanedPin != bWasRemoved)
		{
			Pin->bOrphanedPin = bWasRemoved;
			OutResult.bIsDirty = true;
		}
	}

	// Report of node class is deprecated
	FMetasoundFrontendClass RegisteredClass;
	if (FMetasoundFrontendRegistryContainer::Get()->GetFrontendClassFromRegistered(RegistryKey, RegisteredClass))
	{
		if (RegisteredClass.Metadata.GetIsDeprecated())
		{
			GraphNodePrivate::SetGraphNodeMessage(*this, EMessageSeverity::Warning,
				FString::Format(TEXT("Class '{0} {1}' is deprecated."),
				{
					*RegisteredClass.Metadata.GetClassName().ToString(),
					*RegisteredClass.Metadata.GetVersion().ToString()
				}));
		}
	}


	// Find all available versions & report if upgrade available
	const Metasound::FNodeClassName NodeClassName = Metadata.GetClassName().ToNodeClassName();
	const TArray<FMetasoundFrontendClass> SortedClasses = ISearchEngine::Get().FindClassesWithName(NodeClassName, true /* bSortByVersion */);
	if (SortedClasses.IsEmpty())
	{
		GraphNodePrivate::SetGraphNodeMessage(*this, EMessageSeverity::Error,
			FString::Format(TEXT("Class '{0} {1}' not registered."),
			{
				*Metadata.GetClassName().ToString(),
				*Metadata.GetVersion().ToString()
			}));
		OutResult.bIsDirty = true;
		OutResult.bIsInvalid = true;
	}
	else
	{
		const FMetasoundFrontendVersionNumber& CurrentVersion = Metadata.GetVersion();
		const FMetasoundFrontendClass& HighestRegistryClass = SortedClasses[0];
		if (HighestRegistryClass.Metadata.GetVersion() > CurrentVersion)
		{
			FMetasoundFrontendClass HighestMinorVersionClass;
			FString NodeMsg;
			EMessageSeverity::Type Severity;

			const bool bClassVersionExists = SortedClasses.ContainsByPredicate([InCurrentVersion = &CurrentVersion](const FMetasoundFrontendClass& AvailableClass)
				{
					return AvailableClass.Metadata.GetVersion() == *InCurrentVersion;
				});
			if (bClassVersionExists)
			{
				NodeMsg = FString::Format(TEXT("Node class '{0} {1}' is prior version: Eligible for upgrade to {2}"),
					{
						*Metadata.GetClassName().ToString(),
						*Metadata.GetVersion().ToString(),
						*HighestRegistryClass.Metadata.GetVersion().ToString()
					});
				Severity = EMessageSeverity::Warning;
			}
			else
			{
				NodeMsg = FString::Format(TEXT("Node class '{0} {1}' is missing and ineligible for auto-update.  Highest version '{2}' found."),
					{
						*Metadata.GetClassName().ToString(),
						*Metadata.GetVersion().ToString(),
						*HighestRegistryClass.Metadata.GetVersion().ToString()
					});
				Severity = EMessageSeverity::Error;
				OutResult.bIsInvalid = true;
			}

			GraphNodePrivate::SetGraphNodeMessage(*this, Severity, NodeMsg);
			OutResult.bIsDirty = true;
		}
		else if (HighestRegistryClass.Metadata.GetVersion() == CurrentVersion)
		{
			if (InterfaceUpdates.ContainsChanges())
			{
				GraphNodePrivate::SetGraphNodeMessage(*this, EMessageSeverity::Error,
					FString::Format(TEXT("Node & registered class interface mismatch: '{0} {1}'. Class either versioned improperly, class key collision exists, or AutoUpdate disabled in 'MetaSound' Developer Settings."),
						{
							*Metadata.GetClassName().ToString(),
							*Metadata.GetVersion().ToString()
						}));
				OutResult.bIsDirty = true;
				OutResult.bIsInvalid = true;
			}
		}
		else
		{
			GraphNodePrivate::SetGraphNodeMessage(*this, EMessageSeverity::Error,
				FString::Format(TEXT("Node with class '{0} {1}' interface version higher than that of highest minor revision ({2}) in class registry."),
					{
						*Metadata.GetClassName().ToString(),
						*Metadata.GetVersion().ToString(),
						*HighestRegistryClass.Metadata.GetVersion().ToString()
					}));
			OutResult.bIsDirty = true;
			OutResult.bIsInvalid = true;
		}
	}

	return !OutResult.bIsInvalid;
}

void UMetasoundEditorGraphExternalNode::PostLoad()
{
	Super::PostLoad();
}

FLinearColor UMetasoundEditorGraphExternalNode::GetNodeTitleColor() const
{
	if (const UMetasoundEditorSettings* EditorSettings = GetDefault<UMetasoundEditorSettings>())
	{
		if (bIsClassNative)
		{
			return EditorSettings->NativeNodeTitleColor;
		}

		return EditorSettings->AssetReferenceNodeTitleColor;
	}

	return Super::GetNodeTitleColor();
}

FSlateIcon UMetasoundEditorGraphExternalNode::GetNodeTitleIcon() const
{
	if (bIsClassNative)
	{
		return FSlateIcon("MetaSoundStyle", "MetasoundEditor.Graph.Node.Class.Native");
	}
	else
	{
		return FSlateIcon("MetasoundStyle", "MetasoundEditor.Graph.Node.Class.Graph");
	}
}

UMetasoundEditorGraphMember* UMetasoundEditorGraphVariableNode::GetMember() const
{
	return Variable;
}

bool UMetasoundEditorGraphVariableNode::EnableInteractWidgets() const
{
	using namespace Metasound::Frontend;

	bool bEnabled = false;

	if (Variable)
	{
		FConstVariableHandle VariableHandle = Variable->GetConstVariableHandle();
		FConstNodeHandle MutatorNode = VariableHandle->FindMutatorNode();
		if (MutatorNode->IsValid())
		{
			if (MutatorNode->GetID() == NodeID)
			{
				bEnabled = true;
				MutatorNode->IterateConstInputs([bIsEnabled = &bEnabled](FConstInputHandle InputHandle)
				{
					if (InputHandle->IsConnectionUserModifiable())
					{
						// Don't enable if variable input is connected
						*bIsEnabled &= !InputHandle->IsConnected();
					}
				});
			}
		}
	}

	return bEnabled;
}

FMetasoundFrontendClassName UMetasoundEditorGraphVariableNode::GetClassName() const
{
	return ClassName;
}

FGuid UMetasoundEditorGraphVariableNode::GetNodeID() const
{
	return NodeID;
}

void UMetasoundEditorGraphVariableNode::PinDefaultValueChanged(UEdGraphPin* Pin)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	if (Pin && Pin->Direction == EGPD_Input)
	{
		GetMetasoundChecked().Modify();

		FInputHandle InputHandle = FGraphBuilder::GetInputHandleFromPin(Pin);
		if (InputHandle->IsValid())
		{
			FMetasoundFrontendLiteral LiteralValue;
			if (FGraphBuilder::GetPinLiteral(*Pin, LiteralValue))
			{
				InputHandle->SetLiteral(LiteralValue);
			}

			// If this is the mutator node, synchronize the variable default literal with this default.
			FNodeHandle MutatorNode = Variable->GetVariableHandle()->FindMutatorNode();
			if (MutatorNode->IsValid())
			{
				if (MutatorNode->GetID() == NodeID)
				{
					UMetasoundEditorGraphMemberDefaultLiteral* Literal = Variable->GetLiteral();
					if (ensure(Literal))
					{
						Literal->SetFromLiteral(LiteralValue);
					}

					constexpr bool bPostTransaction = false;
					Variable->UpdateFrontendDefaultLiteral(bPostTransaction);
				}
			}
		}
	}
}

FLinearColor UMetasoundEditorGraphVariableNode::GetNodeTitleColor() const
{
	if (const UMetasoundEditorSettings* EditorSettings = GetDefault<UMetasoundEditorSettings>())
	{
		return EditorSettings->VariableNodeTitleColor;
	}

	return Super::GetNodeTitleColor();
}

FSlateIcon UMetasoundEditorGraphVariableNode::GetNodeTitleIcon() const
{
	return FSlateIcon("MetaSoundStyle", "MetasoundEditor.Graph.Node.Class.Variable");
}

void UMetasoundEditorGraphVariableNode::SetNodeID(FGuid InNodeID)
{
	NodeID = InNodeID;
}
#undef LOCTEXT_NAMESPACE
