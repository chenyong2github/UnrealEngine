// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/MovieGraphNode.h"
#include "Graph/MovieGraphConfig.h"


UMovieGraphNode::UMovieGraphNode()
{
}

TArray<FMovieGraphPinProperties> UMovieGraphNode::GetExposedDynamicPinProperties() const
{
	TArray<FMovieGraphPinProperties> Properties;

	// Exposed dynamic properties are tracked by name; convert to pin properties
	for (const FName& PropertyName : GetExposedDynamicProperties())
	{
		// Note: Currently hardcoded to not allow multiple connections, but this may need to change in the future
		const bool bInAllowMultipleConnections = false;
		Properties.Add(FMovieGraphPinProperties(PropertyName, bInAllowMultipleConnections));
	}

	return Properties;
}

void UMovieGraphNode::PromoteDynamicPropertyToPin(const FName& PropertyName)
{
	// Ensure this is a real dynamic property
	bool bFoundMatchingProperty = false;
	for (const FPropertyBagPropertyDesc& PropertyDescription : GetDynamicPropertyDescriptions())
	{
		if (PropertyDescription.Name == PropertyName)
		{
			bFoundMatchingProperty = true;
			break;
		}
	}

	// Add this property to promoted properties if it hasn't already been promoted
	if (bFoundMatchingProperty && !ExposedDynamicPropertyNames.Contains(PropertyName))
	{
		ExposedDynamicPropertyNames.Add(PropertyName);
	}

	UpdatePins();
}

void UMovieGraphNode::UpdatePins()
{
	TArray<FMovieGraphPinProperties> InputPinProperties = GetInputPinProperties();
	TArray<FMovieGraphPinProperties> OutputPinProperties = GetOutputPinProperties();

	// Include the exposed dynamic properties in the input pins
	InputPinProperties.Append(GetExposedDynamicPinProperties());

	auto UpdatePins = [this](TArray<UMovieGraphPin*>& Pins, const TArray<FMovieGraphPinProperties>& PinProperties)
	{
		bool bAppliedEdgeChanges = false;
		bool bChangedPins = false;

		// Find unmatched pins vs. properties (via name matching)
		TArray<UMovieGraphPin*> UnmatchedPins;
		for (UMovieGraphPin* Pin : Pins)
		{
			if (const FMovieGraphPinProperties* MatchingProperties = PinProperties.FindByPredicate([Pin](const FMovieGraphPinProperties& Prop) { return Prop.Label == Pin->Properties.Label; }))
			{
				if (Pin->Properties != *MatchingProperties)
				{
					Pin->Modify();
					Pin->Properties = *MatchingProperties;
					//bAppliedEdgeCHanges |= Pin->BreakAllIncompatibleEdges();
					bChangedPins = true;
				}
			}
			else
			{
				UnmatchedPins.Add(Pin);
			}
		}

		// Now do the opposite, find any properties that don't have pins
		TArray<FMovieGraphPinProperties> UnmatchedProperties;
		for (const FMovieGraphPinProperties& Properties : PinProperties)
		{
			if (!Pins.FindByPredicate([&Properties](const UMovieGraphPin* Pin) { return Pin->Properties.Label == Properties.Label; }))
			{
				UnmatchedProperties.Add(Properties);
			}
		}

		if (!UnmatchedPins.IsEmpty() || !UnmatchedProperties.IsEmpty())
		{
			Modify();
			bChangedPins = true;
		}

		// Remove old pins
		for (int32 UnmatchedPinIndex = UnmatchedPins.Num() - 1; UnmatchedPinIndex >= 0; UnmatchedPinIndex--)
		{
			const int32 PinIndex = Pins.IndexOfByKey(UnmatchedPins[UnmatchedPinIndex]);
			check(PinIndex >= 0);

			//bAppliedEdgeCHanges |= Pins[PinIndex]->BreakAllEdges();
			Pins.RemoveAt(PinIndex);
		}

		// Add new pins
		for (const FMovieGraphPinProperties& UnmatchedProperty : UnmatchedProperties)
		{
			const int32 InsertIndex = PinProperties.IndexOfByKey(UnmatchedProperty);
			UMovieGraphPin* NewPin = NewObject<UMovieGraphPin>(this);
			NewPin->Node = this;
			NewPin->Properties = UnmatchedProperty;
			Pins.Insert(NewPin, InsertIndex);
		}

		// return bAppliedEdgeChanges ? EPCGChangeType::Edge : None) | (bChangedPins ? EPCGChangeType::Node : None
	};

	UpdatePins(InputPins, InputPinProperties);
	UpdatePins(OutputPins, OutputPinProperties);

	OnNodeChangedDelegate.Broadcast(this);
}

