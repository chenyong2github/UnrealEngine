// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SkyAtmosphereCommonData.cpp
=============================================================================*/

#include "SkyAtmosphereCommonData.h"

#include "Components/SkyAtmosphereComponent.h"

//#pragma optimize( "", off )


FAtmosphereSetup::FAtmosphereSetup(const USkyAtmosphereComponent& SkyAtmosphereComponent)
{
	// Convert Tent distribution to linear curve coefficients.
	auto TentToCoefficients = [](const FTentDistribution& Tent, float& LayerWidth, float& LinTerm0, float&  LinTerm1, float& ConstTerm0, float& ConstTerm1)
	{
		if (Tent.Width > 0.0f && Tent.TipValue > 0.0f)
		{
			const float px = Tent.TipAltitude;
			const float py = Tent.TipValue;
			const float slope = Tent.TipValue / Tent.Width;
			LayerWidth = px;
			LinTerm0 = slope;
			LinTerm1 = -slope;
			ConstTerm0 = py - px * LinTerm0;
			ConstTerm1 = py - px * LinTerm1;
		}
		else
		{
			LayerWidth = 0.0f;
			LinTerm0 = 0.0f;
			LinTerm1 = 0.0f;
			ConstTerm0 = 0.0f;
			ConstTerm1 = 0.0f;
		}
	};

	BottomRadius = SkyAtmosphereComponent.BottomRadius;
	TopRadius = SkyAtmosphereComponent.BottomRadius + SkyAtmosphereComponent.AtmosphereHeight;
	GroundAlbedo = FLinearColor(SkyAtmosphereComponent.GroundAlbedo);
	MultiScatteringFactor = SkyAtmosphereComponent.MultiScatteringFactor;

	RayleighDensityExpScale = -1.0f / SkyAtmosphereComponent.RayleighExponentialDistribution;
	RayleighScattering = (SkyAtmosphereComponent.RayleighScattering * SkyAtmosphereComponent.RayleighScatteringScale).GetClamped(0.0f, 1e38f);

	MieScattering = (SkyAtmosphereComponent.MieScattering * SkyAtmosphereComponent.MieScatteringScale).GetClamped(0.0f, 1e38f);
	MieAbsorption = (SkyAtmosphereComponent.MieAbsorption * SkyAtmosphereComponent.MieAbsorptionScale).GetClamped(0.0f, 1e38f);
	MieExtinction = MieScattering + MieAbsorption;
	MiePhaseG = SkyAtmosphereComponent.MieAnisotropy;
	MieDensityExpScale = -1.0f / SkyAtmosphereComponent.MieExponentialDistribution;

	AbsorptionExtinction = (SkyAtmosphereComponent.OtherAbsorption * SkyAtmosphereComponent.OtherAbsorptionScale).GetClamped(0.0f, 1e38f);
	TentToCoefficients(SkyAtmosphereComponent.OtherTentDistribution, AbsorptionDensity0LayerWidth, AbsorptionDensity0LinearTerm, AbsorptionDensity1LinearTerm, AbsorptionDensity0ConstantTerm, AbsorptionDensity1ConstantTerm);

	TransmittanceMinLightElevationAngle = SkyAtmosphereComponent.TransmittanceMinLightElevationAngle;
}

