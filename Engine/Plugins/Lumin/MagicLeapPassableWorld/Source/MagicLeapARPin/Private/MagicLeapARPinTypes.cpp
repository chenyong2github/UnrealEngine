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
, PinType(EMagicLeapARPinType::SingleUserMultiSession)
{}

FString FMagicLeapARPinState::ToString() const
{
	FString EnumString("Invalid");

	const UEnum* EnumPtr = FindObject<UEnum>(ANY_PACKAGE, TEXT("EMagicLeapARPinType"), true);
	if (EnumPtr)
	{
		EnumString = EnumPtr->GetNameStringByIndex(static_cast<int32>(PinType));
	}

	return FString::Printf(TEXT("Confidence: %0.2f, ValidRadius: %0.2f, RotErr: %0.2f, TransErr: %0.2f, Type = %s"), Confidence, ValidRadius, RotationError, TranslationError, *EnumString);
}

FMagicLeapARPinQuery::FMagicLeapARPinQuery()
: Types( { EMagicLeapARPinType::SingleUserSingleSession, EMagicLeapARPinType::SingleUserMultiSession, EMagicLeapARPinType::MultiUserMultiSession })
, MaxResults(-1)
, TargetPoint(FVector::ZeroVector)
, Radius(0.0f)
, bSorted(false)
{}
