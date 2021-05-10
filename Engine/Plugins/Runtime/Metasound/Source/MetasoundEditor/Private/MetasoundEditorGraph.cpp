// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundEditorGraph.h"

#include "Audio/AudioParameterInterface.h"
#include "Components/AudioComponent.h"
#include "EdGraph/EdGraphNode.h"
#include "Interfaces/ITargetPlatform.h"
#include "MetasoundEditorGraphBuilder.h"
#include "MetasoundEditorGraphInputNodes.h"
#include "MetasoundEditorGraphNode.h"
#include "MetasoundEditorGraphValidation.h"
#include "MetasoundEditorModule.h"
#include "MetasoundUObjectRegistry.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "MetaSoundEditor"


TArray<UMetasoundEditorGraphNode*> UMetasoundEditorGraphVariable::GetNodes() const
{
	TArray<UMetasoundEditorGraphNode*> Nodes;

	UMetasoundEditorGraph* Graph = Cast<UMetasoundEditorGraph>(GetOuter());
	if (ensure(Graph))
	{
		Graph->GetNodesOfClassEx<UMetasoundEditorGraphNode>(Nodes);
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

void UMetasoundEditorGraphVariable::SetDisplayName(const FText& InNewName)
{
	using namespace Metasound::Frontend;

	const FText TransactionLabel = FText::Format(LOCTEXT("Rename Variable", "Rename Metasound {0}"), GetVariableLabel());
	const FScopedTransaction Transaction(TransactionLabel);

	Modify();
	if (UMetasoundEditorGraph* Graph = Cast<UMetasoundEditorGraph>(GetOuter()))
	{
		Graph->GetMetasoundChecked().Modify();
	}

	FNodeHandle NodeHandle = GetNodeHandle();
	NodeHandle->SetDisplayName(InNewName);

	NameChanged.Broadcast(NodeID);
}

void UMetasoundEditorGraphVariable::SetDataType(FName InNewType)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	UMetasoundEditorGraph* Graph = Cast<UMetasoundEditorGraph>(GetOuter());
	if (!ensure(Graph))
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("SetVariableDataType", "Set MetaSound Variable Type"));
	Graph->GetMetasoundChecked().Modify();
	Graph->Modify();

	// 1. Cache current editor input node reference positions & delete nodes.
	TArray<UMetasoundEditorGraphNode*> InputNodes = GetNodes();
	TArray<FVector2D> NodeLocations;
	for (UMetasoundEditorGraphNode* Node : InputNodes)
	{
		if (ensure(Node))
		{
			NodeLocations.Add(FVector2D(Node->NodePosX, Node->NodePosY));
		}
	}

	// 2. Cache the old version's Frontend data.
	FNodeHandle NodeHandle = GetNodeHandle();
	const FString NodeName = NodeHandle->GetNodeName();
	const FText NodeDisplayName = NodeHandle->GetDisplayName();

	// 3. Delete the Frontend variable
	FGraphBuilder::DeleteVariableNodeHandle(*this);

	// 4. Add the new input node with the same identifier data but new datatype.
	UObject& Metasound = Graph->GetMetasoundChecked();
	FNodeHandle NewNodeHandle = AddNodeHandle(NodeName, NodeDisplayName, InNewType);
	if (!ensure(NewNodeHandle->IsValid()))
	{
		return;
	}

	ClassName = NewNodeHandle->GetClassMetadata().ClassName;
	NodeID = NewNodeHandle->GetID();
	TypeName = InNewType;

	// 5. Report data type changed immediately after assignment to child
	// class(es) so underlying data can be fixed-up prior to recreating
	// referencing nodes.
	OnDataTypeChanged();

	// 6. Create new node references in the same locations as the old locations
	for (FVector2D Location : NodeLocations)
	{
		FGraphBuilder::AddNode(Metasound, NewNodeHandle, Location, false /* bInSelectNewNode */);
	}

	// Notify now that the node has a new ID (doing so before creating & syncing Frontend Node &
	// EdGraph variable can result in refreshing editors while in a desync'ed state)
	NameChanged.Broadcast(NodeID);
}

Metasound::Frontend::FNodeHandle UMetasoundEditorGraphVariable::GetNodeHandle() const
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

Metasound::Frontend::FConstNodeHandle UMetasoundEditorGraphVariable::GetConstNodeHandle() const
{
	return GetNodeHandle();
}

