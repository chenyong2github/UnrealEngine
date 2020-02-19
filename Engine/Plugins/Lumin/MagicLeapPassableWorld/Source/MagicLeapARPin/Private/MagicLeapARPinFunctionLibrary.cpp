// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapARPinFunctionLibrary.h"
#include "IMagicLeapARPinFeature.h"

EMagicLeapPassableWorldError UMagicLeapARPinFunctionLibrary::CreateTracker()
{
	IMagicLeapARPinFeature* ARPinImpl = IMagicLeapARPinFeature::Get();
	return (ARPinImpl != nullptr) ? ARPinImpl->CreateTracker() : EMagicLeapPassableWorldError::NotImplemented;
}

EMagicLeapPassableWorldError UMagicLeapARPinFunctionLibrary::DestroyTracker()
{
	IMagicLeapARPinFeature* ARPinImpl = IMagicLeapARPinFeature::Get();
	return (ARPinImpl != nullptr) ? ARPinImpl->DestroyTracker() : EMagicLeapPassableWorldError::NotImplemented;
}

bool UMagicLeapARPinFunctionLibrary::IsTrackerValid()
{
	IMagicLeapARPinFeature* ARPinImpl = IMagicLeapARPinFeature::Get();
	return (ARPinImpl != nullptr) ? ARPinImpl->IsTrackerValid() : false;
}

EMagicLeapPassableWorldError UMagicLeapARPinFunctionLibrary::GetNumAvailableARPins(int32& Count)
{
	Count = 0;
	IMagicLeapARPinFeature* ARPinImpl = IMagicLeapARPinFeature::Get();
	return (ARPinImpl != nullptr) ? ARPinImpl->GetNumAvailableARPins(Count) : EMagicLeapPassableWorldError::NotImplemented;
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

	IMagicLeapARPinFeature* ARPinImpl = IMagicLeapARPinFeature::Get();
	return (ARPinImpl != nullptr) ? ARPinImpl->GetAvailableARPins(NumRequested, Pins) : EMagicLeapPassableWorldError::NotImplemented;
}

EMagicLeapPassableWorldError UMagicLeapARPinFunctionLibrary::GetClosestARPin(const FVector& SearchPoint, FGuid& PinID)
{
	IMagicLeapARPinFeature* ARPinImpl = IMagicLeapARPinFeature::Get();
	return (ARPinImpl != nullptr) ? ARPinImpl->GetClosestARPin(SearchPoint, PinID) : EMagicLeapPassableWorldError::NotImplemented;
}

bool UMagicLeapARPinFunctionLibrary::GetARPinPositionAndOrientation_TrackingSpace(const FGuid& PinID, FVector& Position, FRotator& Orientation, bool& PinFoundInEnvironment)
{
	PinFoundInEnvironment = false;
	IMagicLeapARPinFeature* ARPinImpl = IMagicLeapARPinFeature::Get();
	return (ARPinImpl != nullptr) ? ARPinImpl->GetARPinPositionAndOrientation_TrackingSpace(PinID, Position, Orientation, PinFoundInEnvironment) : false;
}

bool UMagicLeapARPinFunctionLibrary::GetARPinPositionAndOrientation(const FGuid& PinID, FVector& Position, FRotator& Orientation, bool& PinFoundInEnvironment)
{
	PinFoundInEnvironment = false;
	IMagicLeapARPinFeature* ARPinImpl = IMagicLeapARPinFeature::Get();
	return (ARPinImpl != nullptr) ? ARPinImpl->GetARPinPositionAndOrientation(PinID, Position, Orientation, PinFoundInEnvironment) : false;
}

EMagicLeapPassableWorldError UMagicLeapARPinFunctionLibrary::GetARPinState(const FGuid& PinID, FMagicLeapARPinState& State)
{
	IMagicLeapARPinFeature* ARPinImpl = IMagicLeapARPinFeature::Get();
	return (ARPinImpl != nullptr) ? ARPinImpl->GetARPinState(PinID, State) : EMagicLeapPassableWorldError::NotImplemented;
}

FString UMagicLeapARPinFunctionLibrary::GetARPinStateToString(const FMagicLeapARPinState& State)
{
	return State.ToString();
}

void UMagicLeapARPinFunctionLibrary::BindToOnMagicLeapARPinUpdatedDelegate(const FMagicLeapARPinUpdatedDelegate& Delegate)
{
	IMagicLeapARPinFeature* ARPinImpl = IMagicLeapARPinFeature::Get();
	if (ARPinImpl != nullptr)
	{
		ARPinImpl->BindToOnMagicLeapARPinUpdatedDelegate(Delegate);
	}
}

void UMagicLeapARPinFunctionLibrary::UnBindToOnMagicLeapARPinUpdatedDelegate(const FMagicLeapARPinUpdatedDelegate& Delegate)
{
	IMagicLeapARPinFeature* ARPinImpl = IMagicLeapARPinFeature::Get();
	if (ARPinImpl != nullptr)
	{
		ARPinImpl->UnBindToOnMagicLeapARPinUpdatedDelegate(Delegate);
	}
}
