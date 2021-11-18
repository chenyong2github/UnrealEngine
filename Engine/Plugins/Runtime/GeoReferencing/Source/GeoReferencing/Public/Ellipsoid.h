// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "GeographicCoordinates.h"
#include "CartesianCoordinates.h"

#include "Ellipsoid.generated.h"

USTRUCT(BlueprintType)
struct GEOREFERENCING_API FEllipsoid
{
	GENERATED_USTRUCT_BODY()

public:
	FEllipsoid();
	FEllipsoid(double RadiusX, double RadiusY, double RadiusZ);
	FEllipsoid(const FVector& InRadii);

	FVector Radii;
	FVector RadiiSquared;
	FVector OneOverRadii;
	FVector OneOverRadiiSquared;

	FVector GeodeticSurfaceNormal(const FCartesianCoordinates& ECEFLocation) const;
	FVector GeodeticSurfaceNormal(const FGeographicCoordinates& GeographicCoordinates) const;    
};
