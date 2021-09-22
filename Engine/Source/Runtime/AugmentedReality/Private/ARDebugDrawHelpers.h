// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/LargeWorldCoordinates.h"

class UWorld;
UE_DECLARE_LWC_TYPE(Vector, 3);
class FString;
struct FColor;

namespace ARDebugHelpers
{
	void DrawDebugString(const UWorld* InWorld, FVector const& TextLocation, const FString& Text, float Scale, FColor const& TextColor, float Duration, bool bDrawShadow);
}
