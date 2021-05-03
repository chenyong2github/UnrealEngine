// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundEditorGraphConnectionDrawingPolicy.h"

#include "Components/AudioComponent.h"
#include "Editor.h"
#include "MetasoundEditor.h"
#include "MetasoundEditorGraph.h"
#include "MetasoundEditorGraphBuilder.h"
#include "MetasoundEditorGraphSchema.h"
#include "Misc/App.h"


namespace Metasound
{
	namespace Editor
	{
		FConnectionDrawingPolicy* FGraphConnectionDrawingPolicyFactory::CreateConnectionPolicy(
			const UEdGraphSchema* Schema,
			int32 InBackLayerID,
			int32 InFrontLayerID,
			float ZoomFactor,
			const FSlateRect& InClippingRect,
			FSlateWindowElementList& InDrawElements,
			UEdGraph* InGraphObj) const
		{
			if (Schema->IsA(UMetasoundEditorGraphSchema::StaticClass()))
			{
				return new FGraphConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, ZoomFactor, InClippingRect, InDrawElements, InGraphObj);
			}
			return nullptr;
		}

		void FGraphConnectionDrawingPolicy::Draw(TMap<TSharedRef<SWidget>, FArrangedWidget>& InPinGeometries, FArrangedChildren& ArrangedNodes)
		{
			// Draw everything
			FConnectionDrawingPolicy::Draw(InPinGeometries, ArrangedNodes);
		}

		void FGraphConnectionDrawingPolicy::DetermineWiringStyle(UEdGraphPin* OutputPin, UEdGraphPin* InputPin, FConnectionParams& OutParams)
		{
			using namespace Frontend;

			if (!OutputPin || !InputPin || !GraphObj)
			{
				return;
			}

			OutParams.AssociatedPin1 = InputPin;
			OutParams.AssociatedPin2 = OutputPin;

			if (OutputPin->bOrphanedPin || InputPin->bOrphanedPin)
			{
				OutParams.WireColor = FLinearColor::Red;
			}
			else
			{
				OutParams.WireColor = FGraphBuilder::GetPinCategoryColor(OutputPin->PinType);
			}

			bool bExecuted = false;

			// Run through the predecessors, and on
			if (FExecPairingMap* PredecessorMap = PredecessorNodes.Find(OutputPin->GetOwningNode()))
			{
				if (FTimePair* Times = PredecessorMap->Find(InputPin->GetOwningNode()))
				{
					bExecuted = true;

					OutParams.WireThickness = ActiveWireThickness;
					OutParams.bDrawBubbles = true;
				}
			}

			if (!bExecuted)
			{
				if (InputPin->PinType.PinCategory == FGraphBuilder::PinCategoryTrigger)
				{
					OutParams.WireThickness = Settings->DefaultExecutionWireThickness;
				}
				else if (InputPin->PinType.PinCategory == FGraphBuilder::PinCategoryAudio)
				{
					bool bIsActive = false;
					if (UEdGraphNode* Node = InputPin->GetOwningNode())
					{
						if (UEdGraph* Graph = Node->GetGraph())
						{
							TSharedPtr<FEditor> MetasoundEditor = FGraphBuilder::GetEditorForGraph(*Graph);
							if (MetasoundEditor.IsValid())
							{
								bIsActive = MetasoundEditor->GetPlayTime() > 0.0;
							}
						}
					}

					OutParams.bDrawBubbles = bIsActive;
				}
				else
				{
					OutParams.WireThickness = InactiveWireThickness;
				}
			}
		}

		FGraphConnectionDrawingPolicy::FGraphConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj)
			: FConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements)
			, GraphObj(InGraphObj)
		{
			ActiveWireThickness = Settings->TraceAttackWireThickness;
			InactiveWireThickness = Settings->TraceReleaseWireThickness;
		}
	} // namespace Editor
} // namespace Metasound
