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

UWindowsMixedRealityInputSimulationEngineSubsystem* UWindowsMixedRealityInputSimulationEngineSubsystem::GetInputSimulationIfEnabled()
{
	if (!UWindowsMixedRealityInputSimulationEngineSubsystem::IsInputSimulationEnabled())
	{
		return nullptr;
	}

	return GEngine->GetEngineSubsystem<UWindowsMixedRealityInputSimulationEngineSubsystem>();
}

bool UWindowsMixedRealityInputSimulationEngineSubsystem::IsInputSimulationEnabled()
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

bool UWindowsMixedRealityInputSimulationEngineSubsystem::HasPositionalTracking() const
{
	FRWScopeLock ReadLock(DataMutex, SLT_ReadOnly);
	return bHasPositionalTracking;
}

const FQuat& UWindowsMixedRealityInputSimulationEngineSubsystem::GetHeadOrientation() const
{
	FRWScopeLock ReadLock(DataMutex, SLT_ReadOnly);
	return HeadOrientation;
}

const FVector& UWindowsMixedRealityInputSimulationEngineSubsystem::GetHeadPosition() const
{
	FRWScopeLock ReadLock(DataMutex, SLT_ReadOnly);
	return HeadPosition;
}

FWindowsMixedRealityInputSimulationHandState* UWindowsMixedRealityInputSimulationEngineSubsystem::GetHandState(EControllerHand Hand)
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

const FWindowsMixedRealityInputSimulationHandState* UWindowsMixedRealityInputSimulationEngineSubsystem::GetHandState(EControllerHand Hand) const
{
	return const_cast<UWindowsMixedRealityInputSimulationEngineSubsystem*>(this)->GetHandState(Hand);
}

ETrackingStatus UWindowsMixedRealityInputSimulationEngineSubsystem::GetControllerTrackingStatus(EControllerHand Hand) const
{
	FRWScopeLock ReadLock(DataMutex, SLT_ReadOnly);
	if (const FWindowsMixedRealityInputSimulationHandState* handState = GetHandState(Hand))
	{
		return handState->TrackingStatus;
	}
	return ETrackingStatus::NotTracked;
}

bool UWindowsMixedRealityInputSimulationEngineSubsystem::GetHandJointTransform(EControllerHand Hand, EWMRHandKeypoint Keypoint, FTransform& OutTransform) const
{
	FRWScopeLock ReadLock(DataMutex, SLT_ReadOnly);
	if (const FWindowsMixedRealityInputSimulationHandState* handState = GetHandState(Hand))
	{
		if (handState->bHasJointPoses)
		{
			OutTransform = handState->KeypointTransforms[(int32)Keypoint];
			return true;
		}
	}
	return false;
}

bool UWindowsMixedRealityInputSimulationEngineSubsystem::GetHandJointRadius(EControllerHand Hand, EWMRHandKeypoint Keypoint, float& OutRadius) const
{
	FRWScopeLock ReadLock(DataMutex, SLT_ReadOnly);
	if (const FWindowsMixedRealityInputSimulationHandState* handState = GetHandState(Hand))
	{
		if (handState->bHasJointPoses)
		{
			OutRadius = handState->KeypointRadii[(int32)Keypoint];
			return true;
		}
	}
	return false;
}

bool UWindowsMixedRealityInputSimulationEngineSubsystem::HasJointPoses(EControllerHand Hand) const
{
	FRWScopeLock ReadLock(DataMutex, SLT_ReadOnly);
	if (const FWindowsMixedRealityInputSimulationHandState* handState = GetHandState(Hand))
	{
		return handState->bHasJointPoses;
	}
	return false;
}

bool UWindowsMixedRealityInputSimulationEngineSubsystem::GetPressState(EControllerHand Hand, EHMDInputControllerButtons Button, bool OnlyRegisterClicks, bool& OutPressState) const
{
	FRWScopeLock ReadLock(DataMutex, SLT_ReadOnly);
	if (const FWindowsMixedRealityInputSimulationHandState* handState = GetHandState(Hand))
	{
		bool IsPressed = handState->IsButtonPressed[(int32)Button];
		bool WasPressed = handState->PrevButtonPressed[(int32)Button];

		OutPressState = IsPressed;

		if (OnlyRegisterClicks)
		{
			return IsPressed != WasPressed;
		}
		else
		{
			return true;
		}
	}
	return false;
}

void UWindowsMixedRealityInputSimulationEngineSubsystem::UpdateSimulatedData(
	bool HasTracking,
	const FQuat& NewHeadOrientation,
	const FVector& NewHeadPosition,
	const FWindowsMixedRealityInputSimulationHandState& LeftHandState,
	const FWindowsMixedRealityInputSimulationHandState& RightHandState)
{
	FRWScopeLock WriteLock(DataMutex, SLT_Write);

	bHasPositionalTracking = HasTracking;
	HeadOrientation = NewHeadOrientation;
	HeadPosition = NewHeadPosition;

	UpdateSimulatedHandState(EControllerHand::Left, LeftHandState);
	UpdateSimulatedHandState(EControllerHand::Right, RightHandState);
}

void UWindowsMixedRealityInputSimulationEngineSubsystem::UpdateSimulatedHandState(
	EControllerHand Hand,
	const FWindowsMixedRealityInputSimulationHandState& NewHandState)
{
	FWindowsMixedRealityInputSimulationHandState* HandStatePtr = GetHandState(Hand);
	check(HandStatePtr);

	FWindowsMixedRealityInputSimulationHandState::ButtonStateArray WasButtonPressed(HandStatePtr->IsButtonPressed);
	*HandStatePtr = NewHandState;
	HandStatePtr->PrevButtonPressed = WasButtonPressed;
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
