// Copyright Epic Games, Inc. All Rights Reserved.

#include "Builders/GLTFConvertBuilder.h"
#include "Builders/GLTFBuilderUtility.h"

FGLTFConvertBuilder::FGLTFConvertBuilder(const UGLTFExportOptions* ExportOptions, bool bSelectedActorsOnly)
	: FGLTFImageBuilder(ExportOptions)
	, bSelectedActorsOnly(bSelectedActorsOnly)
{
}

FGLTFJsonAccessorIndex FGLTFConvertBuilder::GetOrAddPositionAccessor(const FPositionVertexBuffer* VertexBuffer, const FString& DesiredName)
{
	if (VertexBuffer == nullptr)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	return PositionVertexBufferConverter.GetOrAdd(*this, DesiredName, VertexBuffer);
}

FGLTFJsonAccessorIndex FGLTFConvertBuilder::GetOrAddColorAccessor(const FColorVertexBuffer* VertexBuffer, const FString& DesiredName)
{
	if (VertexBuffer == nullptr)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	return ColorVertexBufferConverter.GetOrAdd(*this, DesiredName, VertexBuffer);
}

FGLTFJsonAccessorIndex FGLTFConvertBuilder::GetOrAddNormalAccessor(const FStaticMeshVertexBuffer* VertexBuffer, const FString& DesiredName)
{
	if (VertexBuffer == nullptr)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	return NormalVertexBufferConverter.GetOrAdd(*this, DesiredName, VertexBuffer);
}

FGLTFJsonAccessorIndex FGLTFConvertBuilder::GetOrAddTangentAccessor(const FStaticMeshVertexBuffer* VertexBuffer, const FString& DesiredName)
{
	if (VertexBuffer == nullptr)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	return TangentVertexBufferConverter.GetOrAdd(*this, DesiredName, VertexBuffer);
}

FGLTFJsonAccessorIndex FGLTFConvertBuilder::GetOrAddUVAccessor(const FStaticMeshVertexBuffer* VertexBuffer, int32 UVIndex, const FString& DesiredName)
{
	if (VertexBuffer == nullptr)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	return UVVertexBufferConverter.GetOrAdd(*this, DesiredName, VertexBuffer, UVIndex);
}

FGLTFJsonAccessorIndex FGLTFConvertBuilder::GetOrAddJointAccessor(const FSkinWeightVertexBuffer* VertexBuffer, int32 JointsGroupIndex, FGLTFBoneMap BoneMap, const FString& DesiredName)
{
	if (VertexBuffer == nullptr)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	return BoneIndexVertexBufferConverter.GetOrAdd(*this, DesiredName, VertexBuffer, JointsGroupIndex, BoneMap);
}

FGLTFJsonAccessorIndex FGLTFConvertBuilder::GetOrAddWeightAccessor(const FSkinWeightVertexBuffer* VertexBuffer, int32 WeightsGroupIndex, const FString& DesiredName)
{
	if (VertexBuffer == nullptr)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	return BoneWeightVertexBufferConverter.GetOrAdd(*this, DesiredName, VertexBuffer, WeightsGroupIndex);
}

FGLTFJsonBufferViewIndex FGLTFConvertBuilder::GetOrAddIndexBufferView(const FRawStaticIndexBuffer* IndexBuffer, const FString& DesiredName)
{
	if (IndexBuffer == nullptr)
	{
		return FGLTFJsonBufferViewIndex(INDEX_NONE);
	}

	return IndexBufferConverter.GetOrAdd(*this, DesiredName, IndexBuffer);
}

FGLTFJsonAccessorIndex FGLTFConvertBuilder::GetOrAddIndexAccessor(const FStaticMeshSection* MeshSection, const FRawStaticIndexBuffer* IndexBuffer, const FString& DesiredName)
{
	if (MeshSection == nullptr)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	return StaticMeshSectionConverter.GetOrAdd(*this, DesiredName, MeshSection, IndexBuffer);
}

FGLTFJsonMeshIndex FGLTFConvertBuilder::GetOrAddMesh(const UStaticMesh* StaticMesh, int32 LODIndex, const FColorVertexBuffer* OverrideVertexColors, const FGLTFMaterialArray& OverrideMaterials, const FString& DesiredName)
{
	if (StaticMesh == nullptr)
	{
		return FGLTFJsonMeshIndex(INDEX_NONE);
	}

	if (LODIndex < 0)
	{
		LODIndex = ExportOptions->DefaultLevelOfDetail;
	}

	return StaticMeshConverter.GetOrAdd(*this, DesiredName.IsEmpty() ? FGLTFBuilderUtility::GetLODName(StaticMesh, LODIndex) : DesiredName, StaticMesh, LODIndex, OverrideVertexColors, OverrideMaterials);
}

