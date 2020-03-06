// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TouchpadGesturesComponent.h"

#if WITH_MLSDK
#define TOUCH_GESTURE_CASE_DEPRECATED(x) case MLInputControllerTouchpadGestureType_##x: { return EMagicLeapTouchpadGestureType::x; }

EMagicLeapTouchpadGestureType MLToUnrealTouchpadGestureType_DEPRECATED(MLInputControllerTouchpadGestureType GestureType)
{
	switch (GestureType)
	{
		TOUCH_GESTURE_CASE_DEPRECATED(Tap)
		TOUCH_GESTURE_CASE_DEPRECATED(ForceTapDown)
		TOUCH_GESTURE_CASE_DEPRECATED(ForceTapUp)
		TOUCH_GESTURE_CASE_DEPRECATED(ForceDwell)
		TOUCH_GESTURE_CASE_DEPRECATED(SecondForceDown)
		TOUCH_GESTURE_CASE_DEPRECATED(LongHold)
		TOUCH_GESTURE_CASE_DEPRECATED(RadialScroll)
		TOUCH_GESTURE_CASE_DEPRECATED(Swipe)
		TOUCH_GESTURE_CASE_DEPRECATED(Scroll)
		TOUCH_GESTURE_CASE_DEPRECATED(Pinch)
	}
	return EMagicLeapTouchpadGestureType::None;
}

#define TOUCH_DIR_CASE_DEPRECATED(x) case MLInputControllerTouchpadGestureDirection_##x: { return EMagicLeapTouchpadGestureDirection::x; }

EMagicLeapTouchpadGestureDirection MLToUnrealTouchpadGestureDirection_DEPRECATED(MLInputControllerTouchpadGestureDirection Direction)
{
	switch (Direction)
	{
		TOUCH_DIR_CASE_DEPRECATED(Up)
		TOUCH_DIR_CASE_DEPRECATED(Down)
		TOUCH_DIR_CASE_DEPRECATED(Left)
		TOUCH_DIR_CASE_DEPRECATED(Right)
		TOUCH_DIR_CASE_DEPRECATED(In)
		TOUCH_DIR_CASE_DEPRECATED(Out)
		TOUCH_DIR_CASE_DEPRECATED(Clockwise)
		TOUCH_DIR_CASE_DEPRECATED(CounterClockwise)
	}
	return EMagicLeapTouchpadGestureDirection::None;
}

FMagicLeapTouchpadGesture MLToUnrealTouchpadGesture_DEPRECATED(EControllerHand hand, FName MotionSource, const MLInputControllerTouchpadGesture& touchpad_gesture)
{
	FMagicLeapTouchpadGesture gesture;
	gesture.Hand = hand;
	gesture.MotionSource = MotionSource;
	gesture.Type = MLToUnrealTouchpadGestureType_DEPRECATED(touchpad_gesture.type);
	gesture.Direction = MLToUnrealTouchpadGestureDirection_DEPRECATED(touchpad_gesture.direction);
	gesture.PositionAndForce = FVector(touchpad_gesture.pos_and_force.x, touchpad_gesture.pos_and_force.y, touchpad_gesture.pos_and_force.z);
	gesture.Speed = touchpad_gesture.speed;
	gesture.Distance = touchpad_gesture.distance;
	gesture.FingerGap = touchpad_gesture.finger_gap;
	gesture.Radius = touchpad_gesture.radius;
	gesture.Angle = touchpad_gesture.angle;

	return gesture;
}

const FName& MLToUnrealButton(EControllerHand Hand, MLInputControllerButton ml_button)
{
	static const FName empty;

	switch (ml_button)
	{
	case MLInputControllerButton_Bumper:
		if (Hand == EControllerHand::Left)
		{
			return FMagicLeapControllerKeyNames::Left_Bumper_Name;
		}
		return FMagicLeapControllerKeyNames::Right_Bumper_Name;
	case MLInputControllerButton_HomeTap:
		if (Hand == EControllerHand::Left)
		{
			return FMagicLeapControllerKeyNames::Left_HomeButton_Name;
		}
		return FMagicLeapControllerKeyNames::Right_HomeButton_Name;
	default:
		break;
	}
	return empty;
}

