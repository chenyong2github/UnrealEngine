// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "MagicLeapHandTrackingTypes.generated.h"

/*! Transforms that could be tracked on the hand. In 0.15.0 RC5 8 of them may be tracked.*/
UENUM(BlueprintType)
enum class EMagicLeapHandTrackingKeypoint : uint8
{
	Thumb_Tip, // SDK 0.21.0 tracked
	Thumb_IP,  // SDK 0.21.0 tracked
	Thumb_MCP, // SDK 0.21.0 tracked
	Thumb_CMC,

	Index_Tip, // SDK 0.21.0 tracked
	Index_DIP,
	Index_PIP, // SDK 0.21.0 tracked
	Index_MCP, // SDK 0.21.0 tracked

	Middle_Tip, // SDK 0.21.0 tracked
	Middle_DIP,
	Middle_PIP, // SDK 0.21.0 tracked
	Middle_MCP, // SDK 0.21.0 tracked

	Ring_Tip, // SDK 0.21.0 tracked
	Ring_DIP,
	Ring_PIP,
	Ring_MCP, // SDK 0.21.0 tracked

	Pinky_Tip, // SDK 0.21.0 tracked
	Pinky_DIP,
	Pinky_PIP,
	Pinky_MCP, // SDK 0.21.0 tracked

	Wrist_Center, // SDK 0.21.0 tracked
	Wrist_Ulnar,
	Wrist_Radial,

	Hand_Center // SDK 0.21.0 tracked
};

const int32 EMagicLeapHandTrackingKeypointCount = static_cast<int32>(EMagicLeapHandTrackingKeypoint::Hand_Center) + 1;

/*! Static key pose types which are available when both hands are separated. */
UENUM(BlueprintType)
enum class EMagicLeapHandTrackingGesture : uint8
{
	/** One finger. */
	Finger,
	/** A closed fist. */
	Fist,
	/** A pinch. */
	Pinch,
	/** A closed fist with the thumb pointed up. */
	Thumb,
	/** An L shape. */
	L,
	/** An open palm of the hand facing the user. */
	OpenHand,
	/** DEPRECATED (USE OpenHand INSTEAD) - A back open hand of the hand facing away from the user. */
	OpenHandBack = EMagicLeapHandTrackingGesture::OpenHand,
	/** A pinch with all fingers, except the index finger and the thumb, extended out. */
	Ok,
	/** A rounded 'C' alphabet shape. */
	C,
	/** No pose detected. */
	NoPose,
	/** No hand was present. */
	NoHand
};

/** Filtering for the keypoints and hand centers. */
UENUM(BlueprintType)
enum class EMagicLeapHandTrackingKeypointFilterLevel : uint8
{
	/** No filtering is done, the points are raw. */
	NoFilter,
	/** Some smoothing at the cost of latency. */
	SimpleSmoothing,
	/** Predictive smoothing, at higher cost of latency. */
	PredictiveSmoothing
};

/** Filtering for the gesture recognition and hand switching. */
UENUM(BlueprintType)
enum class EMagicLeapHandTrackingGestureFilterLevel : uint8
{
	/** No filtering is done, the gestures are raw. */
	NoFilter,
	/** Some robustness to flicker at some cost of latency. */
	SlightRobustnessToFlicker,
	/** More robust to flicker at higher latency cost. */
	MoreRobustnessToFlicker
};

/** Gesture key point transform spaces. */
UENUM(BlueprintType)
enum class EMagicLeapGestureTransformSpace : uint8
{
	/** Gesture key point transforms are reported in Unreal world space. This is more costly on CPU.*/
	World,
	/** Gesture key point transforms are reported in gesture hand center space.*/
	Hand,
	/** Gesture key point transforms are reported in device Tracking space. */
	Tracking
};

/** List of input key names for all left and right hand gestures. */
struct FMagicLeapGestureKeyNames
{
	static const FName Left_Finger_Name;
	static const FName Left_Fist_Name;
	static const FName Left_Pinch_Name;
	static const FName Left_Thumb_Name;
	static const FName Left_L_Name;
	static const FName Left_OpenHand_Name;
	// DEPRECATED
	static const FName Left_OpenHandBack_Name;
	static const FName Left_Ok_Name;
	static const FName Left_C_Name;
	static const FName Left_NoPose_Name;
	static const FName Left_NoHand_Name;

	static const FName Right_Finger_Name;
	static const FName Right_Fist_Name;
	static const FName Right_Pinch_Name;
	static const FName Right_Thumb_Name;
	static const FName Right_L_Name;
	static const FName Right_OpenHand_Name;
	// DEPRECATED
	static const FName Right_OpenHandBack_Name;
	static const FName Right_Ok_Name;
	static const FName Right_C_Name;
	static const FName Right_NoPose_Name;
	static const FName Right_NoHand_Name;
};
