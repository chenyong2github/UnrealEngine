// Copyright Epic Games, Inc. All Rights Reserved.

#include "SequencePlayerLibrary.h"
#include "Animation/AnimNodeContext.h"
#include "Animation/AnimNode_SequencePlayer.h"

DEFINE_LOG_CATEGORY_STATIC(LogSequencePlayerLibrary, Verbose, All);

void USequencePlayerLibrary::SetAccumulatedTime(const FAnimNodeContext& NodeContext, float Time)
{
	if(FAnimNode_SequencePlayer* SequencePlayer = NodeContext.GetAnimNode<FAnimNode_SequencePlayer>())
	{
		SequencePlayer->SetAccumulatedTime(Time);
	}
	else
	{
		UE_LOG(LogSequencePlayerLibrary, Warning, TEXT("SetAccumulatedTime called on a non-sequence player node"));
	}
}

void USequencePlayerLibrary::SetStartPosition(const FAnimNodeContext& NodeContext, float StartPosition)
{
	if(FAnimNode_SequencePlayer* SequencePlayer = NodeContext.GetAnimNode<FAnimNode_SequencePlayer>())
	{
		if(!SequencePlayer->SetStartPosition(StartPosition))
		{
			UE_LOG(LogSequencePlayerLibrary, Warning, TEXT("Could not set start time on sequence player, value is not dynamic. Set it as Always Dynamic or expose it as a pin"));
		}
	}
	else
	{
		UE_LOG(LogSequencePlayerLibrary, Warning, TEXT("SetStartTime called on a non-sequence player node"));
	}
}

void USequencePlayerLibrary::SetPlayRate(const FAnimNodeContext& NodeContext, float PlayRate)
{
	if(FAnimNode_SequencePlayer* SequencePlayer = NodeContext.GetAnimNode<FAnimNode_SequencePlayer>())
	{
		if(!SequencePlayer->SetPlayRate(PlayRate))
		{
			UE_LOG(LogSequencePlayerLibrary, Warning, TEXT("Could not set start time on sequence player, value is not dynamic. Set it as Always Dynamic or expose it as a pin"));
		}
	}
	else
	{
		UE_LOG(LogSequencePlayerLibrary, Warning, TEXT("SetStartTime called on a non-sequence player node"));
	}
}