// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/Nodes/MovieGraphInputNode.h"

#include "Graph/MovieGraphConfig.h"
#include "Styling/AppStyle.h"

UMovieGraphInputNode::UMovieGraphInputNode()
{
#if WITH_EDITOR
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		if (UMovieGraphConfig* Graph = GetGraph())
		{
			// Register delegates for new inputs when they're added to the graph
			Graph->OnGraphInputAddedDelegate.AddUObject(this, &UMovieGraphInputNode::RegisterDelegates);
		}
	}
#endif
}

TArray<FMovieGraphPinProperties> UMovieGraphInputNode::GetOutputPinProperties() const
{
	TArray<FMovieGraphPinProperties> Properties;

	if (const UMovieGraphConfig* ParentGraph = GetGraph())
	{
		for (const UMovieGraphInput* Input : ParentGraph->GetInputs())
		{
			Properties.Add(FMovieGraphPinProperties(FName(Input->Name), EMovieGraphMemberType::Branch, false));
		}
	}
	
	return Properties;
}

#if WITH_EDITOR
FText UMovieGraphInputNode::GetNodeTitle(const bool bGetDescriptive) const
{
	return NSLOCTEXT("MovieGraphNodes", "InputNode_Description", "Input");
}

FText UMovieGraphInputNode::GetMenuCategory() const
{
	return NSLOCTEXT("MovieGraphNodes", "InputNode_Category", "Input/Output");
}

FLinearColor UMovieGraphInputNode::GetNodeTitleColor() const
{
	return FLinearColor::Black;
}

FSlateIcon UMovieGraphInputNode::GetIconAndTint(FLinearColor& OutColor) const
{
	static const FSlateIcon InputIcon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorViewport.VisualizeBufferMode");

	OutColor = FLinearColor::White;
	return InputIcon;
}
#endif // WITH_EDITOR

void UMovieGraphInputNode::RegisterDelegates() const
{
	Super::RegisterDelegates();
	
	if (const UMovieGraphConfig* Graph = GetGraph())
	{
		for (UMovieGraphInput* InputMember : Graph->GetInputs())
		{
			RegisterDelegates(InputMember);
		}
	}
}

void UMovieGraphInputNode::RegisterDelegates(UMovieGraphInput* Input) const
{
#if WITH_EDITOR
	if (Input)
	{
		Input->OnMovieGraphInputChangedDelegate.AddUObject(this, &UMovieGraphInputNode::UpdateExistingPins);
	}
#endif
}

void UMovieGraphInputNode::UpdateExistingPins(UMovieGraphMember* ChangedInput) const
{
	if (const UMovieGraphConfig* Graph = GetGraph())
	{
		const TArray<UMovieGraphInput*> InputMembers = Graph->GetInputs();
		if (InputMembers.Num() == OutputPins.Num())
		{
			for (int32 Index = 0; Index < InputMembers.Num(); ++Index)
			{
				OutputPins[Index]->Properties.Label = FName(InputMembers[Index]->Name);
			}
		}
		
		OnNodeChangedDelegate.Broadcast(this);
	}
}