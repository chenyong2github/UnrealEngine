// Copyright Epic Games, Inc. All Rights Reserved.

#include "SequenceEvaluatorLibrary.h"

#include "Animation/AnimNode_Inertialization.h"
#include "AnimNodes/AnimNode_SequenceEvaluator.h"

DEFINE_LOG_CATEGORY_STATIC(LogSequenceEvaluatorLibrary, Verbose, All);

FSequenceEvaluatorReference USequenceEvaluatorLibrary::ConvertToSequenceEvaluator(const FAnimNodeReference& Node, EAnimNodeReferenceConversionResult& Result)
{
	return FAnimNodeReference::ConvertToType<FSequenceEvaluatorReference>(Node, Result);
}

FSequenceEvaluatorReference USequenceEvaluatorLibrary::SetExplicitTime(const FSequenceEvaluatorReference& SequenceEvaluator, float Time)
{
	SequenceEvaluator.CallAnimNodeFunction<FAnimNode_SequenceEvaluator>(
		TEXT("SetExplicitTime"),
		[Time](FAnimNode_SequenceEvaluator& InSequenceEvaluator)
		{
			if(!InSequenceEvaluator.SetExplicitTime(Time))
			{
				UE_LOG(LogSequenceEvaluatorLibrary, Warning, TEXT("Could not set explicit time on sequence evaluator, value is not dynamic. Set it as Always Dynamic."));
			}
		});

	return SequenceEvaluator;
}

FSequenceEvaluatorReference USequenceEvaluatorLibrary::SetSequence(const FSequenceEvaluatorReference& SequenceEvaluator, UAnimSequenceBase* Sequence)
{
	SequenceEvaluator.CallAnimNodeFunction<FAnimNode_SequenceEvaluator>(
		TEXT("SetSequence"),
		[Sequence](FAnimNode_SequenceEvaluator& InSequenceEvaluator)
		{
			if(!InSequenceEvaluator.SetSequence(Sequence))
			{
				UE_LOG(LogSequenceEvaluatorLibrary, Warning, TEXT("Could not set sequence on sequence evaluator, value is not dynamic. Set it as Always Dynamic."));
			}
		});

	return SequenceEvaluator;
}

FSequenceEvaluatorReference USequenceEvaluatorLibrary::SetSequenceWithInertialBlending(const FAnimUpdateContext& UpdateContext, const FSequenceEvaluatorReference& SequenceEvaluator, UAnimSequenceBase* Sequence, float BlendTime)
{
	SequenceEvaluator.CallAnimNodeFunction<FAnimNode_SequenceEvaluator>(
		TEXT("SetSequenceWithInterialBlending"),
		[Sequence, &UpdateContext, BlendTime](FAnimNode_SequenceEvaluator& InSequenceEvaluator)
		{
			const UAnimSequenceBase* CurrentSequence = InSequenceEvaluator.GetSequence();
			const bool bAnimSequenceChanged = (CurrentSequence != Sequence);
			
			if(!InSequenceEvaluator.SetSequence(Sequence))
			{
				UE_LOG(LogSequenceEvaluatorLibrary, Warning, TEXT("Could not set sequence on sequence evaluator, value is not dynamic. Set it as Always Dynamic."));
			}

			if(bAnimSequenceChanged && BlendTime > 0.0f)
			{
				if (const FAnimationUpdateContext* AnimationUpdateContext = UpdateContext.GetContext())
				{
					if (UE::Anim::IInertializationRequester* InertializationRequester = AnimationUpdateContext->GetMessage<UE::Anim::IInertializationRequester>())
					{
						InertializationRequester->RequestInertialization(BlendTime);
					}
				}
				else
				{
					UE_LOG(LogSequenceEvaluatorLibrary, Warning, TEXT("SetSequenceWithInterialBlending called with invalid context"));
				}
			}
		});

	return SequenceEvaluator;
}
