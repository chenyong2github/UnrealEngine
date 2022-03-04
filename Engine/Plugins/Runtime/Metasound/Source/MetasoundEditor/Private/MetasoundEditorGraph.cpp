// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundEditorGraph.h"

#include "Algo/Transform.h"
#include "AudioParameterControllerInterface.h"
#include "Components/AudioComponent.h"
#include "EdGraph/EdGraphNode.h"
#include "Interfaces/ITargetPlatform.h"
#include "MetasoundAssetBase.h"
#include "MetasoundEditorGraphBuilder.h"
#include "MetasoundEditorGraphInputNode.h"
#include "MetasoundEditorGraphMemberDefaults.h"
#include "MetasoundEditorGraphNode.h"
#include "MetasoundEditorGraphValidation.h"
#include "MetasoundEditorModule.h"
#include "MetasoundUObjectRegistry.h"
#include "MetasoundVariableNodes.h"
#include "MetasoundVertex.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "MetaSoundEditor"


namespace Metasound
{
	namespace Editor
	{
		namespace GraphPrivate
		{
			void OnLiteralChanged(UMetasoundEditorGraphMember& InMember, EPropertyChangeType::Type InChangeType)
			{
				constexpr bool bPostTransaction = false;
				InMember.UpdateFrontendDefaultLiteral(bPostTransaction);

				const bool bCommitChange = InChangeType != EPropertyChangeType::Interactive;
				if (bCommitChange)
				{
					if (UObject* MetaSound = InMember.GetOutermostObject())
					{
						FGraphBuilder::RegisterGraphWithFrontend(*MetaSound);
						if (FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(MetaSound))
						{
							MetaSoundAsset->SetUpdateDetailsOnSynchronization();
						}
					}
				}
			}

			void SetLiteralOrClearIfMatchesDefault(Frontend::FInputHandle& InInputHandle, const FMetasoundFrontendLiteral& InDefaultLiteral)
			{
				using namespace Frontend;

				// Avoids member literal setting the node literal if its not required (which in turn
				// avoids 'Reset To Default' action from being enabled when the default is equal)
				bool bClearLiteral = false;
				if (const FMetasoundFrontendLiteral* ClassDefault = InInputHandle->GetClassDefaultLiteral())
				{
					bClearLiteral = ClassDefault->IsEqual(InDefaultLiteral);
				}

				if (!bClearLiteral)
				{
					FMetasoundFrontendLiteral DefaultTypeLiteral;
					DefaultTypeLiteral.SetFromLiteral(IDataTypeRegistry::Get().CreateDefaultLiteral(InInputHandle->GetDataType()));
					bClearLiteral = InDefaultLiteral.IsEqual(DefaultTypeLiteral);
				}

				if (bClearLiteral)
				{
					InInputHandle->ClearLiteral();
				}
				else
				{
					InInputHandle->SetLiteral(InDefaultLiteral);
				}
			}
		} // namespace GraphPrivate
	} // namespace Editor
} // namespace Metasound

UMetasoundEditorGraph* UMetasoundEditorGraphMember::GetOwningGraph()
{
	return Cast<UMetasoundEditorGraph>(GetOuter());
}

const UMetasoundEditorGraph* UMetasoundEditorGraphMember::GetOwningGraph() const
{
	return Cast<const UMetasoundEditorGraph>(GetOuter());
}

void UMetasoundEditorGraphMember::ConformLiteralDataType()
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetaSoundEditor");
	const FEditorDataType& EditorDataType = EditorModule.FindDataTypeChecked(TypeName);
	const EMetasoundFrontendLiteralType LiteralType = static_cast<EMetasoundFrontendLiteralType>(EditorDataType.RegistryInfo.PreferredLiteralType);

	TSubclassOf<UMetasoundEditorGraphMemberDefaultLiteral> LiteralClass = EditorModule.FindDefaultLiteralClass(LiteralType);
	if (!LiteralClass)
	{
		LiteralClass = UMetasoundEditorGraphMemberDefaultLiteral::StaticClass();
	}

	if (!Literal || Literal->GetClass() != LiteralClass)
	{
		UMetasoundEditorGraphMemberDefaultLiteral* NewLiteral = NewObject<UMetasoundEditorGraphMemberDefaultLiteral>(this, LiteralClass, FName(), RF_Transactional);
		Literal = NewLiteral;
	}
}

FName UMetasoundEditorGraphMember::GetDataType() const
{
	return TypeName;
}

#if WITH_EDITOR
void UMetasoundEditorGraphMember::PostEditUndo()
{
	using namespace Metasound;

	Super::PostEditUndo();

	if (IsValid(this))
	{
		SetDataType(TypeName);

		constexpr bool bPostTransaction = false;
		UpdateFrontendDefaultLiteral(bPostTransaction);

		if (UMetasoundEditorGraph* Graph = GetOwningGraph())
		{
			if (IsValid(Graph))
			{
				UObject* MetaSound = Graph->GetMetasound();
				if (IsValid(MetaSound))
				{
					FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(MetaSound);
					check(MetasoundAsset);
					MetasoundAsset->SetUpdateDetailsOnSynchronization();
					MetasoundAsset->SetSynchronizationRequired();
				}
			}
		}
	}
}
#endif // WITH_EDITOR

void UMetasoundEditorGraphVertex::InitMember(FName InDataType, const FMetasoundFrontendLiteral& InDefaultLiteral, FGuid InNodeID, FMetasoundFrontendClassName&& InClassName)
{
	TypeName = InDataType;
	NodeID = InNodeID;
	ClassName = MoveTemp(InClassName);

	ConformLiteralDataType();

	if (ensure(Literal))
	{
		Literal->SetFromLiteral(InDefaultLiteral);
	}
}

TArray<UMetasoundEditorGraphMemberNode*> UMetasoundEditorGraphVertex::GetNodes() const
{
	TArray<UMetasoundEditorGraphMemberNode*> Nodes;

	const UMetasoundEditorGraph* Graph = GetOwningGraph();
	if (ensure(Graph))
	{
		Graph->GetNodesOfClassEx<UMetasoundEditorGraphMemberNode>(Nodes);
		for (int32 i = Nodes.Num() -1; i >= 0; --i)
		{
			UMetasoundEditorGraphNode* Node = Nodes[i];
			if (Node && Node->GetNodeID() != NodeID)
			{
				Nodes.RemoveAtSwap(i, 1, false /* bAllowShrinking */);
			}
		}
	}

	return Nodes;
}

FText UMetasoundEditorGraphVertex::GetDescription() const
{
	// TODO: should be getting description directly from vertex instead of from
	// node handle.
	return GetConstNodeHandle()->GetDescription();
}

void UMetasoundEditorGraphVertex::SetDescription(const FText& InDescription, bool bPostTransaction)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	{
		const FText TransactionLabel = FText::Format(LOCTEXT("SetGraphVertexTooltipFormat", "Set MetaSound {0}'s ToolTip"), GetGraphMemberLabel());
		const FScopedTransaction Transaction(TransactionLabel, bPostTransaction);

		if (UMetasoundEditorGraph* Graph = GetOwningGraph())
		{
			Graph->Modify();
			UObject& MetaSound = Graph->GetMetasoundChecked();
			MetaSound.Modify();

			FNodeHandle NodeHandle = GetNodeHandle();
			NodeHandle->SetDescription(InDescription);
		}
	}
}

FGuid UMetasoundEditorGraphVertex::GetMemberID() const 
{ 
	return NodeID;
}

FName UMetasoundEditorGraphVertex::GetMemberName() const
{
	using namespace Metasound::Frontend;

	FConstNodeHandle NodeHandle = GetConstNodeHandle();
	return NodeHandle->GetNodeName();
}

