// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VehicleSystemTemplate.h"
#include "VehicleUtility.h"

#if VEHICLE_DEBUGGING_ENABLED
PRAGMA_DISABLE_OPTIMIZATION
#endif

namespace Chaos
{
	enum class FAerofoilType : uint8
	{
		Fixed = 0,
		Wing,
		Rudder,
		Elevator
	};

	struct FAerofoilConfig
	{
		FAerofoilConfig()
			: Offset(FVector(0.f, 0.f, 0.0f))
			, UpAxis(FVector(0.f, 0.f, 1.f))
			, Area(5.0f)
			, Camber(3.0f)
			, MaxControlAngle(1.f)
			, StallAngle(16.0f)
			, MaxCeiling(1E30)
			, MinCeiling(-1E30)
			, Type(FAerofoilType::Fixed)
			, LiftMultiplier(1.0f)
			, DragMultiplier(1.0f)
		{

		}

		FVector Offset;
		FVector UpAxis;
		float Area;
		float Camber;
		float MaxControlAngle;
		float StallAngle;

		float MaxCeiling;
		float MinCeiling;

		FAerofoilType Type;
		float LiftMultiplier;
		float DragMultiplier;
	};

	class FAerofoil : public TVehicleSystem<FAerofoilConfig>
	{
	public:
		FAerofoil()
		{}

		FAerofoil(const FAerofoilConfig* SetupIn)
			: TVehicleSystem<FAerofoilConfig>(SetupIn)
			, CurrentAirDensity(RealWorldConsts::AirDensity())
			, AngleOfAttack(0.f)
			, ControlSurfaceAngle(0.f)
			, AirflowNormal(FVector::ZeroVector)
			, AerofoilId(0)
		{
		}

		/** Set a debug Id so we can identify an individual aerofoil */
		void SetAerofoilId(int Id)
		{
			AerofoilId = Id;
		}

		void SetControlSurface(float CtrlSurfaceInput)
		{
			ControlSurfaceAngle = CtrlSurfaceInput * Setup().MaxControlAngle;
		}

		void SetDensityOfMedium(float InDensity)
		{
			CurrentAirDensity = InDensity;
		}

		FVector GetAxis()
		{
			return Setup().UpAxis;
		}

		FVector GetOffset()
		{
			return Setup().Offset;
		}

		FVector GetCenterOfLiftOffset()
		{
			float X = 0.0f;

			if (Setup().Type == FAerofoilType::Wing)
			{
				X = (CalcCentreOfLift() - 50.0f) / 100.0f;
			}

			return Setup().Offset + FVector(X, 0.0f, 0.0f);
		}

		// returns the combined force of lift and drag at an aerofoil in world coordinates
		// for direct application to the aircrafts rigid body.
		FVector GetForce(FTransform& BodyTransform, const FVector& v, float Altitude, float DeltaTime)
		{
			FVector Force(0.0f, 0.0f, 0.0f);

			float AirflowMagnitudeSqr = v.SizeSquared();

			// can only generate lift if there is airflow over aerofoil, early out
			if (FMath::Abs(AirflowMagnitudeSqr) < SMALL_NUMBER)
			{
				return Force;
			}

			// airflow direction in opposite direction to vehicle direction of travel
			AirflowNormal = -v;
			AirflowNormal.Normalize();

			// determine angle of attack for control surface
			AngleOfAttack = CalcAngleOfAttackDegrees(Setup().UpAxis, AirflowNormal);

			// Aerofoil Camber and Control Surface just lumped together
			float TotalControlAngle = ControlSurfaceAngle + Setup().Camber;

			// dynamic pressure dependent on speed, altitude (air pressure)
			float Common = Setup().Area * CalcDynamicPressure(AirflowMagnitudeSqr, Altitude);

			// Lift and Drag coefficients are based on the angle of attack and Control Angle
			float LiftCoef = CalcLiftCoefficient(AngleOfAttack, TotalControlAngle) * Setup().LiftMultiplier;
			float DragCoef = CalcDragCoefficient(AngleOfAttack, TotalControlAngle) * Setup().DragMultiplier;

			// Combine to create a single force vector
			Force = Setup().UpAxis * (Common * LiftCoef) + AirflowNormal * (Common * DragCoef);

			return Force;
		}

