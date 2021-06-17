// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNodeContextLibrary.h"
#include "Animation/AnimNodeContext.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimNode_LinkedAnimGraph.h"

DEFINE_LOG_CATEGORY_STATIC(LogAnimNodeContextLibrary, Verbose, All);

UAnimInstance* UAnimNodeContextLibrary::GetAnimInstance(const FAnimNodeContext& NodeContext)
{
	return CastChecked<UAnimInstance>(NodeContext.GetContext()->GetAnimInstanceObject());
}

bool UAnimNodeContextLibrary::HasLinkedAnimInstance(const FAnimNodeContext& NodeContext)
{
	if(FAnimNode_LinkedAnimGraph* LinkedAnimGraphNode = NodeContext.GetAnimNode<FAnimNode_LinkedAnimGraph>())
	{
		return LinkedAnimGraphNode->GetTargetInstance<UAnimInstance>() != nullptr;
	}
	return false;
}

UAnimInstance* UAnimNodeContextLibrary::GetLinkedAnimInstance(const FAnimNodeContext& NodeContext)
{
	if(FAnimNode_LinkedAnimGraph* LinkedAnimGraphNode = NodeContext.GetAnimNode<FAnimNode_LinkedAnimGraph>())
	{
		return LinkedAnimGraphNode->GetTargetInstance<UAnimInstance>();
	}
	return nullptr;
}