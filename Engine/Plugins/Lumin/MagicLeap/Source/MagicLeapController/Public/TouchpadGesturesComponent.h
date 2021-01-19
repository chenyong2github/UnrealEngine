// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "InputCoreTypes.h"
#include "TouchpadGesturesComponent.generated.h"

/** Recognized touchpad gesture types. */
UENUM(BlueprintType, meta = (DeprecationMessage = "Use Touchpad Gesture Events instead."))
enum class EMagicLeapTouchpadGestureType : uint8
{
	None,
	Tap,
	ForceTapDown,
	ForceTapUp,
	ForceDwell,
	SecondForceDown,
	LongHold,
	RadialScroll,
	Swipe,
	Scroll,
	Pinch
};

/** Direction of touchpad gesture. */
UENUM(BlueprintType, meta = (DeprecationMessage = "Use Touchpad Gesture Events instead."))
enum class EMagicLeapTouchpadGestureDirection : uint8
{
	None,
	Up,
	Down,
	Left,
	Right,
	In,
	Out,
	Clockwise,
	CounterClockwise
};

/** Information about a recognized touchpad gesture. */
USTRUCT(BlueprintType, meta = (DeprecationMessage="Use Touchpad Gesture Events instead."))
struct MAGICLEAPCONTROLLER_API FMagicLeapTouchpadGesture
{
	GENERATED_BODY()

public:
	/** Hand on which the gesture was performed. */
	UPROPERTY(BlueprintReadOnly, Category = "TouchpadGesture|MagicLeap", meta = (DeprecatedProperty, DeprecationMessage = "Hand is deprecated. Please use MotionSource instead."))
	EControllerHand Hand = EControllerHand::Left;

	/** Motion source on which the gesture was performed. */
	UPROPERTY(BlueprintReadOnly, Category = "TouchpadGesture|MagicLeap", meta = (DeprecatedProperty, DeprecationMessage = "Use Touchpad Gesture Events instead."))
	FName MotionSource;

	/** Type of gesture. */
	UPROPERTY(BlueprintReadOnly, Category = "TouchpadGesture|MagicLeap", meta = (DeprecatedProperty, DeprecationMessage = "Use Touchpad Gesture Events instead."))
	EMagicLeapTouchpadGestureType Type = EMagicLeapTouchpadGestureType::None;

	/** Direction of gesture */
	UPROPERTY(BlueprintReadOnly, Category = "TouchpadGesture|MagicLeap", meta = (DeprecatedProperty, DeprecationMessage = "Use Touchpad Gesture Events instead."))
	EMagicLeapTouchpadGestureDirection Direction = EMagicLeapTouchpadGestureDirection::None;

	/** 
	  Gesture position (x,y) and force (z).
	  Position is in the [-1.0,1.0] range and force is in the [0.0,1.0] range.
	*/
	UPROPERTY(BlueprintReadOnly, Category = "TouchpadGesture|MagicLeap", meta = (DeprecatedProperty, DeprecationMessage = "Use Touchpad Gesture Events instead."))
	FVector PositionAndForce = FVector(0.0f);

	/**
	  Speed of gesture. Note that this takes on different meanings depending
      on the gesture type being performed:
      - For radial gestures, this will be the angular speed around the axis.
      - For pinch gestures, this will be the speed at which the distance
        between fingers is changing. The touchpad is defined as having extents
        of [-1.0,1.0] so touchpad distance has a range of [0.0,2.0]; this value
        will be in touchpad distance per second.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "TouchpadGesture|MagicLeap", meta = (DeprecatedProperty, DeprecationMessage = "Use Touchpad Gesture Events instead."))
	float Speed = 0.0f;

	/**
	  For radial gestures, this is the absolute value of the angle. For scroll
      and pinch gestures, this is the absolute distance traveled in touchpad
      distance. The touchpad is defined as having extents of [-1.0,1.0] so
      this distance has a range of [0.0,2.0].
	 */
	UPROPERTY(BlueprintReadOnly, Category = "TouchpadGesture|MagicLeap", meta = (DeprecatedProperty, DeprecationMessage = "Use Touchpad Gesture Events instead."))
	float Distance = 0.0f;

