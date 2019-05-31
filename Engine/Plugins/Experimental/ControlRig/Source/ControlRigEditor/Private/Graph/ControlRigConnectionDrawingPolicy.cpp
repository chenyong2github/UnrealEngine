// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ControlRigConnectionDrawingPolicy.h"
#include "Graph/ControlRigGraph.h"
#include "Graph/ControlRigGraphNode.h"
#include "ControlRigBlueprint.h"
#include "ControlRigController.h"
#include "Kismet2/BlueprintEditorUtils.h"

void FControlRigConnectionDrawingPolicy::SetIncompatiblePinDrawState(const TSharedPtr<SGraphPin>& StartPin, const TSet< TSharedRef<SWidget> >& VisiblePins)
{
	UEdGraphPin* Pin = StartPin->GetPinObj();
	if (Pin != nullptr)
	{
		UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNodeChecked(Pin->GetOwningNode());
		UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(Blueprint);
		if (RigBlueprint != nullptr)
		{
			FString Left, Right;
			RigBlueprint->Model->SplitPinPath(Pin->GetName(), Left, Right);
			RigBlueprint->ModelController->PrepareCycleCheckingForPin(*Left, *Right, Pin->Direction == EGPD_Input);
		}
	}
	FKismetConnectionDrawingPolicy::SetIncompatiblePinDrawState(StartPin, VisiblePins);
}

void FControlRigConnectionDrawingPolicy::ResetIncompatiblePinDrawState(const TSet< TSharedRef<SWidget> >& VisiblePins)
{
	if (VisiblePins.Num() > 0)
	{
		TSharedRef<SWidget> WidgetRef = *VisiblePins.begin();
		const SGraphPin* PinWidget = (const SGraphPin*)&WidgetRef.Get();
		UEdGraphPin* Pin = PinWidget->GetPinObj();
		if (Pin != nullptr)
		{
			UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNodeChecked(Pin->GetOwningNode());
			UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(Blueprint);
			if (RigBlueprint != nullptr)
			{
				RigBlueprint->ModelController->ResetCycleCheck();
			}
		}
	}
	FKismetConnectionDrawingPolicy::ResetIncompatiblePinDrawState(VisiblePins);
}

void FControlRigConnectionDrawingPolicy::BuildPinToPinWidgetMap(TMap<TSharedRef<SWidget>, FArrangedWidget>& InPinGeometries)
{
	FKismetConnectionDrawingPolicy::BuildPinToPinWidgetMap(InPinGeometries);

	// Add any sub-pins to the widget map if they arent there already, but with their parents geometry.
	for (TMap<TSharedRef<SWidget>, FArrangedWidget>::TIterator ConnectorIt(InPinGeometries); ConnectorIt; ++ConnectorIt)
	{
		struct Local
		{
			static void AddSubPins_Recursive(UEdGraphPin* PinObj, TMap<UEdGraphPin*, TSharedPtr<SGraphPin>>& InPinToPinWidgetMap, TSharedPtr<SGraphPin>& InGraphPinWidget)
			{
				for(UEdGraphPin* SubPin : PinObj->SubPins)
				{
					// Only add to the pin-to-pin widget map if the sub-pin widget is not there already
					TSharedPtr<SGraphPin>* SubPinWidgetPtr = InPinToPinWidgetMap.Find(SubPin);
					if(SubPinWidgetPtr == nullptr)
					{
						SubPinWidgetPtr = &InGraphPinWidget;
					}

					TSharedPtr<SGraphPin> PinWidgetPtr = *SubPinWidgetPtr;
					InPinToPinWidgetMap.Add(SubPin, PinWidgetPtr);
					AddSubPins_Recursive(SubPin, InPinToPinWidgetMap, PinWidgetPtr);
				}
			}
		};

		TSharedPtr<SGraphPin> GraphPinWidget = StaticCastSharedRef<SGraphPin>(ConnectorIt.Key());
		Local::AddSubPins_Recursive(GraphPinWidget->GetPinObj(), PinToPinWidgetMap, GraphPinWidget);
	}
}

void FControlRigConnectionDrawingPolicy::DrawPinGeometries(TMap<TSharedRef<SWidget>, FArrangedWidget>& InPinGeometries, FArrangedChildren& ArrangedNodes)
{
	auto DrawPin = [this, &ArrangedNodes](UEdGraphPin* ThePin, TSharedRef<SWidget>& InSomePinWidget)
	{
		if (ThePin->Direction == EGPD_Output)
		{
			for (int32 LinkIndex=0; LinkIndex < ThePin->LinkedTo.Num(); ++LinkIndex)
			{
				FArrangedWidget* LinkStartWidgetGeometry = nullptr;
				FArrangedWidget* LinkEndWidgetGeometry = nullptr;

				UEdGraphPin* TargetPin = ThePin->LinkedTo[LinkIndex];

				DetermineLinkGeometry(ArrangedNodes, InSomePinWidget, ThePin, TargetPin, /*out*/ LinkStartWidgetGeometry, /*out*/ LinkEndWidgetGeometry);

				if (( LinkEndWidgetGeometry && LinkStartWidgetGeometry ) && !IsConnectionCulled( *LinkStartWidgetGeometry, *LinkEndWidgetGeometry ))
				{
					FConnectionParams Params;
					DetermineWiringStyle(ThePin, TargetPin, /*inout*/ Params);
					DrawSplineWithArrow(LinkStartWidgetGeometry->Geometry, LinkEndWidgetGeometry->Geometry, Params);
				}
			}
		}
	};

	for (TMap<TSharedRef<SWidget>, FArrangedWidget>::TIterator ConnectorIt(InPinGeometries); ConnectorIt; ++ConnectorIt)
	{
		TSharedRef<SWidget> SomePinWidget = ConnectorIt.Key();
		SGraphPin& PinWidget = static_cast<SGraphPin&>(SomePinWidget.Get());
		
		struct Local
		{
			static void DrawSubPins_Recursive(UEdGraphPin* PinObj, TSharedRef<SWidget>& InSomePinWidget, const TFunctionRef<void(UEdGraphPin* Pin, TSharedRef<SWidget>& PinWidget)>& DrawPinFunction)
			{
				DrawPinFunction(PinObj, InSomePinWidget);

				for(UEdGraphPin* SubPin : PinObj->SubPins)
				{
					DrawSubPins_Recursive(SubPin, InSomePinWidget, DrawPinFunction);
				}
			}
		};

		Local::DrawSubPins_Recursive(PinWidget.GetPinObj(), SomePinWidget, DrawPin);
	}
}

void FControlRigConnectionDrawingPolicy::DetermineLinkGeometry(
	FArrangedChildren& ArrangedNodes, 
	TSharedRef<SWidget>& OutputPinWidget,
	UEdGraphPin* OutputPin,
	UEdGraphPin* InputPin,
	/*out*/ FArrangedWidget*& StartWidgetGeometry,
	/*out*/ FArrangedWidget*& EndWidgetGeometry
	)
{
	if (TSharedPtr<SGraphPin>* pOutputWidget = PinToPinWidgetMap.Find(OutputPin))
	{
		StartWidgetGeometry = PinGeometries->Find((*pOutputWidget).ToSharedRef());
	}
	
	if (TSharedPtr<SGraphPin>* pInputWidget = PinToPinWidgetMap.Find(InputPin))
	{
		EndWidgetGeometry = PinGeometries->Find((*pInputWidget).ToSharedRef());
	}
}