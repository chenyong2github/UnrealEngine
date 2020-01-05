// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/NameTypes.h"
#include "UObject/NameTypes.h"
#include "InputCoreTypes.h"

/** Magic Leap motion sources */
struct FMagicLeapMotionSourceNames
{
	static const FName Control0;
	static const FName Control1;
	static const FName MobileApp;
	static const FName Unknown;
};

/** List of input key names for all keys specific to Magic Leap Controller and Mobile Companion App. */
struct FMagicLeapControllerKeyNames
{
	static const FName MotionController_Left_Thumbstick_Z_Name;
	static const FName Left_HomeButton_Name;
	static const FName Left_Bumper_Name;
	static const FName Left_Trigger_Name;
	static const FName Left_Trigger_Axis_Name;
	static const FName Left_Trackpad_X_Name;
	static const FName Left_Trackpad_Y_Name;
	static const FName Left_Trackpad_Force_Name;
	static const FName Left_Trackpad_Touch_Name;
	static const FName Left_Touch1_X_Name;
	static const FName Left_Touch1_Y_Name;
	static const FName Left_Touch1_Force_Name;
	static const FName Left_Touch1_Touch_Name;

	static const FName MotionController_Right_Thumbstick_Z_Name;
	static const FName Right_HomeButton_Name;
	static const FName Right_Bumper_Name;
	static const FName Right_Trigger_Name;
	static const FName Right_Trigger_Axis_Name;
	static const FName Right_Trackpad_X_Name;
	static const FName Right_Trackpad_Y_Name;
	static const FName Right_Trackpad_Force_Name;
	static const FName Right_Trackpad_Touch_Name;
	static const FName Right_Touch1_X_Name;
	static const FName Right_Touch1_Y_Name;
	static const FName Right_Touch1_Force_Name;
	static const FName Right_Touch1_Touch_Name;

	static const FName TouchpadGesture_Swipe_Up_Name;
	static const FName TouchpadGesture_Swipe_Down_Name;
	static const FName TouchpadGesture_Swipe_Right_Name;
	static const FName TouchpadGesture_Swipe_Left_Name;
	static const FName TouchpadGesture_RadialScroll_Clockwise_Name;
	static const FName TouchpadGesture_RadialScroll_CounterClockwise_Name;
	static const FName TouchpadGesture_Scroll_Up_Name;
	static const FName TouchpadGesture_Scroll_Down_Name;
	static const FName TouchpadGesture_Scroll_Right_Name;
	static const FName TouchpadGesture_Scroll_Left_Name;
	static const FName TouchpadGesture_Pinch_In_Name;
	static const FName TouchpadGesture_Pinch_Out_Name;
	static const FName TouchpadGesture_LongHold_Name;
	static const FName TouchpadGesture_Tap_Name;
	static const FName TouchpadGesture_ForceTapDown_Name;
	static const FName TouchpadGesture_SecondForceDown_Name;
	static const FName TouchpadGesture_ForceTapUp_Name;
	static const FName TouchpadGesture_ForceDwell_Name;
	static const FName TouchpadGesture_Any_Name;
};

struct MAGICLEAPCONTROLLER_API FMagicLeapKeys
{
	static const FKey MotionController_Left_Thumbstick_Z;
	static const FKey Left_HomeButton;
	static const FKey Left_Bumper;
	static const FKey Left_Trigger;
	static const FKey Left_Trigger_Axis;
	static const FKey Left_Trackpad_X;
	static const FKey Left_Trackpad_Y;
	static const FKey Left_Trackpad_Force;
	static const FKey Left_Trackpad_Touch;
	static const FKey Left_Touch1_X;
	static const FKey Left_Touch1_Y;
	static const FKey Left_Touch1_Force;
	static const FKey Left_Touch1_Touch;