const FName& MLToUnrealButton(FName MotionSource, MLInputControllerButton ml_button)
{
	static const FName empty;
	return empty;
}
#endif //WITH_MLSDK

const FName& MLTouchToUnrealTrackpadAxis(EControllerHand Hand, uint32 TouchIndex)
{
	static const FName empty;

	switch (TouchIndex)
	{
	case 0:
		if (Hand == EControllerHand::Left)
		{
			return FMagicLeapControllerKeyNames::Left_Trackpad_X_Name;
		}
		return FMagicLeapControllerKeyNames::Right_Trackpad_X_Name;
	case 1:
		if (Hand == EControllerHand::Left)
		{
			return FMagicLeapControllerKeyNames::Left_Trackpad_Y_Name;
		}
		return FMagicLeapControllerKeyNames::Right_Trackpad_Y_Name;
	case 2:
		if (Hand == EControllerHand::Left)
		{
			return FMagicLeapControllerKeyNames::Left_Trackpad_Force_Name;
		}
		return FMagicLeapControllerKeyNames::Right_Trackpad_Force_Name;
	default:
		return empty;
	}

	return empty;
}

const FName& MLTouchToUnrealTouch1Axis(EControllerHand Hand, uint32 TouchIndex)
{
	static const FName empty;

	switch (TouchIndex)
	{
	case 0:
		if (Hand == EControllerHand::Left)
		{
			return FMagicLeapControllerKeyNames::Left_Touch1_X_Name;
		}
		return FMagicLeapControllerKeyNames::Right_Touch1_X_Name;
	case 1:
		if (Hand == EControllerHand::Left)
		{
			return FMagicLeapControllerKeyNames::Left_Touch1_Y_Name;
		}
		return FMagicLeapControllerKeyNames::Right_Touch1_Y_Name;
	case 2:
		if (Hand == EControllerHand::Left)
		{
			return FMagicLeapControllerKeyNames::Left_Touch1_Force_Name;
		}
		return FMagicLeapControllerKeyNames::Right_Touch1_Force_Name;
	default:
		return empty;
	}

	return empty;
}

const FName& MLTouchToUnrealTrackpadButton(EControllerHand Hand)
{
	static const FName empty;

	switch (Hand)
	{
	case EControllerHand::Left:
		return FMagicLeapControllerKeyNames::Left_Trackpad_Touch_Name;
	case EControllerHand::Right:
		return FMagicLeapControllerKeyNames::Right_Trackpad_Touch_Name;
	}

	return empty;
}

const FName& MLTouchToUnrealTouch1Button(EControllerHand Hand)
{
	static const FName empty;

	switch (Hand)
	{
	case EControllerHand::Left:
		return FMagicLeapControllerKeyNames::Left_Touch1_Touch_Name;
	case EControllerHand::Right:
		return FMagicLeapControllerKeyNames::Right_Touch1_Touch_Name;
	}

	return empty;
}

const FName& MLTriggerToUnrealTriggerAxis(EControllerHand Hand)
{
	static const FName empty;

	switch (Hand)
	{
	case EControllerHand::Left:
		return FMagicLeapControllerKeyNames::Left_Trigger_Axis_Name;
	case EControllerHand::Right:
		return FMagicLeapControllerKeyNames::Right_Trigger_Axis_Name;
	}

	return empty;
}

const FName& MLTriggerToUnrealTriggerKey(EControllerHand Hand)
{
	static const FName empty;
	switch (Hand)
	{
	case EControllerHand::Left:
		return FMagicLeapControllerKeyNames::Left_Trigger_Name;
	case EControllerHand::Right:
		return FMagicLeapControllerKeyNames::Right_Trigger_Name;
	}
	return empty;
}

#if WITH_MLSDK

