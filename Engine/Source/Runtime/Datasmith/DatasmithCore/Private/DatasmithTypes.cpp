// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithTypes.h"
#include "DatasmithDefinitions.h"
#include "Math/UnrealMathUtility.h"

FDatasmithTransformFrameInfo FDatasmithTransformFrameInfo::InvalidFrameInfo(MIN_int32, 0.f, 0.f, 0.f);

bool FDatasmithTransformFrameInfo::IsValid() const
{ 
	return FrameNumber != MIN_int32;
}

bool FDatasmithTransformFrameInfo::operator==(const FDatasmithTransformFrameInfo& Other) const
{
	return FMath::IsNearlyEqual(X, Other.X, KINDA_SMALL_NUMBER) &&
		   FMath::IsNearlyEqual(Y, Other.Y, KINDA_SMALL_NUMBER) &&
		   FMath::IsNearlyEqual(Z, Other.Z, KINDA_SMALL_NUMBER);
}

FDatasmithVisibilityFrameInfo FDatasmithVisibilityFrameInfo::InvalidFrameInfo(MIN_int32, false);

bool FDatasmithVisibilityFrameInfo::IsValid() const
{
	return FrameNumber != MIN_int32;
}

bool FDatasmithVisibilityFrameInfo::operator==(const FDatasmithVisibilityFrameInfo& Other) const
{
	return bVisible == Other.bVisible;
}
