// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Layout/ArrangedWidget.h"
#include "Widgets/SWidget.h"
#include "EdGraphUtilities.h"
#include "ConnectionDrawingPolicy.h"


// Forward Declarations
class FSlateRect;
class FSlateWindowElementList;
class UEdGraph;
class UEdGraphSchema;

namespace Metasound
{
	namespace Editor
	{
		struct FGraphConnectionDrawingPolicyFactory : public FGraphPanelPinConnectionFactory
		{
		public:
			virtual ~FGraphConnectionDrawingPolicyFactory() = default;

			// FGraphPanelPinConnectionFactory
			virtual class FConnectionDrawingPolicy* CreateConnectionPolicy(
				const UEdGraphSchema* Schema,
				int32 InBackLayerID,
				int32 InFrontLayerID,
				float ZoomFactor,
				const FSlateRect& InClippingRect,
				FSlateWindowElementList& InDrawElements,
				UEdGraph* InGraphObj) const override;
			// ~FGraphPanelPinConnectionFactory
		};

		// This class draws the connections for an UEdGraph using a SoundCue schema
		class FGraphConnectionDrawingPolicy : public FConnectionDrawingPolicy
		{
		protected:
			// Times for one execution pair within the current graph
			struct FTimePair
			{
				double PredExecTime = 0.0;
				double ThisExecTime = 0.0;
			};

			// Map of pairings
			using FExecPairingMap = TMap<UEdGraphNode*, FTimePair>;

			// Map of nodes that preceded before a given node in the execution sequence (one entry for each pairing)
			TMap<UEdGraphNode*, FExecPairingMap> PredecessorNodes;

			UEdGraph* GraphObj = nullptr;

			FLinearColor ActiveColor;
			FLinearColor InactiveColor;

			float ActiveWireThickness;
			float InactiveWireThickness;

			void Draw(TMap<TSharedRef<SWidget>, FArrangedWidget>& InPinGeometries, FArrangedChildren& ArrangedNodes);

		public:
			FGraphConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float ZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj);

			// FConnectionDrawingPolicy interface
			virtual void DetermineWiringStyle(UEdGraphPin* OutputPin, UEdGraphPin* InputPin, /*inout*/ FConnectionParams& Params) override;
			// End of FConnectionDrawingPolicy interface
		};
	} // namespace Editor
} // namespace Metasound
