// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovieGraphSchema.h"
#include "Graph/MovieGraphConfig.h"
#include "Graph/MovieGraphNode.h"
#include "Graph/MovieEdGraph.h"
#include "Graph/MovieEdGraphConnectionPolicy.h"
#include "Graph/MovieEdGraphNode.h"
#include "UObject/Class.h"
#include "UObject/UObjectIterator.h"
#include "ScopedTransaction.h"
#include "GraphEditor.h"
#include "MovieEdGraphVariableNode.h"

TArray<UClass*> UMovieGraphSchema::MoviePipelineNodeClasses;

#define LOCTEXT_NAMESPACE "MoviePipelineGraphSchema"

const FName UMovieGraphSchema::PC_Branch(TEXT("branch"));
const FName UMovieGraphSchema::PC_Float(TEXT("float"));
const FName UMovieGraphSchema::PC_Integer(TEXT("integer"));
const FName UMovieGraphSchema::PC_Boolean(TEXT("boolean"));
const FName UMovieGraphSchema::PC_String(TEXT("string"));
const FName UMovieGraphSchema::PC_IntPoint(TEXT("intpoint"));

void UMovieGraphSchema::CreateDefaultNodesForGraph(UEdGraph& Graph) const
{
	/*UMovieGraphConfig* RuntimeGraph = CastChecked<UMoviePipelineEdGraph>(&Graph)->GetPipelineGraph();
	const FScopedTransaction Transaction(LOCTEXT("GraphEditor_NewNode", "Create Pipeline Graph Node."));
	RuntimeGraph->Modify();
	const bool bSelectNewNode = false;

	// Input Node
	{
		UMovieGraphNode* RuntimeNode = RuntimeGraph->ConstructRuntimeNode<UMovieGraphInputNode>();

		// Now create the editor graph node
		FGraphNodeCreator<UMoviePipelineEdGraphNodeInput> NodeCreator(Graph);
		UMoviePipelineEdGraphNodeBase* GraphNode = NodeCreator.CreateUserInvokedNode(bSelectNewNode);
		GraphNode->SetRuntimeNode(RuntimeNode);
		NodeCreator.Finalize();
	}

	// Output Node
	{
		UMovieGraphNode* RuntimeNode = RuntimeGraph->ConstructRuntimeNode<UMovieGraphOutputNode>();

		// Now create the editor graph node
		FGraphNodeCreator<UMoviePipelineEdGraphNodeOutput> NodeCreator(Graph);
		UMoviePipelineEdGraphNodeBase* GraphNode = NodeCreator.CreateUserInvokedNode(bSelectNewNode);
		GraphNode->SetRuntimeNode(RuntimeNode);
		NodeCreator.Finalize();
	}*/
}

void UMovieGraphSchema::InitMoviePipelineNodeClasses()
{
	if (MoviePipelineNodeClasses.Num() > 0)
	{
		return;
	}

	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (It->IsChildOf(UMovieGraphNode::StaticClass())
			&& !It->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			MoviePipelineNodeClasses.Add(*It);
		}
	}

	MoviePipelineNodeClasses.Sort();
}