void UMetasoundEditorGraphVertex::SetMemberName(const FName& InNewName, bool bPostTransaction)
{
	using namespace Metasound;
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	{
		const FText TransactionLabel = FText::Format(LOCTEXT("RenameGraphVertexMemberNameFormat", "Set Metasound {0} MemberName"), GetGraphMemberLabel());
		const FScopedTransaction Transaction(TransactionLabel, bPostTransaction);

		UMetasoundEditorGraph* Graph = GetOwningGraph();
		if (Graph)
		{
			Graph->Modify();
			Graph->GetMetasoundChecked().Modify();
		}

		GetNodeHandle()->SetNodeName(InNewName);

		FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&Graph->GetMetasoundChecked());
		check(MetasoundAsset);
		MetasoundAsset->SetSynchronizationRequired();
	}

	NameChanged.Broadcast(NodeID);
}

FText UMetasoundEditorGraphVertex::GetDisplayName() const
{
	constexpr bool bIncludeNamespace = true;
	return Metasound::Editor::FGraphBuilder::GetDisplayName(*GetConstNodeHandle(), bIncludeNamespace);
}

void UMetasoundEditorGraphVertex::SetDisplayName(const FText& InNewName, bool bPostTransaction)
{
	using namespace Metasound::Frontend;

	{
		const FText TransactionLabel = FText::Format(LOCTEXT("RenameGraphVertexDisplayNameFormat", "Set Metasound {0} DisplayName"), GetGraphMemberLabel());
		const FScopedTransaction Transaction(TransactionLabel, bPostTransaction);

		if (UMetasoundEditorGraph* Graph = Cast<UMetasoundEditorGraph>(GetOuter()))
		{
			Graph->Modify();
			Graph->GetMetasoundChecked().Modify();
		}

		FNodeHandle NodeHandle = GetNodeHandle();
		NodeHandle->SetDisplayName(InNewName);

		const TArray<UMetasoundEditorGraphMemberNode*> Nodes = GetNodes();
		for (UMetasoundEditorGraphMemberNode* Node : Nodes)
		{
			const TArray<UEdGraphPin*>& Pins = Node->GetAllPins();
			ensure(Pins.Num() == 1);

			for (UEdGraphPin* Pin : Pins)
			{
				Pin->PinFriendlyName = InNewName;
			}
		}
	}

	NameChanged.Broadcast(NodeID);
}

void UMetasoundEditorGraphVertex::SetDataType(FName InNewType, bool bPostTransaction)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	if (InNewType == GetDataType())
	{
		return;
	}

	UMetasoundEditorGraph* Graph = GetOwningGraph();
	if (!ensure(Graph))
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("SetGraphVertexType", "Set MetaSound GraphVertex Type"), bPostTransaction);
	Graph->GetMetasoundChecked().Modify();
	Graph->Modify();
	Modify();

	// 1. Cache current editor input node reference positions & delete nodes.
	TArray<UMetasoundEditorGraphMemberNode*> InputNodes = GetNodes();
	TArray<FVector2D> NodeLocations;
	for (UMetasoundEditorGraphMemberNode* Node : InputNodes)
	{
		if (ensure(Node))
		{
			NodeLocations.Add(FVector2D(Node->NodePosX, Node->NodePosY));
		}
	}

	// 2. Cache the old version's Frontend data.
	FNodeHandle NodeHandle = GetNodeHandle();
	const FName NodeName = NodeHandle->GetNodeName();
	const FText NodeDisplayName = NodeHandle->GetDisplayName();

	// Remove the current nodes and vertex
	Graph->RemoveMemberNodes(*this);
	Graph->RemoveFrontendMember(*this);

	// 4. Add the new input node with the same identifier data but new datatype.
	UObject& Metasound = Graph->GetMetasoundChecked();
	FNodeHandle NewNodeHandle = AddNodeHandle(NodeName, InNewType);
	NewNodeHandle->SetNodeName(NodeName);
	NewNodeHandle->SetDisplayName(NodeDisplayName);

	if (!ensure(NewNodeHandle->IsValid()))
	{
		return;
	}

	ClassName = NewNodeHandle->GetClassMetadata().GetClassName();
	NodeID = NewNodeHandle->GetID();

	TypeName = InNewType;
	ConformLiteralDataType();

	// 6. Create new node references in the same locations as the old locations
	for (FVector2D Location : NodeLocations)
	{
		FGraphBuilder::AddNode(Metasound, NewNodeHandle, Location, false /* bInSelectNewNode */);
	}

	// Notify now that the node has a new ID (doing so before creating & syncing Frontend Node &
	// EdGraph variable can result in refreshing editors while in a desync'ed state)
	NameChanged.Broadcast(NodeID);
}

Metasound::Frontend::FNodeHandle UMetasoundEditorGraphVertex::GetNodeHandle()
{
	using namespace Metasound;

	UObject* Object = CastChecked<UMetasoundEditorGraph>(GetOuter())->GetMetasound();
	if (!ensure(Object))
	{
		return Frontend::INodeController::GetInvalidHandle();
	}

	FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(Object);
	check(MetasoundAsset);

	return MetasoundAsset->GetRootGraphHandle()->GetNodeWithID(NodeID);
}

Metasound::Frontend::FConstNodeHandle UMetasoundEditorGraphVertex::GetConstNodeHandle() const
{
	using namespace Metasound;

	const UObject* Object = CastChecked<UMetasoundEditorGraph>(GetOuter())->GetMetasound();
	if (!ensure(Object))
	{
		return Frontend::INodeController::GetInvalidHandle();
	}

	const FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(Object);
	check(MetasoundAsset);

	return MetasoundAsset->GetRootGraphHandle()->GetNodeWithID(NodeID);
}

const FMetasoundFrontendVersion& UMetasoundEditorGraphVertex::GetInterfaceVersion() const
{
	return GetConstNodeHandle()->GetInterfaceVersion();
}

bool UMetasoundEditorGraphVertex::IsInterfaceMember() const
{
	using namespace Metasound;

	return GetConstNodeHandle()->IsInterfaceMember();
}

bool UMetasoundEditorGraphVertex::CanRename() const
{
	return !IsInterfaceMember();
}

bool UMetasoundEditorGraphVertex::CanRename(const FText& InNewName, FText& OutError) const
{
	using namespace Metasound::Frontend;

	if (InNewName.IsEmptyOrWhitespace())
	{
		OutError = FText::Format(LOCTEXT("GraphVertexRenameInvalid_NameEmpty", "{0} cannot be empty string."), InNewName);
		return false;
	}

	if (IsInterfaceMember())
	{
		OutError = FText::Format(LOCTEXT("GraphVertexRenameInvalid_GraphVertexRequired", "{0} is interface member and cannot be renamed."), InNewName);
		return false;
	}

	bool bIsNameValid = true;
	const FString NewName = InNewName.ToString();
	FConstNodeHandle NodeHandle = GetConstNodeHandle();
	FConstGraphHandle GraphHandle = NodeHandle->GetOwningGraph();
	GraphHandle->IterateConstNodes([&](FConstNodeHandle NodeToCompare)
	{
		if (NodeID != NodeToCompare->GetID())
		{
			if (NewName == NodeToCompare->GetNodeName().ToString())
			{
				bIsNameValid = false;
				OutError = FText::Format(LOCTEXT("GraphVertexRenameInvalid_NameTaken", "{0} is already in use"), InNewName);
			}
		}
	}, GetClassType());

	return bIsNameValid;
}

#if WITH_EDITOR
void UMetasoundEditorGraphMemberDefaultLiteral::PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent)
{
	if (UMetasoundEditorGraphMember* Member = Cast<UMetasoundEditorGraphMember>(GetOuter()))
	{
		Metasound::Editor::GraphPrivate::OnLiteralChanged(*Member, InPropertyChangedEvent.ChangeType);
	}
}

