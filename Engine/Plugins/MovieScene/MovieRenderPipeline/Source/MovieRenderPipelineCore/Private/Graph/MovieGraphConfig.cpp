// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/MovieGraphConfig.h"

#include "Algo/Transform.h"
#include "Graph/MovieGraphEdge.h"
#include "Graph/Nodes/MovieGraphInputNode.h"
#include "Graph/Nodes/MovieGraphOutputNode.h"
#include "Graph/Nodes/MovieGraphVariableNode.h"
#include "MovieGraphUtils.h"
#include "MovieRenderPipelineCoreModule.h"

#define LOCTEXT_NAMESPACE "MovieGraphConfig"

static const FName MovieGraphGlobalsMemberName("Globals");

#if WITH_EDITOR
void UMovieGraphVariable::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	OnMovieGraphVariableChangedDelegate.Broadcast(this);
}
#endif // WITH_EDITOR

bool UMovieGraphInput::IsDeletable() const
{
	// The input is deletable as long as it's not the Globals input
	return Name != MovieGraphGlobalsMemberName;
}

#if WITH_EDITOR
void UMovieGraphInput::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	
	OnMovieGraphInputChangedDelegate.Broadcast(this);
}
#endif // WITH_EDITOR

bool UMovieGraphOutput::IsDeletable() const
{
	// The output is deletable as long as it's not the Globals output
	return Name != MovieGraphGlobalsMemberName;
}

#if WITH_EDITOR
void UMovieGraphOutput::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	OnMovieGraphOutputChangedDelegate.Broadcast(this);
}
#endif // WITH_EDITOR

FName UMovieGraphConfig::GlobalVariable_ShotName = "shot_name";
FName UMovieGraphConfig::GlobalVariable_SequenceName = "seq_name";
FName UMovieGraphConfig::GlobalVariable_FrameNumber = "frame_num";
FName UMovieGraphConfig::GlobalVariable_CameraName = "camera_name";
FName UMovieGraphConfig::GlobalVariable_RenderLayerName = "render_layer_name";

UMovieGraphConfig::UMovieGraphConfig()
{
	InputNode = CreateDefaultSubobject<UMovieGraphInputNode>(TEXT("DefaultInputNode"));
	OutputNode = CreateDefaultSubobject<UMovieGraphOutputNode>(TEXT("DefaultOutputNode"));

	// Don't add default members in the ctor if this object is being loaded (ie, it's not a new object). Defer that
	// until PostLoad(), otherwise the default members may be overwritten when properties are loaded.
	const bool bIsNewObject = !HasAnyFlags(RF_ClassDefaultObject | RF_NeedLoad | RF_NeedPostLoad);
	if (bIsNewObject)
	{
		AddDefaultMembers();
		InputNode->UpdatePins();
		OutputNode->UpdatePins();
	}
}

void UMovieGraphConfig::PostLoad()
{
	Super::PostLoad();

	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		// TODO: When the graph has stabilized, we can remove this and replace with a system that solely performs
		// upgrades/deprecations. For now, we assume that each load of the graph should re-initialize all default
		// members.
		AddDefaultMembers();
	}
}

void UMovieGraphConfig::TraverseGraphRecursive(UMovieGraphNode* InNode, TSubclassOf<UMovieGraphNode> InClassType, const FMovieGraphTraversalContext& InContext, TArray<UMovieGraphNode*>& OutNodes) const
{
	check(InNode);
	
	if (InNode->IsA(InClassType))
	{
		OutNodes.Add(InNode);
	}

	// Loop through the input pins on each node
	for (UMovieGraphPin* Pin : InNode->GetInputPins())
	{
		// ToDo: Pins should have a type (property, branch line, etc.) and we can mask following them.

		bool bFollowPath = true;
		// If this is an output node we mask their pins against the traversal context
		if (UMovieGraphOutputNode* NodeAsOutputNode = Cast<UMovieGraphOutputNode>(InNode))
		{
			if (InContext.RootBranch.BranchName != NAME_None)
			{
				bFollowPath = Pin->Properties.Label == InContext.RootBranch.BranchName;
				UE_LOG(LogMovieRenderPipeline, Log, TEXT("Traversing Graph with Root Branch: %s (bMatch: %d)"), *InContext.RootBranch.BranchName.ToString(), bFollowPath);
			}
		}

		if (bFollowPath)
		{
			for (UMovieGraphEdge* Edge : Pin->Edges)
			{
				if (UMovieGraphPin* OtherPin = Edge->GetOtherPin(Pin))
				{
					// ToDo: This needs to be upgraded to a full cyclic detection so we don't get stuck in an infinite
					// loop through some badly malformed graph created via scripting/etc.
					if (OtherPin->Node != InNode)
					{
						TraverseGraphRecursive(OtherPin->Node, InClassType, InContext, OutNodes);
					}
				}
			}
		}
	}

}

