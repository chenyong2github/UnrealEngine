// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGEditorGraphNodeInput.h"

#include "PCGNode.h"

#define LOCTEXT_NAMESPACE "PCGEditorGraphNodeInput"

FText UPCGEditorGraphNodeInput::GetNodeTitle(ENodeTitleType::Type TitleType) const
{	
	return FText::FromName(TEXT("Input"));
}

void UPCGEditorGraphNodeInput::AllocateDefaultPins()
{
	if (!PCGNode || PCGNode->HasDefaultOutLabel())
	{
		CreatePin(EEdGraphPinDirection::EGPD_Output, NAME_None, FName(TEXT("Out")));
	}

	if (PCGNode)
	{
		for (const FName& OutLabel : PCGNode->OutLabels())
		{
			CreatePin(EEdGraphPinDirection::EGPD_Output, NAME_None, OutLabel);
		}
	}
}

void UPCGEditorGraphNodeInput::ReconstructNode()
{
	Super::ReconstructNode();
	//TODO: Implement special version for input to avoid the enum type
}

#undef LOCTEXT_NAMESPACE