void UMetasoundEditorGraphMemberDefaultLiteral::PostEditChangeChainProperty(FPropertyChangedChainEvent& InPropertyChangedEvent)
{
	if (UMetasoundEditorGraphMember* Member = Cast<UMetasoundEditorGraphMember>(GetOuter()))
	{
		Metasound::Editor::GraphPrivate::OnLiteralChanged(*Member, InPropertyChangedEvent.ChangeType);
	}
}

void UMetasoundEditorGraphMemberDefaultLiteral::PostEditUndo()
{
	Super::PostEditUndo();

	if (!IsValid(this))
	{
		return;
	}

	constexpr bool bPostTransaction = false;
	UMetasoundEditorGraphMember* Member = CastChecked<UMetasoundEditorGraphMember>(GetOuter());
	Member->UpdateFrontendDefaultLiteral(bPostTransaction);

	if (UMetasoundEditorGraph* Graph = Member->GetOwningGraph())
	{
		Graph->SetSynchronizationRequired();
	}
}
#endif // WITH_EDITOR

Metasound::Editor::ENodeSection UMetasoundEditorGraphInput::GetSectionID() const 
{
	return Metasound::Editor::ENodeSection::Inputs;
}

Metasound::Frontend::FNodeHandle UMetasoundEditorGraphInput::AddNodeHandle(const FName& InName, FName InDataType)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	UMetasoundEditorGraph* Graph = Cast<UMetasoundEditorGraph>(GetOuter());
	if (!ensure(Graph))
	{
		return UMetasoundEditorGraphVertex::AddNodeHandle(InName, InDataType);
	}

	UObject& Metasound = Graph->GetMetasoundChecked();
	Metasound::Frontend::FNodeHandle NewNodeHandle = FGraphBuilder::AddInputNodeHandle(Metasound, InDataType, nullptr /* DefaultValue */, &InName);
	return NewNodeHandle;
}

const FText& UMetasoundEditorGraphInput::GetGraphMemberLabel() const
{
	static const FText Label = LOCTEXT("GraphMemberLabel_Input", "Input");
	return Label;
}

int32 UMetasoundEditorGraphInput::GetSortOrderIndex() const
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	const UMetasoundEditorGraph* MetaSoundGraph = GetOwningGraph();
	FConstGraphHandle GraphHandle = MetaSoundGraph->GetGraphHandle();
	FConstNodeHandle NodeHandle = GetConstNodeHandle();
	const Metasound::FVertexName& NodeName = NodeHandle->GetNodeName();
	return GraphHandle->GetSortOrderIndexForInput(NodeName);
}

void UMetasoundEditorGraphInput::SetSortOrderIndex(int32 InSortOrderIndex)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	UMetasoundEditorGraph* MetaSoundGraph = GetOwningGraph();
	check(MetaSoundGraph);

	FGraphHandle GraphHandle = MetaSoundGraph->GetGraphHandle();
	FConstNodeHandle NodeHandle = GetConstNodeHandle();
	const Metasound::FVertexName& NodeName = NodeHandle->GetNodeName();

	GraphHandle->SetSortOrderIndexForInput(NodeName, InSortOrderIndex);
	MetaSoundGraph->SetSynchronizationRequired();
}

void UMetasoundEditorGraphInput::ResetToClassDefault()
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	UMetasoundEditorGraph* MetaSoundGraph = GetOwningGraph();
	FGraphHandle GraphHandle = MetaSoundGraph->GetGraphHandle();
	FConstNodeHandle NodeHandle = GraphHandle->GetNodeWithID(NodeID);

	FMetasoundFrontendLiteral DefaultLiteral;
	DefaultLiteral.SetFromLiteral(IDataTypeRegistry::Get().CreateDefaultLiteral(GetDataType()));

	Literal->Modify();
	Literal->SetFromLiteral(DefaultLiteral);

	const Metasound::FVertexName& NodeName = NodeHandle->GetNodeName();
	const FGuid VertexID = GraphHandle->GetVertexIDForInputVertex(NodeName);
	GraphHandle->SetDefaultInput(VertexID, DefaultLiteral);

	if (GEditor && MetaSoundGraph->IsPreviewing())
	{
		UAudioComponent* PreviewComponent = GEditor->GetPreviewAudioComponent();
		check(PreviewComponent);

		if (TScriptInterface<IAudioParameterControllerInterface> ParamInterface = PreviewComponent)
		{
			Literal->UpdatePreviewInstance(NodeName, ParamInterface);
		}
	}

	if (UMetasoundEditorGraph* Graph = GetOwningGraph())
	{
		Graph->SetSynchronizationRequired();
	}
}

void UMetasoundEditorGraphInput::SetMemberName(const FName& InNewName, bool bPostTransaction)
{
	// Renaming vertex members must stop the preview component to avoid confusion afterward
	// with newly named input not passing updated values to active previewed instance.
	if (UMetasoundEditorGraph* Graph = GetOwningGraph())
	{
		if (GEditor && Graph->IsPreviewing())
		{
			GEditor->ResetPreviewAudioComponent();
		}
	}

	Super::SetMemberName(InNewName, bPostTransaction);
}

void UMetasoundEditorGraphInput::UpdateFrontendDefaultLiteral(bool bPostTransaction)
{
	using namespace Metasound;
	using namespace Metasound::Frontend;

	UMetasoundEditorGraph* MetaSoundGraph = GetOwningGraph();
	UObject* Metasound = MetaSoundGraph->GetMetasound();
	if (!ensure(Metasound))
	{
		return;
	}

	if (!ensure(Literal))
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("Set Input Default", "Set MetaSound Input Default"), bPostTransaction);
	Metasound->Modify();

	FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(Metasound);
	check(MetasoundAsset);

	FGraphHandle GraphHandle = MetasoundAsset->GetRootGraphHandle();
	FNodeHandle NodeHandle = GraphHandle->GetNodeWithID(NodeID);

	const Metasound::FVertexName& NodeName = NodeHandle->GetNodeName();
	const FGuid VertexID = GraphHandle->GetVertexIDForInputVertex(NodeName);
	GraphHandle->SetDefaultInput(VertexID, Literal->GetDefault());

	if (GEditor && MetaSoundGraph->IsPreviewing())
	{
		UAudioComponent* PreviewComponent = GEditor->GetPreviewAudioComponent();
		check(PreviewComponent);

		if (TScriptInterface<IAudioParameterControllerInterface> ParamInterface = PreviewComponent)
		{
			Metasound::Frontend::FConstNodeHandle ConstNodeHandle = GetConstNodeHandle();
			Metasound::FVertexName VertexKey = NodeHandle->GetNodeName();
			Literal->UpdatePreviewInstance(VertexKey, ParamInterface);
		}
	}
}

Metasound::Frontend::FNodeHandle UMetasoundEditorGraphOutput::AddNodeHandle(const FName& InName, FName InDataType)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	UMetasoundEditorGraph* Graph = Cast<UMetasoundEditorGraph>(GetOuter());
	if (!ensure(Graph))
	{
		return UMetasoundEditorGraphVertex::AddNodeHandle(InName, InDataType);
	}

	UObject& Metasound = Graph->GetMetasoundChecked();
	FNodeHandle NewNodeHandle = FGraphBuilder::AddOutputNodeHandle(Metasound, InDataType, &InName);
	return NewNodeHandle;
}

int32 UMetasoundEditorGraphOutput::GetSortOrderIndex() const
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	const UMetasoundEditorGraph* MetaSoundGraph = GetOwningGraph();
	FConstGraphHandle GraphHandle = MetaSoundGraph->GetGraphHandle();
	FConstNodeHandle NodeHandle = GetConstNodeHandle();
	const Metasound::FVertexName& NodeName = NodeHandle->GetNodeName();
	return GraphHandle->GetSortOrderIndexForOutput(NodeName);
}

