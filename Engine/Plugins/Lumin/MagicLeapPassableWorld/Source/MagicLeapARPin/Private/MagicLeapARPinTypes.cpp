// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapARPinTypes.h"

FMagicLeapARPinState::FMagicLeapARPinState()
: FMagicLeapARPinState(0.0f, 0.0f, 0.0f, 0.0f)
{}

FMagicLeapARPinState::FMagicLeapARPinState(float InConfidence, float InValidRadius, float InRotationError, float InTranslationError)
: Confidence(InConfidence)
, ValidRadius(InValidRadius)
, RotationError(InRotationError)
, TranslationError(InTranslationError)
{}

FString FMagicLeapARPinState::ToString() const
{
	return FString::Printf(TEXT("Confidence: %0.2f, ValidRadius: %0.2f, RotErr: %0.2f, TransErr: %0.2f"), Confidence, ValidRadius, RotationError, TranslationError);
}