UMovieGraphVariable* UMovieGraphConfig::AddGlobalVariable(const FName& InName)
{
	// Don't add duplicate global variables
	const bool VariableExists = Variables.ContainsByPredicate([&InName](const TObjectPtr<UMovieGraphVariable>& Variable)
	{
		return Variable && (Variable->Name == InName);
	});

	if (VariableExists)
	{
		return nullptr;
	}
	
	if (UMovieGraphVariable* NewVariable = AddVariable(InName))
	{
		NewVariable->bIsGlobal = true;
		NewVariable->bIsEditable = false;
		return NewVariable;
	}

	return nullptr;
}

void UMovieGraphConfig::AddDefaultMembers()
{
	const bool InputGlobalsExists = Inputs.ContainsByPredicate([](const UMovieGraphMember* Member)
	{
		return Member && (Member->Name == MovieGraphGlobalsMemberName);
	});

	const bool OutputGlobalsExists = Outputs.ContainsByPredicate([](const UMovieGraphMember* Member)
	{
		return Member && (Member->Name == MovieGraphGlobalsMemberName);
	});

	// Ensure there is a Globals input member
	if (!InputGlobalsExists)
	{
		UMovieGraphInput* NewInput = AddInput();
		NewInput->Name = MovieGraphGlobalsMemberName.ToString();

		InputNode->UpdatePins();
	}

	// Ensure there is a Globals output member
	if (!OutputGlobalsExists)
	{
		UMovieGraphOutput* NewOutput = AddOutput();
		NewOutput->Name = MovieGraphGlobalsMemberName.ToString();

		OutputNode->UpdatePins();
	}

	// Add all of the global variables that should be available in the graph
	static const TArray<FName> GlobalVariableNames =
		{GlobalVariable_ShotName, GlobalVariable_SequenceName, GlobalVariable_FrameNumber,
		 GlobalVariable_CameraName, GlobalVariable_RenderLayerName};
	for (const FName& GlobalVariableName : GlobalVariableNames)
	{
		AddGlobalVariable(GlobalVariableName);
	}
}

TArray<UMovieGraphNode*> UMovieGraphConfig::TraverseGraph(TSubclassOf<UMovieGraphNode> InClassType, const FMovieGraphTraversalContext& InContext) const
{
	TArray<UMovieGraphNode*> OutNodes;

	if (OutputNode)
	{
		TraverseGraphRecursive(OutputNode, InClassType, InContext, /*Out*/OutNodes);
	}
	return OutNodes;
}

bool UMovieGraphConfig::AddLabeledEdge(UMovieGraphNode* FromNode, const FName& FromPinLabel, UMovieGraphNode* ToNode, const FName& ToPinLabel)
{
	if (!FromNode || !ToNode)
	{
		UE_LOG(LogMovieRenderPipeline, Error, TEXT("AddLabeledEdge: Invalid Edge Nodes"));
		return false;
	}

	UMovieGraphPin* FromPin = FromNode->GetOutputPin(FromPinLabel);
	if (!FromPin)
	{
		UE_LOG(LogMovieRenderPipeline, Error, TEXT("AddLabeledEdge: FromNode: %s does not have a pin with the label: %s"), *FromNode->GetName(), *FromPinLabel.ToString());
		return false;
	}

	UMovieGraphPin* ToPin = ToNode->GetInputPin(ToPinLabel);
	if (!ToPin)
	{
		UE_LOG(LogMovieRenderPipeline, Error, TEXT("AddLabeledEdge: ToNode: %s does not have a pin with the label: %s"), *ToNode->GetName(), *ToPinLabel.ToString());
		return false;
	}

	// Add the edge
	FromPin->AddEdgeTo(ToPin);
	bool bConnectionBrokeOtherEdges = false;
//	if (!ToPin->AllowMultipleConnections())
//	{
//		bConnectionBrokeOtherEdges = ToPin->BreakAllIncompatibleEdges();
//	}
//
//#if WITH_EDITOR
//	NotifyGraphChanged(EMoviePipelineGraphChangeType::Structural);
//#endif

	return bConnectionBrokeOtherEdges;
}