void UMovieGraphSchema::GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const
{
	InitMoviePipelineNodeClasses();

	const UMoviePipelineEdGraph* Graph = Cast<UMoviePipelineEdGraph>(ContextMenuBuilder.CurrentGraph);
	if (!Graph)
	{
		return;
	}

	const UMovieGraphConfig* RuntimeGraph = Graph->GetPipelineGraph();
	if (!RuntimeGraph)
	{
		return;
	}

	for (UClass* PipelineNodeClass : MoviePipelineNodeClasses)
	{
		const UMovieGraphNode* PipelineNode = PipelineNodeClass->GetDefaultObject<UMovieGraphNode>();
		if (PipelineNodeClass == UMovieGraphVariableNode::StaticClass())
		{
			// Add variable actions separately
			continue;
		}
		if(PipelineNodeClass == UMovieGraphInputNode::StaticClass() ||
			PipelineNodeClass == UMovieGraphOutputNode::StaticClass())
		{
			// Can't place Input and Output nodes manually.
			continue;
		}

		// This can be used to sort whether or not an option shows up. For now there's no restrictions
		// on where nodes can be made, but eventually we might check which branch they're on (if from pin)
		// to filter out incompatible nodes.
		// if (!ContextMenuBuilder.FromPin || ContextMenuBuilder.FromPin->Direction == EGPD_Input)
		{
			const FText Name = PipelineNode->GetMenuDescription();
			const FText Category = PipelineNode->GetMenuCategory();
			const FText Tooltip = LOCTEXT("CreateNode_Tooltip", "Create a node of this type.");
			
			TSharedPtr<FMovieGraphSchemaAction> NewAction = MakeShared<FMovieGraphSchemaAction_NewNode>(Category, Name, Tooltip); 
			NewAction->NodeClass = PipelineNodeClass;

			ContextMenuBuilder.AddAction(NewAction);
		}
	}

	// Create an accessor node action for each variable the graph has
	for (const UMovieGraphVariable* Variable : RuntimeGraph->GetVariables())
	{
		const FText Name = FText::FromString(Variable->Name);
		const FText Category = LOCTEXT("CreateVariable_Category", "Variables");
		const FText Tooltip = LOCTEXT("CreateVariable_Tooltip", "Create an accessor node for this variable.");
		
		TSharedPtr<FMovieGraphSchemaAction> NewAction = MakeShared<FMovieGraphSchemaAction_NewVariableNode>(Category, Name, Variable->GetGuid(), Tooltip);
		NewAction->NodeClass = UMovieGraphVariableNode::StaticClass();
		
		ContextMenuBuilder.AddAction(NewAction);
	}
}

const FPinConnectionResponse UMovieGraphSchema::CanCreateConnection(const UEdGraphPin* PinA, const UEdGraphPin* PinB) const
{
	// No Circular Connections
	if (PinA->GetOwningNode() == PinB->GetOwningNode())
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("MoviePipeline", "CircularPinError", "No Circular Connections!"));
	}

	// Pins need to be the same type
	if (PinA->PinType.PinCategory != PinB->PinType.PinCategory)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NSLOCTEXT("MoviePipeline", "PinTypeMismatchError", "Pin types don't match!"));
	}
	
	// Make sure the pins are not on the same node
	return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, NSLOCTEXT("MoviePipeline", "PinConnect", "Connect nodes"));
}

bool UMovieGraphSchema::TryCreateConnection(UEdGraphPin* InA, UEdGraphPin* InB) const
{
	// See if the native UEdGraph connection goes through
	const bool bModified = Super::TryCreateConnection(InA, InB);

	// If it does, try to propagate the change to our runtime graph
	if (bModified)
	{
		check(InA && InB);
		const UEdGraphPin* A = (InA->Direction == EGPD_Output) ? InA : InB;
		const UEdGraphPin* B = (InA->Direction == EGPD_Input) ? InA : InB;
		check(A->Direction == EGPD_Output && B->Direction == EGPD_Input);

		UMoviePipelineEdGraphNodeBase* EdGraphNodeA = CastChecked<UMoviePipelineEdGraphNodeBase>(A->GetOwningNode());
		UMoviePipelineEdGraphNodeBase* EdGraphNodeB = CastChecked<UMoviePipelineEdGraphNodeBase>(B->GetOwningNode());

		UMovieGraphNode* RuntimeNodeA = EdGraphNodeA->GetRuntimeNode();
		UMovieGraphNode* RuntimeNodeB = EdGraphNodeB->GetRuntimeNode();
		check(RuntimeNodeA && RuntimeNodeB);

		UMovieGraphConfig* RuntimeGraph = RuntimeNodeA->GetGraph();
		check(RuntimeGraph);

		const bool bReconstructNodeB = RuntimeGraph->AddLabeledEdge(RuntimeNodeA, A->PinName, RuntimeNodeB, B->PinName);
		//if (bReconstructNodeB)
		//{
		//	RuntimeNodeB->ReconstructNode();
		//}
	}

	return bModified;
}

