// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "EdGraphSchema_NiagaraSystemOverview.h"
#include "EdGraphNode_Comment.h"

#define LOCTEXT_NAMESPACE "NiagaraSchema"

UEdGraphSchema_NiagaraSystemOverview::UEdGraphSchema_NiagaraSystemOverview(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UEdGraphSchema_NiagaraSystemOverview::GetContextMenuActions(const UEdGraph* CurrentGraph, const UEdGraphNode* InGraphNode, const UEdGraphPin* InGraphPin, class FMenuBuilder* MenuBuilder, bool bIsDebugging) const
{
	//@TODO System Overview: enable/disable emitter, solo, etc. 
	return;
}

#undef LOCTEXT_NAMESPACE
