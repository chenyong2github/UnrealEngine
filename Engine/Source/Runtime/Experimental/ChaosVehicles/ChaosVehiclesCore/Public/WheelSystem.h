// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VehicleSystemTemplate.h"
#include "VehicleUtility.h"
#include "TireSystem.h"

#if VEHICLE_DEBUGGING_ENABLED
PRAGMA_DISABLE_OPTIMIZATION
#endif

namespace Chaos
{

/**
 * Tire load changes, when cornering outer tires are loaded and inner ones unloaded
 * Similarly load changes when accelerating and breaking.
 * Fx : brake/drive force
 * Fy : Cornering Force
 * Fz : Tire load (vehicle weight)
 *
 * Mx : overturning moment
 * My : moment about brake/drive torque
 * Mz : self-aligning moment
 *
 * Fx : forward speed at wheel center
 *
 *
 * Omega : Rotational Speed [radians/sec]
 * Alpha : Slip Angle [radians]
 * k or Sx: Longitudinal Slip - slip is -ve when braking and +ve when accelerating
 * Re : Effective Wheel Radius
 */

/**
* Wheel setup data that doesn't change during simulation
*/
struct FSimpleWheelConfig
{
	// #todo: use this
	enum EWheelDamageStatus
	{
		NONE,
		BUCKLED,
		FLAT,
		MISSING
	};

	// #todo: use this
	enum EWheelSimulationStatus
	{
		ROLLING,	// wheel speed matches the vehicle ground speed
		SPINNING,	// wheel speed faster than vehicle ground speed
		LOCKED		// wheel is locked and sliding over surface
	};

	// #todo: use this
	enum EFrictionCombineMethod
	{
		Multiply, // default - most correct
		Average
	};
	
	FSimpleWheelConfig() 
		: Offset(FVector(2.f, 1.f, 0.f))
		, WheelMass(20.f) // [Kg]
		, WheelRadius(30.f)
		, WheelWidth(20.f)
		, WheelInertia(20.0f/*VehicleUtility::CalculateInertia(WheelMass, WheelRadius)*/)
		, MaxSteeringAngle(70)
		, MaxBrakeTorque(2000.f)
		, HandbrakeTorque(1000.f)
		, ABSEnabled(false)
		, BrakeEnabled(true)
		, HandbrakeEnabled(true)
		, SteeringEnabled(true)
		, EngineEnabled(false)
		, CheatFrictionForce(1.0f)
		, FrictionCombineMethod(EFrictionCombineMethod::Multiply)
	//	, SingleWheel(false)
	{

	}

	// Basic
	//FSimpleTireConfig Tire;

	// wheel tire
	FVector Offset;
	float WheelMass;			// Mass of wheel [Kg]
	float WheelRadius;			// [cm]
	float WheelWidth;			// [cm]
	float WheelInertia;			// [Kg.m2] = 0.5f * Mass * Radius * Radius

	int	 MaxSteeringAngle;		// Yaw angle of steering [Degrees]

	// brakes
	float MaxBrakeTorque;		// Braking Torque [Nm]
	float HandbrakeTorque;		// Handbrake Torque [Nm]
	bool ABSEnabled;					// Advanced braking system operational

	// setup
	bool BrakeEnabled;			// Regular brakes are enabled for this wheel
	bool HandbrakeEnabled;		// Handbrake is operational on this wheel
	bool SteeringEnabled;		// Steering is operational on this wheel
	bool EngineEnabled;			// Wheel is driven by an engine

	float CheatFrictionForce;
	EFrictionCombineMethod FrictionCombineMethod; //#todo: use this variable
	
	// #todo: simulated Damage
	//EWheelDamageStatus DamageStatus;
	//float BuckleAngle;
};


/**
* Wheel instance data changes during the simulation
*/
class FSimpleWheelSim : public TVehicleSystem<FSimpleWheelConfig>
{
public:

	FSimpleWheelSim(const FSimpleWheelConfig* SetupIn) 
		: TVehicleSystem<FSimpleWheelConfig>(SetupIn)
		, Re(0.3f)
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
		, SlipVelocity(0.f)
		, SlipAngle(0.f)
		, bInContact(false)
		, WheelIndex(0)
	{
	}

// Inputs

