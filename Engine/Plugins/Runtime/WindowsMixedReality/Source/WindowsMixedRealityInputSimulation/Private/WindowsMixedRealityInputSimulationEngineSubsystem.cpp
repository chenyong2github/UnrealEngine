// Copyright (c) Microsoft Corporation. All rights reserved.

#include "WindowsMixedRealityInputSimulationEngineSubsystem.h"
#include "WindowsMixedRealityRuntimeSettings.h"

#include "IHeadMountedDisplay.h"
#include "IXRTrackingSystem.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/GameModeBase.h"
#include "Misc/ScopeLock.h"

#define LOCTEXT_NAMESPACE "WindowsMixedRealityInputSimulation"

UWindowsMixedRealityInputSimulationEngineSubsystem::HandState* UWindowsMixedRealityInputSimulationEngineSubsystem::GetHandState(EControllerHand Hand)
{
	if (Hand == EControllerHand::Left)
	{
		return &HandStates[0];
	}
	if (Hand == EControllerHand::Right)
	{
		return &HandStates[1];
	}
	return nullptr;
}

const UWindowsMixedRealityInputSimulationEngineSubsystem::HandState* UWindowsMixedRealityInputSimulationEngineSubsystem::GetHandState(EControllerHand Hand) const
{
	return const_cast<UWindowsMixedRealityInputSimulationEngineSubsystem*>(this)->GetHandState(Hand);
}

bool UWindowsMixedRealityInputSimulationEngineSubsystem::GetHandJointTransform(EControllerHand Hand, EWMRHandKeypoint Keypoint, FTransform& OutTransform) const
{
	FScopeLock Lock(&HandStatesCriticalSection);
	if (const HandState* handState = GetHandState(Hand))
	{
		if (handState->IsValid)
		{
			OutTransform = handState->KeypointTransforms[(int32)Keypoint];
			return true;
		}
	}
	return false;
}

bool UWindowsMixedRealityInputSimulationEngineSubsystem::GetHandJointRadius(EControllerHand Hand, EWMRHandKeypoint Keypoint, float& OutRadius) const
{
	FScopeLock Lock(&HandStatesCriticalSection);
	if (const HandState* handState = GetHandState(Hand))
	{
		if (handState->IsValid)
		{
			OutRadius = handState->KeypointRadii[(int32)Keypoint];
			return true;
		}
	}
	return false;
}

void UWindowsMixedRealityInputSimulationEngineSubsystem::SetHandJointOrientationAndPosition(EControllerHand Hand, EWMRHandKeypoint Keypoint, const FRotator& Orientation, const FVector& Position)
{
	FScopeLock Lock(&HandStatesCriticalSection);
	if (HandState* handState = GetHandState(Hand))
	{
		handState->KeypointTransforms[(int32)Keypoint].SetComponents(Orientation.Quaternion(), Position, FVector::OneVector);
	}
}

void UWindowsMixedRealityInputSimulationEngineSubsystem::SetHandJointRadius(EControllerHand Hand, EWMRHandKeypoint Keypoint, float Radius)
{
	FScopeLock Lock(&HandStatesCriticalSection);
	if (HandState* handState = GetHandState(Hand))
	{
		handState->KeypointRadii[(int32)Keypoint] = Radius;
	}
}

bool UWindowsMixedRealityInputSimulationEngineSubsystem::IsHandStateValid(EControllerHand Hand) const
{
	FScopeLock Lock(&HandStatesCriticalSection);
	if (const HandState* handState = GetHandState(Hand))
	{
		return handState->IsValid;
	}
	return false;
}

void UWindowsMixedRealityInputSimulationEngineSubsystem::SetHandStateValid(EControllerHand Hand, bool IsValid)
{
	FScopeLock Lock(&HandStatesCriticalSection);
	if (HandState* handState = GetHandState(Hand))
	{
		handState->IsValid = IsValid;
	}
}

void UWindowsMixedRealityInputSimulationEngineSubsystem::SetPressState(EControllerHand Hand, EInputSimulationControllerButtons Button, bool IsPressed)
{
	FScopeLock Lock(&HandStatesCriticalSection);
	if (HandState* handState = GetHandState(Hand))
	{
		handState->PrevButtonPressed[(int32)Button] = handState->IsButtonPressed[(int32)Button];
		handState->IsButtonPressed[(int32)Button] = IsPressed;
	}
}

bool UWindowsMixedRealityInputSimulationEngineSubsystem::GetPressState(EControllerHand Hand, EInputSimulationControllerButtons Button, bool OnlyRegisterClicks, bool& OutPressState) const
{
	FScopeLock Lock(&HandStatesCriticalSection);
	if (const HandState* handState = GetHandState(Hand))
	{
		if (handState->IsValid)
		{
			bool IsPressed = handState->IsButtonPressed[(int32)Button];
			bool WasPressed = handState->PrevButtonPressed[(int32)Button];
			if (OnlyRegisterClicks)
			{
				OutPressState = IsPressed != WasPressed ? IsPressed : false;
			}
			else
			{
				OutPressState = IsPressed;
			}
			return true;
		}
	}
	return false;
}

bool UWindowsMixedRealityInputSimulationEngineSubsystem::IsInputSimulationEnabled() const
{
#if WITH_EDITOR
	// Only use input simulation when enabled
	const UWindowsMixedRealityRuntimeSettings* WMRSettings = UWindowsMixedRealityRuntimeSettings::Get();
	if (!WMRSettings->bEnableInputSimulation)
	{
		return false;
	}

	// Only if there is a XR system
	if (!GEngine->XRSystem.IsValid())
	{
		return false;
	}

	// Only if no HMD is connected
	if (GEngine->XRSystem.Get()->GetHMDDevice()->IsHMDConnected())
	{
		return false;
	}

	return true;
#else
	return false;
#endif
}

bool UWindowsMixedRealityInputSimulationEngineSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if WITH_EDITOR
	// Only use input simulation when enabled
	const UWindowsMixedRealityRuntimeSettings* WMRSettings = UWindowsMixedRealityRuntimeSettings::Get();
	if (!WMRSettings->bEnableInputSimulation)
	{
		return false;
	}

	// Note: XRSystem is created later than engine subsystem,
	// so cannot check if XRSystem exists or HMD is connected at this point.
	// IsInputSimulationEnabled can be used at runtime to check these additional conditions.

	return true;
#else
	return false;
#endif
}

void UWindowsMixedRealityInputSimulationEngineSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
}

void UWindowsMixedRealityInputSimulationEngineSubsystem::Deinitialize()
{
}

#undef LOCTEXT_NAMESPACE