FGLTFJsonMeshIndex FGLTFConvertBuilder::GetOrAddMesh(const UStaticMeshComponent* StaticMeshComponent, const FString& DesiredName)
{
	if (StaticMeshComponent == nullptr)
	{
		return FGLTFJsonMeshIndex(INDEX_NONE);
	}

	const UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh();
	const int32 LODIndex = StaticMeshComponent->ForcedLodModel > 0 ? StaticMeshComponent->ForcedLodModel - 1 : ExportOptions->DefaultLevelOfDetail;
	const FColorVertexBuffer* OverrideVertexColors = StaticMeshComponent->LODData.IsValidIndex(LODIndex) ? StaticMeshComponent->LODData[LODIndex].OverrideVertexColors : nullptr;
	const FGLTFMaterialArray OverrideMaterials = FGLTFMaterialArray(StaticMeshComponent->OverrideMaterials);

	return GetOrAddMesh(StaticMesh, LODIndex, OverrideVertexColors, OverrideMaterials, DesiredName);
}

FGLTFJsonBufferViewIndex FGLTFConvertBuilder::GetOrAddIndexBufferView(const FMultiSizeIndexContainer* IndexContainer, const FString& DesiredName)
{
	if (IndexContainer == nullptr)
	{
		return FGLTFJsonBufferViewIndex(INDEX_NONE);
	}

	return IndexContainerConverter.GetOrAdd(*this, DesiredName, IndexContainer);
}

FGLTFJsonAccessorIndex FGLTFConvertBuilder::GetOrAddIndexAccessor(const FSkelMeshRenderSection* MeshSection, const FMultiSizeIndexContainer* IndexContainer, const FString& DesiredName)
{
	if (MeshSection == nullptr)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	return SkeletalMeshSectionConverter.GetOrAdd(*this, DesiredName, MeshSection, IndexContainer);
}

FGLTFJsonMeshIndex FGLTFConvertBuilder::GetOrAddMesh(const USkeletalMesh* SkeletalMesh, int32 LODIndex, const FColorVertexBuffer* OverrideVertexColors, const FSkinWeightVertexBuffer* OverrideSkinWeights, const FGLTFMaterialArray& OverrideMaterials, const FString& DesiredName)
{
	if (SkeletalMesh == nullptr)
	{
		return FGLTFJsonMeshIndex(INDEX_NONE);
	}

	if (LODIndex < 0)
	{
		LODIndex = ExportOptions->DefaultLevelOfDetail;
	}

	return SkeletalMeshConverter.GetOrAdd(*this, DesiredName.IsEmpty() ? FGLTFBuilderUtility::GetLODName(SkeletalMesh, LODIndex) : DesiredName, SkeletalMesh, LODIndex, OverrideVertexColors, OverrideSkinWeights, OverrideMaterials);
}

FGLTFJsonMeshIndex FGLTFConvertBuilder::GetOrAddMesh(const USkeletalMeshComponent* SkeletalMeshComponent, const FString& DesiredName)
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

	return GetOrAddMesh(SkeletalMesh, LODIndex, OverrideVertexColors, OverrideSkinWeights, OverrideMaterials, DesiredName);
}

FGLTFJsonMaterialIndex FGLTFConvertBuilder::GetOrAddMaterial(const UMaterialInterface* Material, const FString& DesiredName)
{
	if (Material == nullptr)
	{
		return FGLTFJsonMaterialIndex(INDEX_NONE);
	}

	return MaterialConverter.GetOrAdd(*this, DesiredName.IsEmpty() ? Material->GetName() : DesiredName, Material);
}

FGLTFJsonSamplerIndex FGLTFConvertBuilder::GetOrAddSampler(const UTexture* Texture, const FString& DesiredName)
{
	if (Texture == nullptr)
	{
		return FGLTFJsonSamplerIndex(INDEX_NONE);
	}

	return TextureSamplerConverter.GetOrAdd(*this, DesiredName.IsEmpty() ? Texture->GetName() : DesiredName, Texture);
}

FGLTFJsonTextureIndex FGLTFConvertBuilder::GetOrAddTexture(const UTexture2D* Texture, const FString& DesiredName)
{
	if (Texture == nullptr)
	{
		return FGLTFJsonTextureIndex(INDEX_NONE);
	}

	return Texture2DConverter.GetOrAdd(*this, DesiredName.IsEmpty() ? Texture->GetName() : DesiredName, Texture);
}

FGLTFJsonTextureIndex FGLTFConvertBuilder::GetOrAddTexture(const UTextureCube* Texture, ECubeFace CubeFace, const FString& DesiredName)
{
	if (Texture == nullptr)
	{
		return FGLTFJsonTextureIndex(INDEX_NONE);
	}

	return TextureCubeConverter.GetOrAdd(*this, DesiredName.IsEmpty() ? Texture->GetName() : DesiredName, Texture, CubeFace);
}

