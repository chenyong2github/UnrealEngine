// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Engine/EngineTypes.h"
#include "RHIDefinitions.h"
#include "RenderGraph.h"
#include "HairCardsDatas.h"
#include "HairStrandsDatas.h"
#include "GroomAssetCards.h"
#include "GroomResources.h"

struct FHairGroupCardsTextures;

#if WITH_EDITOR
namespace FHairCardsBuilder
{
	bool ImportGeometry(
		const UStaticMesh* StaticMesh,
		FHairCardsDatas& OutCards,
		FHairCardsBulkData& OutBulk,
		FHairStrandsDatas& OutGuides,
		FHairCardsInterpolationBulkData& OutInterpolationBulkData);

	void ExportGeometry(
		const FHairCardsDatas& InCardsData, 
		UStaticMesh* OutStaticMesh);

	void BuildGeometry(
		const FString& LODName,
		const struct FHairStrandsDatas& InRen,
		const struct FHairStrandsDatas& InSim,
		const struct FHairGroupsProceduralCards& Settings,
		FHairCardsProceduralDatas& Out,
		FHairStrandsDatas& OutGuides,
		FHairCardsInterpolationBulkData& OutInterpolationBulk,
		FHairGroupCardsTextures& OutTextures);

	void BuildTextureAtlas(
		FHairCardsProceduralDatas* ProceduralData,
		FHairCardsRestResource* RestResource,
		FHairCardsProceduralResource* AtlasResource,
		FHairGroupCardsTextures* Textures);

	void Convert(const FHairCardsProceduralDatas& In, FHairCardsDatas& Out, FHairCardsBulkData& OutBulk);

	FString GetVersion();
}

namespace FHairMeshesBuilder
{
	void BuildGeometry(
		const struct FHairStrandsDatas& In,
		const struct FHairStrandsDatas& InSim,
		FHairMeshesDatas& Out,
		FHairMeshesBulkData& OutBulk);

	void ImportGeometry(
		const UStaticMesh* StaticMesh,
		FHairMeshesDatas& Out,
		FHairMeshesBulkData& OutBulk);

	FString GetVersion();
}
#endif // WITH_EDITOR