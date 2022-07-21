// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ConstraintChannel.h"
#include "ConstraintChannelHelper.h"

#include "Channels/MovieSceneChannelData.h"

#include "Algo/Unique.h"

template<typename ChannelType>
void FConstraintChannelHelper::GetFramesToCompensate(
	const FMovieSceneConstraintChannel& InActiveChannel,
	const bool InActiveValueToBeSet,
	const FFrameNumber& InTime,
	const TArrayView<ChannelType*>& InChannels,
	TArray<FFrameNumber>& OutFramesAfter)
{
	using ChannelValueType = typename ChannelType::ChannelValueType;

	const bool bHasKeys = (InActiveChannel.GetNumKeys() > 0);
	
	OutFramesAfter.Reset();

	// add the current frame
	OutFramesAfter.Add(InTime);

	// add the next frames that need transform compensation 
	for (const ChannelType* InChannel: InChannels)
	{
		const TMovieSceneChannelData<const ChannelValueType> ChannelData = InChannel->GetData();
		const TArrayView<const FFrameNumber> Times = ChannelData.GetTimes();
		if (!Times.IsEmpty())
		{
			// look for the first next key frame for this channel 
			const int32 NextTimeIndex = Algo::UpperBound(Times, InTime);
			if (Times.IsValidIndex(NextTimeIndex))
			{
				// store the time while the state is different
				for (int32 Index = NextTimeIndex; Index < Times.Num(); ++Index)
				{
					if (!bHasKeys)
					{
						OutFramesAfter.Add(Times[Index]);
					}
					else
					{
						bool NextValue = false; InActiveChannel.Evaluate(Times[Index], NextValue);
						if (NextValue == InActiveValueToBeSet)
						{
							break;
						}
						OutFramesAfter.Add(Times[Index]);
					}
				}
			}
		}
	}

	// uniqueness
	OutFramesAfter.Sort();
	OutFramesAfter.SetNum(Algo::Unique(OutFramesAfter));
}

template< typename ChannelType >
void FConstraintChannelHelper::GetFramesAfter(
	const FMovieSceneConstraintChannel& InActiveChannel,
	const FFrameNumber& InTime,
	const TArrayView<ChannelType*>& InChannels,
	TArray<FFrameNumber>& OutFrames)
{
	using ChannelValueType = typename ChannelType::ChannelValueType;
	
	OutFrames.Reset();
	
	const TMovieSceneChannelData<const bool> ConstraintChannelData = InActiveChannel.GetData();
	const int32 KeyIndex = ConstraintChannelData.FindKey(InTime);
	if (!ConstraintChannelData.GetTimes().IsValidIndex(KeyIndex))
	{
		return;
	}

	const bool CurrentValue = ConstraintChannelData.GetValues()[KeyIndex];
	
	// compute last frame to compensate
	auto GetEndOfCompensationTime = [KeyIndex](const TMovieSceneChannelData<const bool>& InData)
	{
		const TArrayView<const bool> Values = InData.GetValues();
		const TArrayView<const FFrameNumber> Times = InData.GetTimes();

		const bool CurrentValue = Values[KeyIndex];
		for (int32 NextIndex = KeyIndex+1; NextIndex < Times.Num(); ++NextIndex)
		{
			if (Values[NextIndex] != CurrentValue)
			{
				return TOptional<FFrameNumber>(Times[NextIndex]);
			}
		}
		return TOptional<FFrameNumber>();
	};
	const TOptional<FFrameNumber> EndOfCompensationTime = GetEndOfCompensationTime(ConstraintChannelData);

	const bool bHasEndTime = EndOfCompensationTime.IsSet();
	
	// add the current frame
	OutFrames.Add(InTime);

	// add the next frames that need transform compensation 
	for (const ChannelType* InChannel: InChannels)
	{
		const TMovieSceneChannelData<const ChannelValueType> ChannelData = InChannel->GetData();
		const TArrayView<const FFrameNumber> Times = ChannelData.GetTimes();
		if (!Times.IsEmpty())
		{
			// look for the first next key frame for this channel 
			const int32 NextTimeIndex = Algo::UpperBound(Times, InTime);
			if (Times.IsValidIndex(NextTimeIndex))
			{
				// store the time while the state is different
				for (int32 Index = NextTimeIndex; Index < Times.Num(); ++Index)
				{
					if (!bHasEndTime || Times[Index] < EndOfCompensationTime.GetValue() )
					{
						OutFrames.Add(Times[Index]);
					}
				}
			}
		}
	}

	// uniqueness
	OutFrames.Sort();
	OutFrames.SetNum(Algo::Unique(OutFrames));
}