void UMetasoundEditorGraphOutput::SetSortOrderIndex(int32 InSortOrderIndex)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	UMetasoundEditorGraph* MetaSoundGraph = GetOwningGraph();
	check(MetaSoundGraph);

	FGraphHandle GraphHandle = MetaSoundGraph->GetGraphHandle();
	FConstNodeHandle NodeHandle = GetConstNodeHandle();
	const Metasound::FVertexName& NodeName = NodeHandle->GetNodeName();

	GraphHandle->SetSortOrderIndexForOutput(NodeName, InSortOrderIndex);
	MetaSoundGraph->SetSynchronizationRequired();
}

const FText& UMetasoundEditorGraphOutput::GetGraphMemberLabel() const
{
	static const FText Label = LOCTEXT("GraphMemberLabel_Output", "Output");
	return Label;
}

void UMetasoundEditorGraphOutput::ResetToClassDefault()
{
	using namespace Metasound;
	using namespace Metasound::Frontend;

	TArray<UMetasoundEditorGraphMemberNode*> Nodes = GetNodes();
	for (UMetasoundEditorGraphMemberNode* Node : Nodes)
	{
		TArray<FInputHandle> Inputs = Node->GetNodeHandle()->GetInputs();
		if (ensure(!Inputs.IsEmpty()))
		{
			Inputs.Last()->ClearLiteral();
		}
	}

	FMetasoundFrontendLiteral DefaultLiteral;
	DefaultLiteral.SetFromLiteral(IDataTypeRegistry::Get().CreateDefaultLiteral(GetDataType()));

	Literal->Modify();
	Literal->SetFromLiteral(DefaultLiteral);

	if (UMetasoundEditorGraph* Graph = GetOwningGraph())
	{
		Graph->SetSynchronizationRequired();
	}
}

void UMetasoundEditorGraphOutput::UpdateFrontendDefaultLiteral(bool bPostTransaction)
{
	using namespace Metasound;
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	UObject* Metasound = nullptr;
	UMetasoundEditorGraph* MetaSoundGraph = GetOwningGraph();
	if (ensure(MetaSoundGraph))
	{
		Metasound = MetaSoundGraph->GetMetasound();
	}

	if (!ensure(Metasound))
	{
		return;
	}

	if (!ensure(Literal))
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("Set Output Default", "Set MetaSound Output Default"), bPostTransaction);
	Metasound->Modify();

	const FMetasoundFrontendLiteral DefaultLiteral = Literal->GetDefault();

	TArray<UMetasoundEditorGraphMemberNode*> Nodes = GetNodes();
	for (UMetasoundEditorGraphMemberNode* Node : Nodes)
	{
		TArray<FInputHandle> Inputs = Node->GetNodeHandle()->GetInputs();
		if (ensure(!Inputs.IsEmpty()))
		{
			GraphPrivate::SetLiteralOrClearIfMatchesDefault(Inputs.Last(), DefaultLiteral);
		}
	}
}

Metasound::Editor::ENodeSection UMetasoundEditorGraphOutput::GetSectionID() const 
{
	return Metasound::Editor::ENodeSection::Outputs;
}

void UMetasoundEditorGraphVariable::InitMember(FName InDataType, const FMetasoundFrontendLiteral& InDefaultLiteral, FGuid InVariableID)
{
	TypeName = InDataType;
	VariableID = InVariableID;

	ConformLiteralDataType();

	if (ensure(Literal))
	{
		Literal->SetFromLiteral(InDefaultLiteral);
	}
}

const FText& UMetasoundEditorGraphVariable::GetGraphMemberLabel() const
{
	static const FText Label = LOCTEXT("GraphMemberLabel_Variable", "Variable");
	return Label;
}

Metasound::Frontend::FVariableHandle UMetasoundEditorGraphVariable::GetVariableHandle()
{
	using namespace Metasound;

	UObject* Object = CastChecked<UMetasoundEditorGraph>(GetOuter())->GetMetasound();
	if (!ensure(Object))
	{
		return Frontend::IVariableController::GetInvalidHandle();
	}

	FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(Object);
	check(MetasoundAsset);

	return MetasoundAsset->GetRootGraphHandle()->FindVariable(VariableID);
}

Metasound::Frontend::FConstVariableHandle UMetasoundEditorGraphVariable::GetConstVariableHandle() const
{
	using namespace Metasound;

	const UObject* Object = CastChecked<const UMetasoundEditorGraph>(GetOuter())->GetMetasound();
	if (!ensure(Object))
	{
		return Frontend::IVariableController::GetInvalidHandle();
	}

	const FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(Object);
	check(MetasoundAsset);

	return MetasoundAsset->GetRootGraphHandle()->FindVariable(VariableID);
}

void UMetasoundEditorGraphVariable::SetMemberName(const FName& InNewName, bool bPostTransaction)
{
	{
		const FText TransactionLabel = FText::Format(LOCTEXT("RenameGraphVariableMemberNameFormat", "Set Metasound {0} Name"), GetGraphMemberLabel());
		const FScopedTransaction Transaction(TransactionLabel, bPostTransaction);

		if (UMetasoundEditorGraph* Graph = GetOwningGraph())
		{
			Graph->Modify();
			Graph->GetMetasoundChecked().Modify();
		}

		GetVariableHandle()->SetName(InNewName);
	}

	NameChanged.Broadcast(VariableID);
}

FGuid UMetasoundEditorGraphVariable::GetMemberID() const 
{ 
	return VariableID;
}

FName UMetasoundEditorGraphVariable::GetMemberName() const
{
	return GetConstVariableHandle()->GetName();
}

Metasound::Editor::ENodeSection UMetasoundEditorGraphVariable::GetSectionID() const 
{ 
	return Metasound::Editor::ENodeSection::Variables;
}

FText UMetasoundEditorGraphVariable::GetDescription() const
{
	return GetConstVariableHandle()->GetDescription();
}

void UMetasoundEditorGraphVariable::SetDescription(const FText& InDescription, bool bPostTransaction)
{
	const FText TransactionLabel = FText::Format(LOCTEXT("SetGraphVariableTooltipFormat", "Set MetaSound {0}'s ToolTip"), GetGraphMemberLabel());
	const FScopedTransaction Transaction(TransactionLabel, bPostTransaction);

	if (UMetasoundEditorGraph* Graph = GetOwningGraph())
	{
		Graph->Modify();
		UObject& MetaSound = Graph->GetMetasoundChecked();
		MetaSound.Modify();

		GetVariableHandle()->SetDescription(InDescription);
	}
}

bool UMetasoundEditorGraphVariable::CanRename() const
{
	return true;
}

bool UMetasoundEditorGraphVariable::CanRename(const FText& InNewText, FText& OutError) const
{
	using namespace Metasound::Frontend;

	if (InNewText.IsEmptyOrWhitespace())
	{
		OutError = FText::Format(LOCTEXT("GraphVariableRenameInvalid_NameEmpty", "{0} cannot be empty string."), InNewText);
		return false;
	}
	
	const FName InNewName = FName(*InNewText.ToString());
	if (!InNewName.IsValid())
	{
		OutError = FText::Format(LOCTEXT("GraphVariableRenameInvalid_InvalidName", "{0} is an invalid name."), InNewText);
		return false;
	}

	FConstVariableHandle VariableHandle = GetConstVariableHandle();
	TArray<FConstVariableHandle> Variables = VariableHandle->GetOwningGraph()->GetVariables();
	for (const FConstVariableHandle& OtherVariable : Variables)
	{
		if (VariableID != OtherVariable->GetID())
		{
			if (InNewName == OtherVariable->GetName())
			{
				OutError = FText::Format(LOCTEXT("GraphVariableRenameInvalid_NameTaken", "{0} is already in use"), InNewText);
				return false;
			}
		}
	}

	return true;
}