FGLTFJsonTextureIndex FGLTFConvertBuilder::GetOrAddTexture(const UTextureRenderTarget2D* Texture, const FString& DesiredName)
{
	if (Texture == nullptr)
	{
		return FGLTFJsonTextureIndex(INDEX_NONE);
	}

	return TextureRenderTarget2DConverter.GetOrAdd(*this, DesiredName.IsEmpty() ? Texture->GetName() : DesiredName, Texture);
}

FGLTFJsonTextureIndex FGLTFConvertBuilder::GetOrAddTexture(const UTextureRenderTargetCube* Texture, ECubeFace CubeFace, const FString& DesiredName)
{
	if (Texture == nullptr)
	{
		return FGLTFJsonTextureIndex(INDEX_NONE);
	}

	return TextureRenderTargetCubeConverter.GetOrAdd(*this, DesiredName.IsEmpty() ? Texture->GetName() : DesiredName, Texture, CubeFace);
}

FGLTFJsonNodeIndex FGLTFConvertBuilder::GetOrAddNode(const USceneComponent* SceneComponent, const FString& DesiredName)
{
	if (SceneComponent == nullptr)
	{
		return FGLTFJsonNodeIndex(INDEX_NONE);
	}

	return SceneComponentConverter.GetOrAdd(*this, DesiredName, SceneComponent);
}

FGLTFJsonNodeIndex FGLTFConvertBuilder::GetOrAddNode(const AActor* Actor, const FString& DesiredName)
{
	if (Actor == nullptr)
	{
		return FGLTFJsonNodeIndex(INDEX_NONE);
	}

	return ActorConverter.GetOrAdd(*this, DesiredName, Actor);
}

FGLTFJsonSceneIndex FGLTFConvertBuilder::GetOrAddScene(const ULevel* Level, const FString& DesiredName)
{
	if (Level == nullptr)
	{
		return FGLTFJsonSceneIndex(INDEX_NONE);
	}

	return LevelConverter.GetOrAdd(*this, DesiredName, Level);
}

FGLTFJsonSceneIndex FGLTFConvertBuilder::GetOrAddScene(const UWorld* World, const FString& DesiredName)
{
	if (World == nullptr)
	{
		return FGLTFJsonSceneIndex(INDEX_NONE);
	}

	return GetOrAddScene(World->PersistentLevel, DesiredName.IsEmpty() ? World->GetName() : DesiredName);
}

FGLTFJsonCameraIndex FGLTFConvertBuilder::GetOrAddCamera(const UCameraComponent* CameraComponent, const FString& DesiredName)
{
	if (CameraComponent == nullptr)
	{
		return FGLTFJsonCameraIndex(INDEX_NONE);
	}

	return CameraComponentConverter.GetOrAdd(*this, DesiredName.IsEmpty() ? CameraComponent->GetName() : DesiredName, CameraComponent);
}

FGLTFJsonLightIndex FGLTFConvertBuilder::GetOrAddLight(const ULightComponent* LightComponent, const FString& DesiredName)
{
	if (LightComponent == nullptr)
	{
		return FGLTFJsonLightIndex(INDEX_NONE);
	}

	return LightComponentConverter.GetOrAdd(*this, DesiredName.IsEmpty() ? LightComponent->GetName() : DesiredName, LightComponent);
}

FGLTFJsonBackdropIndex FGLTFConvertBuilder::GetOrAddBackdrop(const AActor* Actor, const FString& DesiredName)
{
	if (Actor == nullptr)
	{
		return FGLTFJsonBackdropIndex(INDEX_NONE);
	}

	return BackdropConverter.GetOrAdd(*this, DesiredName, Actor);
}

FGLTFJsonLevelVariantSetsIndex FGLTFConvertBuilder::GetOrAddLevelVariantSets(const ALevelVariantSetsActor* LevelVariantSetsActor, const FString& DesiredName)
{
	if (LevelVariantSetsActor == nullptr)
	{
		return FGLTFJsonLevelVariantSetsIndex(INDEX_NONE);
	}

	return LevelVariantSetsConverter.GetOrAdd(*this, DesiredName, LevelVariantSetsActor);
}

FGLTFJsonLightMapIndex FGLTFConvertBuilder::GetOrAddLightMap(const UStaticMeshComponent* StaticMeshComponent, const FString& DesiredName)
{
	if (StaticMeshComponent == nullptr)
	{
		return FGLTFJsonLightMapIndex(INDEX_NONE);
	}

	return LightMapConverter.GetOrAdd(*this, DesiredName, StaticMeshComponent);
}
