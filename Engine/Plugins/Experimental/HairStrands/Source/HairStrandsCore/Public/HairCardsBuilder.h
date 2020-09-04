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

namespace FHairCardsBuilder
{
	void ImportGeometry(
		const UStaticMesh* StaticMesh,
		FHairCardsDatas& Out);

	void BuildGeometry(
		const struct FHairStrandsDatas& In,
		const struct FHairStrandsDatas& InSim,
		const struct FHairGroupsProceduralCards& Settings,
		FHairCardsProceduralDatas& Out,
		FHairStrandsDatas& OutGuides,
		FHairCardsInterpolationDatas& OutInterpolation);

	void BuildTextureAtlas(
		FHairCardsProceduralDatas* ProceduralData,
		FHairCardsRestResource* RestResource,
		FHairCardsProceduralResource* AtlasResource);

	void Convert(const FHairCardsProceduralDatas& In, FHairCardsDatas& Out);
}

namespace FHairMeshesBuilder
{
	void BuildGeometry(
		const struct FHairStrandsDatas& In,
		const struct FHairStrandsDatas& InSim,
		FHairMeshesDatas& Out);

	void ImportGeometry(
		const UStaticMesh* StaticMesh,
		FHairMeshesDatas& Out);
}