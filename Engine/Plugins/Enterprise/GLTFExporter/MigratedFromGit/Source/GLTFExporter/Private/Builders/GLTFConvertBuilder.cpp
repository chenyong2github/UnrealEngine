// Copyright Epic Games, Inc. All Rights Reserved.

#include "Builders/GLTFConvertBuilder.h"
#include "Builders/GLTFBuilderUtility.h"

FGLTFConvertBuilder::FGLTFConvertBuilder(const UGLTFExportOptions* ExportOptions, bool bSelectedActorsOnly)
	: FGLTFImageBuilder(ExportOptions)
	, bSelectedActorsOnly(bSelectedActorsOnly)
{
}

FGLTFJsonAccessorIndex FGLTFConvertBuilder::GetOrAddPositionAccessor(const FPositionVertexBuffer* VertexBuffer)
{
	if (VertexBuffer == nullptr)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	return PositionVertexBufferConverter.GetOrAdd(VertexBuffer);
}

FGLTFJsonAccessorIndex FGLTFConvertBuilder::GetOrAddColorAccessor(const FColorVertexBuffer* VertexBuffer)
{
	if (VertexBuffer == nullptr)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	return ColorVertexBufferConverter.GetOrAdd(VertexBuffer);
}

FGLTFJsonAccessorIndex FGLTFConvertBuilder::GetOrAddNormalAccessor(const FStaticMeshVertexBuffer* VertexBuffer)
{
	if (VertexBuffer == nullptr)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	return NormalVertexBufferConverter.GetOrAdd(VertexBuffer);
}

FGLTFJsonAccessorIndex FGLTFConvertBuilder::GetOrAddTangentAccessor(const FStaticMeshVertexBuffer* VertexBuffer)
{
	if (VertexBuffer == nullptr)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	return TangentVertexBufferConverter.GetOrAdd(VertexBuffer);
}

FGLTFJsonAccessorIndex FGLTFConvertBuilder::GetOrAddUVAccessor(const FStaticMeshVertexBuffer* VertexBuffer, int32 UVIndex)
{
	if (VertexBuffer == nullptr)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	return UVVertexBufferConverter.GetOrAdd(VertexBuffer, UVIndex);
}

FGLTFJsonAccessorIndex FGLTFConvertBuilder::GetOrAddJointAccessor(const FSkinWeightVertexBuffer* VertexBuffer, int32 JointsGroupIndex, FGLTFBoneMap BoneMap)
{
	if (VertexBuffer == nullptr)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	return BoneIndexVertexBufferConverter.GetOrAdd(VertexBuffer, JointsGroupIndex, BoneMap);
}

FGLTFJsonAccessorIndex FGLTFConvertBuilder::GetOrAddWeightAccessor(const FSkinWeightVertexBuffer* VertexBuffer, int32 WeightsGroupIndex)
{
	if (VertexBuffer == nullptr)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	return BoneWeightVertexBufferConverter.GetOrAdd(VertexBuffer, WeightsGroupIndex);
}

FGLTFJsonBufferViewIndex FGLTFConvertBuilder::GetOrAddIndexBufferView(const FRawStaticIndexBuffer* IndexBuffer)
{
	if (IndexBuffer == nullptr)
	{
		return FGLTFJsonBufferViewIndex(INDEX_NONE);
	}

	return IndexBufferConverter.GetOrAdd(IndexBuffer);
}

FGLTFJsonAccessorIndex FGLTFConvertBuilder::GetOrAddIndexAccessor(const FStaticMeshSection* MeshSection, const FRawStaticIndexBuffer* IndexBuffer)
{
	if (MeshSection == nullptr)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	return StaticMeshSectionConverter.GetOrAdd(MeshSection, IndexBuffer);
}

FGLTFJsonMeshIndex FGLTFConvertBuilder::GetOrAddMesh(const UStaticMesh* StaticMesh, int32 LODIndex, const FColorVertexBuffer* OverrideVertexColors, const FGLTFMaterialArray& OverrideMaterials)
{
	if (StaticMesh == nullptr)
	{
		return FGLTFJsonMeshIndex(INDEX_NONE);
	}

	if (LODIndex < 0)
	{
		LODIndex = ExportOptions->DefaultLevelOfDetail;
	}

	if (!ExportOptions->bExportVertexColors)
	{
		OverrideVertexColors = nullptr;
	}

	return StaticMeshConverter.GetOrAdd(
		StaticMesh,
		LODIndex,
		OverrideVertexColors,
		OverrideMaterials == StaticMesh->StaticMaterials ? FGLTFMaterialArray(): OverrideMaterials);
}