FLinearColor FAtmosphereSetup::GetTransmittanceAtGroundLevel(const FVector& SunDirection) const
{
	// The following code is from SkyAtmosphere.usf and has been converted to lambda functions. 
	// It compute transmittance from the origin towards a sun direction. 

	auto RayIntersectSphere = [&](FVector RayOrigin, FVector RayDirection, FVector SphereOrigin, float SphereRadius)
	{
		FVector LocalPosition = RayOrigin - SphereOrigin;
		float LocalPositionSqr = FVector::DotProduct(LocalPosition, LocalPosition);

		FVector QuadraticCoef;
		QuadraticCoef.X = FVector::DotProduct(RayDirection, RayDirection);
		QuadraticCoef.Y = 2.0f * FVector::DotProduct(RayDirection, LocalPosition);
		QuadraticCoef.Z = LocalPositionSqr - SphereRadius * SphereRadius;

		float Discriminant = QuadraticCoef.Y * QuadraticCoef.Y - 4.0f * QuadraticCoef.X * QuadraticCoef.Z;

		// Only continue if the ray intersects the sphere
		FVector2D Intersections = { -1.0f, -1.0f };
		if (Discriminant >= 0)
		{
			float SqrtDiscriminant = sqrt(Discriminant);
			Intersections.X = (-QuadraticCoef.Y - 1.0f * SqrtDiscriminant) / (2 * QuadraticCoef.X);
			Intersections.Y = (-QuadraticCoef.Y + 1.0f * SqrtDiscriminant) / (2 * QuadraticCoef.X);
		}
		return Intersections;
	};

	// Nearest intersection of ray r,mu with sphere boundary
	auto raySphereIntersectNearest = [&](FVector RayOrigin, FVector RayDirection, FVector SphereOrigin, float SphereRadius)
	{
		FVector2D sol = RayIntersectSphere(RayOrigin, RayDirection, SphereOrigin, SphereRadius);
		const float sol0 = sol.X;
		const float sol1 = sol.Y;
		if (sol0 < 0.0f && sol1 < 0.0f)
		{
			return -1.0f;
		}
		if (sol0 < 0.0f)
		{
			return FMath::Max(0.0f, sol1);
		}
		else if (sol1 < 0.0f)
		{
			return FMath::Max(0.0f, sol0);
		}
		return FMath::Max(0.0f, FMath::Min(sol0, sol1));
	};

	auto OpticalDepth = [&](FVector RayOrigin, FVector RayDirection)
	{
		float TMax = raySphereIntersectNearest(RayOrigin, RayDirection, FVector(0.0f, 0.0f, 0.0f), TopRadius);

		FLinearColor OpticalDepthRGB = FLinearColor(ForceInitToZero);
		FVector VectorZero = FVector(ForceInitToZero);
		if (TMax > 0.0f)
		{
			const float SampleCount = 15.0f;
			const float SampleStep = 1.0f / 15.0f;
			const float SampleLength = SampleStep * TMax;
			for (float SampleT = 0.0f; SampleT < 1.0f; SampleT += SampleStep)
			{
				FVector Pos = RayOrigin + RayDirection * (TMax * SampleT);
				const float viewHeight = (FVector::Distance(Pos, VectorZero) - BottomRadius);

				const float densityMie = FMath::Max(0.0f, FMath::Exp(MieDensityExpScale * viewHeight));
				const float densityRay = FMath::Max(0.0f, FMath::Exp(RayleighDensityExpScale * viewHeight));
				const float densityOzo = FMath::Clamp(viewHeight < AbsorptionDensity0LayerWidth ?
					AbsorptionDensity0LinearTerm * viewHeight + AbsorptionDensity0ConstantTerm :
					AbsorptionDensity1LinearTerm * viewHeight + AbsorptionDensity1ConstantTerm,
					0.0f, 1.0f);

				FLinearColor SampleExtinction = densityMie * MieExtinction + densityRay * RayleighScattering + densityOzo * AbsorptionExtinction;
				OpticalDepthRGB += SampleLength * SampleExtinction;
			}
		}

		return OpticalDepthRGB;
	};

	// Assuming camera is along Z on (0,0,earthRadius + 500m)
	const FVector WorldPos = FVector(0.0f, 0.0f, BottomRadius + 0.5);
	FVector2D AzimuthElevation = FMath::GetAzimuthAndElevation(SunDirection, FVector::ForwardVector, FVector::LeftVector, FVector::UpVector); // TODO: make it work over the entire virtual planet with a local basis
	AzimuthElevation.Y = FMath::Max(FMath::DegreesToRadians(TransmittanceMinLightElevationAngle), AzimuthElevation.Y);
	const FVector WorldDir = FVector(FMath::Cos(AzimuthElevation.Y), 0.0f, FMath::Sin(AzimuthElevation.Y)); // no need to take azimuth into account as transmittance is symmetrical around zenith axis.
	FLinearColor OpticalDepthRGB = OpticalDepth(WorldPos, WorldDir);
	return FLinearColor(FMath::Exp(-OpticalDepthRGB.R), FMath::Exp(-OpticalDepthRGB.G), FMath::Exp(-OpticalDepthRGB.B));
}


