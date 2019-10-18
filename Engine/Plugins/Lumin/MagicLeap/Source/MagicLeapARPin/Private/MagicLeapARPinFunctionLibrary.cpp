// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MagicLeapARPinFunctionLibrary.h"
#include "IMagicLeapARPinModule.h"

EMagicLeapPassableWorldError UMagicLeapARPinFunctionLibrary::CreateTracker()
{
	return IMagicLeapARPinModule::Get().CreateTracker();
}

EMagicLeapPassableWorldError UMagicLeapARPinFunctionLibrary::DestroyTracker()
{
	return IMagicLeapARPinModule::Get().DestroyTracker();
}

bool UMagicLeapARPinFunctionLibrary::IsTrackerValid()
{
	return IMagicLeapARPinModule::Get().IsTrackerValid();
}

EMagicLeapPassableWorldError UMagicLeapARPinFunctionLibrary::GetNumAvailableARPins(int32& Count)
{
	return IMagicLeapARPinModule::Get().GetNumAvailableARPins(Count);
}

EMagicLeapPassableWorldError UMagicLeapARPinFunctionLibrary::GetAvailableARPins(int32 NumRequested, TArray<FGuid>& Pins)
{
	if (NumRequested <= 0)
	{
		GetNumAvailableARPins(NumRequested);
	}

	if (NumRequested == 0)
	{
		// There are no coordinate frames to return, so this call did succeed without any errors, it just returned an array of size 0.
		Pins.Reset();
		return EMagicLeapPassableWorldError::None;
	}

	return IMagicLeapARPinModule::Get().GetAvailableARPins(NumRequested, Pins);
}

EMagicLeapPassableWorldError UMagicLeapARPinFunctionLibrary::GetClosestARPin(const FVector& SearchPoint, FGuid& PinID)
{
	return IMagicLeapARPinModule::Get().GetClosestARPin(SearchPoint, PinID);
}

bool UMagicLeapARPinFunctionLibrary::GetARPinPositionAndOrientation(const FGuid& PinID, FVector& Position, FRotator& Orientation, bool& PinFoundInEnvironment)
{
	return IMagicLeapARPinModule::Get().GetARPinPositionAndOrientation(PinID, Position, Orientation, PinFoundInEnvironment);
}
