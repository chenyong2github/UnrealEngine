// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGEditorGraphNode.h"

#include "PCGEditorGraphSchema.h"
#include "PCGNode.h"
#include "PCGSettings.h"

#include "Framework/Commands/GenericCommands.h"
#include "GraphEditorActions.h"
#include "ToolMenu.h"
#include "ToolMenuSection.h"

#define LOCTEXT_NAMESPACE "PCGEditorGraphNode"

FText UPCGEditorGraphNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (PCGNode)
	{
		return FText::FromName(PCGNode->GetNodeTitle());
	}
	else
	{
		return FText::FromName(TEXT("Unnamed node"));
	}
}

void UPCGEditorGraphNode::GetNodeContextMenuActions(UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const
{
	if (!Context->Node)
	{
		return;
	}

	FToolMenuSection& Section = Menu->AddSection("EdGraphSchemaGeneral", LOCTEXT("GeneralHeader", "General"));
	Section.AddMenuEntry(FGenericCommands::Get().Delete);
	Section.AddMenuEntry(FGenericCommands::Get().Cut);
	Section.AddMenuEntry(FGenericCommands::Get().Copy);
	Section.AddMenuEntry(FGenericCommands::Get().Duplicate);

	Super::GetNodeContextMenuActions(Menu, Context);
}

void UPCGEditorGraphNode::AllocateDefaultPins()
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

void UPCGEditorGraphNode::ReconstructNode()
{
	Super::ReconstructNode();
	//TODO: Implement special version for settings to avoid the enum type
}

void UPCGEditorGraphNode::OnRenameNode(const FString& NewName)
{
	FName TentativeName(NewName);

	if (GetCanRenameNode() && PCGNode && PCGNode->GetNodeTitle() != TentativeName)
	{
		PCGNode->Modify();
		PCGNode->NodeTitle = FName(NewName);
	}
}

#undef LOCTEXT_NAMESPACE
