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

	struct CHAOSVEHICLESCORE_API FSimpleSteeringConfig
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


	class CHAOSVEHICLESCORE_API FSimpleSteeringSim : public TVehicleSystem<FSimpleSteeringConfig>
	{
	public:

		FSimpleSteeringSim(const FSimpleSteeringConfig* SetupIn);

		void GetLeftHingeLocations(FVector2D& OutC1, FVector2D& OutP, FVector2D& OutC2);

		void GetRightHingeLocations(FVector2D& OutC1, FVector2D& OutP, FVector2D& OutC2);

		void CalculateAkermannAngle(float Input, float& OutSteerLeft, float& OutSteerRight);

	private:
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