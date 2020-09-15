// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundBop.h"

#include "MetasoundDataReference.h"
#include "MetasoundDataTypeRegistrationMacro.h"
#include "MetasoundOperatorSettings.h"

namespace Metasound
{
	FBop::FBop(bool bShouldBop, const FOperatorSettings& InSettings)
	:	NumFramesPerBlock(InSettings.GetNumFramesPerBlock())
	,	SampleRate(InSettings.GetSampleRate())
	{
		if (bShouldBop)
		{
			BopFrame(0);
		}
	}

	FBop::FBop(int32 InFrameToBop, const FOperatorSettings& InSettings)
	:	NumFramesPerBlock(InSettings.GetNumFramesPerBlock())
	,	SampleRate(InSettings.GetSampleRate())
	{
		BopFrame(InFrameToBop);
	}

	FBop::FBop(const FOperatorSettings& InSettings)
		: FBop(false, InSettings)
	{}

	void FBop::BopFrame(int32 InFrameToBop)
	{
		// Insert bop frame index into sorted bopped frames
		BoppedFrames.Insert(InFrameToBop, Algo::LowerBound(BoppedFrames, InFrameToBop));

		if (InFrameToBop < NumFramesPerBlock)
		{
			// Update last bop pos. 
			UpdateLastBopIndexInBlock();
		}

		bHasBop = true;
	}


	void FBop::AdvanceBlock()
	{
		Advance(NumFramesPerBlock);
	}

	void FBop::Advance(int32 InNumFrames)
	{
		if (bHasBop)
		{
			const int32 NumToRemove = Algo::LowerBound(BoppedFrames, InNumFrames);

			if (NumToRemove)
			{
				BoppedFrames.RemoveAt(0, NumToRemove, false /* bAllowShrinking */);
			}

			const int32 Num = BoppedFrames.Num();

			if (Num > 0)
			{
				for (int32 i = 0; i < Num; i++)
				{
					BoppedFrames[i] -= InNumFrames;
				}
			}
			else
			{
				bHasBop = false;
			}

			UpdateLastBopIndexInBlock();
		}
	}

	int32 FBop::Num() const
	{
		return BoppedFrames.Num();
	}

	int32 FBop::operator[](int32 InBopIndex) const
	{
		return BoppedFrames[InBopIndex];
	}

	bool FBop::IsBopped() const
	{
		return bHasBop;
	}

	bool FBop::IsBoppedInBlock() const
	{
		return LastBopIndexInBlock > 0;
	}

	FBop::operator bool() const 
	{
		return IsBoppedInBlock();
	}

	void FBop::Reset()
	{
		BoppedFrames.Reset();
		LastBopIndexInBlock = INDEX_NONE;
		bHasBop = false;
	}

	void FBop::UpdateLastBopIndexInBlock()
	{
		LastBopIndexInBlock = Algo::LowerBound(BoppedFrames, NumFramesPerBlock);
	}
}
