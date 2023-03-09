// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/MovieEdGraphNode.h"

#include "Graph/MovieGraphPin.h"
#include "Graph/MovieGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Misc/TransactionObjectEvent.h"
#include "MovieEdGraph.h"
#include "PropertyBag.h"
#include "ToolMenu.h"
#include "EdGraph/EdGraphSchema.h"

#define LOCTEXT_NAMESPACE "MoviePipelineEdGraphNodeBase"

void UMoviePipelineEdGraphNodeBase::Construct(UMovieGraphNode* InRuntimeNode)
{
	check(InRuntimeNode);
	RuntimeNode = InRuntimeNode;
	RuntimeNode->GraphNode = this;
	RuntimeNode->OnNodeChangedDelegate.AddUObject(this, &UMoviePipelineEdGraphNodeBase::OnRuntimeNodeChanged);
	
	NodePosX = InRuntimeNode->GetNodePosX();
	NodePosY = InRuntimeNode->GetNodePosY();
	// NodeComment = InRuntimeNode->NodeComment;
	// bCommentBubblePinned = InRuntimeNode->bCommentBubblePinned;
	// bCommentBubbleVisible = InRuntimeNode->bCommentBubbleVisible;
}

void UMoviePipelineEdGraphNodeBase::PostTransacted(const FTransactionObjectEvent& TransactionEvent)
{
	const TArray<FName> ChangedProperties = TransactionEvent.GetChangedProperties();

	if (ChangedProperties.Contains(GET_MEMBER_NAME_CHECKED(UEdGraphNode, NodePosX)) ||
		ChangedProperties.Contains(GET_MEMBER_NAME_CHECKED(UEdGraphNode, NodePosY)))
	{
		UpdatePosition();
	}
}

FEdGraphPinType UMoviePipelineEdGraphNodeBase::GetPinType(const UMovieGraphPin* InPin)
{
	FEdGraphPinType EdPinType;
	EdPinType.ResetToDefaults();
	
	EdPinType.PinCategory = FName("TestCategory");
	EdPinType.PinSubCategory = FName("TestSubCategory");
	return EdPinType;
}

void UMoviePipelineEdGraphNodeBase::UpdatePosition()
{
	if (RuntimeNode)
	{
		RuntimeNode->Modify();
		RuntimeNode->SetNodePosX(NodePosX);
		RuntimeNode->SetNodePosY(NodePosY);
	}
}

void UMoviePipelineEdGraphNode::AllocateDefaultPins()
{
	if(RuntimeNode)
	{
		for(const UMovieGraphPin* InputPin : RuntimeNode->GetInputPins())
		{
			CreatePin(EEdGraphPinDirection::EGPD_Input, GetPinType(InputPin), InputPin->Properties.Label);
		}
		
		for(const UMovieGraphPin* OutputPin : RuntimeNode->GetOutputPins())
		{
			CreatePin(EEdGraphPinDirection::EGPD_Output, GetPinType(OutputPin), OutputPin->Properties.Label);
		}
	}
}

FText UMoviePipelineEdGraphNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (RuntimeNode)
	{
		return RuntimeNode->GetMenuDescription();
	}

	return LOCTEXT("GraphTestTitle", "TestTitle");
}

FText UMoviePipelineEdGraphNode::GetTooltipText() const
{
	return LOCTEXT("GraphTestTooltip", "Test Tooltip");
}

void UMoviePipelineEdGraphNode::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	Super::GetNodeContextMenuActions(Menu, Context);
	
	if (!Context->Node || !RuntimeNode)
	{
		return;
	}

	GetPropertyPromotionContextMenuActions(Menu, Context);
}

void UMoviePipelineEdGraphNode::GetPropertyPromotionContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	// Exclude the dynamic properties which are used for EditCondition metadata
	static const FName EditConditionKey = FName("EditCondition");
	TArray<FName> ExcludedPropertyNames;
	for (const FPropertyBagPropertyDesc& PropertyDescription : RuntimeNode->GetDynamicPropertyDescriptions())
	{
		for (const FPropertyBagPropertyDescMetaData& Metadata : PropertyDescription.MetaData)
		{
			if (Metadata.Key == EditConditionKey)
			{
				ExcludedPropertyNames.Add(FName(Metadata.Value));
			}
		}
	}

	// Exclude dynamic properties which have already been promoted
	ExcludedPropertyNames.Append(RuntimeNode->GetExposedDynamicProperties());

	FToolMenuSection& Section = Menu->AddSection("MoviePipelineGraphExposeAsPin", LOCTEXT("ExposeAsPin", "Expose Property as Pin"));

	const TArray<FPropertyBagPropertyDesc>& PropertyDescriptions = RuntimeNode->GetDynamicPropertyDescriptions();
	for (const FPropertyBagPropertyDesc& PropertyDescription : PropertyDescriptions)
	{
		if (ExcludedPropertyNames.Contains(PropertyDescription.Name))
		{
			continue;
		}
		
		Section.AddMenuEntry(
			PropertyDescription.Name,
			FText::FromName(PropertyDescription.Name),
			LOCTEXT("PromotePropertyToPin", "Promote this property to a pin on this node."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateUObject(this, &UMoviePipelineEdGraphNode::PromotePropertyToPin, PropertyDescription.Name),
				FCanExecuteAction())
		);
	}

	if (PropertyDescriptions.Num() == ExcludedPropertyNames.Num())
	{
		Section.AddMenuEntry(
			"NoPropertiesAvailable",
			FText::FromString("No properties available"),
			LOCTEXT("PromotePropertyToPin_NoneAvailable", "No properties are available to promote."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction(),
				FCanExecuteAction::CreateLambda([]() { return false; }))
		);
	}
}

