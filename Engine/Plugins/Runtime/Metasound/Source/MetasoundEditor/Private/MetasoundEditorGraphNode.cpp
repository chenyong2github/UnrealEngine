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
#include "MetasoundFrontend.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundFrontendSearchEngine.h"
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
	using namespace Metasound;

	// Don't remove unused pins here. Reconstruction can occur while duplicating or pasting nodes,
	// and subsequent steps clean-up unused pins.  This can be called mid-copy, which means the node
	// handle may be invalid.  Setting to remove unused causes premature removal and then default values
	// are lost.
	// TODO: User will want to see dead pins as well for node definition changes. Label and color-code dead
	// pins (ex. red), and leave dead connections for reference like BP.
	Editor::FGraphBuilder::SynchronizeNodePins(*this, GetNodeHandle(), false /* bRemoveUnusedPins */, false /* bLogChanges */);
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

void UMetasoundEditorGraphNode::PinDefaultValueChanged(UEdGraphPin* Pin)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	if (Pin && Pin->Direction == EGPD_Input)
	{
		GetMetasoundChecked().Modify();

		FNodeHandle NodeHandle = GetNodeHandle();
		IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetaSoundEditor");
		FGraphBuilder::AddOrUpdateLiteralInput(GetMetasoundChecked(), NodeHandle, *Pin);
	}
}

FString UMetasoundEditorGraphNode::GetPinMetaData(FName InPinName, FName InKey)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	if (InKey == "DisallowedClasses")
	{
		if (UEdGraphPin* Pin = FindPin(InPinName, EGPD_Input))
		{
			FInputHandle Handle = FGraphBuilder::GetInputHandleFromPin(Pin);

			FMetasoundFrontendRegistryContainer* Registry = FMetasoundFrontendRegistryContainer::Get();
			if (!ensure(Registry))
			{
				return FString();
			}

			Metasound::FDataTypeRegistryInfo DataTypeInfo;
			if (!ensure(Registry->GetInfoForDataType(Handle->GetDataType(), DataTypeInfo)))
			{
				return FString();
			}

			const EMetasoundFrontendLiteralType LiteralType = GetMetasoundFrontendLiteralType(DataTypeInfo.PreferredLiteralType);
			if (LiteralType != EMetasoundFrontendLiteralType::UObject && LiteralType != EMetasoundFrontendLiteralType::UObjectArray)
			{
				return FString();
			}

			UClass* ProxyGenClass = DataTypeInfo.ProxyGeneratorClass;
			if (!ProxyGenClass)
			{
				return FString();
			}

			FString DisallowedClasses;
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

				if (Class->IsChildOf(ProxyGenClass))
				{
					if (!DisallowedClasses.IsEmpty())
					{
						DisallowedClasses += TEXT(",");
					}
					DisallowedClasses += *ClassIt->GetName();
				}
			}

			return DisallowedClasses;
		}

		return FString();
	}

	return Super::GetPinMetaData(InPinName, InKey);
}

void UMetasoundEditorGraphNode::PostEditImport()
{
}

void UMetasoundEditorGraphNode::PostEditChangeProperty(FPropertyChangedEvent& InEvent)
{
	const FName PropertyName = InEvent.GetPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UEdGraphNode, NodePosX)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UEdGraphNode, NodePosY))
	{
		UpdatePosition();
	}

	Super::PostEditChangeProperty(InEvent);
}

void UMetasoundEditorGraphNode::PostEditChangeChainProperty(FPropertyChangedChainEvent& InEvent)
{
	Super::PostEditChangeChainProperty(InEvent);
}

