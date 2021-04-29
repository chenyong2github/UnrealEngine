// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Using double precision maths from the Geometry plugin. 
#include "VectorTypes.h"
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
	FEllipsoid(const FVector3d& InRadii);

	FVector3d Radii;
	FVector3d RadiiSquared;
	FVector3d OneOverRadii;
	FVector3d OneOverRadiiSquared;

	FVector3d GeodeticSurfaceNormal(const FCartesianCoordinates& ECEFLocation) const;
	FVector3d GeodeticSurfaceNormal(const FGeographicCoordinates& GeographicCoordinates) const;    
};
