// Copyright Epic Games, Inc. All Rights Reserved.

#include "TP_VehicleAdvWheelRear.h"
#include "UObject/ConstructorHelpers.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS

UTP_VehicleAdvWheelRear::UTP_VehicleAdvWheelRear()
{
	WheelRadius = 18.f;
	WheelWidth = 20.0f;
	LongitudinalFrictionForceMultiplier = 2.5f;
	LateralFrictionForceMultiplier = 2.0f;
	bAffectedByHandbrake = true;
	bAffectedBySteering = false;
	AxleType = EAxleType::Rear;
	SpringRate = 400.0f;
	SpringPreload = 100.f;
	RollbarScaling = 0.5f;
	SuspensionMaxRaise = 8;
	SuspensionMaxDrop = 12.0f;
	WheelLoadRatio = 0.5f;
}

PRAGMA_ENABLE_DEPRECATION_WARNINGS
