// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimationStateMachineLibrary.h"

#include "Animation/AnimNode_StateMachine.h"
#include "AnimNodes/AnimNode_StateResult.h"
#include "Animation/AnimInstanceProxy.h"

DEFINE_LOG_CATEGORY_STATIC(LogAnimationStateMachineLibrary, Verbose, All);

void UAnimationStateMachineLibrary::ConvertToAnimationStateResult(const FAnimNodeReference& Node, FAnimationStateResultReference& AnimationState, EAnimNodeReferenceConversionResult& Result)
{
	AnimationState = FAnimNodeReference::ConvertToType<FAnimationStateResultReference>(Node, Result);
}

bool UAnimationStateMachineLibrary::IsStateBlendingIn(const FAnimUpdateContext& UpdateContext, const FAnimationStateResultReference& Node)
{
	bool bResult = false;

	Node.CallAnimNodeFunction<FAnimNode_StateResult>(
		TEXT("IsStateBlendingIn"),
		[&UpdateContext, &bResult](FAnimNode_StateResult& StateResultNode)
		{
			if (const FAnimationUpdateContext* AnimationUpdateContext = UpdateContext.GetContext())
			{
				IAnimClassInterface* AnimBlueprintClass = AnimationUpdateContext->GetAnimClass();

				// Previous node to an FAnimNode_StateResult is always its owning FAnimNode_StateMachine
				const int32 MachineIndex =  AnimBlueprintClass->GetAnimNodeProperties().Num() - 1 - AnimationUpdateContext->GetPreviousNodeId();
				const int32 StateIndex = StateResultNode.GetStateIndex();

				const FAnimInstanceProxy* AnimInstanceProxy = AnimationUpdateContext->AnimInstanceProxy;
				if (const FAnimNode_StateMachine* MachineInstance = AnimInstanceProxy->GetStateMachineInstance(MachineIndex))
				{
					const int32 CurrentStateIndex = MachineInstance->GetCurrentState();

					const float StateWeight = AnimInstanceProxy->GetRecordedStateWeight(MachineInstance->StateMachineIndexInClass, StateIndex);
					if ((StateWeight < 1.0f) && (CurrentStateIndex == StateIndex))
					{
						bResult = true;
					}
				}
			}
		});

	return bResult;
}

bool UAnimationStateMachineLibrary::IsStateBlendingOut(const FAnimUpdateContext& UpdateContext, const FAnimationStateResultReference& Node)
{
	bool bResult = false;

	Node.CallAnimNodeFunction<FAnimNode_StateResult>(
		TEXT("IsStateBlendingOut"),
		[&UpdateContext, &bResult](FAnimNode_StateResult& StateResultNode)
		{
			if (const FAnimationUpdateContext* AnimationUpdateContext = UpdateContext.GetContext())
			{
				IAnimClassInterface* AnimBlueprintClass = AnimationUpdateContext->GetAnimClass();

				// Previous node to an FAnimNode_StateResult is always its owning FAnimNode_StateMachine
				const int32 MachineIndex = AnimBlueprintClass->GetAnimNodeProperties().Num() - 1 - AnimationUpdateContext->GetPreviousNodeId();
				const int32 StateIndex = StateResultNode.GetStateIndex();

				const FAnimInstanceProxy* AnimInstanceProxy = AnimationUpdateContext->AnimInstanceProxy;
				if (const FAnimNode_StateMachine* MachineInstance = AnimInstanceProxy->GetStateMachineInstance(MachineIndex))
				{
					const int32 CurrentStateIndex = MachineInstance->GetCurrentState();

					const float StateWeight = AnimInstanceProxy->GetRecordedStateWeight(MachineInstance->StateMachineIndexInClass, StateIndex);
					if ((StateWeight > 0.0f) && (CurrentStateIndex != StateIndex))
					{
						bResult = true;
					}
				}
			}
		});

	return bResult;
}
