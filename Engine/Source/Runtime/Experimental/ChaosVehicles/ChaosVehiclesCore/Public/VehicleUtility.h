// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// Disable Optimizations in non debug build configurations
#define VEHICLE_DEBUGGING_ENABLED 0

namespace Chaos
{

	struct RealWorldConsts
	{
		FORCEINLINE static float AirDensity()
		{
			return 1.225f; // kg / m3;
		}

		FORCEINLINE static float HalfAirDensity()
		{
			return 0.6125f; // kg / m3;
		}

		FORCEINLINE static float DryRoadFriction()
		{
			return 0.7f; // friction coefficient
		}

		FORCEINLINE static float WetRoadFriction()
		{
			return 0.4f; // friction coefficient
		}


	};

	class FVehicleUtility
	{
	public:

		/** clamp value between 0 and 1 */
		FORCEINLINE static void ClampNormalRange(float& InOutValue)
		{
			InOutValue = FMath::Clamp(InOutValue, 0.f, 1.f);
		}

		FORCEINLINE static float CalculateInertia(float Mass, float Radius)
		{
			return (0.5f * Mass * Radius * Radius);
		}
	};

	/** revolutions per minute to radians per second */
	FORCEINLINE float RPMToOmega(float RPM)
	{
		return RPM * PI / 30.f;
	}

	/** radians per second to revolutions per minute */
	FORCEINLINE float OmegaToRPM(float Omega)
	{
		return Omega * 30.f / PI;
	}

	/** km/h to cm/s */
	FORCEINLINE float KmHToCmS(float KmH)
	{
		return KmH * 100000.f / 3600.f;
	}

	/** cm/s to km/h */
	FORCEINLINE float CmSToKmH(float CmS)
	{
		return CmS * 3600.f / 100000.f;
	}

	/** cm/s to miles per hour */
	FORCEINLINE float CmSToMPH(float CmS)
	{
		return CmS * 2236.94185f / 100000.f;
	}

	/** miles per hour to cm/s */
	FORCEINLINE float MPHToCmS(float MPH)
	{
		return MPH * 100000.f / 2236.94185f;
	}

	/** miles per hour to meters per second */
	FORCEINLINE float MPHToMS(float MPH)
	{
		return MPH * 1609.34f / 3600.f;
	}

	/** meters per second to miles per hour */
	FORCEINLINE float MSToMPH(float MS)
	{
		return MS * 3600.f / 1609.34f;
	}

	/** cm to meters */
	FORCEINLINE float CmToM(float Cm)
	{
		return Cm * 0.01f;
	}

	/** meters to cm */
	FORCEINLINE float MToCm(float M)
	{
		return M * 100.0f;
	}

	/** Km to miles */
	FORCEINLINE float KmToMile(float Km)
	{
		return Km * 0.62137f;
	}

	/** miles to Km */
	FORCEINLINE float MileToKm(float Miles)
	{
		return Miles * 1.60934f;
	}

	/** meters squared to cm squared */
	FORCEINLINE float M2ToCm2(float M2)
	{
		return M2 * 100.f * 100.f;
	}

	/** cm squared to meters squared */
	FORCEINLINE float Cm2ToM2(float Cm2)
	{
		return Cm2 / (100.f * 100.f);
	}

	FORCEINLINE float DegToRad(float InDeg)
	{
		return InDeg * PI / 180.f;
	}

	FORCEINLINE float RadToDeg(float InRad)
	{
		return InRad * 180.f / PI;
	}

} // namespace Chaos