bool UMovieGraphConfig::RemoveEdge(UMovieGraphNode* FromNode, const FName& FromPinLabel, UMovieGraphNode* ToNode, const FName& ToPinLabel)
{
	if (!FromNode || !ToNode)
	{
		UE_LOG(LogMovieRenderPipeline, Error, TEXT("RemoveEdge: Invalid Edge Nodes"));
		return false;
	}

	UMovieGraphPin* FromPin = FromNode->GetOutputPin(FromPinLabel);
	if (!FromPin)
	{
		UE_LOG(LogMovieRenderPipeline, Error, TEXT("RemoveEdge: FromNode: %s does not have a pin with the label: %s"), *FromNode->GetName(), *FromPinLabel.ToString());
		return false;
	}

	UMovieGraphPin* ToPin = ToNode->GetInputPin(ToPinLabel);
	if (!ToPin)
	{
		UE_LOG(LogMovieRenderPipeline, Error, TEXT("RemoveEdge: ToNode: %s does not have a pin with the label: %s"), *ToNode->GetName(), *ToPinLabel.ToString());
		return false;
	}

	bool bChanged = ToPin->BreakEdgeTo(FromPin);

//#if WITH_EDITOR
// 	   if(bChanged) {
//	NotifyGraphChanged(EMoviePipelineGraphChangeType::Structural);
// 	   }
//#endif

	return bChanged;
}

bool UMovieGraphConfig::RemoveAllInboundEdges(UMovieGraphNode* InNode)
{
	if (!InNode)
	{
		UE_LOG(LogMovieRenderPipeline, Error, TEXT("RemoveAllInboundEdges: Invalid Edge Nodes"));
		return false;
	}

	bool bChanged = false;
	for (UMovieGraphPin* InputPin : InNode->InputPins)
	{
		bChanged |= InputPin->BreakAllEdges();
	}

	//#if WITH_EDITOR
	// 	   if(bChanged) {
	//	NotifyGraphChanged(EMoviePipelineGraphChangeType::Structural);
	// 	   }
	//#endif

	return bChanged;
}

bool UMovieGraphConfig::RemoveAllOutboundEdges(UMovieGraphNode* InNode)
{
	if (!InNode)
	{
		UE_LOG(LogMovieRenderPipeline, Error, TEXT("RemoveAllOutboundEdges: Invalid Edge Nodes"));
		return false;
	}

	bool bChanged = false;
	for (UMovieGraphPin* OutputPin : InNode->OutputPins)
	{
		bChanged |= OutputPin->BreakAllEdges();
	}

	//#if WITH_EDITOR
	// 	   if(bChanged) {
	//	NotifyGraphChanged(EMoviePipelineGraphChangeType::Structural);
	// 	   }
	//#endif

	return bChanged;
}

bool UMovieGraphConfig::RemoveInboundEdges(UMovieGraphNode* InNode, const FName& InPinName)
{
	if (!InNode)
	{
		UE_LOG(LogMovieRenderPipeline, Error, TEXT("RemoveInboundEdges: Invalid Edge Nodes"));
		return false;
	}

	bool bChanged = false;
	if(UMovieGraphPin* InputPin = InNode->GetInputPin(InPinName))
	{
		bChanged |= InputPin->BreakAllEdges();
	}

	//#if WITH_EDITOR
	// 	   if(bChanged) {
	//	NotifyGraphChanged(EMoviePipelineGraphChangeType::Structural);
	// 	   }
	//#endif

	return bChanged;
}

bool UMovieGraphConfig::RemoveOutboundEdges(UMovieGraphNode* InNode, const FName& InPinName)
{
	if (!InNode)
	{
		UE_LOG(LogMovieRenderPipeline, Error, TEXT("RemoveOutboundEdges: Invalid Edge Nodes"));
		return false;
	}

	bool bChanged = false;
	if (UMovieGraphPin* OutputPin = InNode->GetOutputPin(InPinName))
	{
		bChanged |= OutputPin->BreakAllEdges();
	}

	//#if WITH_EDITOR
	// 	   if(bChanged) {
	//	NotifyGraphChanged(EMoviePipelineGraphChangeType::Structural);
	// 	   }
	//#endif

	return bChanged;
}

bool UMovieGraphConfig::RemoveNodes(TArray<UMovieGraphNode*> InNodes)
{
	bool bChanged = false;
	for (UMovieGraphNode* Node : InNodes)
	{
		bChanged |= RemoveNode(Node);
	}
	return bChanged;
}

