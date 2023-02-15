// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/MovieGraphConfig.h"
#include "Graph/MovieGraphEdge.h"
#include "MovieRenderPipelineCoreModule.h"


UMovieGraphConfig::UMovieGraphConfig()
{
	InputNode = CreateDefaultSubobject<UMovieGraphInputNode>(TEXT("DefaultInputNode"));
	OutputNode = CreateDefaultSubobject<UMovieGraphOutputNode>(TEXT("DefaultOutputNode"));
	
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		InputNode->UpdatePins();
		OutputNode->UpdatePins();
	}
}

void UMovieGraphConfig::TraverseTest(UMovieGraphNode* InNode)
{
	UE_LOG(LogTemp, Warning, TEXT("Visiting Node: %s"), *InNode->GetFName().ToString());

	for (UMovieGraphPin* Pin : InNode->InputPins)
	{
		for (UMovieGraphEdge* Edge : Pin->Edges)
		{
			if (UMovieGraphPin* OtherPin = Edge->GetOtherPin(Pin))
			{
				if (OtherPin->Node != InNode)
				{
					TraverseTest(OtherPin->Node);
				}
			}
		}
	}
}

UMovieGraphNode* UMovieGraphConfig::FindTraversalStartForContext(const FMovieGraphTraversalContext& InContext) const
{
	return OutputNode;
}

void UMovieGraphConfig::TraversalTest()
{
	FMovieGraphTraversalContext Context;
	Context.ShotName = TEXT("Test");
	Context.RenderLayerName = TEXT("Test2");

	UMovieGraphNode* StartNode = FindTraversalStartForContext(Context);
	if (StartNode)
	{
		UE_LOG(LogTemp, Warning, TEXT("Traversing!"));
		TraverseTest(StartNode);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Could not find traversal entry point for context"));
	}
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