	/** Set the wheel radius - can change dynamically during simulation if desired */
	void SetWheelRadius(float NewRadius)
	{
		Re = NewRadius;
	}

	/** set wheel rotational speed to match the specified linear forwards speed */
	void SetMatchingSpeed(float LinearMetersPerSecondIn)
	{
		Omega = LinearMetersPerSecondIn / Re;
	}

	/** Set the braking torque - decelerating rotational force */
	void SetBrakeTorque(float BrakeTorqueIn)
	{
		BrakeTorque = BrakeTorqueIn;
	}

	/** Set the drive torque - accelerating rotational force */
	void SetDriveTorque(float EngineTorqueIn)
	{
		DriveTorque = EngineTorqueIn;
	}

	/** Set the vehicle's speed at the wheels location in local wheel coords */
	void SetVehicleGroundSpeed(FVector& VIn)
	{
		GroundVelocityVector = VIn;
	}

	/** Set the force pressing the wheel into the terrain - from suspension */
	void SetWheelLoadForce(float WheelLoadForceIn)
	{
		ForceIntoSurface = WheelLoadForceIn;
		
		if (ForceIntoSurface > SMALL_NUMBER)
		{
			bInContact = true;
		}
		else
		{
			bInContact = false;
		}
	}

	/** Set the friction coefficient of the surface under the wheel */
	void SetSurfaceFriction(float InFriction)
	{
		SurfaceFriction = InFriction;
	}

	void SetOnGround(bool OnGround)
	{
		bInContact = OnGround;
	}

	void SetSteeringAngle(float InAngle)
	{
		SteeringAngle = InAngle;
	}

	void SetMaxOmega(float InMaxOmega)
	{
		MaxOmega = InMaxOmega;
	}

	void SetWheelIndex(uint32 InIndex)
	{
		WheelIndex = InIndex;
	}

// Outputs

	/**
	 * Amount of friction we can expect after taking into account the amount the wheel slips
	 */
	static float GetNormalisedFrictionFromSlipAngle(float SlipIn)
	{
		FVehicleUtility::ClampNormalRange(SlipIn);

		// typical slip angle graph; normalized scales
		// Friction between 0 and 1 for values of slip between 0 and 1
		float FunctionResult = 1.125f * (1.0f - exp(-20.0f * SlipIn)) - 0.25f * SlipIn;
		return FMath::Max(0.0f, FMath::Min(1.0f, FunctionResult));
	}

	/** return the calculated available friction force */
	FVector GetForceFromFriction() const
	{
		return ForceFromFriction;
	}

	/** Get the radius of the wheel [cm] */
	float GetEffectiveRadius() const
	{
		return Re;
	}

	/** Get the angular position of the wheel [radians] */
	float GetAngularPosition() const
	{
		return AngularPosition;
	}

	/** Get the angular velocity of the wheel [radians/sec] */
	float GetAngularVelocity() const
	{
		return Omega;
	}

	/** Get the wheel RPM [revolutions per minute] */
	float GetWheelRPM()
	{
		return OmegaToRPM(Omega);
	}

	/** Is the wheel in contact with the terrain or another object */
	bool InContact() const
	{
		return bInContact;
	}

	float GetSteeringAngle() const
	{
		return SteeringAngle;
	}

	/** Get the current longitudinal slip value [0 no slip - using static friction, 1 full slip - using dynamic friction] */
	float GetNormalizedLongitudinalSlip() const
	{
		return Sx;
	}

	float GetNormalizedLateralSlip() const
	{
		return FMath::Clamp(RadToDeg(SlipAngle) / 30.0f, 0.f, 1.f);
	}

	/** Get the magnitude of the force pressing the wheel into the terrain */
	float GetWheelLoadForce() const
	{
		return ForceIntoSurface;
	}

	/** Get the friction coefficient of the surface in contact with the wheel */
	float GetSurfaceFriction() const
	{
		return SurfaceFriction;
	}

	/** Get the slip angle for this wheel - angle between wheel forward axis and velocity vector [degrees] */
	float GetSlipAngle() const
	{
		return SlipAngle;
	}

