// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConversationSideEffectNode.h"
#include "ConversationInstance.h"

void UConversationSideEffectNode::CauseSideEffect(const FConversationContext& Context) const
{
	if (Context.IsServerContext())
	{
		ServerCauseSideEffect(Context);
	}

	if (Context.IsClientContext())
	{
		ClientCauseSideEffect(Context);
	}
}

void UConversationSideEffectNode::ServerCauseSideEffect_Implementation(const FConversationContext& Context) const
{
}

void UConversationSideEffectNode::ClientCauseSideEffect_Implementation(const FConversationContext& Context) const
{
}