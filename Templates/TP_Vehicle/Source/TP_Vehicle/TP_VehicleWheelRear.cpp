// Copyright Epic Games, Inc. All Rights Reserved.

#include "TP_VehicleWheelRear.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS

UTP_VehicleWheelRear::UTP_VehicleWheelRear()
{
	WheelRadius = 35.f;
	WheelWidth = 20.0f;
	LongitudinalFrictionForceMultiplier = 2.5f;
	LateralFrictionForceMultiplier = 2.0f;
	bAffectedByHandbrake = true;
	bAffectedBySteering = false;
	AxleType = EAxleType::Rear;
	SpringRate = 400.0f;
	SpringPreload = 100.f;
	RollbarScaling = 0.5f;
	SuspensionMaxRaise = 10.0f;
	SuspensionMaxDrop = 15.0f;
	WheelLoadRatio = 0.5f;
	MaxSteerAngle = 0.f;
}

PRAGMA_ENABLE_DEPRECATION_WARNINGS
