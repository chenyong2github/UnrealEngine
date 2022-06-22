// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractureModeSettings.h"

#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionConvexPropertiesInterface.h"

void UFractureModeSettings::ApplyDefaultConvexSettings(FGeometryCollection& GeometryCollection) const
{
	FGeometryCollectionConvexPropertiesInterface::FConvexCreationProperties Properties = GeometryCollection.GetConvexProperties();
	Properties.FractionRemove = ConvexFractionAllowRemove;
	Properties.SimplificationThreshold = ConvexSimplificationDistanceThreshold;
	Properties.CanExceedFraction = ConvexCanExceedFraction;
	Properties.RemoveOverlaps = ConvexRemoveOverlaps;
	GeometryCollection.SetConvexProperties(Properties);
}