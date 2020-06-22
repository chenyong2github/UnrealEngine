// Copyright Epic Games, Inc. All Rights Reserved.

#include "WheelSystem.h"

#if VEHICLE_DEBUGGING_ENABLED
PRAGMA_DISABLE_OPTIMIZATION
#endif

namespace Chaos
{
	FSimpleWheelSim::FSimpleWheelSim(const FSimpleWheelConfig* SetupIn) : TVehicleSystem<FSimpleWheelConfig>(SetupIn)
		, Re(30.f)
		, Omega(0.f)
		, Sx(0.f)
		, DriveTorque(0.f)
		, BrakeTorque(0.f)
		, ForceIntoSurface(0.f)
		, GroundVelocityVector(FVector::ZeroVector)
		, AngularPosition(0.f)
		, SteeringAngle(0.f)
		, SurfaceFriction(1.f)
		, ForceFromFriction(FVector::ZeroVector)
		, MassPerWheel(250.f)
		, SlipVelocity(0.f)
		, SlipAngle(0.f)
		, bInContact(false)
		, WheelIndex(0)
	{

	}

	void FSimpleWheelSim::Simulate(float DeltaTime)
	{
		SlipAngle = FMath::Atan2(GroundVelocityVector.Y, GroundVelocityVector.X);

		float FinalLongitudinalForce = 0.f;

		// IMPORTANT - The physics system is mostly unit-less i.e. can work in meters or cm, however there are 
		// a couple of places where the results are wrong if Cm is used. This is one of them, the simulated radius
		// for torque must be real size to obtain the correct output values. Meter vs Cm test appears in HeadessChaos
		AppliedLinearDriveForce = DriveTorque / CmToM(Re);
		AppliedLinearBrakeForce = BrakeTorque / CmToM(Re);

		// #todo: currently just letting the brake override the throttle
		bool Braking = BrakeTorque > FMath::Abs(DriveTorque);

		// are we actually touching the ground
		if (ForceIntoSurface > SMALL_NUMBER)
		{
			AdhesiveLimit = (ForceIntoSurface)*SurfaceFriction * Setup().CheatFrictionForceMultiplier;

			if (Braking)
			{
				// whether the velocity is +ve or -ve when we brake we are slowing the vehicle down
				// so force is opposing current direction of travel.
				float ForceRequiredToBringToStop = MassPerWheel * (GroundVelocityVector.X) / DeltaTime;
				FinalLongitudinalForce = AppliedLinearBrakeForce;

				// check we are not applying more force than required so we end up overshooting 
				// and accelerating in the opposite direction
				if (FinalLongitudinalForce > FMath::Abs(ForceRequiredToBringToStop))
				{
					FinalLongitudinalForce = FMath::Abs(ForceRequiredToBringToStop);
				}

				// ensure the brake opposes current direction of travel
				if (GroundVelocityVector.X > 0.0f)
				{
					FinalLongitudinalForce = -FinalLongitudinalForce;
				}
			}
			else
			{
				FinalLongitudinalForce = AppliedLinearDriveForce;
			}

			// lateral grip
			float FinalLateralForce = -(MassPerWheel * GroundVelocityVector.Y) / DeltaTime;

			float BrakeFactor = Braking ? 0.1f * (10.0f - (FMath::Clamp(FMath::Abs(FinalLongitudinalForce) / AdhesiveLimit, 1.f, 10.f) - 1.0f)) : 1.f;

			ForceFromFriction.X = FinalLongitudinalForce;

			float DynamicFrictionScaling = 0.75f;
			float TractionControlAndABSScaling = 0.98f;	// how close to perfection is the system working

			// we can only obtain as much accel/decel force as the friction will allow
			if (FMath::Abs(FinalLongitudinalForce) > AdhesiveLimit)
			{
				if (Braking && Setup().ABSEnabled || !Braking && Setup().TractionControlEnabled)
				{
					ForceFromFriction.X = AdhesiveLimit * TractionControlAndABSScaling;
				}
				else
				{
					ForceFromFriction.X = AdhesiveLimit * DynamicFrictionScaling;
				}
			}

			if (FinalLongitudinalForce < -AdhesiveLimit)
			{
				ForceFromFriction.X = -ForceFromFriction.X;
			}

			// Lateral needs more grip!
			AdhesiveLimit *= 2.0f;
			ForceFromFriction.Y = FinalLateralForce;
			if (FinalLateralForce > AdhesiveLimit)
			{
				ForceFromFriction.Y = AdhesiveLimit * 0.7f; // broke friction - want more analog feel
			}
			else if (FinalLateralForce < -AdhesiveLimit)
			{
				ForceFromFriction.Y = -AdhesiveLimit * 0.7f; // broke friction - want more analog feel
			}

			// wheel rolling - just match the ground speed exactly
			if (BrakeFactor < 1.0f)
			{
				Omega *= BrakeFactor;
			}
			else
			{
				float GroundOmega = GroundVelocityVector.X / Re;
				Omega += (GroundOmega - Omega);
			}

		}
		// Wheel angular position
		AngularPosition += Omega * DeltaTime;

		while (AngularPosition >= PI * 2.f)
		{
			AngularPosition -= PI * 2.f;
		}
		while (AngularPosition <= -PI * 2.f)
		{
			AngularPosition += PI * 2.f;
		}

		if (!bInContact)
		{
			ForceFromFriction = FVector::ZeroVector;
		}

		return;
	}

} // namespace Chaos

#if VEHICLE_DEBUGGING_ENABLED
	PRAGMA_ENABLE_OPTIMIZATION
#endif
