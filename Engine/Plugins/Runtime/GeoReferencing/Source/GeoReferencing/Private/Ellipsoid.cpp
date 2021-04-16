// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ellipsoid.h"

FEllipsoid::FEllipsoid()
	: FEllipsoid(1.0, 1.0, 1.0)
{

}

FEllipsoid::FEllipsoid(double RadiusX, double RadiusY, double RadiusZ)
	: FEllipsoid(FVector3d(RadiusX, RadiusY, RadiusZ))
{

}

FEllipsoid::FEllipsoid(const FVector3d& InRadii)
	: Radii(InRadii)
	, RadiiSquared(InRadii.X * InRadii.X, InRadii.Y * InRadii.Y, InRadii.Z * InRadii.Z)
{
	check(InRadii.X != 0 && InRadii.Y != 0 && InRadii.Z != 0);

	OneOverRadii = FVector3d(1.0 / InRadii.X, 1.0 / InRadii.Y, 1.0 / InRadii.Z);
	OneOverRadiiSquared = FVector3d(1.0 / (InRadii.X * InRadii.X), 1.0 / (InRadii.Y * InRadii.Y), 1.0 / (InRadii.Z * InRadii.Z));
}

FVector3d FEllipsoid::GeodeticSurfaceNormal(const FCartesianCoordinates& ECEFLocation) const
{
	FVector3d Normal( ECEFLocation.X * OneOverRadiiSquared.X, ECEFLocation.Y * OneOverRadiiSquared.Y, ECEFLocation.Z * OneOverRadiiSquared.Z);

	return Normal.Normalized();
}

FVector3d FEllipsoid::GeodeticSurfaceNormal(const FGeographicCoordinates& GeographicCoordinates) const
{
	double LongitudeRad = FMathd::DegToRad * GeographicCoordinates.Longitude ;
	double LatitudeRad = FMathd::DegToRad * GeographicCoordinates.Latitude;
	double cosLatitude = FMathd::Cos(LatitudeRad);

	return FVector3d(cosLatitude * FMathd::Cos(LongitudeRad), cosLatitude * FMathd::Sin(LongitudeRad), FMathd::Sin(LatitudeRad)).Normalized();
}
