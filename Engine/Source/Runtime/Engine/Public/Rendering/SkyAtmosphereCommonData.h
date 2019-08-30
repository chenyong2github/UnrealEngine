// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SkyAtmosphereCommonData.h
=============================================================================*/

#pragma once

#include "CoreMinimal.h"



class USkyAtmosphereComponent;



struct FAtmosphereSetup
{
	float BottomRadius;
	float TopRadius;

	float MultiScatteringFactor;

	FLinearColor RayleighScattering;
	float RayleighDensityExpScale;

	FLinearColor MieScattering;
	FLinearColor MieExtinction;
	FLinearColor MieAbsorption;
	float MieDensityExpScale;
	float MiePhaseG;

	FLinearColor AbsorptionExtinction;
	float AbsorptionDensity0LayerWidth;
	float AbsorptionDensity0ConstantTerm;
	float AbsorptionDensity0LinearTerm;
	float AbsorptionDensity1ConstantTerm;
	float AbsorptionDensity1LinearTerm;

	FLinearColor GroundAlbedo;

	ENGINE_API FAtmosphereSetup(const USkyAtmosphereComponent& SkyAtmosphereComponent);

	ENGINE_API FLinearColor GetTransmittanceAtGroundLevel(const FVector& SunDirection) const;
};