MLInputControllerFeedbackPatternLED UnrealToMLPatternLED(EMagicLeapControllerLEDPattern LEDPattern)
{
	switch (LEDPattern)
	{
	case EMagicLeapControllerLEDPattern::None:
		return MLInputControllerFeedbackPatternLED_None;
	case EMagicLeapControllerLEDPattern::Clock01:
		return MLInputControllerFeedbackPatternLED_Clock1;
	case EMagicLeapControllerLEDPattern::Clock02:
		return MLInputControllerFeedbackPatternLED_Clock2;
	case EMagicLeapControllerLEDPattern::Clock03:
		return MLInputControllerFeedbackPatternLED_Clock3;
	case EMagicLeapControllerLEDPattern::Clock04:
		return MLInputControllerFeedbackPatternLED_Clock4;
	case EMagicLeapControllerLEDPattern::Clock05:
		return MLInputControllerFeedbackPatternLED_Clock5;
	case EMagicLeapControllerLEDPattern::Clock06:
		return MLInputControllerFeedbackPatternLED_Clock6;
	case EMagicLeapControllerLEDPattern::Clock07:
		return MLInputControllerFeedbackPatternLED_Clock7;
	case EMagicLeapControllerLEDPattern::Clock08:
		return MLInputControllerFeedbackPatternLED_Clock8;
	case EMagicLeapControllerLEDPattern::Clock09:
		return MLInputControllerFeedbackPatternLED_Clock9;
	case EMagicLeapControllerLEDPattern::Clock10:
		return MLInputControllerFeedbackPatternLED_Clock10;
	case EMagicLeapControllerLEDPattern::Clock11:
		return MLInputControllerFeedbackPatternLED_Clock11;
	case EMagicLeapControllerLEDPattern::Clock12:
		return MLInputControllerFeedbackPatternLED_Clock12;
	case EMagicLeapControllerLEDPattern::Clock01_07:
		return MLInputControllerFeedbackPatternLED_Clock1And7;
	case EMagicLeapControllerLEDPattern::Clock02_08:
		return MLInputControllerFeedbackPatternLED_Clock2And8;
	case EMagicLeapControllerLEDPattern::Clock03_09:
		return MLInputControllerFeedbackPatternLED_Clock3And9;
	case EMagicLeapControllerLEDPattern::Clock04_10:
		return MLInputControllerFeedbackPatternLED_Clock4And10;
	case EMagicLeapControllerLEDPattern::Clock05_11:
		return MLInputControllerFeedbackPatternLED_Clock5And11;
	case EMagicLeapControllerLEDPattern::Clock06_12:
		return MLInputControllerFeedbackPatternLED_Clock6And12;
	default:
		UE_LOG(LogMagicLeapController, Error, TEXT("Unhandled LED Pattern type %d"), static_cast<int32>(LEDPattern));
		break;
	}
	return MLInputControllerFeedbackPatternLED_Ensure32Bits;
}

MLInputControllerFeedbackEffectLED UnrealToMLEffectLED(EMagicLeapControllerLEDEffect LEDEffect)
{
	switch (LEDEffect)
	{
	case EMagicLeapControllerLEDEffect::RotateCW:
		return MLInputControllerFeedbackEffectLED_RotateCW;
	case EMagicLeapControllerLEDEffect::RotateCCW:
		return MLInputControllerFeedbackEffectLED_RotateCCW;
	case EMagicLeapControllerLEDEffect::Pulse:
		return MLInputControllerFeedbackEffectLED_Pulse;
	case EMagicLeapControllerLEDEffect::PaintCW:
		return MLInputControllerFeedbackEffectLED_PaintCW;
	case EMagicLeapControllerLEDEffect::PaintCCW:
		return MLInputControllerFeedbackEffectLED_PaintCCW;
	case EMagicLeapControllerLEDEffect::Blink:
		return MLInputControllerFeedbackEffectLED_Blink;
	default:
		UE_LOG(LogMagicLeapController, Error, TEXT("Unhandled LED effect type %d"), static_cast<int32>(LEDEffect));
		break;
	}
	return MLInputControllerFeedbackEffectLED_Ensure32Bits;
}

