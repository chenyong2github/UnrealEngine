// Copyright Epic Games, Inc. All Rights Reserved.

#include "GroomAssetCards.h"

FHairCardsClusterSettings::FHairCardsClusterSettings()
{
	ClusterDecimation = 0.1f;
	Type = EHairCardsClusterType::High;
	bUseGuide = true;
}

FHairCardsGeometrySettings::FHairCardsGeometrySettings()
{
	CardsPerCluster = 1;
	MinSegmentLength = 1;
	UseCurveOrientation = 1;
};

FHairCardsTextureSettings::FHairCardsTextureSettings()
{
	AtlasMaxResolution = 2048;
	PixelPerCentimeters = 60;
	LengthTextureCount = 10;
	DensityTextureCount = 1;
};

FHairGroupsProceduralCards::FHairGroupsProceduralCards()
{
	ClusterSettings	= FHairCardsClusterSettings();
	GeometrySettings= FHairCardsGeometrySettings();
	TextureSettings	= FHairCardsTextureSettings();
}

FHairGroupsCardsSourceDescription::FHairGroupsCardsSourceDescription()
{
	Material = nullptr;
	SourceType = EHairCardsSourceType::Procedural;
	ProceduralSettings = FHairGroupsProceduralCards();
	GroupIndex = 0;
	LODIndex = -1;
}

bool FHairCardsClusterSettings::operator==(const FHairCardsClusterSettings& A) const
{
	return
		Type == A.Type &&
		ClusterDecimation == A.ClusterDecimation &&
		bUseGuide == A.bUseGuide;
}

bool FHairCardsGeometrySettings::operator==(const FHairCardsGeometrySettings& A) const
{
	return
		CardsPerCluster == A.CardsPerCluster &&
		MinSegmentLength == A.MinSegmentLength &&
		UseCurveOrientation == A.UseCurveOrientation;
}

bool FHairCardsTextureSettings::operator==(const FHairCardsTextureSettings& A) const
{
	return
		AtlasMaxResolution == A.AtlasMaxResolution &&
		PixelPerCentimeters == A.PixelPerCentimeters &&
		LengthTextureCount == A.LengthTextureCount &&
		DensityTextureCount == A.DensityTextureCount;
}

bool FHairGroupsProceduralCards::operator==(const FHairGroupsProceduralCards& A) const
{
	return
		ClusterSettings == A.ClusterSettings &&
		GeometrySettings == A.GeometrySettings &&
		TextureSettings == A.TextureSettings;
}


bool FHairGroupsCardsSourceDescription::operator==(const FHairGroupsCardsSourceDescription& A) const
{
	return
		Material == A.Material &&
		SourceType == A.SourceType &&
		ProceduralSettings == A.ProceduralSettings &&
		GroupIndex == A.GroupIndex &&
		LODIndex == A.LODIndex;
}