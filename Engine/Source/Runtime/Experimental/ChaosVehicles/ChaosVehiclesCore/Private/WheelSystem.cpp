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
		, Spin(0.f)
	{

	}

	void FSimpleWheelSim::Simulate(float DeltaTime)
	{
		SlipAngle = FMath::Atan2(GroundVelocityVector.Y, GroundVelocityVector.X);

		float FinalLongitudinalForce = 0.f;

		// The physics system is mostly unit-less i.e. can work in meters or cm, however there are 
		// a couple of places where the results are wrong if Cm is used. This is one of them, the simulated radius
		// for torque must be real size to obtain the correct output values.
		AppliedLinearDriveForce = DriveTorque / CmToM(Re);
		AppliedLinearBrakeForce = BrakeTorque / CmToM(Re);

		// currently just letting the brake override the throttle
		bool Braking = BrakeTorque > FMath::Abs(DriveTorque);
		float BrakeFactor = 1.0f;
		float K = 0.4f;
		
		// are we actually touching the ground
		if (ForceIntoSurface > SMALL_NUMBER)
		{
			LongitudinalAdhesiveLimit = ForceIntoSurface * SurfaceFriction * Setup().LongitudinalFrictionMultiplier;
			LateralAdhesiveLimit = ForceIntoSurface * SurfaceFriction * Setup().LateralFrictionMultiplier;

			if (Braking)
			{

				// whether the velocity is +ve or -ve when we brake we are slowing the vehicle down
				// so force is opposing current direction of travel.
				float ForceRequiredToBringToStop = MassPerWheel * K * (GroundVelocityVector.X) / DeltaTime;
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
			float FinalLateralForce = -(MassPerWheel * K * GroundVelocityVector.Y) / DeltaTime;

			ForceFromFriction.X = FinalLongitudinalForce;

			float DynamicFrictionLongitudialScaling = 0.75f;
			float TractionControlAndABSScaling = 0.98f;	// how close to perfection is the system working

			SideSlipModifier = 1.0f;
			bool Locked = false;
			bool Spinning = false;
			
			// we can only obtain as much accel/decel force as the friction will allow
			if (FMath::Abs(FinalLongitudinalForce) > LongitudinalAdhesiveLimit)
			{
				if (Braking)
				{
					BrakeFactor = FMath::Clamp(LongitudinalAdhesiveLimit / FMath::Abs(FinalLongitudinalForce), 0.6f, 1.0f);
				}

				if ((Braking && Setup().ABSEnabled) || (!Braking && Setup().TractionControlEnabled))
				{
					Spin = 0.0f;
					ForceFromFriction.X = LongitudinalAdhesiveLimit * TractionControlAndABSScaling;
				}
				else
				{
					if (!Braking)
					{
						Spinning = true;
						Spin += 0.5f * DeltaTime;
						Spin = FMath::Clamp(Spin, -2.f, 2.f);
					}
					else
					{
						Locked = true;
					}
					ForceFromFriction.X = LongitudinalAdhesiveLimit * DynamicFrictionLongitudialScaling;
				}
			}
			else
			{
				Spin = 0.0f;
			}

			if (FinalLongitudinalForce < -LongitudinalAdhesiveLimit)
			{
				ForceFromFriction.X = -ForceFromFriction.X;
			}

			static float DynamicFrictionLateralScaling = 0.75f;
			if (Locked || Spinning)
			{
				SideSlipModifier *= Setup().SideSlipModifier;
			}

			// Lateral needs more grip to feel right!
			LateralAdhesiveLimit *= 1.0f * SideSlipModifier;
			ForceFromFriction.Y = FinalLateralForce;
			if (FMath::Abs(FinalLateralForce) > LateralAdhesiveLimit)
			{
				ForceFromFriction.Y = LateralAdhesiveLimit * DynamicFrictionLateralScaling;
			}

			if (FinalLateralForce < -LateralAdhesiveLimit)
			{
				ForceFromFriction.Y = -ForceFromFriction.Y;
			}


			// wheel rolling - just match the ground speed exactly
			if (BrakeFactor < 1.0f)
			{
				Omega *= BrakeFactor;
			}
			else if (Spin > 0.1f)
			{
				Omega += Spin;
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
