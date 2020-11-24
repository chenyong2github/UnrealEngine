// Copyright Epic Games, Inc. All Rights Reserved.

#include "GroomAssetCards.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"

FHairCardsClusterSettings::FHairCardsClusterSettings()
{
	ClusterDecimation = 0.1f;
	Type = EHairCardsClusterType::High;
	bUseGuide = true;
}

FHairCardsGeometrySettings::FHairCardsGeometrySettings()
{
	GenerationType = EHairCardsGenerationType::UseGuides;
	CardsCount = 5000;
	ClusterType = EHairCardsClusterType::High;
	MinSegmentLength = 1;
	AngularThreshold = 5;
	MinCardsLength = 0;
	MaxCardsLength = 0;
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
	Version = 0;
}

FHairGroupsCardsSourceDescription::FHairGroupsCardsSourceDescription()
{
	MaterialSlotName = NAME_None;
	SourceType = EHairCardsSourceType::Imported;
	ProceduralMeshKey.Empty();
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
		MinSegmentLength == A.MinSegmentLength &&
		AngularThreshold == A.AngularThreshold &&
		MinCardsLength == A.MinCardsLength &&
		MaxCardsLength == A.MaxCardsLength &&
		GenerationType == A.GenerationType &&
		CardsCount == A.CardsCount &&
		ClusterType == A.ClusterType;
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
		TextureSettings == A.TextureSettings &&
		Version == A.Version;
}

void FHairGroupsProceduralCards::BuildDDCKey(FArchive& Ar)
{
	Ar << GeometrySettings.GenerationType;
	if (GeometrySettings.GenerationType == EHairCardsGenerationType::CardsCount)
	{
		Ar << GeometrySettings.CardsCount;
	}
	Ar << GeometrySettings.ClusterType;
	Ar << GeometrySettings.MinSegmentLength;
	Ar << GeometrySettings.AngularThreshold;
	Ar << GeometrySettings.MinCardsLength;
	Ar << GeometrySettings.MaxCardsLength;

	Ar << TextureSettings.AtlasMaxResolution;
	Ar << TextureSettings.PixelPerCentimeters;
	Ar << TextureSettings.LengthTextureCount;
	Ar << Version;
}

bool FHairGroupsCardsSourceDescription::operator==(const FHairGroupsCardsSourceDescription& A) const
{
	return
		MaterialSlotName == A.MaterialSlotName &&
		SourceType == A.SourceType &&
		ProceduralSettings == A.ProceduralSettings &&
		GroupIndex == A.GroupIndex &&
		LODIndex == A.LODIndex &&
		ImportedMesh == A.ImportedMesh;
}

FString FHairGroupsCardsSourceDescription::GetMeshKey() const
{
#if WITH_EDITORONLY_DATA
	if (SourceType == EHairCardsSourceType::Imported && ImportedMesh)
	{
		ImportedMesh->ConditionalPostLoad();
		FStaticMeshSourceModel& SourceModel = ImportedMesh->GetSourceModel(0);
		if (SourceModel.MeshDescriptionBulkData)
		{
			return SourceModel.MeshDescriptionBulkData->GetIdString();
		}

	}
	else if (SourceType == EHairCardsSourceType::Procedural && ProceduralMesh)
	{
		ProceduralMesh->ConditionalPostLoad();
		FStaticMeshSourceModel& SourceModel = ProceduralMesh->GetSourceModel(0);
		if (SourceModel.MeshDescriptionBulkData)
		{
			return SourceModel.MeshDescriptionBulkData->GetIdString();
		}
	}
#endif
	return TEXT("INVALID_MESH");
}

bool FHairGroupsCardsSourceDescription::HasMeshChanged() const
{
#if WITH_EDITORONLY_DATA
	if (SourceType == EHairCardsSourceType::Imported && ImportedMesh)
	{
		return ImportedMeshKey == GetMeshKey();
	}
	else if (SourceType == EHairCardsSourceType::Procedural && ProceduralMesh)
	{
		ProceduralMesh->ConditionalPostLoad();
		return ProceduralMeshKey == GetMeshKey();
	}
#endif
	return false;
}

void FHairGroupsCardsSourceDescription::UpdateMeshKey()
{
#if WITH_EDITORONLY_DATA
	if (SourceType == EHairCardsSourceType::Imported && ImportedMesh)
	{
		ImportedMeshKey = GetMeshKey();
	}
	else if (SourceType == EHairCardsSourceType::Procedural && ProceduralMesh)
	{
		ProceduralMeshKey = GetMeshKey();
	}
#endif
}