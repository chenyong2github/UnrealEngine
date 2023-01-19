// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGEditorGraphNode.h"

#include "PCGEditorModule.h"
#include "PCGNode.h"

#include "Framework/Commands/GenericCommands.h"
#include "PCGPin.h"
#include "ToolMenu.h"
#include "ToolMenuSection.h"

#define LOCTEXT_NAMESPACE "PCGEditorGraphNode"

UPCGEditorGraphNode::UPCGEditorGraphNode(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCanRenameNode = true;
}

FText UPCGEditorGraphNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	constexpr int32 NodeTitleMaxSize = 70;

	if (PCGNode)
	{
		FString Res = PCGNode->GetNodeTitle().ToString();
		return FText::FromString((Res.Len() > NodeTitleMaxSize) ? Res.Left(NodeTitleMaxSize) : Res);
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
	if (PCGNode)
	{
		CreatePins(PCGNode->GetInputPins(), PCGNode->GetOutputPins());
	}
}

void UPCGEditorGraphNode::OnRenameNode(const FString& NewName)
{
	if (!GetCanRenameNode())
	{
		return;
	}

	if(NewName.Len() >= NAME_SIZE)
	{
		UE_LOG(LogPCGEditor, Error, TEXT("New name for PCG node is too long."));
		return;
	}

	if (!PCGNode)
	{
		return;
	}

	const FName TentativeName(*NewName);

	if (PCGNode->GetNodeTitle() != TentativeName)
	{
		PCGNode->Modify();
		PCGNode->NodeTitle = TentativeName;
	}
}

#undef LOCTEXT_NAMESPACE