bool UMetasoundEditorGraphVariable::CanRename(const FText& InNewName, FText& OutError) const
{
	using namespace Metasound::Frontend;

	if (InNewName.IsEmpty())
	{
		OutError = FText::Format(LOCTEXT("VariableRenameInvalid_NameEmpty", "{0} cannot be empty string."), InNewName);
		return false;
	}

	FConstNodeHandle NodeHandle = GetConstNodeHandle();
	FConstGraphHandle GraphHandle = NodeHandle->GetOwningGraph();

	bool bIsNameValid = true;
	GraphHandle->IterateConstNodes([&](FConstNodeHandle NodeToCompare)
	{
		if (NodeID != NodeToCompare->GetID())
		{
			if (InNewName.CompareToCaseIgnored(NodeToCompare->GetDisplayName()) == 0)
			{
				bIsNameValid = false;
				OutError = FText::Format(LOCTEXT("VariableRenameInvalid_NameTaken", "{0} is already in use"), InNewName);
			}
		}
	}, GetClassType());

		return bIsNameValid;
}

#if WITH_EDITORONLY_DATA
void UMetasoundEditorGraphInputLiteral::PostEditUndo()
{
	Super::PostEditUndo();

	if (UMetasoundEditorGraphInput* Input = Cast<UMetasoundEditorGraphInput>(GetOuter()))
	{
		Input->OnLiteralChanged(false /* bPostTransaction */);
	}
}
#endif // WITH_EDITORONLY_DATA

void UMetasoundEditorGraphInput::UpdateDocumentInput(bool bPostTransaction)
{
	using namespace Metasound;
	using namespace Metasound::Frontend;

	UMetasoundEditorGraph* MetasoundGraph = CastChecked<UMetasoundEditorGraph>(GetOuter());
	UObject* Metasound = MetasoundGraph->GetMetasound();
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
	const FString& NodeName = NodeHandle->GetNodeName();

	const FGuid VertexID = GraphHandle->GetVertexIDForInputVertex(NodeName);
	GraphHandle->SetDefaultInput(VertexID, Literal->GetDefault());
}

void UMetasoundEditorGraphInput::UpdatePreviewInstance(const Metasound::FVertexKey& InParameterName, TScriptInterface<IAudioParameterInterface>& InParamInterface) const
{
	if (ensure(Literal))
	{
		Literal->UpdatePreviewInstance(InParameterName, InParamInterface);
	}
}

Metasound::Frontend::FNodeHandle UMetasoundEditorGraphInput::AddNodeHandle(const FString& InNodeName, const FText& InNodeDisplayName, FName InDataType)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	UMetasoundEditorGraph* Graph = Cast<UMetasoundEditorGraph>(GetOuter());
	if (!ensure(Graph))
	{
		return Super::AddNodeHandle(InNodeName, InNodeDisplayName, InDataType);
	}

	UObject& Metasound = Graph->GetMetasoundChecked();
	Metasound::Frontend::FNodeHandle NewNodeHandle = FGraphBuilder::AddInputNodeHandle(Metasound, InNodeName, InDataType, FText::GetEmpty());
	NewNodeHandle->SetDisplayName(InNodeDisplayName);
	return NewNodeHandle;
}

const FText& UMetasoundEditorGraphInput::GetVariableLabel() const
{
	static const FText Label = LOCTEXT("VariableLabel_Input", "Input");
	return Label;
}

#if WITH_EDITORONLY_DATA
void UMetasoundEditorGraphInput::PostEditUndo()
{
	Super::PostEditUndo();

	if (!Literal)
	{
		if (UMetasoundEditorGraph* MetasoundGraph = Cast<UMetasoundEditorGraph>(GetOuter()))
		{
			MetasoundGraph->RemoveVariable(*CastChecked<UMetasoundEditorGraphVariable>(this));
		}
		return;
	}

	OnLiteralChanged(false /* bPostTransaction */);
}
#endif // WITH_EDITORONLY_DATA

