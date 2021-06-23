// Copyright Epic Games, Inc. All Rights Reserved.

#include "SequencePlayerLibrary.h"

#include "Animation/AnimNode_Inertialization.h"
#include "Animation/AnimNode_SequencePlayer.h"

DEFINE_LOG_CATEGORY_STATIC(LogSequencePlayerLibrary, Verbose, All);

FSequencePlayerReference USequencePlayerLibrary::ConvertToSequencePlayer(const FAnimNodeReference& Node, EAnimNodeReferenceConversionResult& Result)
{
	return FAnimNodeReference::ConvertToType<FSequencePlayerReference>(Node, Result);
}

FSequencePlayerReference USequencePlayerLibrary::SetAccumulatedTime(const FSequencePlayerReference& SequencePlayer, float Time)
{
	SequencePlayer.CallAnimNodeFunction<FAnimNode_SequencePlayer>(
		TEXT("SetAccumulatedTime"),
		[Time](FAnimNode_SequencePlayer& InSequencePlayer)
		{
			InSequencePlayer.SetAccumulatedTime(Time);
		});

	return SequencePlayer;
}

FSequencePlayerReference USequencePlayerLibrary::SetStartPosition(const FSequencePlayerReference& SequencePlayer, float StartPosition)
{
	SequencePlayer.CallAnimNodeFunction<FAnimNode_SequencePlayer>(
		TEXT("SetStartPosition"),
		[StartPosition](FAnimNode_SequencePlayer& InSequencePlayer)
		{
			if(!InSequencePlayer.SetStartPosition(StartPosition))
			{
				UE_LOG(LogSequencePlayerLibrary, Warning, TEXT("Could not set start position on sequence player, value is not dynamic. Set it as Always Dynamic."));
			}
		});

	return SequencePlayer;
}

FSequencePlayerReference USequencePlayerLibrary::SetPlayRate(const FSequencePlayerReference& SequencePlayer, float PlayRate)
{
	SequencePlayer.CallAnimNodeFunction<FAnimNode_SequencePlayer>(
		TEXT("SetPlayRate"),
		[PlayRate](FAnimNode_SequencePlayer& InSequencePlayer)
		{
			if(!InSequencePlayer.SetPlayRate(PlayRate))
			{
				UE_LOG(LogSequencePlayerLibrary, Warning, TEXT("Could not set play rate on sequence player, value is not dynamic. Set it as Always Dynamic."));
			}
		});

	return SequencePlayer;
}

FSequencePlayerReference USequencePlayerLibrary::SetSequence(const FSequencePlayerReference& SequencePlayer, UAnimSequenceBase* Sequence)
{
	SequencePlayer.CallAnimNodeFunction<FAnimNode_SequencePlayer>(
		TEXT("SetSequence"),
		[Sequence](FAnimNode_SequencePlayer& InSequencePlayer)
		{
			if(!InSequencePlayer.SetSequence(Sequence))
			{
				UE_LOG(LogSequencePlayerLibrary, Warning, TEXT("Could not set sequence on sequence player, value is not dynamic. Set it as Always Dynamic."));
			}
		});

	return SequencePlayer;
}


FSequencePlayerReference USequencePlayerLibrary::SetSequenceWithInertialBlending(const FAnimUpdateContext& UpdateContext, const FSequencePlayerReference& SequencePlayer, UAnimSequenceBase* Sequence, float BlendTime)
{
	SequencePlayer.CallAnimNodeFunction<FAnimNode_SequencePlayer>(
		TEXT("SetSequenceWithInterialBlending"),
		[Sequence, &UpdateContext, BlendTime](FAnimNode_SequencePlayer& InSequencePlayer)
		{
			const UAnimSequenceBase* CurrentSequence = InSequencePlayer.GetSequence();
			const bool bAnimSequenceChanged = (CurrentSequence != Sequence);
			
			if(!InSequencePlayer.SetSequence(Sequence))
			{
				UE_LOG(LogSequencePlayerLibrary, Warning, TEXT("Could not set sequence on sequence player, value is not dynamic. Set it as Always Dynamic."));
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
					UE_LOG(LogSequencePlayerLibrary, Warning, TEXT("SetSequenceWithInertialBlending called with invalid context"));
				}
			}
		});

	return SequencePlayer;
}