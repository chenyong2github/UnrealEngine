// Copyright (c) Microsoft Corporation. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Subsystems/EngineSubsystem.h"
#include "WindowsMixedRealityHandTrackingTypes.h"

#include "WindowsMixedRealityInputSimulationEngineSubsystem.generated.h"

UENUM()
enum class EInputSimulationControllerButtons : uint8
{
	Select,
	Grasp,
	Menu,
	Thumbstick,
	Touchpad,
	TouchpadIsTouched
};

const int32 EInputSimulationControllerButtonsCount = static_cast<int32>(EInputSimulationControllerButtons::TouchpadIsTouched) + 1;

/** Engine subsystem that stores input simulation data for access by the XR device. */
UCLASS(ClassGroup = WindowsMixedReality)
class WINDOWSMIXEDREALITYINPUTSIMULATION_API UWindowsMixedRealityInputSimulationEngineSubsystem
	: public UEngineSubsystem
{
	GENERATED_BODY()

public:

	struct HandState
	{
		bool IsValid = false;
		
		FTransform KeypointTransforms[EWMRHandKeypointCount];
		float KeypointRadii[EWMRHandKeypointCount];
		
		bool IsButtonPressed[EInputSimulationControllerButtonsCount] = { false };
		bool PrevButtonPressed[EInputSimulationControllerButtonsCount] = { false };
	};

	bool GetHandJointTransform(EControllerHand Hand, EWMRHandKeypoint Keypoint, FTransform& OutTransform) const;
	bool GetHandJointRadius(EControllerHand Hand, EWMRHandKeypoint Keypoint, float& OutRadius) const;
	void SetHandJointOrientationAndPosition(EControllerHand Hand, EWMRHandKeypoint Keypoint, const FRotator& Orientation, const FVector& Position);
	void SetHandJointRadius(EControllerHand Hand, EWMRHandKeypoint Keypoint, float Radius);
	
	bool IsHandStateValid(EControllerHand Hand) const;
	void SetHandStateValid(EControllerHand Hand, bool IsValid);

	void SetPressState(EControllerHand Hand, EInputSimulationControllerButtons Button, bool IsPressed);
	bool GetPressState(EControllerHand Hand, EInputSimulationControllerButtons Button, bool OnlyRegisterClicks, bool& OutPressState) const;

	/** Checks both the user settings and HMD availability to determine if input simulation is enabled.
	 * Note: The engine subsystem is created before the XRSystem, so it may exist even if a HMD is connected
	 *       and input simulation is ultimately not used at runtime.
	 */
	bool IsInputSimulationEnabled() const;

	//
	// USubsystem implementation

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:

	HandState* GetHandState(EControllerHand Hand);
	const HandState* GetHandState(EControllerHand Hand) const;

private:

	HandState HandStates[2];

	/** Critical section for reading/writing hand states. */
	mutable FCriticalSection HandStatesCriticalSection;

};