void UMetasoundEditorGraphInput::OnLiteralChanged(bool bPostTransaction)
{
	UpdateDocumentInput(bPostTransaction);

	const bool bIsPreviewing = CastChecked<UMetasoundEditorGraph>(GetOuter())->IsPreviewing();
	if (bIsPreviewing)
	{
		UAudioComponent* PreviewComponent = GEditor->GetPreviewAudioComponent();
		check(PreviewComponent);

		if (TScriptInterface<IAudioParameterInterface> ParamInterface = PreviewComponent->GetParameterInterface())
		{
			// TODO: fix how identifying the parameter to update is determined. It should not be done
			// with a "DisplayName" but rather the vertex Guid.
			Metasound::Frontend::FConstNodeHandle NodeHandle = GetConstNodeHandle();
			Metasound::FVertexKey VertexKey = Metasound::FVertexKey(NodeHandle->GetDisplayName().ToString());
			UpdatePreviewInstance(VertexKey, ParamInterface);
		}
	}
}

void UMetasoundEditorGraphInput::OnDataTypeChanged()
{
	using namespace Metasound;
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetaSoundEditor");
	const FEditorDataType& EditorDataType = EditorModule.FindDataType(TypeName);
	const EMetasoundFrontendLiteralType LiteralType = static_cast<EMetasoundFrontendLiteralType>(EditorDataType.RegistryInfo.PreferredLiteralType);

	TSubclassOf<UMetasoundEditorGraphInputLiteral> InputLiteralClass = EditorModule.FindInputLiteralClass(LiteralType);
	if (!InputLiteralClass)
	{
		InputLiteralClass = UMetasoundEditorGraphInputLiteral::StaticClass();
	}
	Literal = NewObject<UMetasoundEditorGraphInputLiteral>(this, InputLiteralClass, FName(), RF_Transactional);
}

Metasound::Frontend::FNodeHandle UMetasoundEditorGraphOutput::AddNodeHandle(const FString& InNodeName, const FText& InNodeDisplayName, FName InDataType)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	UMetasoundEditorGraph* Graph = Cast<UMetasoundEditorGraph>(GetOuter());
	if (!ensure(Graph))
	{
		return Super::AddNodeHandle(InNodeName, InNodeDisplayName, InDataType);
	}

	UObject& Metasound = Graph->GetMetasoundChecked();
	Metasound::Frontend::FNodeHandle NewNodeHandle = FGraphBuilder::AddOutputNodeHandle(Metasound, InNodeName, InDataType, FText::GetEmpty());
	NewNodeHandle->SetDisplayName(InNodeDisplayName);
	return NewNodeHandle;
}

const FText& UMetasoundEditorGraphOutput::GetVariableLabel() const
{
	static const FText Label = LOCTEXT("VariableLabel_Output", "Output");
	return Label;
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

		return NewInputNode;
	}
	
	return nullptr;	
}

Metasound::Frontend::FDocumentHandle UMetasoundEditorGraph::GetDocumentHandle() const
{
	return GetGraphHandle()->GetOwningDocument();
}

Metasound::Frontend::FGraphHandle UMetasoundEditorGraph::GetGraphHandle() const
{
	FMetasoundAssetBase* MetasoundAsset = Metasound::IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&GetMetasoundChecked());
	check(MetasoundAsset);

	return MetasoundAsset->GetRootGraphHandle();
}

UObject* UMetasoundEditorGraph::GetMetasound() const
{
	return GetOuter();
}

UObject& UMetasoundEditorGraph::GetMetasoundChecked() const
{
	UObject* ParentMetasound = GetOuter();
	check(ParentMetasound);
	return *ParentMetasound;
}

void UMetasoundEditorGraph::Synchronize()
{
	if (UObject* ParentMetasound = GetOuter())
	{
		Metasound::Editor::FGraphBuilder::SynchronizeGraph(*ParentMetasound);
	}
}

