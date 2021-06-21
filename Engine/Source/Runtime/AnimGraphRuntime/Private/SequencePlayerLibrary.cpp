// Copyright Epic Games, Inc. All Rights Reserved.

#include "SequencePlayerLibrary.h"
#include "Animation/AnimNode_SequencePlayer.h"

FSequencePlayerReference USequencePlayerLibrary::ConvertToSequencePlayerContext(const FAnimNodeReference& Node, EAnimNodeReferenceConversionResult& Result)
{
	return FAnimNodeReference::ConvertToType<FSequencePlayerReference>(Node, Result);
}

FSequencePlayerReference USequencePlayerLibrary::SetAccumulatedTime(const FSequencePlayerReference& SequencePlayerContext, float Time)
{
	SequencePlayerContext.CallAnimNodeFunction<FAnimNode_SequencePlayer>(
		TEXT("SetAccumulatedTime"),
		[Time](FAnimNode_SequencePlayer& SequencePlayer)
		{
			SequencePlayer.SetAccumulatedTime(Time);
		});

	return SequencePlayerContext;
}

FSequencePlayerReference USequencePlayerLibrary::SetStartPosition(const FSequencePlayerReference& SequencePlayerContext, float StartPosition)
{
	SequencePlayerContext.CallAnimNodeFunction<FAnimNode_SequencePlayer>(
		TEXT("SetStartPosition"),
		[StartPosition](FAnimNode_SequencePlayer& SequencePlayer)
		{
			SequencePlayer.SetStartPosition(StartPosition);
		});

	return SequencePlayerContext;
}

FSequencePlayerReference USequencePlayerLibrary::SetPlayRate(const FSequencePlayerReference& SequencePlayerContext, float PlayRate)
{
	SequencePlayerContext.CallAnimNodeFunction<FAnimNode_SequencePlayer>(
		TEXT("SetPlayRate"),
		[PlayRate](FAnimNode_SequencePlayer& SequencePlayer)
		{
			SequencePlayer.SetPlayRate(PlayRate);
		});

	return SequencePlayerContext;
}