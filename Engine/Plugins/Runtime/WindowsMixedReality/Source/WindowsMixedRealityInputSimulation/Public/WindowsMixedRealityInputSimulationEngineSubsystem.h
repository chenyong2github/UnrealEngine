// Copyright (c) Microsoft Corporation. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "IMotionController.h"
#include "Subsystems/EngineSubsystem.h"
#include "WindowsMixedRealityFunctionLibrary.h"
#include "WindowsMixedRealityHandTrackingTypes.h"
#include "Containers/StaticBitArray.h"
#include "Containers/StaticArray.h"

#include "WindowsMixedRealityInputSimulationEngineSubsystem.generated.h"

struct WINDOWSMIXEDREALITYINPUTSIMULATION_API FWindowsMixedRealityInputSimulationPointerPose
{
	FVector Origin = FVector::ZeroVector;
	FVector Direction = FVector::ForwardVector;
	FVector Up = FVector::UpVector;
	FQuat Orientation = FQuat::Identity;
};

/** Simulated data for one hand. */
struct WINDOWSMIXEDREALITYINPUTSIMULATION_API FWindowsMixedRealityInputSimulationHandState
{
	typedef TStaticArray<FTransform, (uint32)EWMRHandKeypointCount> KeypointTransformArray;
	typedef TStaticArray<float, (uint32)EWMRHandKeypointCount> KeypointRadiusArray;
	typedef TStaticBitArray<uint32(EHMDInputControllerButtons::Count)> ButtonStateArray;

	ETrackingStatus TrackingStatus = ETrackingStatus::NotTracked;

	bool bHasJointPoses = false;
	KeypointTransformArray KeypointTransforms = KeypointTransformArray(FTransform::Identity);
	KeypointRadiusArray KeypointRadii = KeypointRadiusArray(1.0f);

	ButtonStateArray IsButtonPressed = ButtonStateArray();
	ButtonStateArray PrevButtonPressed = ButtonStateArray();

	bool bHasPointerPose = false;
	FWindowsMixedRealityInputSimulationPointerPose PointerPose;
};

/** Engine subsystem that stores input simulation data for access by the XR device. */
UCLASS(ClassGroup = WindowsMixedReality)
class WINDOWSMIXEDREALITYINPUTSIMULATION_API UWindowsMixedRealityInputSimulationEngineSubsystem
	: public UEngineSubsystem
{
	GENERATED_BODY()

public:

	/** Checks both the user settings and HMD availability to determine if input simulation is enabled.
	 * Note: The engine subsystem is created before the XRSystem, so it may exist even if a HMD is connected
	 *       and input simulation is ultimately not used at runtime.
	 */
	static bool IsInputSimulationEnabled();

	/** Utility function to ensure input simulation is only used when enabled.
	 * Returns the engine subsystem if it exists and if input simulation is enabled.
	 */
	static UWindowsMixedRealityInputSimulationEngineSubsystem* GetInputSimulationIfEnabled();
		
	/** True if the HMD has a valid head position vector. */
	bool HasPositionalTracking() const;
	/** Orientation of the HMD. */
	const FQuat& GetHeadOrientation() const;
	/** Position of the HMD if positional tracking is available. */
	const FVector& GetHeadPosition() const;
		
	/** Current tracking status of a hand. */
	ETrackingStatus GetControllerTrackingStatus(EControllerHand Hand) const;

	/** Get the transform of a hand joint.
	 *  Returns true if the output transform is valid.
	 */
	bool GetHandJointTransform(EControllerHand Hand, EWMRHandKeypoint Keypoint, FTransform& OutTransform) const;
	/** Get the radius of a hand joint.
	 *  Returns true if the output radius is valid.
	 */
	bool GetHandJointRadius(EControllerHand Hand, EWMRHandKeypoint Keypoint, float& OutRadius) const;
	
	/** True if joint transforms are available for a hand. */
	bool HasJointPoses(EControllerHand Hand) const;

	/** State of buttons and gestures of the hand controller.
	 * If OnlyRegisterClicks is true only changes from unpressed to pressed will be registered.
	 * Returns true only if the button state is valid.
	 */
	bool GetPressState(EControllerHand Hand, EHMDInputControllerButtons Button, bool OnlyRegisterClicks, bool& OutPressState) const;

	/** Get the pointer pose for a hand.
	 */
	bool GetHandPointerPose(EControllerHand Hand, FWindowsMixedRealityInputSimulationPointerPose& OutPointerPose) const;

	/** Update the simulated data. */
	void UpdateSimulatedData(
		bool HasTracking,
		const FQuat& NewHeadOrientation,
		const FVector& NewHeadPosition,
		const FWindowsMixedRealityInputSimulationHandState& LeftHandState,
		const FWindowsMixedRealityInputSimulationHandState& RightHandState);

	//
	// USubsystem implementation

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:

	/** Utility function for getting the state of a hand. */
	FWindowsMixedRealityInputSimulationHandState* GetHandState(EControllerHand Hand);
	/** Utility function for getting the state of a hand. */
	const FWindowsMixedRealityInputSimulationHandState* GetHandState(EControllerHand Hand) const;

	/** Utility function for updating the state of a hand. */
	void UpdateSimulatedHandState(EControllerHand Hand, const FWindowsMixedRealityInputSimulationHandState& NewHandState);

private:

	bool bHasPositionalTracking = false;
	FQuat HeadOrientation = FQuat::Identity;
	FVector HeadPosition = FVector::ZeroVector;

	FWindowsMixedRealityInputSimulationHandState HandStates[2];

	/** Lock for accessing simulated data */
	mutable FRWLock DataMutex;

};
