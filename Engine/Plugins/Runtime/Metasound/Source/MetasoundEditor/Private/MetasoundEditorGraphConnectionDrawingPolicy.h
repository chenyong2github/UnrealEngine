// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Layout/ArrangedWidget.h"
#include "Widgets/SWidget.h"
#include "EdGraphUtilities.h"
#include "ConnectionDrawingPolicy.h"

class FSlateRect;
class FSlateWindowElementList;
class UEdGraph;
class UEdGraphSchema;

struct FMetasoundGraphConnectionDrawingPolicyFactory : public FGraphPanelPinConnectionFactory
{
public:
	virtual ~FMetasoundGraphConnectionDrawingPolicyFactory() = default;

	// FGraphPanelPinConnectionFactory
	virtual class FConnectionDrawingPolicy* CreateConnectionPolicy(const UEdGraphSchema* Schema, int32 InBackLayerID, int32 InFrontLayerID, float ZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, class UEdGraph* InGraphObj) const override;
	// ~FGraphPanelPinConnectionFactory
};


// This class draws the connections for an UEdGraph using a Metasound schema
class FMetasoundGraphConnectionDrawingPolicy : public FConnectionDrawingPolicy
{
protected:
	// Times for one execution pair within the current graph
	struct FTimePair
	{
		double PredExecTime;
		double ThisExecTime;

		FTimePair()
			: PredExecTime(0.0)
			, ThisExecTime(0.0)
		{
		}
	};

	// Map of pairings
	using FExecPairingMap = TMap<UEdGraphNode*, FTimePair>;

	// Map of nodes that preceded before a given node in the execution sequence (one entry for each pairing)
	TMap<UEdGraphNode*, FExecPairingMap> PredecessorNodes;

	UEdGraph* GraphObj;

	FLinearColor ActiveColor;
	FLinearColor InactiveColor;

	float ActiveWireThickness;
	float InactiveWireThickness;

public:
	FMetasoundGraphConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float ZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj);

	// FConnectionDrawingPolicy interface
	virtual void DetermineWiringStyle(UEdGraphPin* OutputPin, UEdGraphPin* InputPin, /*inout*/ FConnectionParams& Params) override;
	virtual void Draw(TMap<TSharedRef<SWidget>, FArrangedWidget>& PinGeometries, FArrangedChildren& ArrangedNodes) override;
	// End of FConnectionDrawingPolicy interface
};