bool UMovieGraphConfig::RemoveNode(UMovieGraphNode* InNode)
{
	if (!InNode)
	{
		UE_LOG(LogMovieRenderPipeline, Error, TEXT("RemoveNode: Invalid Node"));
		return false;
	}

	RemoveAllInboundEdges(InNode);
	RemoveAllOutboundEdges(InNode);

#if WITH_EDITOR
	TArray<UMovieGraphNode*> RemovedNodes;
	RemovedNodes.Add(InNode);
	OnGraphNodesDeletedDelegate.Broadcast(RemovedNodes);
#endif

	return AllNodes.RemoveSingle(InNode) == 1;
}

template<typename T>
T* UMovieGraphConfig::AddMember(TArray<TObjectPtr<T>>& InMemberArray, const FName& InBaseName)
{
	static_assert(std::is_base_of_v<UMovieGraphMember, T>, "T is not derived from UMovieGraphMember");
	
	using namespace UE::MoviePipeline::RenderGraph;

	// TODO: This can be replaced with just CreateDefaultSubobject() when AddDefaultMembers() isn't called from PostLoad()
	//
	// This method will be called in two cases: 1) when default members are being added to a new graph when it is being
	// initially created or loaded via PostLoad(), or 2) a member is being added to the graph by the user. For case 1,
	// when the constructor is running, RF_NeedInitialization will be set. CreateDefaultSubobject() needs to be called
	// in this scenario instead of NewObject().
	const bool bIsNewObject = HasAnyFlags(RF_NeedInitialization);
	T* NewMember = bIsNewObject
		? CreateDefaultSubobject<T>(MakeUniqueObjectName(this, T::StaticClass()))
		: NewObject<T>(this, NAME_None);
	
	if (!NewMember)
	{
		UE_LOG(LogMovieRenderPipeline, Warning, TEXT("Unable to create new member object in the graph."));
		return nullptr;
	}

	InMemberArray.Add(NewMember);
	NewMember->SetFlags(RF_Transactional);
	NewMember->Type = EMovieGraphMemberType::Float;
	NewMember->SetGuid(FGuid::NewGuid());

	// Generate and set a unique name
	TArray<FString> ExistingMemberNames;
	Algo::Transform(InMemberArray, ExistingMemberNames, [](const T* Member) { return Member->Name; });
	NewMember->Name = GetUniqueName(ExistingMemberNames, InBaseName.ToString());

	return NewMember;
}

UMovieGraphVariable* UMovieGraphConfig::AddVariable(const FName InCustomBaseName)
{
	static const FText VariableBaseName = LOCTEXT("VariableBaseName", "Variable");
	
	UMovieGraphVariable* NewVariable = AddMember(
		Variables, !InCustomBaseName.IsNone() ? InCustomBaseName : FName(*VariableBaseName.ToString()));

#if WITH_EDITOR
	OnGraphVariablesChangedDelegate.Broadcast();
#endif

	return NewVariable;
}

UMovieGraphInput* UMovieGraphConfig::AddInput()
{
	static const FText InputBaseName = LOCTEXT("InputBaseName", "Input");

	UMovieGraphInput* NewInput = AddMember(Inputs, FName(*InputBaseName.ToString()));
	InputNode->UpdatePins();
	
#if WITH_EDITOR
	OnGraphInputAddedDelegate.Broadcast(NewInput);
#endif

	return NewInput;
}

UMovieGraphOutput* UMovieGraphConfig::AddOutput()
{
	static const FText OutputBaseName = LOCTEXT("OutputBaseName", "Output");
	
	UMovieGraphOutput* NewOutput = AddMember(Outputs, FName(*OutputBaseName.ToString()));
	OutputNode->UpdatePins();

#if WITH_EDITOR
	OnGraphOutputAddedDelegate.Broadcast(NewOutput);
#endif

	return NewOutput;
}

UMovieGraphVariable* UMovieGraphConfig::GetVariableByGuid(const FGuid& InGuid) const
{
	for (const TObjectPtr<UMovieGraphVariable> Variable : Variables)
	{
		if (Variable->GetGuid() == InGuid)
		{
			return Variable;
		}
	}

	return nullptr;
}

TArray<UMovieGraphVariable*> UMovieGraphConfig::GetVariables(const bool bIncludeGlobal) const
{
	if (bIncludeGlobal)
	{
		return Variables;
	}

	return Variables.FilterByPredicate([](const UMovieGraphVariable* Var) { return Var && !Var->IsGlobal(); });
}

