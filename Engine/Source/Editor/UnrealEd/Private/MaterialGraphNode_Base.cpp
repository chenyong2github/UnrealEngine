// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MaterialGraphNode_Base.cpp
=============================================================================*/

#include "MaterialGraph/MaterialGraphNode_Base.h"
#include "EdGraph/EdGraphSchema.h"
#include "MaterialGraph/MaterialGraph.h"
#include "MaterialGraph/MaterialGraphSchema.h"

/////////////////////////////////////////////////////
// UMaterialGraphNode_Base

UMaterialGraphNode_Base::UMaterialGraphNode_Base(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

const FMaterialGraphPinInfo& UMaterialGraphNode_Base::GetPinInfo(const class UEdGraphPin* Pin) const
{
	const FMaterialGraphPinInfo* PinInfo = PinInfoMap.Find(Pin);
	checkf(PinInfo, TEXT("Missing info for pin %s, missing call to RegisterPin()?"), *Pin->GetName());
	return *PinInfo;
}

uint32 UMaterialGraphNode_Base::GetOutputType(const UEdGraphPin* OutputPin) const
{
	return GetPinInfo(OutputPin).Type;
}

uint32 UMaterialGraphNode_Base::GetInputType(const UEdGraphPin* InputPin) const
{
	return GetPinInfo(InputPin).Type;
}

void UMaterialGraphNode_Base::ReplaceNode(UMaterialGraphNode_Base* OldNode)
{
	check(OldNode);
	check(OldNode != this);

	// Copy Inputs from old node
	for (int32 PinIndex = 0; PinIndex < OldNode->InputPins.Num(); PinIndex++)
	{
		if (PinIndex < InputPins.Num())
		{
			ModifyAndCopyPersistentPinData(*InputPins[PinIndex], *OldNode->InputPins[PinIndex]);
		}
	}

	// Copy Outputs from old node
	for (int32 PinIndex = 0; PinIndex < OldNode->OutputPins.Num(); PinIndex++)
	{
		// Try to find an equivalent output in this node
		int32 FoundPinIndex = -1;
		{
			// First check names
			for (int32 NewPinIndex = 0; NewPinIndex < OutputPins.Num(); NewPinIndex++)
			{
				if (OldNode->OutputPins[PinIndex]->PinName == OutputPins[NewPinIndex]->PinName)
				{
					FoundPinIndex = NewPinIndex;
					break;
				}
			}
		}
		if (FoundPinIndex == -1)
		{
			// Now check types
			for (int32 NewPinIndex = 0; NewPinIndex < OutputPins.Num(); NewPinIndex++)
			{
				if (OldNode->OutputPins[PinIndex]->PinType == OutputPins[NewPinIndex]->PinType)
				{
					FoundPinIndex = NewPinIndex;
					break;
				}
			}
		}

		// If we can't find an equivalent output in this node, just use the first
		// The user will have to fix up any issues from the mismatch
		FoundPinIndex = FMath::Max(FoundPinIndex, 0);
		if (FoundPinIndex < OutputPins.Num())
		{
			ModifyAndCopyPersistentPinData(*OutputPins[FoundPinIndex], *OldNode->OutputPins[PinIndex]);
		}
	}

	// Break the original pin links
	for (int32 OldPinIndex = 0; OldPinIndex < OldNode->Pins.Num(); ++OldPinIndex)
	{
		UEdGraphPin* OldPin = OldNode->Pins[OldPinIndex];
		OldPin->Modify();
		OldPin->BreakAllPinLinks();
	}
}

void UMaterialGraphNode_Base::InsertNewNode(UEdGraphPin* FromPin, UEdGraphPin* NewLinkPin, TSet<UEdGraphNode*>& OutNodeList)
{
	const UMaterialGraphSchema* Schema = CastChecked<UMaterialGraphSchema>(GetSchema());

	// The pin we are creating from already has a connection that needs to be broken. We want to "insert" the new node in between, so that the output of the new node is hooked up too
	UEdGraphPin* OldLinkedPin = FromPin->LinkedTo[0];
	check(OldLinkedPin);

	FromPin->BreakAllPinLinks();

	// Hook up the old linked pin to the first valid output pin on the new node
	for (int32 OutpinPinIdx=0; OutpinPinIdx<Pins.Num(); OutpinPinIdx++)
	{
		UEdGraphPin* OutputPin = Pins[OutpinPinIdx];
		check(OutputPin);
		if (ECanCreateConnectionResponse::CONNECT_RESPONSE_MAKE == Schema->CanCreateConnection(OldLinkedPin, OutputPin).Response)
		{
			if (Schema->TryCreateConnection(OldLinkedPin, OutputPin))
			{
				OutNodeList.Add(OldLinkedPin->GetOwningNode());
				OutNodeList.Add(this);
			}
			break;
		}
	}

	if (Schema->TryCreateConnection(FromPin, NewLinkPin))
	{
		OutNodeList.Add(FromPin->GetOwningNode());
		OutNodeList.Add(this);
	}
}

void UMaterialGraphNode_Base::AllocateDefaultPins()
{
	check(Pins.Num() == 0);
	check(InputPins.Num() == 0);
	check(OutputPins.Num() == 0);
	check(PinInfoMap.Num() == 0);

	CreateInputPins();
	CreateOutputPins();
}

void UMaterialGraphNode_Base::RegisterPin(UEdGraphPin* Pin, int32 Index, uint32 Type)
{
	FMaterialGraphPinInfo& PinInfo = PinInfoMap.FindOrAdd(Pin);
	PinInfo.Type = Type;
	PinInfo.Index = Index;

	if (Type & MCT_Execution)
	{
		if (Pin->Direction == EGPD_Input)
		{
			checkf(ExecInputPin == nullptr, TEXT("Only 1 exec input pin allowed"));
			check(Index == 0);
			ExecInputPin = Pin;
		}
	}
	else
	{
		switch (Pin->Direction)
		{
		case EGPD_Input: verify(InputPins.Add(Pin) == Index); break;
		case EGPD_Output: verify(OutputPins.Add(Pin) == Index); break;
		default: checkNoEntry(); break;
		}
	}
}

void UMaterialGraphNode_Base::ReconstructNode()
{
	Modify();

	// Break any links to 'orphan' pins
	for (int32 PinIndex = 0; PinIndex < Pins.Num(); ++PinIndex)
	{
		UEdGraphPin* Pin = Pins[PinIndex];
		TArray<class UEdGraphPin*>& LinkedToRef = Pin->LinkedTo;
		for (int32 LinkIdx=0; LinkIdx < LinkedToRef.Num(); LinkIdx++)
		{
			UEdGraphPin* OtherPin = LinkedToRef[LinkIdx];
			// If we are linked to a pin that its owner doesn't know about, break that link
			if (!OtherPin->GetOwningNode()->Pins.Contains(OtherPin))
			{
				Pin->LinkedTo.Remove(OtherPin);
			}
		}
	}

	// Move the existing pins to a saved array
	TArray<UEdGraphPin*> OldInputPins = MoveTemp(InputPins);
	TArray<UEdGraphPin*> OldOutputPins = MoveTemp(OutputPins);
	TArray<UEdGraphPin*> OldPins = MoveTemp(Pins);
	Pins.Reset();
	PinInfoMap.Reset();
	InputPins.Reset();
	OutputPins.Reset();

	// Recreate the new pins
	AllocateDefaultPins();

	for (int32 PinIndex = 0; PinIndex < OldInputPins.Num(); PinIndex++)
	{
		if (PinIndex < InputPins.Num())
		{
			InputPins[PinIndex]->MovePersistentDataFromOldPin(*OldInputPins[PinIndex]);
		}
	}

	for (int32 PinIndex = 0; PinIndex < OldOutputPins.Num(); PinIndex++)
	{
		if (PinIndex < OutputPins.Num())
		{
			OutputPins[PinIndex]->MovePersistentDataFromOldPin(*OldOutputPins[PinIndex]);
		}
	}

	// Throw away the original pins
	for (UEdGraphPin* OldPin : OldPins)
	{
		OldPin->Modify();
		UEdGraphNode::DestroyPin(OldPin);
	}

	GetGraph()->NotifyGraphChanged();
}

void UMaterialGraphNode_Base::RemovePinAt(const int32 PinIndex, const EEdGraphPinDirection PinDirection)
{
	Super::RemovePinAt(PinIndex, PinDirection);

	UMaterialGraph* MaterialGraph = CastChecked<UMaterialGraph>(GetGraph());
	MaterialGraph->LinkMaterialExpressionsFromGraph();
}

void UMaterialGraphNode_Base::AutowireNewNode(UEdGraphPin* FromPin)
{
	if (FromPin != NULL)
	{
		const UMaterialGraphSchema* Schema = CastChecked<UMaterialGraphSchema>(GetSchema());

		TSet<UEdGraphNode*> NodeList;

		// auto-connect from dragged pin to first compatible pin on the new node
		for (int32 i=0; i<Pins.Num(); i++)
		{
			UEdGraphPin* Pin = Pins[i];
			check(Pin);
			FPinConnectionResponse Response = Schema->CanCreateConnection(FromPin, Pin);
			if (ECanCreateConnectionResponse::CONNECT_RESPONSE_MAKE == Response.Response) //-V1051
			{
				if (Schema->TryCreateConnection(FromPin, Pin))
				{
					NodeList.Add(FromPin->GetOwningNode());
					NodeList.Add(this);
				}
				break;
			}
			else if(ECanCreateConnectionResponse::CONNECT_RESPONSE_BREAK_OTHERS_A == Response.Response)
			{
				InsertNewNode(FromPin, Pin, NodeList);
				break;
			}
		}

		// Send all nodes that received a new pin connection a notification
		for (auto It = NodeList.CreateConstIterator(); It; ++It)
		{
			UEdGraphNode* Node = (*It);
			Node->NodeConnectionListChanged();
		}
	}
}

bool UMaterialGraphNode_Base::CanCreateUnderSpecifiedSchema(const UEdGraphSchema* Schema) const
{
	return Schema->IsA(UMaterialGraphSchema::StaticClass());
}

void UMaterialGraphNode_Base::ModifyAndCopyPersistentPinData(UEdGraphPin& TargetPin, const UEdGraphPin& SourcePin) const
{
	if (SourcePin.LinkedTo.Num() > 0)
	{
		TargetPin.Modify();

		for (int32 LinkIndex = 0; LinkIndex < SourcePin.LinkedTo.Num(); ++LinkIndex)
		{
			UEdGraphPin* OtherPin = SourcePin.LinkedTo[LinkIndex];
			OtherPin->Modify();
		}
	}

	TargetPin.CopyPersistentDataFromOldPin(SourcePin);
}

FString UMaterialGraphNode_Base::GetDocumentationLink() const
{
	return TEXT("Shared/GraphNodes/Material");
}