template< typename ChannelType >
void FConstraintChannelHelper::GetFramesWithinActiveState(
	const FMovieSceneConstraintChannel& InActiveChannel,
	const TArrayView<ChannelType*>& InChannels,
	TArray<FFrameNumber>& OutFrames)
{
	using ChannelValueType = typename ChannelType::ChannelValueType;
	
	OutFrames.Reset();
	
	const TMovieSceneChannelData<const bool> ConstraintChannelData = InActiveChannel.GetData();
	const TArrayView<const FFrameNumber>& ActiveTimes = ConstraintChannelData.GetTimes();
	if (ActiveTimes.IsEmpty())
	{
		return;
	}

	const FFrameNumber& FirstTime = ActiveTimes[0];
	const FFrameNumber& LastTime = ActiveTimes.Last();
	
	// add active times
	OutFrames.Append(ActiveTimes);

	const bool bIsLastStateInactive = ConstraintChannelData.GetValues().Last() == false; 

	// add frames where the constraint is active 
	for (const ChannelType* InChannel: InChannels)
	{
		const TMovieSceneChannelData<const ChannelValueType> ChannelData = InChannel->GetData();
		const TArrayView<const FFrameNumber> Times = ChannelData.GetTimes();
		if (!Times.IsEmpty())
		{
			// look for the first next key frame for this channel 
			const int32 NextTimeIndex = Algo::UpperBound(Times,FirstTime);
			if (Times.IsValidIndex(NextTimeIndex))
			{
				// store the time is the state is active
				for (int32 Index = NextTimeIndex; Index < Times.Num(); ++Index)
				{
					bool bIsActive = false;
					InActiveChannel.Evaluate(Times[Index], bIsActive);
					if (bIsActive)
					{
						OutFrames.Add(Times[Index]);
					}

					if (bIsLastStateInactive && Times[Index] > LastTime)
					{
						break;
					}
				}
			}
		}
	}

	// uniqueness
	OutFrames.Sort();
	OutFrames.SetNum(Algo::Unique(OutFrames));
}

template< typename ChannelType >
void FConstraintChannelHelper::MoveTransformKeys(
	const TArrayView<ChannelType*>& InChannels,
	const FFrameNumber& InCurrentTime,
	const FFrameNumber& InNextTime)
{
	const FFrameNumber Delta = InNextTime - InCurrentTime;
	if (Delta == 0)
	{
		return;
	}
	
	for (ChannelType* Channel: InChannels)
	{
		TMovieSceneChannelData<typename ChannelType::ChannelValueType> Data = Channel->GetData();
		const TArrayView<const FFrameNumber> Times = Data.GetTimes();
		const int32 NumTimes = Times.Num();

		if (Delta > 0) //if we are moving keys positively in time we start from end frames and move them so we can use indices
		{
			for (int32 KeyIndex = NumTimes - 1; KeyIndex >= 0; --KeyIndex)
			{
				const FFrameNumber& Frame = Times[KeyIndex];
				const FFrameNumber AbsDiff = FMath::Abs(Frame - InCurrentTime);
				if (AbsDiff<= 1)
				{
					Data.MoveKey(KeyIndex, Frame + Delta);
				}
			}
		}
		else
		{
			for (int32 KeyIndex = 0; KeyIndex < NumTimes; ++KeyIndex)
			{
				const FFrameNumber& Frame = Times[KeyIndex];
				const FFrameNumber AbsDiff = FMath::Abs( Frame - InCurrentTime);
				if (AbsDiff <= 1)
				{
					Data.MoveKey(KeyIndex, Frame + Delta);
				}
			}
		}
	}
}

template< typename ChannelType >
void FConstraintChannelHelper::DeleteTransformKeys(
	const TArrayView<ChannelType*>& InChannels,
	const FFrameNumber& InTime)
{
	for (ChannelType* Channel: InChannels)
	{
		TMovieSceneChannelData<typename ChannelType::ChannelValueType> Data = Channel->GetData();
		const TArrayView<const FFrameNumber> Times = Data.GetTimes();
		
		const int32 KeyIndex = Algo::UpperBound(Times, InTime);
		if (Times.IsValidIndex(KeyIndex))
		{
			Data.RemoveKey(KeyIndex);
		}
	}
}