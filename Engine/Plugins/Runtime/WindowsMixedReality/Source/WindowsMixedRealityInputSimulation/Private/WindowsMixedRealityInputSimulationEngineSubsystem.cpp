// Copyright (c) Microsoft Corporation. All rights reserved.

#include "WindowsMixedRealityInputSimulationEngineSubsystem.h"
#include "WindowsMixedRealityRuntimeSettings.h"

#include "IHeadMountedDisplay.h"
#include "IXRTrackingSystem.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/GameModeBase.h"
#include "Misc/ScopeLock.h"

#if WITH_EDITOR
#include "Editor/EditorEngine.h"
#endif

#define LOCTEXT_NAMESPACE "WindowsMixedRealityInputSimulation"

UDEPRECATED_WindowsMixedRealityInputSimulationEngineSubsystem* UDEPRECATED_WindowsMixedRealityInputSimulationEngineSubsystem::GetInputSimulationIfEnabled()
{
	if (!UDEPRECATED_WindowsMixedRealityInputSimulationEngineSubsystem::IsInputSimulationEnabled())
	{
		return nullptr;
	}

	return GEngine->GetEngineSubsystem<UDEPRECATED_WindowsMixedRealityInputSimulationEngineSubsystem>();
}

bool UDEPRECATED_WindowsMixedRealityInputSimulationEngineSubsystem::IsInputSimulationEnabled()
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

	// Only if playing inside the editor viewport
	if (GIsEditor)
	{
		UEditorEngine* EdEngine = Cast<UEditorEngine>(GEngine);

		if (EdEngine->GetPlayInEditorSessionInfo().IsSet())
		{
			// If we are in a VRPreview we are remoting or connected to wmr, and don't want simulated input.			
			return EdEngine->GetPlayInEditorSessionInfo()->OriginalRequestParams.SessionPreviewTypeOverride != EPlaySessionPreviewType::VRPreview;
		}
		else
		{
			return false;
		}

	}
#endif

	return false;
}

bool UDEPRECATED_WindowsMixedRealityInputSimulationEngineSubsystem::HasPositionalTracking() const
{
	FRWScopeLock ReadLock(DataMutex, SLT_ReadOnly);
	return bHasPositionalTracking;
}

const FQuat& UDEPRECATED_WindowsMixedRealityInputSimulationEngineSubsystem::GetHeadOrientation() const
{
	FRWScopeLock ReadLock(DataMutex, SLT_ReadOnly);
	return HeadOrientation;
}

const FVector& UDEPRECATED_WindowsMixedRealityInputSimulationEngineSubsystem::GetHeadPosition() const
{
	FRWScopeLock ReadLock(DataMutex, SLT_ReadOnly);
	return HeadPosition;
}

FWindowsMixedRealityInputSimulationHandState* UDEPRECATED_WindowsMixedRealityInputSimulationEngineSubsystem::GetHandState(EControllerHand Hand)
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

const FWindowsMixedRealityInputSimulationHandState* UDEPRECATED_WindowsMixedRealityInputSimulationEngineSubsystem::GetHandState(EControllerHand Hand) const
{
	return const_cast<UDEPRECATED_WindowsMixedRealityInputSimulationEngineSubsystem*>(this)->GetHandState(Hand);
}

ETrackingStatus UDEPRECATED_WindowsMixedRealityInputSimulationEngineSubsystem::GetControllerTrackingStatus(EControllerHand Hand) const
{
	FRWScopeLock ReadLock(DataMutex, SLT_ReadOnly);
	if (const FWindowsMixedRealityInputSimulationHandState* handState = GetHandState(Hand))
	{
		return handState->TrackingStatus;
	}
	return ETrackingStatus::NotTracked;
}

bool UDEPRECATED_WindowsMixedRealityInputSimulationEngineSubsystem::GetHandJointTransform(EControllerHand Hand, EWMRHandKeypoint Keypoint, FTransform& OutTransform) const
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

bool UDEPRECATED_WindowsMixedRealityInputSimulationEngineSubsystem::GetHandJointRadius(EControllerHand Hand, EWMRHandKeypoint Keypoint, float& OutRadius) const
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

bool UDEPRECATED_WindowsMixedRealityInputSimulationEngineSubsystem::HasJointPoses(EControllerHand Hand) const
{
	FRWScopeLock ReadLock(DataMutex, SLT_ReadOnly);
	if (const FWindowsMixedRealityInputSimulationHandState* handState = GetHandState(Hand))
	{
		return handState->bHasJointPoses;
	}
	return false;
}

bool UDEPRECATED_WindowsMixedRealityInputSimulationEngineSubsystem::GetPressState(EControllerHand Hand, EHMDInputControllerButtons Button, bool OnlyRegisterClicks, bool& OutPressState) const
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

bool UDEPRECATED_WindowsMixedRealityInputSimulationEngineSubsystem::GetHandPointerPose(EControllerHand Hand, FWindowsMixedRealityInputSimulationPointerPose& OutPointerPose) const
{
	FRWScopeLock ReadLock(DataMutex, SLT_ReadOnly);
	if (const FWindowsMixedRealityInputSimulationHandState* handState = GetHandState(Hand))
	{
		if (handState->bHasPointerPose)
		{
			OutPointerPose = handState->PointerPose;
			return true;
		}
	}
	return false;
}

void UDEPRECATED_WindowsMixedRealityInputSimulationEngineSubsystem::UpdateSimulatedData(
	bool HasTracking,
	const FQuat& NewHeadOrientation,
	const FVector& NewHeadPosition,
	const FWindowsMixedRealityInputSimulationHandState& LeftHandState,
	const FWindowsMixedRealityInputSimulationHandState& RightHandState)
{
	if (IsInputSimulationEnabled())
	{
		FRWScopeLock WriteLock(DataMutex, SLT_Write);

		bHasPositionalTracking = HasTracking;
		HeadOrientation = NewHeadOrientation;
		HeadPosition = NewHeadPosition;

		UpdateSimulatedHandState(EControllerHand::Left, LeftHandState);
		UpdateSimulatedHandState(EControllerHand::Right, RightHandState);
	}
}

void UDEPRECATED_WindowsMixedRealityInputSimulationEngineSubsystem::UpdateSimulatedHandState(
	EControllerHand Hand,
	const FWindowsMixedRealityInputSimulationHandState& NewHandState)
{
	FWindowsMixedRealityInputSimulationHandState* HandStatePtr = GetHandState(Hand);
	check(HandStatePtr);

	FWindowsMixedRealityInputSimulationHandState::ButtonStateArray WasButtonPressed(HandStatePtr->IsButtonPressed);
	*HandStatePtr = NewHandState;
	HandStatePtr->PrevButtonPressed = WasButtonPressed;
}

bool UDEPRECATED_WindowsMixedRealityInputSimulationEngineSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Enable input simulation only in the editor
#if WITH_EDITOR
	return true;
#else
	return false;
#endif
}

void UDEPRECATED_WindowsMixedRealityInputSimulationEngineSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
}

void UDEPRECATED_WindowsMixedRealityInputSimulationEngineSubsystem::Deinitialize()
{
}

#undef LOCTEXT_NAMESPACE
