// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimTypes.h"

struct ICompressedAnimData;

/* Encapsulates decompression related data used by bone compression codecs. */
struct FAnimSequenceDecompressionContext
{
	FAnimSequenceDecompressionContext(float SequenceLength_, EAnimInterpolationType Interpolation_, const FName& AnimName_, const ICompressedAnimData& CompressedAnimData_)
		: SequenceLength(SequenceLength_), Interpolation(Interpolation_), AnimName(AnimName_), CompressedAnimData(CompressedAnimData_), Time(0.f)
	{}

	// Anim info
	float SequenceLength;
	EAnimInterpolationType Interpolation;
	FName AnimName;

	const ICompressedAnimData& CompressedAnimData;
	float Time;
	float RelativePos;

	void Seek(float SampleAtTime)
	{
		Time = SampleAtTime;
		RelativePos = SampleAtTime / SequenceLength;
	}
};