	/** Get the drive torque being applied to the wheel [N.m] */
	float GetDriveTorque() const
	{
		return DriveTorque;
	}

	/** Get the braking torque being applied to the wheel [N.m] */
	float GetBrakeTorque() const
	{
		return BrakeTorque;
	}

	/** Get the road speed at the wheel */
	float GetRoadSpeed() const
	{
		return GroundVelocityVector.X;
	}

	/** Get the linear ground speed of the wheel based on its current rotational speed */
	float GetWheelGroundSpeed() const
	{
		return Omega * Re;
	}

	/** 
	 * Simulate - figure out wheel lateral and longitudinal forces based on available friction at the wheel
	 *	Wheel load force from body weight and the surface friction together determine the grip available at the wheel
	 *	DriveTorque accelerates the wheel
	 *	BrakeTorque decelerates the wheel
	 *
	 *	#todo: this function is a mess and needs tidied/consolidated
	 *	#todo: lateral friction is currently not affected by longitudinal wheel slip
	 *	#todo: wheel slip angle isn't being used
	 */
	void Simulate(float DeltaTime)
	{
		SlipAngle = FMath::Atan2(GroundVelocityVector.Y, GroundVelocityVector.X);

		ForceFromFriction = FVector::ZeroVector;
		float NormalizedFrictionFromSlip = 1.0f;// RealWorldConsts::DryRoadFriction();
		Sx = 0.2f;
		//if (FMath::Abs(DriveTorque) < SMALL_NUMBER && FMath::Abs(BrakeTorque) < SMALL_NUMBER)
		//{
		//	BrakeTorque = 10.0f; // loss in transmission system - until we hook one up.
		//}

		float FinalTorque = 0.f;
		float AbsDriveTorque = FMath::Abs(DriveTorque);
		if (BrakeTorque > AbsDriveTorque)
		{
			BrakeTorque = BrakeTorque - AbsDriveTorque;
		}
		else
		{
			//DriveTorque = DriveTorque;// -BrakeTorque;
			BrakeTorque = 0.f;
		}


		//	if (bInContact)
		{
			float Direction = GroundVelocityVector.X > 0 ? -1.f : 1.f;
			if (BrakeTorque > SMALL_NUMBER && FMath::Abs(GroundVelocityVector.X) > SMALL_NUMBER)
			{
				if (Omega >= 0.f)
				{
					// this is only correct for GroundVelocityVector.X >= 0
					Omega -= (BrakeTorque * DeltaTime) / Setup().WheelMass;
					if (Omega < 0.f)
					{
						Omega = 0.f;	// wheel has locked
					}
				}
				else
				{
					// this is only correct for GroundVelocityVector.X < 0
					Omega += (BrakeTorque * DeltaTime) / Setup().WheelMass;
					if (Omega > 0.f)
					{
						Omega = 0.f;	// wheel has locked
					}
				}


				float diff = GroundVelocityVector.X - Omega * Re;
				Sx = FMath::Abs((GroundVelocityVector.X - Omega * Re) / GroundVelocityVector.X);
				NormalizedFrictionFromSlip = GetNormalisedFrictionFromSlipAngle(Sx) * SurfaceFriction;

				ForceFromFriction.X = Direction * NormalizedFrictionFromSlip * ForceIntoSurface;
			}
			else
			{
				if (DriveTorque > SMALL_NUMBER)
				{
					Omega += (DriveTorque * DeltaTime) / Setup().WheelMass;

					Sx = 0.0f;
					if (GroundVelocityVector.X > SMALL_NUMBER)
					{
						Sx = (Omega * Re - GroundVelocityVector.X) / GroundVelocityVector.X;
					}
					else
					{
						Sx = Omega * Re;
					}
					// FVehicleUtility::ClampNormalRange(Sx);

					NormalizedFrictionFromSlip = GetNormalisedFrictionFromSlipAngle(Sx) * SurfaceFriction;
					ForceFromFriction.X = NormalizedFrictionFromSlip * ForceIntoSurface;
				}
				else
				{
					if (DriveTorque < -SMALL_NUMBER)
					{
						Omega += (DriveTorque * DeltaTime) / Setup().WheelMass;

						Sx = 0.0f;
						if (FMath::Abs(GroundVelocityVector.X) > SMALL_NUMBER)
						{
							Sx = (Omega * Re + GroundVelocityVector.X) / GroundVelocityVector.X;
						}
						else
						{
							Sx = Omega * Re;
						}
						Sx = FMath::Abs(Sx);
					//	FVehicleUtility::ClampNormalRange(Sx);

						NormalizedFrictionFromSlip = GetNormalisedFrictionFromSlipAngle(Sx) * SurfaceFriction;
						ForceFromFriction.X = -NormalizedFrictionFromSlip * ForceIntoSurface;
					}
					else
					{
						if (InContact())
						{
							// wheel rolling - just match the ground speed exactly
							float GroundOmega = GroundVelocityVector.X / Re;
							Omega += GroundOmega - Omega;
						}
						else
						{
							Omega *= 0.995f; // friction slowing wheel down
						}
					}
				}

			}
		}


		// lateral slip - cheat f = mass * v / dt;
		// cancel out any lateral wheel movement
		// this is not correct
		static float QuarterMass = 1500.0f * 0.25f;
		float KillSpeed = -0.25f * QuarterMass * GroundVelocityVector.Y / DeltaTime;

		float SlipAngleEffect = 1.0f;
		//SlipAngleEffect = (FMath::Abs(RadToDeg(SlipAngle)) > 20.f) ? 0.3f : 1.0f;

		//float SlipAngleEffect = GetNormalisedFrictionFromSlipAngle(RadToDeg(SlipAngle) / 20.0f);
		//float SlipAngleEffect = GetNormalisedFrictionFromSlipAngle(Sx);

		float FrictionFromSlipSide = /*GetNormalisedFrictionFromSlipAngle(GetNormalizedLateralSlip()) * */ SurfaceFriction;
		float ForceFromSideSlip = FrictionFromSlipSide * ForceIntoSurface;

		//////////////////////////////////////////////////////////////////////////
		//if (Handbrake)
		//{
		//	FVector KillSpeedForce = -0.5f * QuarterMass * GroundVelocityVector / DeltaTime;
		//	float Length = KillSpeedForce.Size();
		//	float ScaleBack = SurfaceFriction / Length;
		//	KillSpeedForce *= ScaleBack;
		//	ForceFromFriction = KillSpeedForce;
		//}
		//else
		{

			float F = 0.f;
			if (KillSpeed > 0.f)
			{
				F = FMath::Min(ForceFromSideSlip, KillSpeed);
			}
			else
			{
				F = FMath::Max(-ForceFromSideSlip, KillSpeed);

			}
			ForceFromFriction.Y = F;
		}
		//////////////////////////////////////////////////////////////////////////

	//	Omega = FMath::Clamp(Omega, -MaxOmega, MaxOmega);

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
	}

public:

