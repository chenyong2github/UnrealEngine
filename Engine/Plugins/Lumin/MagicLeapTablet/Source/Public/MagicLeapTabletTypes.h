// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Engine/Engine.h"
#include "MagicLeapTabletTypes.generated.h"

UENUM(BlueprintType)
enum class EMagicLeapInputTabletDeviceType : uint8
{
	Unknown,
	Wacom,
};

UENUM(BlueprintType)
enum class EMagicLeapInputTabletDeviceToolType : uint8
{
	Unknown,
	Pen,
	Eraser,
};

UENUM(BlueprintType)
enum class EMagicLeapInputTabletDeviceButton : uint8
{
	Unknown,
	Button1,
	Button2,
	Button3,
	Button4,
	Button5,
	Button6,
	Button7,
	Button8,
	Button9,
	Button10,
	Button11,
	Button12,
};

/** Mask for determinig the validity of the additional pen data. */
enum class EMagicLeapInputTabletDeviceStateMask : uint8
{
	HasType = 1 << 0,
	HasToolType = 1 << 1,
	HasPenTouchPosAndForce = 1 << 2,
	HasAdditionalPenTouchData = 1 << 3,
	HasPenTouchActive = 1 << 4,
	HasConnectionState = 1 << 5,
	HasPenDistance = 1 << 6,
	HasTimestamp = 1 << 7,
};

USTRUCT(BlueprintType)
struct MAGICLEAPTABLET_API FMagicLeapInputTabletDeviceState
{
	GENERATED_BODY();
	/** Version of this structure. */
	uint32 Version;

	/** Type of this tablet device. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tablet | MagicLeap")
	EMagicLeapInputTabletDeviceType Type;

	/** Type of tool used with the tablet. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tablet | MagicLeap")
	EMagicLeapInputTabletDeviceToolType ToolType;

	/**
	  The current touch position (x,y) and force (z).
	  Position is in the [-1.0,1.0] range and force is in the [0.0,1.0] range.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tablet | MagicLeap")
	FVector PenTouchPosAndForce;

	/**
	  The Additional coordinate values (x, y, z).
	  It could contain data specific to the device type.
	  For example, it could hold tilt values while using pen.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tablet | MagicLeap")
	TArray<int32> AdditionalPenTouchData;

	/** Is touch active. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tablet | MagicLeap")
	bool bIsPenTouchActive;

	/** If this tablet is connected. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tablet | MagicLeap")
	bool bIsConnected;

	/** Distance between pen and tablet. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tablet | MagicLeap")
	float PenDistance;

	/** Time since device bootup. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tablet | MagicLeap")
	FTimespan Timestamp;

	/**
	  Flags to denote which of the above fields are valid.
	  #EMagicLeapInputTabletDeviceStateMask defines the bitmap.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tablet | MagicLeap")
	int32 ValidFieldsFlag;
};

/** Delegate used to signal when a tablet device has connected to the application. */
DECLARE_DYNAMIC_DELEGATE_OneParam(FMagicLeapTabletOnConnectedDelegate, uint8, DeviceId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMagicLeapTabletOnConnectedDelegateMulti, uint8, DeviceId);
/** Delegate used to signal when a tablet device has disconnected from the application. */
DECLARE_DYNAMIC_DELEGATE_OneParam(FMagicLeapTabletOnDisconnectedDelegate, uint8, DeviceId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMagicLeapTabletOnDisconnectedDelegateMulti, uint8, DeviceId);
/** Delegate used to signal the pen touch state of a connected tablet. */
DECLARE_DYNAMIC_DELEGATE_TwoParams(FMagicLeapTabletOnPenTouchDelegate, uint8, DeviceId, const FMagicLeapInputTabletDeviceState&, DeviceState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMagicLeapTabletOnPenTouchDelegateMulti, uint8, DeviceId, const FMagicLeapInputTabletDeviceState&, DeviceState);
/** Delegate used to signal the ring touch state of a connected tablet. */
DECLARE_DYNAMIC_DELEGATE_TwoParams(FMagicLeapTabletOnRingTouchDelegate, uint8, DeviceId, const int32&, TouchValue);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMagicLeapTabletOnRingTouchDelegateMulti, uint8, DeviceId, const int32&, TouchValue);
/** Delegate used to signal a button press from a connected tablet. */
DECLARE_DYNAMIC_DELEGATE_TwoParams(FMagicLeapTabletOnButtonDownDelegate, uint8, DeviceId, const EMagicLeapInputTabletDeviceButton&, Button);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMagicLeapTabletOnButtonDownDelegateMulti, uint8, DeviceId, const EMagicLeapInputTabletDeviceButton&, Button);
/** Delegate used to signal a button release from a connected tablet. */
DECLARE_DYNAMIC_DELEGATE_TwoParams(FMagicLeapTabletOnButtonUpDelegate, uint8, DeviceId, const EMagicLeapInputTabletDeviceButton&, Button);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMagicLeapTabletOnButtonUpDelegateMulti, uint8, DeviceId, const EMagicLeapInputTabletDeviceButton&, Button);