UMetasoundEditorGraphInput* UMetasoundEditorGraph::FindInput(FGuid InNodeID) const
{
	const TObjectPtr<UMetasoundEditorGraphInput>* Input = Inputs.FindByPredicate([InNodeID](const TObjectPtr<UMetasoundEditorGraphInput>& Input)
	{
		return Input->NodeID == InNodeID;
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

	if (UMetasoundEditorGraphInput* NewInput = NewObject<UMetasoundEditorGraphInput>(this, FName(), RF_Transactional))
	{
		NewInput->NodeID = NodeID;
		NewInput->ClassName = InNodeHandle->GetClassMetadata().ClassName;
		NewInput->TypeName = TypeName;

		FMetasoundFrontendLiteral DefaultLiteral = Graph->GetDefaultInput(VertexID);
		EMetasoundFrontendLiteralType LiteralType = DefaultLiteral.GetType();
		IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetaSoundEditor");
		TSubclassOf<UMetasoundEditorGraphInputLiteral> InputLiteralClass = EditorModule.FindInputLiteralClass(LiteralType);

		NewInput->Literal = NewObject<UMetasoundEditorGraphInputLiteral>(NewInput, InputLiteralClass, FName(), RF_Transactional);
		NewInput->Literal->SetFromLiteral(DefaultLiteral);

		Inputs.Add(NewInput);
		return NewInput;
	}

	checkNoEntry();
	return nullptr;
}

UMetasoundEditorGraphOutput* UMetasoundEditorGraph::FindOutput(FGuid InNodeID) const
{
	const TObjectPtr<UMetasoundEditorGraphOutput>* Output = Outputs.FindByPredicate([InNodeID](const TObjectPtr<UMetasoundEditorGraphOutput>& Output)
	{
		return Output->NodeID == InNodeID;
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

	if (UMetasoundEditorGraphOutput* NewOutput = NewObject<UMetasoundEditorGraphOutput>(this, FName(), RF_Transactional))
	{
		NewOutput->NodeID = NodeID;
		NewOutput->ClassName = InNodeHandle->GetClassMetadata().ClassName;
		NewOutput->TypeName = TypeName;
		Outputs.Add(NewOutput);
		return NewOutput;
	}

	checkNoEntry();
	return nullptr;
}

UMetasoundEditorGraphVariable* UMetasoundEditorGraph::FindVariable(FGuid InNodeID) const
{
	if (UMetasoundEditorGraphOutput* Output = FindOutput(InNodeID))
	{
		return Output;
	}

	return FindInput(InNodeID);
}

bool UMetasoundEditorGraph::ContainsInput(UMetasoundEditorGraphInput* InInput) const
{
	return Inputs.Contains(InInput);
}

bool UMetasoundEditorGraph::ContainsOutput(UMetasoundEditorGraphOutput* InOutput) const
{
	return Outputs.Contains(InOutput);
}

void UMetasoundEditorGraph::IterateInputs(TUniqueFunction<void(UMetasoundEditorGraphInput&)> InFunction) const
{
	for (UMetasoundEditorGraphInput* Input : Inputs)
	{
		if (ensure(Input))
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
	UAudioComponent* PreviewComponent = GEditor->GetPreviewAudioComponent();
	if (!PreviewComponent)
	{
		return false;
	}

	if (!PreviewComponent->IsPlaying())
	{
		return false;
	}

	UObject* ParamInterfaceObject = PreviewComponent->GetParameterInterface().GetObject();
	if (!ParamInterfaceObject)
	{
		return false;
	}

	return ParamInterfaceObject->GetUniqueID() == PreviewID;
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

bool UMetasoundEditorGraph::RemoveVariable(UMetasoundEditorGraphVariable& InVariable)
{
	if (UMetasoundEditorGraphInput* Input = Cast<UMetasoundEditorGraphInput>(&InVariable))
	{
		return Inputs.Remove(Input) > 0;
	}

	if (UMetasoundEditorGraphOutput* Output = Cast<UMetasoundEditorGraphOutput>(&InVariable))
	{
		return Outputs.Remove(Output) > 0;
	}

	return false;
}

bool UMetasoundEditorGraph::Validate(Metasound::Editor::FGraphValidationResults& OutResults)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	bool bMarkDirty = false;
	bool bIsValid = false;

	OutResults = FGraphValidationResults();

	TArray<UMetasoundEditorGraphExternalNode*> ExternalNodes;
	GetNodesOfClass<UMetasoundEditorGraphExternalNode>(ExternalNodes);
	for (UMetasoundEditorGraphExternalNode* ExternalNode : ExternalNodes)
	{
		FGraphNodeValidationResult NodeResult(*ExternalNode);
		bIsValid |= ExternalNode->Validate(NodeResult);
		bMarkDirty |= NodeResult.bIsDirty;
		OutResults.NodeResults.Add(NodeResult);
	}

	if (bMarkDirty)
	{
		MarkPackageDirty();
	}

	return bIsValid;
}
#undef LOCTEXT_NAMESPACE