	static const FKey MotionController_Right_Thumbstick_Z;
	static const FKey Right_HomeButton;
	static const FKey Right_Bumper;
	static const FKey Right_Trigger;
	static const FKey Right_Trigger_Axis;
	static const FKey Right_Trackpad_X;
	static const FKey Right_Trackpad_Y;
	static const FKey Right_Trackpad_Force;
	static const FKey Right_Trackpad_Touch;
	static const FKey Right_Touch1_X;
	static const FKey Right_Touch1_Y;
	static const FKey Right_Touch1_Force;
	static const FKey Right_Touch1_Touch;

	static const FKey TouchpadGesture_Swipe_Up;
	static const FKey TouchpadGesture_Swipe_Down;
	static const FKey TouchpadGesture_Swipe_Right;
	static const FKey TouchpadGesture_Swipe_Left;
	static const FKey TouchpadGesture_RadialScroll_Clockwise;
	static const FKey TouchpadGesture_RadialScroll_CounterClockwise;
	static const FKey TouchpadGesture_Scroll_Up;
	static const FKey TouchpadGesture_Scroll_Down;
	static const FKey TouchpadGesture_Scroll_Right;
	static const FKey TouchpadGesture_Scroll_Left;
	static const FKey TouchpadGesture_Pinch_In;
	static const FKey TouchpadGesture_Pinch_Out;
	static const FKey TouchpadGesture_LongHold;
	static const FKey TouchpadGesture_Tap;
	static const FKey TouchpadGesture_ForceTapDown;
	static const FKey TouchpadGesture_SecondForceDown;
	static const FKey TouchpadGesture_ForceTapUp;
	static const FKey TouchpadGesture_ForceDwell;
	static const FKey TouchpadGesture_Any;
};

/** Defines the Magic Leap controller type. */
UENUM(BlueprintType)
enum class EMagicLeapControllerType : uint8
{
	None,
	Device,
	MobileApp
};

/** LED patterns supported on the controller. */
UENUM(BlueprintType)
enum class EMagicLeapControllerLEDPattern : uint8
{
	None,
	Clock01,
	Clock02,
	Clock03,
	Clock04,
	Clock05,
	Clock06,
	Clock07,
	Clock08,
	Clock09,
	Clock10,
	Clock11,
	Clock12,
	Clock01_07,
	Clock02_08,
	Clock03_09,
	Clock04_10,
	Clock05_11,
	Clock06_12
};

/** LED effects supported on the controller. */
UENUM(BlueprintType)
enum class EMagicLeapControllerLEDEffect : uint8
{
	RotateCW,
	RotateCCW,
	Pulse,
	PaintCW,
	PaintCCW,
	Blink
};

/** LED colors supported on the controller. */
UENUM(BlueprintType)
enum class EMagicLeapControllerLEDColor : uint8
{
	BrightMissionRed,
	PastelMissionRed,
	BrightFloridaOrange,
	PastelFloridaOrange,
	BrightLunaYellow,
	PastelLunaYellow,
	BrightNebulaPink,
	PastelNebulaPink,
	BrightCosmicPurple,
	PastelCosmicPurple,
	BrightMysticBlue,
	PastelMysticBlue,
	BrightCelestialBlue,
	PastelCelestialBlue,
	BrightShaggleGreen,
	PastelShaggleGreen
};

/** LED speeds supported on the controller. */
UENUM(BlueprintType)
enum class EMagicLeapControllerLEDSpeed : uint8
{
	Slow,
	Medium,
	Fast
};

/** Haptic patterns supported on the controller. */
UENUM(BlueprintType)
enum class EMagicLeapControllerHapticPattern : uint8
{
	None,
	Click,
	Bump,
	DoubleClick,
	Buzz,
	Tick,
	ForceDown,
	ForceUp,
	ForceDwell,
	SecondForceDown
};

/** Haptic intesities supported on the controller. */
UENUM(BlueprintType)
enum class EMagicLeapControllerHapticIntensity : uint8
{
	Low,
	Medium,
	High
};

/** Tracking modes provided by Magic Leap. */
UENUM(BlueprintType)
enum class EMagicLeapControllerTrackingMode : uint8
{
	InputService,
	CoordinateFrameUID,
};