TArray<UMovieGraphInput*> UMovieGraphConfig::GetInputs() const
{
	return Inputs;
}

TArray<UMovieGraphOutput*> UMovieGraphConfig::GetOutputs() const
{
	return Outputs;
}

bool UMovieGraphConfig::DeleteMember(UMovieGraphMember* MemberToDelete)
{
	if (!MemberToDelete)
	{
		return false;
	}

	if (!MemberToDelete->IsDeletable())
	{
		UE_LOG(LogMovieRenderPipeline, Error, TEXT("DeleteMember: The member '%s' cannot be deleted because it is flagged as non-deletable."), *MemberToDelete->Name);
		return false;
	}

	if (UMovieGraphVariable* GraphVariableToDelete = Cast<UMovieGraphVariable>(MemberToDelete))
	{
		return DeleteVariableMember(GraphVariableToDelete);
	}

	if (UMovieGraphInput* GraphInputToDelete = Cast<UMovieGraphInput>(MemberToDelete))
	{
		return DeleteInputMember(GraphInputToDelete);
	}

	if (UMovieGraphOutput* GraphOutputToDelete = Cast<UMovieGraphOutput>(MemberToDelete))
	{
		return DeleteOutputMember(GraphOutputToDelete);
	}

	return false;
}

bool UMovieGraphConfig::DeleteVariableMember(UMovieGraphVariable* VariableMemberToDelete)
{
	if (!VariableMemberToDelete)
	{
		return false;
	}
	
	// Find all accessor nodes using this graph variable
	TArray<TObjectPtr<UMovieGraphNode>> NodesToRemove =
		AllNodes.FilterByPredicate([VariableMemberToDelete](const TObjectPtr<UMovieGraphNode>& GraphNode)
		{
			if (const UMovieGraphVariableNode* VariableNode = Cast<UMovieGraphVariableNode>(GraphNode))
			{
				const UMovieGraphVariable* GraphVariable = VariableNode->GetVariable();
				if (GraphVariable && (GraphVariable->GetGuid() == VariableMemberToDelete->GetGuid()))
				{
					return true;
				}
			}

			return false;
		});

	// Remove accessor nodes (which broadcasts our node changed delegates)
	TArray<UMovieGraphNode*> RemovedNodes;
	for (const TObjectPtr<UMovieGraphNode>& NodeToRemove : NodesToRemove)
	{
		if (RemoveNode(NodeToRemove.Get()))
		{
			RemovedNodes.Add(NodeToRemove.Get());
		}
	}

	// Remove this variable from the variables tracked by the graph
	Variables.RemoveSingle(VariableMemberToDelete);

#if WITH_EDITOR
	OnGraphVariablesChangedDelegate.Broadcast();
#endif

	return true;
}

TArray<FMovieGraphBranch> UMovieGraphConfig::GetOutputBranches() const
{
	TArray<FMovieGraphBranch> Branches;
	if (OutputNode)
	{
		for (UMovieGraphPin* Pin : OutputNode->GetInputPins())
		{
			FMovieGraphBranch& NewBranch = Branches.AddDefaulted_GetRef();
			NewBranch.BranchName = Pin->Properties.Label;
		}
	}

	return Branches;
}

#if WITH_EDITOR
void UMovieGraphConfig::SetEditorOnlyNodes(const TArray<TObjectPtr<const UObject>>& InNodes)
{
	EditorOnlyNodes.Empty();

	for (const TObjectPtr<const UObject>& Node : InNodes)
	{
		EditorOnlyNodes.Add(DuplicateObject(Node.Get(), this));
	}
}
#endif	// WITH_EDITOR

bool UMovieGraphConfig::DeleteInputMember(UMovieGraphInput* InputMemberToDelete)
{
	if (InputMemberToDelete)
	{
		Inputs.RemoveSingle(InputMemberToDelete);
		RemoveOutboundEdges(InputNode, FName(InputMemberToDelete->Name));

		// This calls OnNodeChangedDelegate to update the graph
		InputNode->UpdatePins();

		return true;
	}

	return false;
}

bool UMovieGraphConfig::DeleteOutputMember(UMovieGraphOutput* OutputMemberToDelete)
{
	if (OutputMemberToDelete)
	{
		Outputs.RemoveSingle(OutputMemberToDelete);
		RemoveInboundEdges(OutputNode, FName(OutputMemberToDelete->Name));

		// This calls OnNodeChangedDelegate to update the graph
		OutputNode->UpdatePins();

		return true;
	}

	return false;
}

#undef LOCTEXT_NAMESPACE