		/**
		 * Dynamic air pressure = 0.5 * AirDensity * Vsqr
		 * This function reduces the dynamic pressure in a linear fashion with altitude between 
		 * MinCeiling & MaxCeiling in order to limit the aircrafts altitude with a 'natural feel'
		 * without having a hard limit
		 */
		float CalcDynamicPressure(float VelocitySqr, float InAltitude)
		{
			float AltitudeMultiplierEffect = 1.0f;

			//if (InAltitude > Setup().MaxCeiling)
			//{
			//	AltitudeMultiplierEffect = 0.4f;
			//}
			//else if (InAltitude > Setup().MinCeiling)
			//{
			//	AltitudeMultiplierEffect = (Setup().MaxCeiling - InAltitude) / (Setup().MaxCeiling - Setup().MinCeiling);
			//}

			//FMath::Clamp(AltitudeMultiplierEffect, Setup().MinCeiling, Setup().MaxCeiling);

			return AltitudeMultiplierEffect * 0.5f * CurrentAirDensity * VelocitySqr;
		}

		/**  Center of lift moves fore/aft based on current AngleOfAttack */
		float CalcCentreOfLift()
		{
			// moves backwards past stall angle
			if (AngleOfAttack > Setup().StallAngle)
			{
				return (AngleOfAttack - Setup().StallAngle) * 10.0f + 20.0f;
			}

			// moves forwards below stall angle
			return (Setup().StallAngle - AngleOfAttack) * 20.0f / Setup().StallAngle + 20.0f;
		}

		/** Returns drag coefficient for the current angle of attack of the aerofoil surface */
		float CalcDragCoefficient(float InAngleOfAttack, float InControlSurfaceAngle)
		{
			if (InAngleOfAttack > 90.f)
			{
				InAngleOfAttack = 180.f - InAngleOfAttack;
			}

			if (InAngleOfAttack < -90.f)
			{
				InAngleOfAttack = -180.f - InAngleOfAttack;
			}

			float Value = (InAngleOfAttack + InControlSurfaceAngle) / (Setup().StallAngle + FMath::Abs(InControlSurfaceAngle));
			return (0.05f + Value* Value);
		}

		/**
		 * Returns lift coefficient for the current angle of attack of the aerofoil surface
		 * Cheating by making control surface part of entire aerofoil movement
		 */
		float CalcLiftCoefficient(float InAngleOfAttack, float InControlSurfaceAngle)
		{
			float PeakValue = 2.0f; // typically the Coefficient can reach this peak value
			float TotalAngle = InAngleOfAttack + InControlSurfaceAngle;

			if (FMath::Abs(TotalAngle) > (Setup().StallAngle * 2.0f))
			{
				return 0.0f;
			}

			return FMath::Sin(TotalAngle * (PI * 0.5f) / Setup().StallAngle) * PeakValue;
		}

		/** Angle of attack is the angle between the aerofoil and the airflow vector */
		float CalcAngleOfAttackDegrees(const FVector& UpAxis, const FVector& InAirflowVector)
		{
			float fMag = FVector::DotProduct(UpAxis, InAirflowVector);
			return RadToDeg(FMath::Asin(fMag));
		}

		float CurrentAirDensity;
		float AngleOfAttack;
		float ControlSurfaceAngle;
		FVector AirflowNormal;
		int AerofoilId;
	};

} // namespace Chaos

#if VEHICLE_DEBUGGING_ENABLED
PRAGMA_ENABLE_OPTIMIZATION
#endif