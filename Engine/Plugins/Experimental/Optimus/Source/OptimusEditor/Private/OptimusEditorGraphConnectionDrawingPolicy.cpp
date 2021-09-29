// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusEditorGraphConnectionDrawingPolicy.h"
#include "EdGraph/EdGraph.h"

FOptimusEditorGraphConnectionDrawingPolicy::FOptimusEditorGraphConnectionDrawingPolicy(
	int32 InBackLayerID,
	int32 InFrontLayerID,
	float ZoomFactor,
	const FSlateRect& InClippingRect,
	FSlateWindowElementList& InDrawElements,
	UEdGraph* InGraphObj
	) :
	FConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, ZoomFactor, InClippingRect, InDrawElements),
	Graph(InGraphObj)
{
}


void FOptimusEditorGraphConnectionDrawingPolicy::DetermineWiringStyle(
	UEdGraphPin* OutputPin,
	UEdGraphPin* InputPin,
	FConnectionParams& Params
	)
{
	FConnectionDrawingPolicy::DetermineWiringStyle(OutputPin, InputPin, Params);
	
	if (ensure(Graph))
	{
		const UEdGraphSchema* Schema = Graph->GetSchema();

		Params.WireColor = Schema->GetPinTypeColor(OutputPin->PinType);
	}
}
