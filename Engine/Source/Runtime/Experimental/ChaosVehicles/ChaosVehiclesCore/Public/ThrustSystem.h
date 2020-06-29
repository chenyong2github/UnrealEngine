// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VehicleSystemTemplate.h"
#include "VehicleUtility.h"

#if VEHICLE_DEBUGGING_ENABLED
PRAGMA_DISABLE_OPTIMIZATION
#endif

namespace Chaos
{

	struct FSimpleThrustConfig
	{
		FVector Offset;
		FVector Axis;
		TArray<float> Curve;
		float MaxSpeed;
		float MaxThrustForce;
		float MaxControlAngle;
		// control axis
	};

	class FSimpleThrustSim : public TVehicleSystem<FSimpleThrustConfig>
	{
	public:

		FSimpleThrustSim(const FSimpleThrustConfig* SetupIn)
			: TVehicleSystem<FSimpleThrustConfig>(SetupIn)
			, ThrottlePosition(0.f)
			, ThrustForce(FVector::ZeroVector)
			, ThrustDirection(FVector::ZeroVector)
			, ThrusterStarted(false)
			, MaintainAltitude(true)
			, Altitude(0.f)
			, WorldVelocity(FVector::ZeroVector)
			, Pitch(0.f)
			, Roll(0.f)
			, Yaw(0.f)
		{
		}

		void SetThrottle(float InThrottle)
		{
			ThrottlePosition = FMath::Clamp(InThrottle, -1.f, 1.f);
		}

		void SetPitch(float InPitch)
		{
			
			Pitch = -FMath::Clamp(InPitch, -1.f, 1.f) * Setup().MaxControlAngle;
		}

		void SetRoll(float InRoll)
		{
			Roll = FMath::Clamp(InRoll, -1.f, 1.f) * Setup().MaxControlAngle;
		}

		void SetAltitude(float InAltitude)
		{
			Altitude = InAltitude;
		}

		void SetWorldVelocity(const FVector& InVelocity)
		{
			WorldVelocity = InVelocity;
		}

		// Get functions
		const FVector& GetThrustForce() const
		{
			return ThrustForce;
		}

		const FVector& GetThrustDirection() const
		{
			return ThrustDirection;
		}

		const FVector GetThrustLocation() const
		{
			static float HalfBladeLength = 8.0f;
			FVector Location = Setup().Offset + FVector(Pitch, -0.25*Roll, 0.f) * HalfBladeLength;
		
			return Location;
		}

		// simulate
		void Simulate(float DeltaTime)
		{
			static float Multiplier = 20.0f;
			FVector CorrectionalForce = FVector::ZeroVector;
			if (MaintainAltitude)
			{
				CorrectionalForce.Z = -Multiplier *  WorldVelocity.Z / DeltaTime;
			}
			FVector LocalThrustDirection = Setup().Axis;
			//FRotator Rot(Pitch, Roll, Roll);
			//ThrustDirection = Rot.RotateVector(LocalThrustDirection);
			ThrustForce = LocalThrustDirection * (ThrottlePosition * Setup().MaxThrustForce) + CorrectionalForce;
		}

	protected:
			
		float ThrottlePosition; // [0..1 Normalized position]

		FVector ThrustForce;
		FVector ThrustDirection;

		bool ThrusterStarted;	// is the 'engine' turned off or has it been started

		bool MaintainAltitude;	// #todo: another derived class?
		float Altitude;			// #todo: another derived class?
		FVector WorldVelocity;

		float Pitch;
		float Roll;
		float Yaw;

	};



	class Rotor : FSimpleThrustSim
	{

	};

} // namespace Chaos

#if VEHICLE_DEBUGGING_ENABLED
PRAGMA_ENABLE_OPTIMIZATION
#endif