void UMovieGraphSchema::BreakPinLinks(UEdGraphPin& TargetPin, bool bSendsNodeNotification) const
{
	const FScopedTransaction Transaction(LOCTEXT("MoviePipelineGraphEditor_BreakPinLinks", "Break Pin Links"));
	Super::BreakPinLinks(TargetPin, bSendsNodeNotification);

	UEdGraphNode* GraphNode = TargetPin.GetOwningNode();
	UMoviePipelineEdGraphNodeBase* MoviePipelineEdGraphNode = CastChecked<UMoviePipelineEdGraphNodeBase>(GraphNode);

	UMovieGraphNode* RuntimeNode = MoviePipelineEdGraphNode->GetRuntimeNode();
	check(RuntimeNode);

	UMovieGraphConfig* RuntimeGraph = RuntimeNode->GetGraph();
	check(RuntimeGraph);

	if (TargetPin.Direction == EEdGraphPinDirection::EGPD_Input)
	{
		RuntimeGraph->RemoveInboundEdges(RuntimeNode, TargetPin.PinName);
	}
	else if (TargetPin.Direction == EEdGraphPinDirection::EGPD_Output)
	{
		RuntimeGraph->RemoveOutboundEdges(RuntimeNode, TargetPin.PinName);
	}
}

void UMovieGraphSchema::BreakSinglePinLink(UEdGraphPin* SourcePin, UEdGraphPin* TargetPin) const
{
	const FScopedTransaction Transaction(LOCTEXT("MoviePipelineGraphEditor_BreakSinglePinLinks", "Break Single Pin Link"));
	Super::BreakSinglePinLink(SourcePin, TargetPin);

	UEdGraphNode* SourceGraphNode = SourcePin->GetOwningNode();
	UEdGraphNode* TargetGraphNode = TargetPin->GetOwningNode();

	UMoviePipelineEdGraphNodeBase* SourcePipelineGraphNode = CastChecked<UMoviePipelineEdGraphNodeBase>(SourceGraphNode);
	UMoviePipelineEdGraphNodeBase* TargetPipelineGraphNode = CastChecked<UMoviePipelineEdGraphNodeBase>(TargetGraphNode);

	UMovieGraphNode* SourceRuntimeNode = SourcePipelineGraphNode->GetRuntimeNode();
	UMovieGraphNode* TargetRuntimeNode = TargetPipelineGraphNode->GetRuntimeNode();
	check(SourceRuntimeNode && TargetRuntimeNode);

	UMovieGraphConfig* RuntimeGraph = SourceRuntimeNode->GetGraph();
	check(RuntimeGraph);

	RuntimeGraph->RemoveEdge(SourceRuntimeNode, SourcePin->PinName, TargetRuntimeNode, TargetPin->PinName);
}

FLinearColor UMovieGraphSchema::GetPinTypeColor(const FEdGraphPinType& PinType) const
{
	const UGraphEditorSettings* Settings = GetDefault<UGraphEditorSettings>();

	if (PinType.PinCategory == PC_Branch)
	{
		return Settings->ExecutionPinTypeColor;
	}

	if (PinType.PinCategory == PC_Float)
	{
		return Settings->FloatPinTypeColor;
	}

	if (PinType.PinCategory == PC_Integer)
	{
		return Settings->IntPinTypeColor;
	}

	if (PinType.PinCategory == PC_Boolean)
	{
		return Settings->BooleanPinTypeColor;
	}

	if (PinType.PinCategory == PC_String)
	{
		return Settings->StringPinTypeColor;
	}

	if (PinType.PinCategory == PC_IntPoint)
	{
		return Settings->VectorPinTypeColor;
	}
	
	return Settings->DefaultPinTypeColor;
}

