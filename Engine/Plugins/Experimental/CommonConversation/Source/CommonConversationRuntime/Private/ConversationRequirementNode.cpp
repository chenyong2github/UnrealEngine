// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConversationRequirementNode.h"

EConversationRequirementResult MergeRequirements(EConversationRequirementResult CurrentResult, EConversationRequirementResult MergeResult)
{
	if ((int64)MergeResult > (int64)CurrentResult)
	{
		return MergeResult;
	}

	return CurrentResult;
}

EConversationRequirementResult UConversationRequirementNode::IsRequirementSatisfied_Implementation(const FConversationContext& Context) const
{
	return EConversationRequirementResult::FailedAndHidden;
}