FGLTFJsonMeshIndex FGLTFConvertBuilder::GetOrAddMesh(const UStaticMeshComponent* StaticMeshComponent)
{
	if (StaticMeshComponent == nullptr)
	{
		return FGLTFJsonMeshIndex(INDEX_NONE);
	}

	const UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh();
	const int32 LODIndex = StaticMeshComponent->ForcedLodModel > 0 ? StaticMeshComponent->ForcedLodModel - 1 : ExportOptions->DefaultLevelOfDetail;
	const FColorVertexBuffer* OverrideVertexColors = StaticMeshComponent->LODData.IsValidIndex(LODIndex) ? StaticMeshComponent->LODData[LODIndex].OverrideVertexColors : nullptr;
	const FGLTFMaterialArray OverrideMaterials = FGLTFMaterialArray(StaticMeshComponent->OverrideMaterials);

	return GetOrAddMesh(StaticMesh, LODIndex, OverrideVertexColors, OverrideMaterials);
}

FGLTFJsonBufferViewIndex FGLTFConvertBuilder::GetOrAddIndexBufferView(const FMultiSizeIndexContainer* IndexContainer)
{
	if (IndexContainer == nullptr)
	{
		return FGLTFJsonBufferViewIndex(INDEX_NONE);
	}

	return IndexContainerConverter.GetOrAdd(IndexContainer);
}

FGLTFJsonAccessorIndex FGLTFConvertBuilder::GetOrAddIndexAccessor(const FSkelMeshRenderSection* MeshSection, const FMultiSizeIndexContainer* IndexContainer)
{
	if (MeshSection == nullptr)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	return SkeletalMeshSectionConverter.GetOrAdd(MeshSection, IndexContainer);
}

FGLTFJsonMeshIndex FGLTFConvertBuilder::GetOrAddMesh(const USkeletalMesh* SkeletalMesh, int32 LODIndex, const FColorVertexBuffer* OverrideVertexColors, const FSkinWeightVertexBuffer* OverrideSkinWeights, const FGLTFMaterialArray& OverrideMaterials)
{
	if (SkeletalMesh == nullptr)
	{
		return FGLTFJsonMeshIndex(INDEX_NONE);
	}

	if (LODIndex < 0)
	{
		LODIndex = ExportOptions->DefaultLevelOfDetail;
	}

	if (!ExportOptions->bExportVertexColors)
	{
		OverrideVertexColors = nullptr;
	}

	return SkeletalMeshConverter.GetOrAdd(SkeletalMesh, LODIndex, OverrideVertexColors, OverrideSkinWeights, OverrideMaterials);
}

FGLTFJsonMeshIndex FGLTFConvertBuilder::GetOrAddMesh(const USkeletalMeshComponent* SkeletalMeshComponent)
{
	if (SkeletalMeshComponent == nullptr)
	{
		return FGLTFJsonMeshIndex(INDEX_NONE);
	}

	const USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->SkeletalMesh;
	const int32 LODIndex = SkeletalMeshComponent->GetForcedLOD() > 0 ? SkeletalMeshComponent->GetForcedLOD() - 1 : ExportOptions->DefaultLevelOfDetail;
	const FColorVertexBuffer* OverrideVertexColors = SkeletalMeshComponent->LODInfo.IsValidIndex(LODIndex) ? SkeletalMeshComponent->LODInfo[LODIndex].OverrideVertexColors : nullptr;
	const FSkinWeightVertexBuffer* OverrideSkinWeights = SkeletalMeshComponent->LODInfo.IsValidIndex(LODIndex) ? SkeletalMeshComponent->LODInfo[LODIndex].OverrideSkinWeights : nullptr;
	const FGLTFMaterialArray OverrideMaterials = FGLTFMaterialArray(SkeletalMeshComponent->OverrideMaterials);

	return GetOrAddMesh(SkeletalMesh, LODIndex, OverrideVertexColors, OverrideSkinWeights, OverrideMaterials);
}

FGLTFJsonMaterialIndex FGLTFConvertBuilder::GetOrAddMaterial(const UMaterialInterface* Material)
{
	if (Material == nullptr)
	{
		return FGLTFJsonMaterialIndex(INDEX_NONE);
	}

	return MaterialConverter.GetOrAdd(Material);
}

FGLTFJsonSamplerIndex FGLTFConvertBuilder::GetOrAddSampler(const UTexture* Texture)
{
	if (Texture == nullptr)
	{
		return FGLTFJsonSamplerIndex(INDEX_NONE);
	}

	return TextureSamplerConverter.GetOrAdd(Texture);
}

