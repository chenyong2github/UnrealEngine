// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/Nodes/MovieGraphVariableNode.h"

#include "Graph/MovieGraphConfig.h"
#include "Styling/AppStyle.h"

UMovieGraphVariableNode::UMovieGraphVariableNode()
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		RegisterDelegates();
	}
}

TArray<FMovieGraphPinProperties> UMovieGraphVariableNode::GetOutputPinProperties() const
{
	TArray<FMovieGraphPinProperties> Properties;
	
	if (GraphVariable)
	{
		Properties.Add(FMovieGraphPinProperties(FName(GraphVariable->Name), GraphVariable->Type, false));
	}
	else
	{
		Properties.Add(FMovieGraphPinProperties(TEXT("Unknown"), EMovieGraphMemberType::Float, false));
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

		RegisterDelegates();
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

FLinearColor UMovieGraphVariableNode::GetNodeTitleColor() const
{
	static const FLinearColor VariableNodeColor = FLinearColor(0.188f, 0.309f, 0.286f);
	return VariableNodeColor;
}

FSlateIcon UMovieGraphVariableNode::GetIconAndTint(FLinearColor& OutColor) const
{
	static const FSlateIcon VariableIcon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "Kismet.AllClasses.VariableIcon");

	OutColor = FLinearColor::White;
	return VariableIcon;
}
#endif // WITH_EDITOR

void UMovieGraphVariableNode::RegisterDelegates() const
{
	Super::RegisterDelegates();

#if WITH_EDITOR
	if (GraphVariable)
	{
		GraphVariable->OnMovieGraphVariableChangedDelegate.AddUObject(this, &UMovieGraphVariableNode::UpdateOutputPin);
	}
#endif
}

void UMovieGraphVariableNode::UpdateOutputPin(UMovieGraphMember* ChangedVariable) const
{
	if (!OutputPins.IsEmpty() && ChangedVariable)
	{
		// Update the output pin to reflect the variable data model
		OutputPins[0]->Properties.Label = FName(ChangedVariable->Name);
		OutputPins[0]->Properties.Type = ChangedVariable->Type;
	}

	OnNodeChangedDelegate.Broadcast(this);
}