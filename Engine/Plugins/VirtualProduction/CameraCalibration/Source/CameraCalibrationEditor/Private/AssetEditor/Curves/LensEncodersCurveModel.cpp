// Copyright Epic Games, Inc. All Rights Reserved.

#include "LensEncodersCurveModel.h"

#include "LensFile.h"



FLensEncodersCurveModel::FLensEncodersCurveModel(ULensFile* InOwner, EEncoderType InEncoderType)
	: FLensDataCurveModel(InOwner)
	, EncoderType(InEncoderType)
{
	check(InOwner);

	if (EncoderType == EEncoderType::Focus)
	{
		CurrentCurve = LensFile->EncodersTable.Focus;
	}
	else
	{
		CurrentCurve = LensFile->EncodersTable.Iris;
	}

	bIsCurveValid = true;

	OnCurveModified().AddRaw(this, &FLensEncodersCurveModel::OnCurveModifiedCallback);
}

void FLensEncodersCurveModel::OnCurveModifiedCallback() const
{
	//Can definitely get optimized. This is a catch all to keep both curves aligned
	if (EncoderType == EEncoderType::Focus)
	{
		LensFile->EncodersTable.Focus = CurrentCurve;
	}
	else
	{
		LensFile->EncodersTable.Iris = CurrentCurve;
	}
}