FGLTFJsonTextureIndex FGLTFConvertBuilder::GetOrAddTexture(const UTexture2D* Texture)
{
	if (Texture == nullptr)
	{
		return FGLTFJsonTextureIndex(INDEX_NONE);
	}

	return Texture2DConverter.GetOrAdd(Texture);
}

FGLTFJsonTextureIndex FGLTFConvertBuilder::GetOrAddTexture(const UTextureCube* Texture, ECubeFace CubeFace)
{
	if (Texture == nullptr)
	{
		return FGLTFJsonTextureIndex(INDEX_NONE);
	}

	return TextureCubeConverter.GetOrAdd(Texture, CubeFace);
}

FGLTFJsonTextureIndex FGLTFConvertBuilder::GetOrAddTexture(const UTextureRenderTarget2D* Texture)
{
	if (Texture == nullptr)
	{
		return FGLTFJsonTextureIndex(INDEX_NONE);
	}

	return TextureRenderTarget2DConverter.GetOrAdd(Texture);
}

FGLTFJsonTextureIndex FGLTFConvertBuilder::GetOrAddTexture(const UTextureRenderTargetCube* Texture, ECubeFace CubeFace)
{
	if (Texture == nullptr)
	{
		return FGLTFJsonTextureIndex(INDEX_NONE);
	}

	return TextureRenderTargetCubeConverter.GetOrAdd(Texture, CubeFace);
}

FGLTFJsonNodeIndex FGLTFConvertBuilder::GetOrAddNode(const USceneComponent* SceneComponent)
{
	if (SceneComponent == nullptr)
	{
		return FGLTFJsonNodeIndex(INDEX_NONE);
	}

	return SceneComponentConverter.GetOrAdd(SceneComponent);
}

FGLTFJsonNodeIndex FGLTFConvertBuilder::GetOrAddNode(const AActor* Actor)
{
	if (Actor == nullptr)
	{
		return FGLTFJsonNodeIndex(INDEX_NONE);
	}

	return ActorConverter.GetOrAdd(Actor);
}

FGLTFJsonSceneIndex FGLTFConvertBuilder::GetOrAddScene(const ULevel* Level)
{
	if (Level == nullptr)
	{
		return FGLTFJsonSceneIndex(INDEX_NONE);
	}

	return LevelConverter.GetOrAdd(Level);
}

FGLTFJsonSceneIndex FGLTFConvertBuilder::GetOrAddScene(const UWorld* World)
{
	if (World == nullptr)
	{
		return FGLTFJsonSceneIndex(INDEX_NONE);
	}

	return GetOrAddScene(World->PersistentLevel);
}

FGLTFJsonCameraIndex FGLTFConvertBuilder::GetOrAddCamera(const UCameraComponent* CameraComponent)
{
	if (CameraComponent == nullptr)
	{
		return FGLTFJsonCameraIndex(INDEX_NONE);
	}

	return CameraComponentConverter.GetOrAdd(CameraComponent);
}

FGLTFJsonLightIndex FGLTFConvertBuilder::GetOrAddLight(const ULightComponent* LightComponent)
{
	if (LightComponent == nullptr)
	{
		return FGLTFJsonLightIndex(INDEX_NONE);
	}

	return LightComponentConverter.GetOrAdd(LightComponent);
}

FGLTFJsonBackdropIndex FGLTFConvertBuilder::GetOrAddBackdrop(const AActor* Actor)
{
	if (Actor == nullptr)
	{
		return FGLTFJsonBackdropIndex(INDEX_NONE);
	}

	return BackdropConverter.GetOrAdd(Actor);
}

FGLTFJsonVariationIndex FGLTFConvertBuilder::GetOrAddVariation(const ALevelVariantSetsActor* LevelVariantSetsActor)
{
	if (LevelVariantSetsActor == nullptr)
	{
		return FGLTFJsonVariationIndex(INDEX_NONE);
	}

	return VariationConverter.GetOrAdd(LevelVariantSetsActor);
}

FGLTFJsonLightMapIndex FGLTFConvertBuilder::GetOrAddLightMap(const UStaticMeshComponent* StaticMeshComponent)
{
	if (StaticMeshComponent == nullptr)
	{
		return FGLTFJsonLightMapIndex(INDEX_NONE);
	}

	return LightMapConverter.GetOrAdd(StaticMeshComponent);
}

FGLTFJsonHotspotIndex FGLTFConvertBuilder::GetOrAddHotspot(const UGLTFInteractionHotspotComponent* HotspotComponent)
{
	if (HotspotComponent == nullptr)
	{
		return FGLTFJsonHotspotIndex(INDEX_NONE);
	}

	return HotspotComponentConverter.GetOrAdd(HotspotComponent);
}