void UMetasoundEditorGraphNode::PostEditUndo()
{
	UEdGraphPin::ResolveAllPinReferences();

	// This can trigger and the handle is no longer valid if transaction
	// is being undone on a graph node that is orphaned.  If orphaned,
	// bail early.
	Metasound::Frontend::FNodeHandle NodeHandle = GetNodeHandle();
	if (!NodeHandle->IsValid())
	{
		return;
	}

	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin && Pin->Direction == EGPD_Input)
		{
			UObject& Metasound = GetMetasoundChecked();
			Metasound::Editor::FGraphBuilder::AddOrUpdateLiteralInput(Metasound, NodeHandle, *Pin);
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

FString UMetasoundEditorGraphNode::GetDocumentationExcerptName() const
{
	// Default the node to searching for an excerpt named for the C++ node class name, including the U prefix.
	// This is done so that the excerpt name in the doc file can be found by find-in-files when searching for the full class name.
	return FString::Printf(TEXT("%s%s"), UMetaSound::StaticClass()->GetPrefixCPP(), *UMetaSound::StaticClass()->GetName());
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
	if (ensure(Output))
	{
		return Output->NodeID;
	}
	return FGuid();
}

bool UMetasoundEditorGraphOutputNode::CanUserDeleteNode() const
{
	return !GetNodeHandle()->IsRequired();
}

void UMetasoundEditorGraphOutputNode::SetNodeID(FGuid InNodeID)
{
	if (ensure(Output))
	{
		Output->NodeID = InNodeID;
	}
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
				const FString PinNameString = Pin->PinName.ToString();
				if (Entry.Name == PinNameString)
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
				const FString PinNameString = Pin->PinName.ToString();
				if (Entry.Name == PinNameString)
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

FMetasoundFrontendVersionNumber UMetasoundEditorGraphExternalNode::GetMajorUpdateAvailable() const
{
	using namespace Metasound::Frontend;

	const FMetasoundFrontendClassMetadata& Metadata = GetConstNodeHandle()->GetClassMetadata();
	const FMetasoundFrontendVersionNumber& CurrentVersion = Metadata.Version;
	Metasound::FNodeClassName NodeClassName = Metadata.ClassName.ToNodeClassName();

	FMetasoundFrontendClass ClassWithHighestVersion;
	if (ISearchEngine::Get().FindClassWithHighestVersion(NodeClassName, ClassWithHighestVersion))
	{
		if (ClassWithHighestVersion.Metadata.Version.Major > CurrentVersion.Major)
		{
			return ClassWithHighestVersion.Metadata.Version;
		}
	}

	return FMetasoundFrontendVersionNumber::GetInvalid();
}

FMetasoundFrontendVersionNumber UMetasoundEditorGraphExternalNode::GetMinorUpdateAvailable() const
{
	using namespace Metasound::Frontend;

	const FMetasoundFrontendClassMetadata& Metadata = GetConstNodeHandle()->GetClassMetadata();
	const FMetasoundFrontendVersionNumber& CurrentVersion = Metadata.Version;
	Metasound::FNodeClassName NodeClassName = Metadata.ClassName.ToNodeClassName();

	FMetasoundFrontendClass ClassWithHighestVersion;
	if (ISearchEngine::Get().FindClassWithMajorVersion(NodeClassName, CurrentVersion.Major, ClassWithHighestVersion))
	{
		if (ClassWithHighestVersion.Metadata.Version.Minor > CurrentVersion.Minor)
		{
			return ClassWithHighestVersion.Metadata.Version;
		}
	}

	return FMetasoundFrontendVersionNumber::GetInvalid();
}

UMetasoundEditorGraphExternalNode* UMetasoundEditorGraphExternalNode::UpdateToVersion(const FMetasoundFrontendVersionNumber& InNewVersion, bool bInPropagateErrorMessages)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	const FMetasoundFrontendClassMetadata& Metadata = GetConstNodeHandle()->GetClassMetadata();
	const TArray<FMetasoundFrontendClass> SortedVersions = ISearchEngine::Get().FindClassesWithName(Metadata.ClassName.ToNodeClassName(), true /* bInSortByVersion */);

	auto IsClassOfNewVersion = [InNewVersion](const FMetasoundFrontendClass& SortedClass)
	{
		return SortedClass.Metadata.Version == InNewVersion;
	};

	if (!ensure(SortedVersions.ContainsByPredicate(IsClassOfNewVersion)))
	{
		return this;
	}

	struct FConnectionInfo
	{
		TArray<UEdGraphPin*> PinsLinkedTo;
		FName DataType;
		FString DefaultValue;
	};

	// Cache pin connections by name to try so they can be
	// hooked back up after swapping to the new class version.
	TMap<FName, FConnectionInfo> InputConnections;
	IteratePins([Connections = &InputConnections](UEdGraphPin& Pin, int32 /* Index */)
	{
		FConstInputHandle InputHandle = FGraphBuilder::GetConstInputHandleFromPin(&Pin);
		Connections->Add(Pin.GetFName(), FConnectionInfo
		{
			Pin.LinkedTo,
			InputHandle->GetDataType(),
			Pin.DefaultValue
		});
	}, EGPD_Input);

	TMap<FName, FConnectionInfo> OutputConnections;
	IteratePins([Connections = &OutputConnections](UEdGraphPin& Pin, int32 /* Index */)
	{
		FConstOutputHandle OutputHandle = FGraphBuilder::GetConstOutputHandleFromPin(&Pin);
		Connections->Add(Pin.GetFName(), FConnectionInfo
		{
			Pin.LinkedTo,
			OutputHandle->GetDataType()
		});
	}, EGPD_Output);

	FNodeHandle NodeHandle = GetNodeHandle();
	FMetasoundFrontendVersionNumber InitVersion = NodeHandle->GetClassMetadata().Version;

	// Only update the version on the Metadata. Name and version are the only fields that pertain
	// to building valid class data (via the GetRegistryKey look-up).  All other Metadata is
	// superfluous for this use case.
	FMetasoundFrontendClassMetadata NewMetadata = NodeHandle->GetClassMetadata();
	NewMetadata.Version = InNewVersion;

	const FVector2D NodePos(NodePosX, NodePosY);
	if (!ensureAlways(FGraphBuilder::DeleteNode(*this)))
	{
		return this;
	}

	Modify();
	UObject& Metasound = GetMetasoundChecked();
	UMetasoundEditorGraphExternalNode* ReplacementNode = FGraphBuilder::AddExternalNode(Metasound, NewMetadata, NodePos);
	if (!ensureAlways(ReplacementNode))
	{
		return this;
	}

	ReplacementNode->Modify();
	ReplacementNode->IteratePins([&](UEdGraphPin& Pin, int32 /* Index */)
	{
		if (FConnectionInfo* ConnectionInfo = InputConnections.Find(Pin.GetFName()))
		{
			FInputHandle InputHandle = FGraphBuilder::GetInputHandleFromPin(&Pin);
			if (ConnectionInfo->DataType == InputHandle->GetDataType())
			{
				if (ConnectionInfo->PinsLinkedTo.IsEmpty())
				{
					Pin.DefaultValue = ConnectionInfo->DefaultValue;
					FGraphBuilder::AddOrUpdateLiteralInput(Metasound, InputHandle->GetOwningNode(), Pin, true /* bForcePinValueAsDefault */);
				}
				else
				{
					for (UEdGraphPin* OutputPin : ConnectionInfo->PinsLinkedTo)
					{
						FGraphBuilder::ConnectNodes(Pin, *OutputPin, true /* bInConnectEdPins */);
					}
				}
			}
		}
	}, EGPD_Input);

	ReplacementNode->IteratePins([&](UEdGraphPin& Pin, int32 /* Index */)
	{
		if (FConnectionInfo* ConnectionInfo = OutputConnections.Find(Pin.GetFName()))
		{
			FConstOutputHandle OutputHandle = FGraphBuilder::GetConstOutputHandleFromPin(&Pin);
			if (ConnectionInfo->DataType == OutputHandle->GetDataType())
			{
				for (UEdGraphPin* InputPin : ConnectionInfo->PinsLinkedTo)
				{
					FGraphBuilder::ConnectNodes(*InputPin, Pin, true /* bInConnectEdPins */);
				}
			}
		}
	}, EGPD_Output);

	ReplacementNode->NodeUpgradeMessage = FText::Format(LOCTEXT("NodeVersionUpgradeMessageFormat", "Class upgraded from {0} to {1}"),
		FText::FromString(*InitVersion.ToString()),
		FText::FromString(*InNewVersion.ToString())
	);

	if (ReplacementNode != this)
	{
		if (bInPropagateErrorMessages)
		{
			ReplacementNode->bHasCompilerMessage = bHasCompilerMessage;
			ReplacementNode->ErrorMsg = ErrorMsg;
			ReplacementNode->ErrorType = ErrorType;
		}
	}

	return ReplacementNode;
}

bool UMetasoundEditorGraphExternalNode::Validate(Metasound::Editor::FGraphNodeValidationResult& OutResult)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	OutResult = FGraphNodeValidationResult(*this);

	FNodeHandle NodeHandle = GetNodeHandle();

	if (bHasCompilerMessage)
	{
		bHasCompilerMessage = false;
		ErrorMsg.Reset();
		OutResult.bIsDirty = true;
	}

	if (!ensure(NodeHandle->IsValid()))
	{
		GraphNodePrivate::SetGraphNodeMessage(*this, EMessageSeverity::Error, TEXT("Node is invalid (Frontend node not found)"));
		OutResult.bIsInvalid = true;
		OutResult.bIsDirty = true;
		return true;
	}

	const FMetasoundFrontendClassMetadata& Metadata = NodeHandle->GetClassMetadata();
	if (!ensure(Metadata.Type == EMetasoundFrontendClassType::External))
	{
		GraphNodePrivate::SetGraphNodeMessage(*this, EMessageSeverity::Error, TEXT("Node is of type 'External' but Frontend counterpart is not natively-defined."));
		OutResult.bIsDirty = true;
		OutResult.bIsInvalid = true;
		return true;
	}

	const Metasound::FNodeClassName NodeClassName = Metadata.ClassName.ToNodeClassName();
	const TArray<FMetasoundFrontendClass> SortedClasses = ISearchEngine::Get().FindClassesWithName(NodeClassName, true /* bSortByVersion */);

	if (SortedClasses.IsEmpty())
	{
		OutResult.MissingClass = NodeClassName.GetFullName();
		const FString NewErrorMsg = FString::Format(TEXT("Missing Class '{0}': {1}"),
			{
				*Metadata.ClassName.ToString(),
				*Metadata.PromptIfMissing.ToString()
			});

		for (UEdGraphPin* Pin : Pins)
		{
			Pin->bOrphanedPin = true;
		}

		GraphNodePrivate::SetGraphNodeMessage(*this, EMessageSeverity::Error, NewErrorMsg);
		OutResult.bIsDirty = true;
		OutResult.bIsInvalid = true;
		return true;
	}

	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin->bOrphanedPin)
		{
			Pin->bOrphanedPin = false;
			OutResult.bIsDirty = true;
		}
	}

	if (SortedClasses[0].Metadata.Version.Major > Metadata.Version.Major)
	{
		const FMetasoundFrontendVersionNumber& CurrentVersion = NodeHandle->GetClassMetadata().Version;

		FMetasoundFrontendClass HighestMinorVersionClass;
		FString NodeMsg;
		EMessageSeverity::Type Severity;

		// Only compare major version as minor can be incorrect and graph
		// will still build and divert to using the new upgraded version.
		if (ISearchEngine::Get().FindClassWithMajorVersion(NodeClassName, CurrentVersion.Major, HighestMinorVersionClass))
		{
			NodeMsg = FString::Format(TEXT("Node class '{0}' out-of-date: Eligible for upgrade to {1}"),
			{
				*Metadata.ClassName.ToString(),
				*SortedClasses[0].Metadata.Version.ToString()
			});
			Severity = EMessageSeverity::Warning;
		}
		else
		{
			NodeMsg = FString::Format(TEXT("Node class '{0} ({1})' is missing.  Eligible for upgrade to {2}"),
			{
				*Metadata.ClassName.ToString(),
				*Metadata.Version.ToString(),
				*SortedClasses[0].Metadata.Version.ToString()
			});
			Severity = EMessageSeverity::Error;
		}

		GraphNodePrivate::SetGraphNodeMessage(*this, Severity, NodeMsg);
		OutResult.bIsDirty = true;
	}

	auto IsMinorUpgradeVersion = [&](const FMetasoundFrontendVersionNumber& Version)
	{
		if (Version.Major != Metadata.Version.Major)
		{
			return false;
		}

		return Version.Minor > Metadata.Version.Minor;
	};

	return false;
}
#undef LOCTEXT_NAMESPACE