void UMovieGraphNode::UpdateDynamicProperties()
{
	TArray<FPropertyBagPropertyDesc> DesiredDynamicProperties = GetDynamicPropertyDescriptions();

	// Check to see if we need to remake our property bag
	bool bHasAllProperties = DynamicProperties.GetNumPropertiesInBag() == DesiredDynamicProperties.Num();
	if (bHasAllProperties)
	{
		// If there's still the same number of properties before/after, then we need to do the more expensive
		// check, which is to see if every property in the desired list is already inside the property bag.
		for (const FPropertyBagPropertyDesc& Desc : DesiredDynamicProperties)
		{
			bool bBagContainsProperty = DynamicProperties.FindPropertyDescByName(Desc.Name) != nullptr;
			if (!bBagContainsProperty)
			{
				bHasAllProperties = false;
				break;
			}
		}
	}

	// If we don't have all the properties in our bag already, we need to generate a new bag with the correct
	// layout, and then we have to migrate the existing bag over (so existing, matching, values stay).
	if (!bHasAllProperties)
	{
		FInstancedPropertyBag NewPropertyBag;
		NewPropertyBag.AddProperties(DesiredDynamicProperties);

		DynamicProperties.MigrateToNewBagInstance(NewPropertyBag);
	}
}

UMovieGraphConfig* UMovieGraphNode::GetGraph() const
{
	return Cast<UMovieGraphConfig>(GetOuter());
}

UMovieGraphPin* UMovieGraphNode::GetInputPin(const FName& Label) const
{
	for (UMovieGraphPin* InputPin : InputPins)
	{
		if (InputPin->Properties.Label == Label)
		{
			return InputPin;
		}
	}

	return nullptr;
}

UMovieGraphPin* UMovieGraphNode::GetOutputPin(const FName& Label) const
{
	for (UMovieGraphPin* OutputPin : OutputPins)
	{
		if (OutputPin->Properties.Label == Label)
		{
			return OutputPin;
		}
	}

	return nullptr;
}

TArray<FMovieGraphPinProperties> UMovieGraphVariableNode::GetOutputPinProperties() const
{
	TArray<FMovieGraphPinProperties> Properties;
	
	if (GraphVariable)
	{
		Properties.Add(FMovieGraphPinProperties(FName(GraphVariable->Name), false));
	}
	else
	{
		Properties.Add(FMovieGraphPinProperties(TEXT("Unknown"), false));
	}
	
	return Properties;
}

void UMovieGraphVariableNode::SetVariable(UMovieGraphVariable* InVariable)
{
	if (InVariable)
	{
		GraphVariable = InVariable;

		// Update the output pin to reflect the new variable, and update the pin whenever the variable changes
		// (eg, when the variable is renamed)
		UpdateOutputPin(GraphVariable);

#if WITH_EDITOR
		GraphVariable->OnMovieGraphVariableChangedDelegate.AddUObject(this, &UMovieGraphVariableNode::UpdateOutputPin);
#endif
	}
}

#if WITH_EDITOR
FText UMovieGraphVariableNode::GetMenuDescription() const
{
	return GraphVariable ? FText::FromString(GraphVariable->Name) : FText();
}
	
FText UMovieGraphVariableNode::GetMenuCategory() const
{
	return NSLOCTEXT("MovieGraphNode", "VariableNode_Category", "Variables");
}
#endif // WITH_EDITOR

void UMovieGraphVariableNode::UpdateOutputPin(UMovieGraphVariable* ChangedVariable)
{
	if (!OutputPins.IsEmpty() && ChangedVariable)
	{
		// Update the output pin w/ the name of the variable
		OutputPins[0]->Properties.Label = FName(ChangedVariable->Name);
	}
}