// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGEditorGraphNodeOutput.h"

#include "PCGNode.h"

#define LOCTEXT_NAMESPACE "PCGEditorGraphNodeOutput"

FText UPCGEditorGraphNodeOutput::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return FText::FromName(TEXT("Output"));
}

void UPCGEditorGraphNodeOutput::AllocateDefaultPins()
{
	if (!PCGNode || PCGNode->HasDefaultInLabel())
	{
		CreatePin(EEdGraphPinDirection::EGPD_Input, NAME_None, FName(TEXT("In")));
	}

	if (PCGNode)
	{
		for (const FName& InLabel : PCGNode->InLabels())
		{
			CreatePin(EEdGraphPinDirection::EGPD_Input, NAME_None, InLabel);
		}
	}
}

void UPCGEditorGraphNodeOutput::ReconstructNode()
{
	Super::ReconstructNode();
	//TODO: Implement special version for output to avoid the enum type
}

#undef LOCTEXT_NAMESPACE
