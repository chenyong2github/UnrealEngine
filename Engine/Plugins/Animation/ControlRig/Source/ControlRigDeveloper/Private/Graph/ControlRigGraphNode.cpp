// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/ControlRigGraphNode.h"
#include "Graph/ControlRigGraph.h"
#include "Graph/ControlRigGraphSchema.h"
#include "IControlRigEditorModule.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ControlRigGraphNode)

#define LOCTEXT_NAMESPACE "ControlRigGraphNode"

UControlRigGraphNode::UControlRigGraphNode()
: URigVMEdGraphNode()
{
}

void UControlRigGraphNode::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
#if WITH_EDITOR
	// todo - part of this is likely generic 
	const UControlRigGraphSchema* Schema = Cast<UControlRigGraphSchema>(GetSchema());
	IControlRigEditorModule::Get().GetContextMenuActions(Schema, Menu, Context);
#endif
}

#undef LOCTEXT_NAMESPACE