void UMoviePipelineEdGraphNode::PromotePropertyToPin(const FName PropertyName) const
{
	RuntimeNode->PromoteDynamicPropertyToPin(PropertyName);
}

bool UMoviePipelineEdGraphNodeBase::ShouldCreatePin(const UMovieGraphPin* InPin) const
{
	return true;
}

void UMoviePipelineEdGraphNodeBase::CreatePins(const TArray<UMovieGraphPin*>& InInputPins, const TArray<UMovieGraphPin*>& InOutputPins)
{
	bool bHasAdvancedPin = false;

	for (const UMovieGraphPin* InputPin : InInputPins)
	{
		if (!ShouldCreatePin(InputPin))
		{
			continue;
		}

		UEdGraphPin* Pin = CreatePin(EEdGraphPinDirection::EGPD_Input, GetPinType(InputPin), InputPin->Properties.Label);
		// Pin->bAdvancedView = InputPin->Properties.bAdvancedPin;
		bHasAdvancedPin |= Pin->bAdvancedView;
	}

	for (const UMovieGraphPin* OutputPin : InOutputPins)
	{
		if (!ShouldCreatePin(OutputPin))
		{
			continue;
		}

		UEdGraphPin* Pin = CreatePin(EEdGraphPinDirection::EGPD_Output, GetPinType(OutputPin), OutputPin->Properties.Label);
		// Pin->bAdvancedView = OutputPin->Properties.bAdvancedPin;
		bHasAdvancedPin |= Pin->bAdvancedView;
	}

	if (bHasAdvancedPin && AdvancedPinDisplay == ENodeAdvancedPins::NoPins)
	{
		AdvancedPinDisplay = ENodeAdvancedPins::Hidden;
	}
	else if (!bHasAdvancedPin)
	{
		AdvancedPinDisplay = ENodeAdvancedPins::NoPins;
	}
}

void UMoviePipelineEdGraphNodeBase::AutowireNewNode(UEdGraphPin* FromPin)
{
	if (RuntimeNode == nullptr || FromPin == nullptr)
	{
		return;
	}

	const bool bFromPinIsInput = FromPin->Direction == EEdGraphPinDirection::EGPD_Input;
	const TArray<TObjectPtr<UMovieGraphPin>>& OtherPinsList = bFromPinIsInput ? RuntimeNode->GetOutputPins() : RuntimeNode->GetInputPins();

	// Try to connect to the first compatible pin
	bool bDidAutoconnect = false;
	for (const TObjectPtr<UMovieGraphPin>& OtherPin : OtherPinsList)
	{
		check(OtherPin);

		const FName& OtherPinName = OtherPin->Properties.Label;
		UEdGraphPin* ToPin = FindPinChecked(OtherPinName, bFromPinIsInput ? EEdGraphPinDirection::EGPD_Output : EEdGraphPinDirection::EGPD_Input);
		if (ToPin && GetSchema()->TryCreateConnection(FromPin, ToPin))
		{
			// Connection succeeded. Notify our other node that their connections changed.
			if (ToPin->GetOwningNode())
			{
				ToPin->GetOwningNode()->NodeConnectionListChanged();
			}
			bDidAutoconnect = true;
			break;
		}
	}

	// Notify ourself of the connection list changing too.
	if (bDidAutoconnect)
	{
		NodeConnectionListChanged();
	}
}

void UMoviePipelineEdGraphNodeBase::OnRuntimeNodeChanged(const UMovieGraphNode* InChangedNode)
{
	if (InChangedNode == GetRuntimeNode())
	{
		ReconstructNode();
	}
}

void UMoviePipelineEdGraphNodeBase::ReconstructNode()
{
	ReconstructPins();

	UMoviePipelineEdGraph* Graph = CastChecked<UMoviePipelineEdGraph>(GetGraph());
	
	// Reconstruct connections
	const bool bCreateInbound = true;
	const bool bCreateOutbound = true;
	Graph->CreateLinks(this, bCreateInbound, bCreateOutbound);

	Graph->NotifyGraphChanged();
}

void UMoviePipelineEdGraphNodeBase::ReconstructPins()
{
	// Store copy of old pins
	TArray<UEdGraphPin*> OldPins = MoveTemp(Pins);
	Pins.Reset();
	
	// Generate new pins
	CreatePins(RuntimeNode->GetInputPins(), RuntimeNode->GetOutputPins());
	
	// Transfer persistent data from old to new pins
	for (UEdGraphPin* OldPin : OldPins)
	{
		for (UEdGraphPin* NewPin : Pins)
		{
			if ((OldPin->PinName == NewPin->PinName) && (OldPin->Direction == NewPin->Direction))
			{
				// Remove invalid entries
				OldPin->LinkedTo.Remove(nullptr);

				NewPin->MovePersistentDataFromOldPin(*OldPin);
				break;
			}
		}
	}
	
	// Remove old pins
	for (UEdGraphPin* OldPin : OldPins)
	{
		OldPin->BreakAllPinLinks();
		OldPin->SubPins.Remove(nullptr);
		DestroyPin(OldPin);
	}
}

#undef LOCTEXT_NAMESPACE