#define LED_COLOR_CASE(x) case EMagicLeapControllerLEDColor::x: { return MLInputControllerFeedbackColorLED_##x; }
MLInputControllerFeedbackColorLED UnrealToMLColorLED(EMagicLeapControllerLEDColor LEDColor)
{
	switch (LEDColor)
	{
		LED_COLOR_CASE(BrightMissionRed)
			LED_COLOR_CASE(PastelMissionRed)
			LED_COLOR_CASE(BrightFloridaOrange)
			LED_COLOR_CASE(PastelFloridaOrange)
			LED_COLOR_CASE(BrightLunaYellow)
			LED_COLOR_CASE(PastelLunaYellow)
			LED_COLOR_CASE(BrightNebulaPink)
			LED_COLOR_CASE(PastelNebulaPink)
			LED_COLOR_CASE(BrightCosmicPurple)
			LED_COLOR_CASE(PastelCosmicPurple)
			LED_COLOR_CASE(BrightMysticBlue)
			LED_COLOR_CASE(PastelMysticBlue)
			LED_COLOR_CASE(BrightCelestialBlue)
			LED_COLOR_CASE(PastelCelestialBlue)
			LED_COLOR_CASE(BrightShaggleGreen)
			LED_COLOR_CASE(PastelShaggleGreen)
	default:
		UE_LOG(LogMagicLeapController, Error, TEXT("Unhandled LED color type %d"), static_cast<int32>(LEDColor));
		break;
	}
	return MLInputControllerFeedbackColorLED_Ensure32Bits;
}

MLInputControllerFeedbackEffectSpeedLED UnrealToMLSpeedLED(EMagicLeapControllerLEDSpeed LEDSpeed)
{
	switch (LEDSpeed)
	{
	case EMagicLeapControllerLEDSpeed::Slow:
		return MLInputControllerFeedbackEffectSpeedLED_Slow;
	case EMagicLeapControllerLEDSpeed::Medium:
		return MLInputControllerFeedbackEffectSpeedLED_Medium;
	case EMagicLeapControllerLEDSpeed::Fast:
		return MLInputControllerFeedbackEffectSpeedLED_Fast;
	default:
		UE_LOG(LogMagicLeapController, Error, TEXT("Unhandled LED speed type %d"), static_cast<int32>(LEDSpeed));
		break;
	}
	return MLInputControllerFeedbackEffectSpeedLED_Ensure32Bits;
}

MLInputControllerFeedbackPatternVibe UnrealToMLPatternVibe(EMagicLeapControllerHapticPattern HapticPattern)
{
	switch (HapticPattern)
	{
	case EMagicLeapControllerHapticPattern::None:
		return MLInputControllerFeedbackPatternVibe_None;
	case EMagicLeapControllerHapticPattern::Click:
		return MLInputControllerFeedbackPatternVibe_Click;
	case EMagicLeapControllerHapticPattern::Bump:
		return MLInputControllerFeedbackPatternVibe_Bump;
	case EMagicLeapControllerHapticPattern::DoubleClick:
		return MLInputControllerFeedbackPatternVibe_DoubleClick;
	case EMagicLeapControllerHapticPattern::Buzz:
		return MLInputControllerFeedbackPatternVibe_Buzz;
	case EMagicLeapControllerHapticPattern::Tick:
		return MLInputControllerFeedbackPatternVibe_Tick;
	case EMagicLeapControllerHapticPattern::ForceDown:
		return MLInputControllerFeedbackPatternVibe_ForceDown;
	case EMagicLeapControllerHapticPattern::ForceUp:
		return MLInputControllerFeedbackPatternVibe_ForceUp;
	case EMagicLeapControllerHapticPattern::ForceDwell:
		return MLInputControllerFeedbackPatternVibe_ForceDwell;
	case EMagicLeapControllerHapticPattern::SecondForceDown:
		return MLInputControllerFeedbackPatternVibe_SecondForceDown;
	default:
		UE_LOG(LogMagicLeapController, Error, TEXT("Unhandled Haptic Pattern type %d"), static_cast<int32>(HapticPattern));
		break;
	}
	return MLInputControllerFeedbackPatternVibe_Ensure32Bits;
}

MLInputControllerFeedbackIntensity UnrealToMLHapticIntensity(EMagicLeapControllerHapticIntensity HapticIntensity)
{
	switch (HapticIntensity)
	{
	case EMagicLeapControllerHapticIntensity::Low:
		return MLInputControllerFeedbackIntensity_Low;
	case EMagicLeapControllerHapticIntensity::Medium:
		return MLInputControllerFeedbackIntensity_Medium;
	case EMagicLeapControllerHapticIntensity::High:
		return MLInputControllerFeedbackIntensity_High;
	default:
		UE_LOG(LogMagicLeapController, Error, TEXT("Unhandled Haptic Intensity type %d"), static_cast<int32>(HapticIntensity));
		break;
	}
	return MLInputControllerFeedbackIntensity_Ensure32Bits;
}
#endif //WITH_MLSDK