FConnectionDrawingPolicy* UMovieGraphSchema::CreateConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID,
	float InZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements,
	UEdGraph* InGraphObj) const
{
	return new FMovieEdGraphConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements, InGraphObj);
}

FMovieGraphSchemaAction_NewNode::FMovieGraphSchemaAction_NewNode(FText InNodeCategory, FText InDisplayName, FText InToolTip)
	: FMovieGraphSchemaAction(MoveTemp(InNodeCategory), MoveTemp(InDisplayName), MoveTemp(InToolTip), 0)
{
	
}

UEdGraphNode* FMovieGraphSchemaAction_NewNode::PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode)
{
	UMovieGraphConfig* RuntimeGraph = CastChecked<UMoviePipelineEdGraph>(ParentGraph)->GetPipelineGraph();
	const FScopedTransaction Transaction(LOCTEXT("GraphEditor_NewNode", "Create Pipeline Graph Node."));
	RuntimeGraph->Modify();

	UMovieGraphNode* RuntimeNode = RuntimeGraph->ConstructRuntimeNode<UMovieGraphNode>(NodeClass);

	// Now create the editor graph node
	FGraphNodeCreator<UMoviePipelineEdGraphNode> NodeCreator(*ParentGraph);
	UMoviePipelineEdGraphNode* GraphNode = NodeCreator.CreateUserInvokedNode(bSelectNewNode);
	GraphNode->Construct(RuntimeNode);
	GraphNode->NodePosX = Location.X;
	GraphNode->NodePosY = Location.Y;


	// Finalize generates a guid, calls a post-place callback, and allocates default pins if needed
	NodeCreator.Finalize();

	if (FromPin)
	{
		GraphNode->AutowireNewNode(FromPin);
	}
	return GraphNode;
}

FMovieGraphSchemaAction_NewVariableNode::FMovieGraphSchemaAction_NewVariableNode(FText InNodeCategory, FText InDisplayName, FGuid InVariableGuid, FText InToolTip)
	: FMovieGraphSchemaAction(MoveTemp(InNodeCategory), MoveTemp(InDisplayName), MoveTemp(InToolTip), 0)
	, VariableGuid(InVariableGuid)
{
	
}

UEdGraphNode* FMovieGraphSchemaAction_NewVariableNode::PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode)
{
	UMovieGraphConfig* RuntimeGraph = CastChecked<UMoviePipelineEdGraph>(ParentGraph)->GetPipelineGraph();
	const FScopedTransaction Transaction(LOCTEXT("GraphEditor_NewVariableNode", "Add New Variable Accessor Node"));
	RuntimeGraph->Modify();

	UMovieGraphNode* RuntimeNode = RuntimeGraph->ConstructRuntimeNode<UMovieGraphNode>(NodeClass);
	if (UMovieGraphVariableNode* VariableNode = Cast<UMovieGraphVariableNode>(RuntimeNode))
	{
		VariableNode->SetVariable(RuntimeGraph->GetVariableByGuid(VariableGuid));
	}

	// Now create the variable node
	FGraphNodeCreator<UMoviePipelineEdGraphVariableNode> NodeCreator(*ParentGraph);
	UMoviePipelineEdGraphVariableNode* GraphNode = NodeCreator.CreateNode(bSelectNewNode);
	GraphNode->Construct(RuntimeNode);
	GraphNode->NodePosX = Location.X;
	GraphNode->NodePosY = Location.Y;
	
	// Finalize generates a guid, calls a post-place callback, and allocates default pins if needed
	NodeCreator.Finalize();

	if (FromPin)
	{
		GraphNode->AutowireNewNode(FromPin);
	}


	return GraphNode;
}

#undef LOCTEXT_NAMESPACE // "MoviePipelineGraphSchema"
