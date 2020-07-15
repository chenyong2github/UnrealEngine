// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundEditorGraphNode_Root.h"

#include "ToolMenus.h"
#include "GraphEditorSettings.h"
#include "MetasoundEditorCommands.h"


#define LOCTEXT_NAMESPACE "MetasoundGraphNode_Root"

UMetasoundEditorGraphNode_Root::UMetasoundEditorGraphNode_Root(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FLinearColor UMetasoundEditorGraphNode_Root::GetNodeTitleColor() const
{
	return GetDefault<UGraphEditorSettings>()->ResultNodeTitleColor;
}

FText UMetasoundEditorGraphNode_Root::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("RootTitle", "Output");
}

FText UMetasoundEditorGraphNode_Root::GetTooltipText() const
{
	return LOCTEXT("RootToolTip", "Wire the final Sound Node into this node");
}

void UMetasoundEditorGraphNode_Root::CreateInputPins()
{
	// TODO: Implement input pin creation from Metasound doc data
	CreatePin(EGPD_Input, TEXT("MetasoundEditorGraphNode"), TEXT("Root"), NAME_None);
}

void UMetasoundEditorGraphNode_Root::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	// TODO: Implement menu actions
}
#undef LOCTEXT_NAMESPACE
