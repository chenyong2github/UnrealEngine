// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConversationGraphNode_SideEffect.h"
#include "ConversationEditorColors.h"

UConversationGraphNode_SideEffect::UConversationGraphNode_SideEffect(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bIsSubNode = true;
}

void UConversationGraphNode_SideEffect::AllocateDefaultPins()
{
	// No pins for side effects
}

FLinearColor UConversationGraphNode_SideEffect::GetNodeBodyTintColor() const
{
	return ConversationEditorColors::NodeBody::SideEffectColor;
}
