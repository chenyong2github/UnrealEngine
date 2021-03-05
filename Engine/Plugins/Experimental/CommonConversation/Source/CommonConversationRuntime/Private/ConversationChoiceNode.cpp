// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConversationChoiceNode.h"

bool UConversationChoiceNode::GenerateChoice(const FConversationContext& Context, FClientConversationOptionEntry& ChoiceEntry) const
{
	FillChoice(Context, ChoiceEntry);
	return true;
}

void UConversationChoiceNode::FillChoice_Implementation(const FConversationContext& Context, FClientConversationOptionEntry& ChoiceEntry) const
{
	ChoiceEntry.ChoiceText = DefaultChoiceDisplayText;
	ChoiceEntry.ChoiceTags = ChoiceTags;
}