TArray<UMetasoundEditorGraphMemberNode*> UMetasoundEditorGraphVariable::GetNodes() const
{
	TArray<UMetasoundEditorGraphMemberNode*> Nodes;

	FVariableEditorNodes EditorNodes = GetVariableNodes();
	if (nullptr != EditorNodes.MutatorNode)
	{
		Nodes.Add(EditorNodes.MutatorNode);
	}
	Nodes.Append(EditorNodes.AccessorNodes);
	Nodes.Append(EditorNodes.DeferredAccessorNodes);

	return Nodes;
}

FText UMetasoundEditorGraphVariable::GetDisplayName() const
{
	return Metasound::Editor::FGraphBuilder::GetDisplayName(*GetConstVariableHandle());
}

void UMetasoundEditorGraphVariable::SetDisplayName(const FText& InNewName, bool bPostTransaction)
{
	using namespace Metasound::Frontend;

	{
		const FText TransactionLabel = FText::Format(LOCTEXT("RenameGraphVariableDisplayNameFormat", "Set Metasound {0} DisplayName"), GetGraphMemberLabel());
		const FScopedTransaction Transaction(TransactionLabel, bPostTransaction);
		if (UMetasoundEditorGraph* Graph = GetOwningGraph())
		{
			Graph->Modify();
			Graph->GetMetasoundChecked().Modify();
		}

		FVariableHandle VariableHandle = GetVariableHandle();
		VariableHandle->SetDisplayName(InNewName);
	}

	NameChanged.Broadcast(VariableID);
}

void UMetasoundEditorGraphVariable::SetDataType(FName InNewType, bool bPostTransaction)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	if (InNewType == GetDataType())
	{
		return;
	}

	UMetasoundEditorGraph* Graph = GetOwningGraph();
	if (!ensure(Graph))
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("SetGraphVariableType", "Set MetaSound GraphVariable Type"), bPostTransaction);
	{
		Graph->GetMetasoundChecked().Modify();
		Graph->Modify();
		Modify();

		// Changing the data type requires that the variable and the associated nodes
		// be removed and readded. Before removing, cache required info to be set after
		// readding. It is assumed that connections are discarded because connections
		// require data types to be equal between to TO and FROM pin. 
		struct FCachedData
		{
			FName MemberName;
			FText DisplayName;
			FText Description;
			FVariableNodeLocations Locations;
		} CachedData;

		FConstVariableHandle OrigVariable = GetConstVariableHandle();

		// Cache variable metadata
		CachedData.MemberName = OrigVariable->GetName();
		CachedData.DisplayName = OrigVariable->GetDisplayName();
		CachedData.Description = OrigVariable->GetDescription();
		CachedData.Locations = GetVariableNodeLocations();


		// Remove the current variable
		Graph->RemoveMemberNodes(*this);
		FGraphHandle FrontendGraph = Graph->GetGraphHandle();
		FrontendGraph->RemoveVariable(VariableID);
		VariableID = FGuid();

		// Add variable with new type to frontend
		FVariableHandle FrontendVariable = FrontendGraph->AddVariable(InNewType);

		if (!ensure(FrontendVariable->IsValid()))
		{
			// Failed to add a new variable with the given data type. 
			return;
		}

		// Setup this object with new variable data
		VariableID = FrontendVariable->GetID();

		constexpr bool bPostSubTransaction = false;
		SetMemberName(CachedData.MemberName, bPostSubTransaction);
		SetDisplayName(CachedData.DisplayName, bPostSubTransaction);
		SetDescription(CachedData.Description, bPostSubTransaction);

		TypeName = InNewType;
		ConformLiteralDataType();

		// Add the nodes with the same identifier data but new datatype.
		UObject& Metasound = Graph->GetMetasoundChecked();
		AddVariableNodes(Metasound, FrontendGraph, CachedData.Locations);
	}

	// Notify now that the variable has a new ID (doing so before creating & syncing Frontend Node &
	// EdGraph variable can result in refreshing editors while in a desync'ed state)
	NameChanged.Broadcast(VariableID);
}

UMetasoundEditorGraphVariable::FVariableEditorNodes UMetasoundEditorGraphVariable::GetVariableNodes() const
{
	using namespace Metasound::Frontend;

	FVariableEditorNodes VariableNodes;
	TArray<UMetasoundEditorGraphMemberNode*> AllMetasoundNodes;

	const UMetasoundEditorGraph* Graph = GetOwningGraph();
	if (ensure(Graph))
	{
		Graph->GetNodesOfClassEx<UMetasoundEditorGraphMemberNode>(AllMetasoundNodes);
		FConstVariableHandle FrontendVariable = GetConstVariableHandle();

		// Find the mutator node if it exists.
		{
			FConstNodeHandle FrontendMutatorNode = FrontendVariable->FindMutatorNode();
			if (FrontendMutatorNode->IsValid())
			{
				const FGuid& MutatorNodeID = FrontendMutatorNode->GetID();
				auto IsNodeWithID = [&MutatorNodeID](const UMetasoundEditorGraphMemberNode* InNode)
				{
					return (nullptr != InNode) && (MutatorNodeID == InNode->GetNodeID());
				};

				if (UMetasoundEditorGraphMemberNode** FoundMutatorNode = AllMetasoundNodes.FindByPredicate(IsNodeWithID))
				{
					VariableNodes.MutatorNode = *FoundMutatorNode;
				}
			}
		}

		// Find all accessor nodes
		{
			TSet<FGuid> AccessorNodeIDs;
			for (const FConstNodeHandle& FrontendAccessorNode : FrontendVariable->FindAccessorNodes())
			{
				AccessorNodeIDs.Add(FrontendAccessorNode->GetID());
			}
			auto IsNodeInAccessorSet = [&AccessorNodeIDs](const UMetasoundEditorGraphMemberNode* InNode)
			{
				return (nullptr != InNode) && AccessorNodeIDs.Contains(InNode->GetNodeID());
			};
			VariableNodes.AccessorNodes = AllMetasoundNodes.FilterByPredicate(IsNodeInAccessorSet);
		}

		// Find all deferred accessor nodes
		{
			TSet<FGuid> DeferredAccessorNodeIDs;
			for (const FConstNodeHandle& FrontendAccessorNode : FrontendVariable->FindDeferredAccessorNodes())
			{
				DeferredAccessorNodeIDs.Add(FrontendAccessorNode->GetID());
			}
			auto IsNodeInDeferredAccessorSet = [&DeferredAccessorNodeIDs](const UMetasoundEditorGraphMemberNode* InNode)
			{
				return (nullptr != InNode) && DeferredAccessorNodeIDs.Contains(InNode->GetNodeID());
			};
			VariableNodes.DeferredAccessorNodes = AllMetasoundNodes.FilterByPredicate(IsNodeInDeferredAccessorSet);
		}
	}

	return VariableNodes;

}

UMetasoundEditorGraphVariable::FVariableNodeLocations UMetasoundEditorGraphVariable::GetVariableNodeLocations() const
{
	FVariableNodeLocations Locations;
	// Cache current node positions 
	FVariableEditorNodes EditorNodes = GetVariableNodes();
	auto GetNodeLocation = [](const UMetasoundEditorGraphMemberNode* InNode) { return FVector2D(InNode->NodePosX, InNode->NodePosY); };

	if (nullptr != EditorNodes.MutatorNode)
	{
		Locations.MutatorLocation = GetNodeLocation(EditorNodes.MutatorNode);
	}
	Algo::Transform(EditorNodes.AccessorNodes, Locations.AccessorLocations, GetNodeLocation);
	Algo::Transform(EditorNodes.DeferredAccessorNodes, Locations.DeferredAccessorLocations, GetNodeLocation);

	return Locations;
}