	/**
	  Distance between the two fingers performing the gestures in touchpad
      distance. The touchpad is defined as having extents of [-1.0,1.0] so
      this distance has a range of [0.0,2.0].
	 */
	UPROPERTY(BlueprintReadOnly, Category = "TouchpadGesture|MagicLeap", meta = (DeprecatedProperty, DeprecationMessage = "Use Touchpad Gesture Events instead."))
	float FingerGap = 0.0f;

	/**
	  For radial gestures, this is the radius of the gesture. The touchpad
      is defined as having extents of [-1.0,1.0] so this radius has a range
      of [0.0,2.0].
	 */
	UPROPERTY(BlueprintReadOnly, Category = "TouchpadGesture|MagicLeap", meta = (DeprecatedProperty, DeprecationMessage = "Use Touchpad Gesture Events instead."))
	float Radius = 0.0f;

	/** Angle from the center of the touchpad to the finger. */
	UPROPERTY(BlueprintReadOnly, Category = "TouchpadGesture|MagicLeap", meta = (DeprecatedProperty, DeprecationMessage = "Use Touchpad Gesture Events instead."))
	float Angle = 0.0f;
};

class MAGICLEAPCONTROLLER_API IMagicLeapTouchpadGestures
{
public:
	virtual void OnTouchpadGestureStartCallback(const FMagicLeapTouchpadGesture& GestureData) = 0;
	virtual void OnTouchpadGestureContinueCallback(const FMagicLeapTouchpadGesture& GestureData) = 0;
	virtual void OnTouchpadGestureEndCallback(const FMagicLeapTouchpadGesture& GestureData) = 0;
};

/** DEPRECATED use Touchpad Gesture Events instead. - Delegates touchpad gesture events for the Magic Leap Controller & MLMA */
UCLASS(ClassGroup = MagicLeap, BlueprintType, Blueprintable, EditInlineNew, meta = (Deprecated, DeprecationMessage="Use Touchpad Gesture Events instead.", BlueprintSpawnableComponent))
class MAGICLEAPCONTROLLER_API UMagicLeapTouchpadGesturesComponent : public UActorComponent, public IMagicLeapTouchpadGestures
{
	GENERATED_BODY()

public:
	UMagicLeapTouchpadGesturesComponent();

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTouchpadGestureEvent, const FMagicLeapTouchpadGesture&, GestureData);

	/** DEPRECATED use Touchpad Gesture Events instead. - Event called when a touchpad gesture starts. Provides all the meta data about the given gestures. */
	UPROPERTY(BlueprintAssignable, meta = (DeprecatedProperty, DeprecationMessage="Use Touchpad Gesture Events instead."))
	FTouchpadGestureEvent OnTouchpadGestureStart;

	/** DEPRECATED use Touchpad Gesture Events instead. - Event called when a touchpad gesture continues. Provides all the meta data about the given gestures. */
	UPROPERTY(BlueprintAssignable, meta = (DeprecatedProperty, DeprecationMessage="Use Touchpad Gesture Events instead."))
	FTouchpadGestureEvent OnTouchpadGestureContinue;

	/** DEPRECATED use Touchpad Gesture Events instead. - Event called when a touchpad gesture ends. Provides all the meta data about the given gestures. */
	UPROPERTY(BlueprintAssignable, meta = (DeprecatedProperty, DeprecationMessage="Use Touchpad Gesture Events instead."))
	FTouchpadGestureEvent OnTouchpadGestureEnd;

	/** IMagicLeapTouchpadGestures interface */
	virtual void OnTouchpadGestureStartCallback(const FMagicLeapTouchpadGesture& GestureData) override;
	virtual void OnTouchpadGestureContinueCallback(const FMagicLeapTouchpadGesture& GestureData) override;
	virtual void OnTouchpadGestureEndCallback(const FMagicLeapTouchpadGesture& GestureData) override;

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	FCriticalSection CriticalSection;
	TArray<FMagicLeapTouchpadGesture> PendingTouchpadGestureStart;
	TArray<FMagicLeapTouchpadGesture> PendingTouchpadGestureContinue;
	TArray<FMagicLeapTouchpadGesture> PendingTouchpadGestureEnd;
};
