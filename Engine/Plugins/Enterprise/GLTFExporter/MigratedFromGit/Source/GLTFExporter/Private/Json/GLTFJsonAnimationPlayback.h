// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonObject.h"

struct FGLTFJsonAnimationPlayback : IGLTFJsonObject
{
	FString Name;

	bool bLoop;
	bool bAutoPlay;

	float PlayRate;
	float StartTime;

	FGLTFJsonAnimationPlayback()
		: bLoop(true)
		, bAutoPlay(true)
		, PlayRate(1)
		, StartTime(0)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override
	{
		if (!Name.IsEmpty())
		{
			Writer.Write(TEXT("name"), Name);
		}

		if (bLoop != true)
		{
			Writer.Write(TEXT("loop"), bLoop);
		}

		if (bAutoPlay != true)
		{
			Writer.Write(TEXT("autoPlay"), bAutoPlay);
		}

		if (!FMath::IsNearlyEqual(PlayRate, 1))
		{
			Writer.Write(TEXT("playRate"), PlayRate);
		}

		if (!FMath::IsNearlyEqual(StartTime, 0))
		{
			Writer.Write(TEXT("startTime"), StartTime);
		}
	}

	bool operator==(const FGLTFJsonAnimationPlayback& Other) const
	{
		return bLoop == Other.bLoop
			&& bAutoPlay == Other.bAutoPlay
			&& PlayRate == Other.PlayRate
			&& StartTime == Other.StartTime;
	}

	bool operator!=(const FGLTFJsonAnimationPlayback& Other) const
	{
		return !(*this == Other);
	}
};