void UMetasoundEditorGraphVariable::AddVariableNodes(UObject& InMetasound, Metasound::Frontend::FGraphHandle& InFrontendGraph, const FVariableNodeLocations& InNodeLocs)
{
	using namespace Metasound::Frontend;
	using namespace Metasound::Editor;

	if (InNodeLocs.MutatorLocation)
	{
		bool bMutatorNodeAlreadyExists = GetConstVariableHandle()->FindMutatorNode()->IsValid();
		if (ensure(!bMutatorNodeAlreadyExists))
		{
			FNodeHandle MutatorFrontendNode = InFrontendGraph->FindOrAddVariableMutatorNode(VariableID);
			FGraphBuilder::AddNode(InMetasound, MutatorFrontendNode, *InNodeLocs.MutatorLocation, false /* bInSelectNewNode */);
		}
	}

	for (const FVector2D& Location : InNodeLocs.AccessorLocations)
	{
		FNodeHandle AccessorFrontendNode = InFrontendGraph->AddVariableAccessorNode(VariableID);
		FGraphBuilder::AddNode(InMetasound, AccessorFrontendNode, Location, false /* bInSelectNewNode */);
	}

	for (const FVector2D& Location : InNodeLocs.DeferredAccessorLocations)
	{
		FNodeHandle DeferredAccessorFrontendNode = InFrontendGraph->AddVariableDeferredAccessorNode(VariableID);
		FGraphBuilder::AddNode(InMetasound, DeferredAccessorFrontendNode, Location, false /* bInSelectNewNode */);
	}
}

const FGuid& UMetasoundEditorGraphVariable::GetVariableID() const
{
	return VariableID;
}

void UMetasoundEditorGraphVariable::ResetToClassDefault()
{
	using namespace Metasound;
	using namespace Metasound::Frontend;

	FMetasoundFrontendLiteral DefaultLiteral;
	DefaultLiteral.SetFromLiteral(IDataTypeRegistry::Get().CreateDefaultLiteral(GetDataType()));

	Literal->Modify();
	Literal->SetFromLiteral(DefaultLiteral);

	FVariableHandle VariableHandle = GetVariableHandle();
	VariableHandle->SetLiteral(DefaultLiteral);

	FNodeHandle MutatorNode = VariableHandle->FindMutatorNode();
	if (MutatorNode->IsValid())
	{
		FInputHandle InputHandle = MutatorNode->GetInputWithVertexName(VariableNames::GetInputDataName());
		if (ensure(InputHandle->IsValid()))
		{
			InputHandle->ClearLiteral();
		}
	}

	if (UMetasoundEditorGraph* Graph = GetOwningGraph())
	{
		Graph->SetSynchronizationRequired();
	}
}

void UMetasoundEditorGraphVariable::UpdateFrontendDefaultLiteral(bool bPostTransaction)
{
	using namespace Metasound;
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	UObject* Metasound = nullptr;
	UMetasoundEditorGraph* MetaSoundGraph = GetOwningGraph();
	if (ensure(MetaSoundGraph))
	{
		Metasound = MetaSoundGraph->GetMetasound();
	}

	if (!ensure(Metasound))
	{
		return;
	}

	if (!ensure(Literal))
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("Set Variable Default", "Set MetaSound Variable Default"), bPostTransaction);
	Metasound->Modify();

	const FMetasoundFrontendLiteral DefaultLiteral = Literal->GetDefault();
	FVariableHandle VariableHandle = GetVariableHandle();
	VariableHandle->SetLiteral(DefaultLiteral);

	FNodeHandle MutatorNode = VariableHandle->FindMutatorNode();
	if (MutatorNode->IsValid())
	{
		FInputHandle InputHandle = MutatorNode->GetInputWithVertexName(VariableNames::GetInputDataName());
		if (ensure(InputHandle->IsValid()))
		{
			GraphPrivate::SetLiteralOrClearIfMatchesDefault(InputHandle, DefaultLiteral);
		}
	}
}

UMetasoundEditorGraphInputNode* UMetasoundEditorGraph::CreateInputNode(Metasound::Frontend::FNodeHandle InNodeHandle, bool bInSelectNewNode)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	TArray<FConstOutputHandle> NodeOutputs = InNodeHandle->GetConstOutputs();
	if (!ensure(!NodeOutputs.IsEmpty()))
	{
		return nullptr;
	}

	if (!ensure(InNodeHandle->GetClassMetadata().GetType() == EMetasoundFrontendClassType::Input))
	{
		return nullptr;
	}

	UEdGraphNode* NewEdGraphNode = CreateNode(UMetasoundEditorGraphInputNode::StaticClass(), bInSelectNewNode);
	UMetasoundEditorGraphInputNode* NewInputNode = CastChecked<UMetasoundEditorGraphInputNode>(NewEdGraphNode);
	if (ensure(NewInputNode))
	{
		NewInputNode->CreateNewGuid();
		NewInputNode->PostPlacedNewNode();

		NewInputNode->Input = FindOrAddInput(InNodeHandle);

		if (NewInputNode->Pins.IsEmpty())
		{
			NewInputNode->AllocateDefaultPins();
		}

		NewInputNode->CacheTitle();

		return NewInputNode;
	}
	
	return nullptr;
}

Metasound::Frontend::FDocumentHandle UMetasoundEditorGraph::GetDocumentHandle()
{
	return GetGraphHandle()->GetOwningDocument();
}

Metasound::Frontend::FConstDocumentHandle UMetasoundEditorGraph::GetDocumentHandle() const
{
	return GetGraphHandle()->GetOwningDocument();
}

Metasound::Frontend::FGraphHandle UMetasoundEditorGraph::GetGraphHandle() 
{
	FMetasoundAssetBase* MetasoundAsset = Metasound::IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&GetMetasoundChecked());
	check(MetasoundAsset);

	return MetasoundAsset->GetRootGraphHandle();
}

Metasound::Frontend::FConstGraphHandle UMetasoundEditorGraph::GetGraphHandle() const
{
	const FMetasoundAssetBase* MetasoundAsset = Metasound::IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&GetMetasoundChecked());
	check(MetasoundAsset);

	return MetasoundAsset->GetRootGraphHandle();
}

void UMetasoundEditorGraph::PreSave(FObjectPreSaveContext InSaveContext)
{
	using namespace Metasound::Frontend;

	TArray<UMetasoundEditorGraphNode*> MetaSoundNodes;
	GetNodesOfClass<UMetasoundEditorGraphNode>(MetaSoundNodes);
	for (UMetasoundEditorGraphNode* Node : MetaSoundNodes)
	{
		FNodeHandle NodeHandle = Node->GetNodeHandle();
		FMetasoundFrontendNodeStyle Style = NodeHandle->GetNodeStyle();
		Style.bMessageNodeUpdated = false;
		NodeHandle->SetNodeStyle(Style);
	}

	Super::PreSave(InSaveContext);
}

UObject* UMetasoundEditorGraph::GetMetasound()
{
	return GetOuter();
}

const UObject* UMetasoundEditorGraph::GetMetasound() const
{
	return GetOuter();
}

UObject& UMetasoundEditorGraph::GetMetasoundChecked()
{
	UObject* ParentMetasound = GetMetasound();
	check(ParentMetasound);
	return *ParentMetasound;
}

const UObject& UMetasoundEditorGraph::GetMetasoundChecked() const
{
	const UObject* ParentMetasound = GetMetasound();
	check(ParentMetasound);
	return *ParentMetasound;
}

void UMetasoundEditorGraph::RegisterGraphWithFrontend()
{
	using namespace Metasound::Editor;

	if (UObject* ParentMetasound = GetOuter())
	{
		FGraphBuilder::RegisterGraphWithFrontend(*ParentMetasound);
	}
}

void UMetasoundEditorGraph::ClearVersionedOnLoad()
{
	bVersionedOnLoad = false;
}

