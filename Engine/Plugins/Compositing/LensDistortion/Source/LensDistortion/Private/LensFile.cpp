// Copyright Epic Games, Inc. All Rights Reserved.


#include "LensFile.h"

#include "LensInterpolationUtils.h"


ULensFile::ULensFile()
{
}

bool ULensFile::EvaluateDistortionParameters(float InFocus, float InZoom, FDistortionParameters& OutEvaluatedValue)
{
	if (DistortionMapping.Num() <= 0)
	{
		return false;
	}

	if (DistortionMapping.Num() == 1)
	{
		OutEvaluatedValue = DistortionMapping[0].Parameters;
		return true;
	}

	FDistortionMapPoint InterpPoint;
	const bool bSuccess = LensInterpolationUtils::FIZMappingBilinearInterpolation<FDistortionMapPoint>(InFocus, InZoom, DistortionMapping, InterpPoint);
	if (bSuccess)
	{
		OutEvaluatedValue = MoveTemp(InterpPoint.Parameters);
	}

	return bSuccess;
}

bool ULensFile::EvaluateIntrinsicParameters(float InFocus, float InZoom, FIntrinsicParameters& OutEvaluatedValue)
{
	if (IntrinsicMapping.Num() <= 0)
	{
		return false;
	}

	if (IntrinsicMapping.Num() == 1)
	{
		OutEvaluatedValue = IntrinsicMapping[0].Parameters;
		return true;
	}

	FIntrinsicMapPoint InterpPoint;
	const bool bSuccess = LensInterpolationUtils::FIZMappingBilinearInterpolation<FIntrinsicMapPoint>(InFocus, InZoom, IntrinsicMapping, InterpPoint);
	if (bSuccess)
	{
		OutEvaluatedValue = MoveTemp(InterpPoint.Parameters);
	}

	return bSuccess;
}

bool ULensFile::EvaluateNodalPointOffset(float InFocus, float InZoom, FNodalPointOffset& OutEvaluatedValue)
{
	if (NodalOffsetMapping.Num() <= 0)
	{
		return false;
	}

	if (NodalOffsetMapping.Num() == 1)
	{
		OutEvaluatedValue = NodalOffsetMapping[0].NodalOffset;
		return true;
	}

	FNodalOffsetMapPoint InterpPoint;
	const bool bSuccess = LensInterpolationUtils::FIZMappingBilinearInterpolation<FNodalOffsetMapPoint>(InFocus, InZoom, NodalOffsetMapping, InterpPoint);
	if (bSuccess)
	{
		OutEvaluatedValue = MoveTemp(InterpPoint.NodalOffset);
	}

	return bSuccess;
}

bool ULensFile::HasFocusEncoderMapping() const
{
	return EncoderMapping.Focus.Num() > 0;
}

bool ULensFile::EvaluateNormalizedFocus(float InNormalizedValue, float& OutEvaluatedValue)
{
	/***TEMP TEMP TEMP*******/
	/** Once there is a UI + methods to add encoder points we can get rid of that. */
	TArray<FEncoderPoint> CopiedSorted = EncoderMapping.Focus;
	CopiedSorted.Sort([](const FEncoderPoint& LHS, const FEncoderPoint& RHS) { return LHS.NormalizedValue > RHS.NormalizedValue; });
	/************************/

	return LensInterpolationUtils::InterpolateEncoderValue(InNormalizedValue, CopiedSorted, OutEvaluatedValue);
}

bool ULensFile::HasIrisEncoderMapping() const
{
	return EncoderMapping.Iris.Num() > 0;

}

float ULensFile::EvaluateNormalizedIris(float InNormalizedValue, float& OutEvaluatedValue)
{
	/***TEMP TEMP TEMP*******/
	/** Once there is a UI + methods to add encoder points we can get rid of that. */
	TArray<FEncoderPoint> CopiedSorted = EncoderMapping.Iris;
	CopiedSorted.Sort([](const FEncoderPoint& LHS, const FEncoderPoint& RHS) { return LHS.NormalizedValue > RHS.NormalizedValue; });
	/************************/

	return LensInterpolationUtils::InterpolateEncoderValue(InNormalizedValue, CopiedSorted, OutEvaluatedValue);
}

bool ULensFile::HasZoomEncoderMapping() const
{
	return EncoderMapping.Zoom.Num() > 0;
}

float ULensFile::EvaluateNormalizedZoom(float InNormalizedValue, float& OutEvaluatedValue)
{
	/***TEMP TEMP TEMP*******/
	/** Once there is a UI + methods to add encoder points we can get rid of that. */
	TArray<FEncoderPoint> CopiedSorted = EncoderMapping.Zoom;
	CopiedSorted.Sort([](const FEncoderPoint& LHS, const FEncoderPoint& RHS) { return LHS.NormalizedValue > RHS.NormalizedValue; });
	/************************/

	return LensInterpolationUtils::InterpolateEncoderValue(InNormalizedValue, CopiedSorted, OutEvaluatedValue);
}
