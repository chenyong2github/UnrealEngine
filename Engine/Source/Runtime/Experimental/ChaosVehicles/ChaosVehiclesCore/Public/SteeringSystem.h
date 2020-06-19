// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VehicleSystemTemplate.h"
#include "VehicleUtility.h"
#include "SteeringUtility.h"

#if VEHICLE_DEBUGGING_ENABLED
PRAGMA_DISABLE_OPTIMIZATION
#endif

namespace Chaos
{

	struct FSimpleSteeringConfig
	{
		FSimpleSteeringConfig()
			: TrackWidth(1.8f)
			, WheelBase(3.8f)
			, TrackEndRadius(0.2f)
		{}

		float TrackWidth;
		float WheelBase;
		float TrackEndRadius;
	};

	class FSimpleSteeringSim : public TVehicleSystem<FSimpleSteeringConfig>
	{
	public:

		FSimpleSteeringSim(const FSimpleSteeringConfig* SetupIn)
			: TVehicleSystem<FSimpleSteeringConfig>(SetupIn)
		{
			float TrackWidth = Setup().TrackWidth;
			float WheelBase = Setup().WheelBase;

			float Beta = FSteeringUtility::CalculateBetaDegrees(TrackWidth, WheelBase);

			FSteeringUtility::CalcJointPositions(TrackWidth, Beta, Setup().TrackEndRadius, C1, R1, C2, R2);

			FSteeringUtility::CalculateAkermannAngle(false, 0.0f, C2, R1, R2, RestAngle, LeftRodPt, RightPivot);

			// #todo: Calculate this from Max Wheel Angle
			SteerInputScaling = 30.f;
		}

		void GetLeftHingeLocations(FVector2D& OutC1, FVector2D& OutP, FVector2D& OutC2)
		{
			OutC1 = LeftRodPt;
			OutP = LeftPivot;
			OutC2 = C2;
			OutC1.X = -OutC1.X;
			OutP.X = -OutP.X;
			OutC2.X = -OutC2.X;
		}

		void GetRightHingeLocations(FVector2D& OutC1, FVector2D& OutP, FVector2D& OutC2)
		{
			OutC1 = RightRodPt;
			OutP = RightPivot;
			OutC2 = C2;
		}

		void CalculateAkermannAngle(float Input, float& OutSteerLeft, float& OutSteerRight)
		{
			FSteeringUtility::CalculateAkermannAngle(true, Input* SteerInputScaling, C2, R1, R2, OutSteerLeft, LeftRodPt, LeftPivot);
			FSteeringUtility::CalculateAkermannAngle(false, Input* SteerInputScaling, C2, R1, R2, OutSteerRight, RightRodPt, RightPivot);

			OutSteerLeft -= RestAngle;
			OutSteerRight -= RestAngle;
		}

		FVector2D C1;
		FVector2D C2;
		float R1;
		float R2;
		float SteerInputScaling;

		FVector2D LeftRodPt, RightRodPt;
		FVector2D LeftPivot;
		FVector2D RightPivot;

		float RestAngle;

	};

} // namespace Chaos

#if VEHICLE_DEBUGGING_ENABLED
PRAGMA_ENABLE_OPTIMIZATION
#endif