bool UMetasoundEditorGraph::GetVersionedOnLoad() const
{
	return bVersionedOnLoad;
}

void UMetasoundEditorGraph::SetVersionedOnLoad()
{
	bVersionedOnLoad = true;
}

void UMetasoundEditorGraph::SetSynchronizationRequired()
{
	using namespace Metasound;

	if (UObject* ParentMetasound = GetOuter())
	{
		FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(ParentMetasound);
		check(MetasoundAsset);
		MetasoundAsset->SetSynchronizationRequired();
	}
}

UMetasoundEditorGraphInput* UMetasoundEditorGraph::FindInput(FGuid InNodeID) const
{
	const TObjectPtr<UMetasoundEditorGraphInput>* Input = Inputs.FindByPredicate([InNodeID](const TObjectPtr<UMetasoundEditorGraphInput>& InInput)
	{
		if (InInput)
		{
			return InInput->NodeID == InNodeID;
		}

		return false;
	});
	return Input ? Input->Get() : nullptr;
}

UMetasoundEditorGraphInput* UMetasoundEditorGraph::FindInput(FName InName) const
{
	const TObjectPtr<UMetasoundEditorGraphInput>* Input = Inputs.FindByPredicate([InName](const TObjectPtr<UMetasoundEditorGraphInput>& InInput)
	{
		if (InInput)
		{
			const FName NodeName = InInput->GetMemberName();
			return NodeName == InName;
		}

		return false;
	});
	return Input ? Input->Get() : nullptr;
}

UMetasoundEditorGraphInput* UMetasoundEditorGraph::FindOrAddInput(Metasound::Frontend::FNodeHandle InNodeHandle)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	FGraphHandle Graph = InNodeHandle->GetOwningGraph();

	FName TypeName;
	FGuid VertexID;

	ensure(InNodeHandle->GetNumInputs() == 1);
	InNodeHandle->IterateConstInputs([InGraph = &Graph, InTypeName = &TypeName, InVertexID = &VertexID](FConstInputHandle InputHandle)
	{
		*InTypeName = InputHandle->GetDataType();
		*InVertexID = (*InGraph)->GetVertexIDForInputVertex(InputHandle->GetName());
	});

	const FGuid NodeID = InNodeHandle->GetID();
	if (TObjectPtr<UMetasoundEditorGraphInput> Input = FindInput(NodeID))
	{
		ensure(Input->TypeName == TypeName);
		return Input;
	}

	UMetasoundEditorGraphInput* NewInput = NewObject<UMetasoundEditorGraphInput>(this, FName(), RF_Transactional);
	if (ensure(NewInput))
	{
		FMetasoundFrontendLiteral DefaultLiteral = Graph->GetDefaultInput(VertexID);
		FMetasoundFrontendClassName ClassName = InNodeHandle->GetClassMetadata().GetClassName();
		NewInput->InitMember(TypeName, DefaultLiteral, NodeID, MoveTemp(ClassName));
		Inputs.Add(NewInput);

		return NewInput;
	}

	return nullptr;
}

UMetasoundEditorGraphOutput* UMetasoundEditorGraph::FindOutput(FGuid InNodeID) const
{
	const TObjectPtr<UMetasoundEditorGraphOutput>* Output = Outputs.FindByPredicate([InNodeID](const TObjectPtr<UMetasoundEditorGraphOutput>& InOutput)
	{
		if (InOutput)
		{
			return InOutput->NodeID == InNodeID;
		}
		return false;
	});
	return Output ? Output->Get() : nullptr;
}

UMetasoundEditorGraphOutput* UMetasoundEditorGraph::FindOutput(FName InName) const
{
	const TObjectPtr<UMetasoundEditorGraphOutput>* Output = Outputs.FindByPredicate([&InName](const TObjectPtr<UMetasoundEditorGraphOutput>& InOutput)
	{
		if (InOutput)
		{
			return InName == InOutput->GetMemberName();
		}
		return false;
	});
	return Output ? Output->Get() : nullptr;
}

UMetasoundEditorGraphOutput* UMetasoundEditorGraph::FindOrAddOutput(Metasound::Frontend::FNodeHandle InNodeHandle)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	FGraphHandle Graph = InNodeHandle->GetOwningGraph();

	FName TypeName;
	FGuid VertexID;

	ensure(InNodeHandle->GetNumOutputs() == 1);
	InNodeHandle->IterateConstOutputs([InGraph = &Graph, InTypeName = &TypeName, InVertexID = &VertexID](FConstOutputHandle OutputHandle)
	{
		*InTypeName = OutputHandle->GetDataType();
		*InVertexID = (*InGraph)->GetVertexIDForInputVertex(OutputHandle->GetName());
	});

	const FGuid NodeID = InNodeHandle->GetID();
	if (TObjectPtr<UMetasoundEditorGraphOutput> Output = FindOutput(NodeID))
	{
		ensure(Output->TypeName == TypeName);
		return Output;
	}

	UMetasoundEditorGraphOutput* NewOutput = NewObject<UMetasoundEditorGraphOutput>(this, FName(), RF_Transactional);
	if (ensure(NewOutput))
	{
		FMetasoundFrontendLiteral DefaultLiteral;
		DefaultLiteral.SetFromLiteral(IDataTypeRegistry::Get().CreateDefaultLiteral(TypeName));

		FMetasoundFrontendClassName ClassName = InNodeHandle->GetClassMetadata().GetClassName();
		NewOutput->InitMember(TypeName, DefaultLiteral, NodeID, MoveTemp(ClassName));
		Outputs.Add(NewOutput);

		return NewOutput;
	}

	return nullptr;
}

UMetasoundEditorGraphVariable* UMetasoundEditorGraph::FindVariable(const FGuid& InVariableID) const
{
	const TObjectPtr<UMetasoundEditorGraphVariable>* Variable = Variables.FindByPredicate([&InVariableID](const TObjectPtr<UMetasoundEditorGraphVariable>& InVariable)
	{
		if (InVariable)
		{
			return InVariable->GetVariableID() == InVariableID;
		}

		return false;
	});
	return Variable ? Variable->Get() : nullptr;
}

UMetasoundEditorGraphVariable* UMetasoundEditorGraph::FindOrAddVariable(const Metasound::Frontend::FConstVariableHandle& InVariableHandle)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	FName TypeName = InVariableHandle->GetDataType();
	const FGuid VariableID = InVariableHandle->GetID();
	const FGuid NodeID = InVariableHandle->GetID();

	if (TObjectPtr<UMetasoundEditorGraphVariable> EditorVariable = FindVariable(VariableID))
	{
		ensure(EditorVariable->TypeName == TypeName);
		return EditorVariable;
	}

	UMetasoundEditorGraphVariable* NewVariable = NewObject<UMetasoundEditorGraphVariable>(this, FName(), RF_Transactional);
	if (ensure(NewVariable))
	{
		const FMetasoundFrontendLiteral DefaultLiteral = InVariableHandle->GetLiteral();
		NewVariable->InitMember(InVariableHandle->GetDataType(), DefaultLiteral, InVariableHandle->GetID());
		Variables.Add(NewVariable);
		return NewVariable;
	}

	return nullptr;
}

UMetasoundEditorGraphMember* UMetasoundEditorGraph::FindMember(FGuid InNodeID) const
{
	if (UMetasoundEditorGraphOutput* Output = FindOutput(InNodeID))
	{
		return Output;
	}

	if (UMetasoundEditorGraphInput* Input = FindInput(InNodeID))
	{
		return Input;
	}

	return FindVariable(InNodeID); // Note: ID is a VariableID in this case. 
}