	float Re;		// [cm] Effective Wheel Radius could change dynamically if get a flat?, tire shreds
	float Omega;	// [radians/sec] Wheel Rotation Angular Velocity
	float Sx;
	// In
	float DriveTorque;				// [N.m]
	float BrakeTorque;				// [N.m]
	float ForceIntoSurface;			// [N]
	FVector GroundVelocityVector;	// [Unreal Units cm.s-1]
	float AngularPosition;			// [radians]
	float SteeringAngle;			// [degrees ATM]
	float SurfaceFriction;
	float MaxOmega;

	FVector ForceFromFriction;

	// Not sure about these here
	//float BrakeInput;			// Normalised [0 to 1]
	//float HandbrakeInput;		// Normalised [0 to 1]
	//float SteeringInput;		// Normalised [0 to 1]

//	float CurrentAngularVelocity;	// [radians/second]
	// Wheel transform

	float SlipVelocity;			// Relative velocity between tire patch and ground ?? vector ??
	float SlipAngle;			// Angle between wheel forwards and velocity vector
	bool bInContact;			// Is tire in contact with the ground or free in the air
	uint32 WheelIndex;			// purely for debugging purpoese
};


} // namespace Chaos

#if VEHICLE_DEBUGGING_ENABLED
PRAGMA_ENABLE_OPTIMIZATION
#endif
