// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFLightMapConverters.h"
#include "Json/GLTFJsonLightMap.h"
#include "Builders/GLTFContainerBuilder.h"
#include "Engine/MapBuildDataRegistry.h"

FGLTFJsonLightMapIndex FGLTFLightMapConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, const UStaticMeshComponent* StaticMeshComponent)
{
	const AActor* Owner = StaticMeshComponent->GetOwner();
	if (Owner == nullptr)
	{
		// TODO: report invalid mesh component
		return FGLTFJsonLightMapIndex(INDEX_NONE);
	}

	const UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh();

	if (StaticMesh == nullptr)
	{
		return FGLTFJsonLightMapIndex(INDEX_NONE);
	}

	const int32 LODIndex = StaticMeshComponent->ForcedLodModel > 0 ? StaticMeshComponent->ForcedLodModel - 1 : /* auto-select */ 0;

	if (LODIndex < 0 || StaticMesh->GetNumLODs() <= LODIndex || StaticMeshComponent->LODData.Num() <= LODIndex)
	{
		return FGLTFJsonLightMapIndex(INDEX_NONE);
	}

	const int32 CoordinateIndex = StaticMesh->LightMapCoordinateIndex;
	const FStaticMeshLODResources& LODResources = StaticMesh->GetLODForExport(LODIndex);

	if (CoordinateIndex < 0 || CoordinateIndex >= LODResources.GetNumTexCoords())
	{
		return FGLTFJsonLightMapIndex(INDEX_NONE);
	}

	const FStaticMeshComponentLODInfo& ComponentLODInfo = StaticMeshComponent->LODData[LODIndex];
	const FMeshMapBuildData* MeshMapBuildData = StaticMeshComponent->GetMeshMapBuildData(ComponentLODInfo);

	if (MeshMapBuildData == nullptr || MeshMapBuildData->LightMap == nullptr)
	{
		return FGLTFJsonLightMapIndex(INDEX_NONE);
	}

	const FLightMap2D* LightMap2D = MeshMapBuildData->LightMap->GetLightMap2D();
	check(LightMap2D);

	// TODO: is it correct to use SM5?
	const FLightMapInteraction LightMapInteraction = LightMap2D->GetInteraction(ERHIFeatureLevel::Type::SM5);

	const bool bUseHighQualityLightMap = true;
	const ULightMapTexture2D* Texture = LightMapInteraction.GetTexture(bUseHighQualityLightMap);
	const FGLTFJsonTextureIndex TextureIndex = Builder.GetOrAddTexture(Texture);

	if (TextureIndex == INDEX_NONE)
	{
		return FGLTFJsonLightMapIndex(INDEX_NONE);
	}

	const FVector2D& CoordinateBias = LightMap2D->GetCoordinateBias();
	const FVector2D& CoordinateScale = LightMap2D->GetCoordinateScale();
	const FVector4& LightMapAdd = LightMapInteraction.GetAddArray()[0];
	const FVector4& LightMapScale = LightMapInteraction.GetScaleArray()[0];

	FGLTFJsonLightMap JsonLightMap;
	JsonLightMap.Name = Name.IsEmpty() ? Owner->GetName() + TEXT("_") + StaticMeshComponent->GetName() : Name;
	JsonLightMap.Texture.Index = TextureIndex;
	JsonLightMap.Texture.TexCoord = CoordinateIndex;
	JsonLightMap.ValueScale = { LightMapScale.X, LightMapScale.Y, LightMapScale.Z, LightMapScale.W };
	JsonLightMap.ValueOffset = { LightMapAdd.X, LightMapAdd.Y, LightMapAdd.Z, LightMapAdd.W };
	JsonLightMap.CoordinateScale = { CoordinateScale.X, CoordinateScale.Y };
	JsonLightMap.CoordinateOffset = { CoordinateBias.X, CoordinateBias.Y };

	return Builder.AddLightMap(JsonLightMap);
}
