// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundEditorGraphConnectionDrawingPolicy.h"

#include "Misc/App.h"
#include "MetasoundEditorGraph.h"
#include "MetasoundEditorGraphSchema.h"
#include "Components/AudioComponent.h"

#include "Editor.h"

FConnectionDrawingPolicy* FMetasoundGraphConnectionDrawingPolicyFactory::CreateConnectionPolicy(const class UEdGraphSchema* Schema, int32 InBackLayerID, int32 InFrontLayerID, float ZoomFactor, const class FSlateRect& InClippingRect, class FSlateWindowElementList& InDrawElements, class UEdGraph* InGraphObj) const
{
	if (Schema->IsA(UMetasoundEditorGraphSchema::StaticClass()))
	{
		return new FMetasoundGraphConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, ZoomFactor, InClippingRect, InDrawElements, InGraphObj);
	}
	return nullptr;
}

FMetasoundGraphConnectionDrawingPolicy::FMetasoundGraphConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float ZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj)
	: FConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, ZoomFactor, InClippingRect, InDrawElements)
	, GraphObj(InGraphObj)
{
	// Cache off the editor options
	ActiveColor = Settings->TraceAttackColor;
	InactiveColor = Settings->TraceReleaseColor;

	ActiveWireThickness = Settings->TraceAttackWireThickness;
	InactiveWireThickness = Settings->TraceReleaseWireThickness;

	// Don't want to draw ending arrowheads
	ArrowImage = nullptr;
	ArrowRadius = FVector2D::ZeroVector;
}

void FMetasoundGraphConnectionDrawingPolicy::Draw(TMap<TSharedRef<SWidget>, FArrangedWidget>& InPinGeometries, FArrangedChildren& ArrangedNodes)
{
	// Draw everything
	FConnectionDrawingPolicy::Draw(InPinGeometries, ArrangedNodes);
}

// Give specific editor modes a chance to highlight this connection or darken non-interesting connections
void FMetasoundGraphConnectionDrawingPolicy::DetermineWiringStyle(UEdGraphPin* OutputPin, UEdGraphPin* InputPin, /*inout*/ FConnectionParams& Params)
{
	Params.AssociatedPin1 = OutputPin;
	Params.AssociatedPin2 = InputPin;

	// Get the schema and grab the default color from it
	check(OutputPin);
	check(GraphObj);
	const UEdGraphSchema* Schema = GraphObj->GetSchema();

	Params.WireColor = Schema->GetPinTypeColor(OutputPin->PinType);

	if (!InputPin)
	{
		return;
	}
	
	bool bExecuted = false;

	// Run thru the predecessors, and on
	if (FExecPairingMap* PredecessorMap = PredecessorNodes.Find(InputPin->GetOwningNode()))
	{
		if (FTimePair* Times = PredecessorMap->Find(OutputPin->GetOwningNode()))
		{
			bExecuted = true;

			Params.WireThickness = ActiveWireThickness;
			Params.WireColor = ActiveColor;

			Params.bDrawBubbles = true;
		}
	}

	if (!bExecuted)
	{
		// It's not followed, fade it and keep it thin
		Params.WireColor = InactiveColor;
		Params.WireThickness = InactiveWireThickness;
	}
}