UMetasoundEditorGraphMember* UMetasoundEditorGraph::FindAdjacentMember(const UMetasoundEditorGraphMember& InMember)
{
	int32 IndexInArray = Inputs.IndexOfByPredicate([&](const TObjectPtr<UMetasoundEditorGraphInput>& InputMember)
	{
		return &InMember == ToRawPtr(InputMember);
	});

	if (INDEX_NONE != IndexInArray)
	{
		if (IndexInArray < (Inputs.Num() - 1))
		{
			return Inputs[IndexInArray + 1];
		}
		else if (IndexInArray > 0)
		{
			return Inputs[IndexInArray - 1];
		}
		else
		{
			if (Outputs.Num() > 0)
			{
				return Outputs[0];
			}
		}

		return nullptr;
	}

	IndexInArray = Outputs.IndexOfByPredicate([&](const TObjectPtr<UMetasoundEditorGraphOutput>& OutputMember)
	{
		return &InMember == ToRawPtr(OutputMember);
	});

	if (INDEX_NONE != IndexInArray)
	{
		if (IndexInArray < (Outputs.Num() - 1))
		{
			return Outputs[IndexInArray + 1];
		}
		else if (IndexInArray > 0)
		{
			return Outputs[IndexInArray - 1];
		}
		else if (Inputs.Num() > 0)
		{
			return Inputs.Last();
		}

		return nullptr;
	}

	return nullptr;
}

bool UMetasoundEditorGraph::ContainsInput(const UMetasoundEditorGraphInput& InInput) const
{
	return Inputs.Contains(&InInput);
}

bool UMetasoundEditorGraph::ContainsOutput(const UMetasoundEditorGraphOutput& InOutput) const
{
	return Outputs.Contains(&InOutput);
}

bool UMetasoundEditorGraph::ContainsVariable(const UMetasoundEditorGraphVariable& InVariable) const
{
	return Variables.Contains(&InVariable);
}

void UMetasoundEditorGraph::IterateInputs(TUniqueFunction<void(UMetasoundEditorGraphInput&)> InFunction) const
{
	for (UMetasoundEditorGraphInput* Input : Inputs)
	{
		if (Input)
		{
			InFunction(*Input);
		}
	}
}

void UMetasoundEditorGraph::SetPreviewID(uint32 InPreviewID)
{
	PreviewID = InPreviewID;
}

bool UMetasoundEditorGraph::IsPreviewing() const
{
	if (GEditor)
	{
		UAudioComponent* PreviewComponent = GEditor->GetPreviewAudioComponent();
		if (!PreviewComponent)
		{
			return false;
		}

		if (!PreviewComponent->IsPlaying())
		{
			return false;
		}

		return PreviewComponent->GetUniqueID() == PreviewID;
	}

	return false;
}

bool UMetasoundEditorGraph::IsEditable() const
{
	return GetGraphHandle()->GetGraphStyle().bIsGraphEditable;
}

void UMetasoundEditorGraph::SetForceRefreshNodes()
{
	bForceRefreshNodes = true;
}

void UMetasoundEditorGraph::ClearForceRefreshNodes()
{
	bForceRefreshNodes = false;
}

bool UMetasoundEditorGraph::RequiresForceRefreshNodes() const
{
	return bForceRefreshNodes;
}

void UMetasoundEditorGraph::IterateOutputs(TUniqueFunction<void(UMetasoundEditorGraphOutput&)> InFunction) const
{
	for (UMetasoundEditorGraphOutput* Output : Outputs)
	{
		if (ensure(Output))
		{
			InFunction(*Output);
		}
	}
}

bool UMetasoundEditorGraph::ValidateInternal(Metasound::Editor::FGraphValidationResults& OutResults)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	bool bMarkDirty = false;
	bool bIsValid = true;

	OutResults = FGraphValidationResults();

	TArray<UMetasoundEditorGraphNode*> NodesToValidate;
	GetNodesOfClass<UMetasoundEditorGraphNode>(NodesToValidate);
	for (UMetasoundEditorGraphNode* Node : NodesToValidate)
	{
		FGraphNodeValidationResult NodeResult(*Node);

		bIsValid &= Node->Validate(NodeResult);
		bMarkDirty |= NodeResult.bIsDirty;

		OutResults.NodeResults.Add(NodeResult);
	}

	if (bMarkDirty)
	{
		MarkPackageDirty();
	}

	return bIsValid;
}

bool UMetasoundEditorGraph::RemoveMember(UMetasoundEditorGraphMember& InGraphMember)
{
	bool bSuccess = RemoveMemberNodes(InGraphMember);
	int32 NumRemoved = 0;
	if (UMetasoundEditorGraphInput* Input = Cast<UMetasoundEditorGraphInput>(&InGraphMember))
	{
		bSuccess = RemoveFrontendInput(*Input);
		NumRemoved = Inputs.Remove(Input);
	}
	else if (UMetasoundEditorGraphOutput* Output = Cast<UMetasoundEditorGraphOutput>(&InGraphMember))
	{
		bSuccess = RemoveFrontendOutput(*Output);
		NumRemoved = Outputs.Remove(Output);
	}
	else if (UMetasoundEditorGraphVariable* Variable = Cast<UMetasoundEditorGraphVariable>(&InGraphMember))
	{
		bSuccess = RemoveFrontendVariable(*Variable);
		NumRemoved = Variables.Remove(Variable);
	}
	else
	{
		bSuccess = false;
	}

	// Required to ensure member is removed from the editor views on next tick when refreshing details.
	if (NumRemoved > 0)
	{
		InGraphMember.MarkAsGarbage();
	}

	bSuccess = bSuccess && (NumRemoved > 0);

	return bSuccess;
}

bool UMetasoundEditorGraph::RemoveMemberNodes(UMetasoundEditorGraphMember& InGraphMember)
{
	bool bSuccess = true;
	for (UMetasoundEditorGraphNode* Node : InGraphMember.GetNodes())
	{
		if (ensure(Node))
		{
			bSuccess &= Metasound::Editor::FGraphBuilder::DeleteNode(*Node);
		}
	}
	return bSuccess;
}

bool UMetasoundEditorGraph::RemoveFrontendMember(UMetasoundEditorGraphMember& InMember)
{
	if (UMetasoundEditorGraphInput* Input = Cast<UMetasoundEditorGraphInput>(&InMember))
	{
		return RemoveFrontendInput(*Input);
	}
	else if (UMetasoundEditorGraphOutput* Output = Cast<UMetasoundEditorGraphOutput>(&InMember))
	{
		return RemoveFrontendOutput(*Output);
	}
	else if (UMetasoundEditorGraphVariable* Variable = Cast<UMetasoundEditorGraphVariable>(&InMember))
	{
		return RemoveFrontendVariable(*Variable);
	}

	return false;
}

bool UMetasoundEditorGraph::RemoveFrontendInput(UMetasoundEditorGraphInput& Input)
{
	using namespace Metasound::Frontend;

	FGraphHandle Graph = GetGraphHandle();
	FConstNodeHandle InputNode = Graph->GetNodeWithID(Input.NodeID);
	return Graph->RemoveInputVertex(InputNode->GetNodeName());
}

bool UMetasoundEditorGraph::RemoveFrontendOutput(UMetasoundEditorGraphOutput& Output)
{
	using namespace Metasound::Frontend;

	FGraphHandle Graph = GetGraphHandle();
	FConstNodeHandle OutputNode = Graph->GetNodeWithID(Output.NodeID);
	return Graph->RemoveOutputVertex(OutputNode->GetNodeName());
}

bool UMetasoundEditorGraph::RemoveFrontendVariable(UMetasoundEditorGraphVariable& Variable)
{
	FGuid VariableID = Variable.GetVariableID();

	// If the UMetasoundEditorGraphVariable is being deleted via an undo action, then the VariableID
	// will be invalid and the frontend variable will already have been cleaned up.
	if (VariableID.IsValid())
	{
		return GetGraphHandle()->RemoveVariable(VariableID);
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
