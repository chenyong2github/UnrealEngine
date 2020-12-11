// Copyright Epic Games, Inc. All Rights Reserved.

#include "TP_VehicleWheelFront.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS

UTP_VehicleWheelFront::UTP_VehicleWheelFront()
{
	WheelRadius = 35.f;
	WheelWidth = 20.0f;
	LongitudinalFrictionForceMultiplier = 2.5f;
	LateralFrictionForceMultiplier = 2.0f;
	bAffectedByHandbrake = false;
	bAffectedBySteering = true;
	AxleType = EAxleType::Front;
	SpringRate = 400.0f;
	SpringPreload = 100.f;
	RollbarScaling = 0.5f;
	SuspensionMaxRaise = 10.0f;
	SuspensionMaxDrop = 15.0f;
	WheelLoadRatio = 0.5f;
	MaxSteerAngle = 50.f;
}

PRAGMA_ENABLE_DEPRECATION_WARNINGS

