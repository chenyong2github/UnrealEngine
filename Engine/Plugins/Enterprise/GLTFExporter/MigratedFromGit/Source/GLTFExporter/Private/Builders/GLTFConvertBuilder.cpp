// Copyright Epic Games, Inc. All Rights Reserved.

#include "Builders/GLTFConvertBuilder.h"
#include "Converters/GLTFMeshUtility.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureCube.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/TextureRenderTargetCube.h"

FGLTFConvertBuilder::FGLTFConvertBuilder(const FString& FilePath, const UGLTFExportOptions* ExportOptions, const TSet<AActor*>& SelectedActors)
	: FGLTFImageBuilder(FilePath, ExportOptions)
	, SelectedActors(SelectedActors)
{
}

bool FGLTFConvertBuilder::IsSelectedActor(const AActor* Actor) const
{
	return SelectedActors.Num() == 0 || SelectedActors.Contains(Actor);
}

bool FGLTFConvertBuilder::IsRootActor(const AActor* Actor) const
{
	const AActor* ParentActor = Actor->GetAttachParentActor();
	return ParentActor == nullptr || !IsSelectedActor(ParentActor);
}

FGLTFJsonAccessorIndex FGLTFConvertBuilder::GetOrAddPositionAccessor(const FGLTFMeshSection* MeshSection, const FPositionVertexBuffer* VertexBuffer)
{
	if (VertexBuffer == nullptr)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	return PositionBufferConverter.GetOrAdd(MeshSection, VertexBuffer);
}

FGLTFJsonAccessorIndex FGLTFConvertBuilder::GetOrAddColorAccessor(const FGLTFMeshSection* MeshSection, const FColorVertexBuffer* VertexBuffer)
{
	if (VertexBuffer == nullptr)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	return ColorBufferConverter.GetOrAdd(MeshSection, VertexBuffer);
}

FGLTFJsonAccessorIndex FGLTFConvertBuilder::GetOrAddNormalAccessor(const FGLTFMeshSection* MeshSection, const FStaticMeshVertexBuffer* VertexBuffer)
{
	if (VertexBuffer == nullptr)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	return NormalBufferConverter.GetOrAdd(MeshSection, VertexBuffer);
}

FGLTFJsonAccessorIndex FGLTFConvertBuilder::GetOrAddTangentAccessor(const FGLTFMeshSection* MeshSection, const FStaticMeshVertexBuffer* VertexBuffer)
{
	if (VertexBuffer == nullptr)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	return TangentBufferConverter.GetOrAdd(MeshSection, VertexBuffer);
}

FGLTFJsonAccessorIndex FGLTFConvertBuilder::GetOrAddUVAccessor(const FGLTFMeshSection* MeshSection, const FStaticMeshVertexBuffer* VertexBuffer, int32 UVIndex)
{
	if (VertexBuffer == nullptr)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	return UVBufferConverter.GetOrAdd(MeshSection, VertexBuffer, UVIndex);
}

FGLTFJsonAccessorIndex FGLTFConvertBuilder::GetOrAddJointAccessor(const FGLTFMeshSection* MeshSection, const FSkinWeightVertexBuffer* VertexBuffer, int32 InfluenceOffset)
{
	if (VertexBuffer == nullptr)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	return BoneIndexBufferConverter.GetOrAdd(MeshSection, VertexBuffer, InfluenceOffset);
}

FGLTFJsonAccessorIndex FGLTFConvertBuilder::GetOrAddWeightAccessor(const FGLTFMeshSection* MeshSection, const FSkinWeightVertexBuffer* VertexBuffer, int32 InfluenceOffset)
{
	if (VertexBuffer == nullptr)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	return BoneWeightBufferConverter.GetOrAdd(MeshSection, VertexBuffer, InfluenceOffset);
}

FGLTFJsonAccessorIndex FGLTFConvertBuilder::GetOrAddIndexAccessor(const FGLTFMeshSection* MeshSection)
{
	if (MeshSection == nullptr)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	return IndexBufferConverter.GetOrAdd(MeshSection);
}

FGLTFJsonMeshIndex FGLTFConvertBuilder::GetOrAddMesh(const UStaticMesh* StaticMesh, const FGLTFMaterialArray& Materials, int32 LODIndex)
{
	if (StaticMesh == nullptr)
	{
		return FGLTFJsonMeshIndex(INDEX_NONE);
	}

	return StaticMeshConverter.GetOrAdd(StaticMesh, nullptr, Materials, LODIndex);
}

FGLTFJsonMeshIndex FGLTFConvertBuilder::GetOrAddMesh(const USkeletalMesh* SkeletalMesh, const FGLTFMaterialArray& Materials, int32 LODIndex)
{
	if (SkeletalMesh == nullptr)
	{
		return FGLTFJsonMeshIndex(INDEX_NONE);
	}

	return SkeletalMeshConverter.GetOrAdd(SkeletalMesh, nullptr, Materials, LODIndex);
}

FGLTFJsonMeshIndex FGLTFConvertBuilder::GetOrAddMesh(const UMeshComponent* MeshComponent, const FGLTFMaterialArray& Materials, int32 LODIndex)
{
	if (const UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(MeshComponent))
	{
		return GetOrAddMesh(StaticMeshComponent, Materials, LODIndex);
	}

	if (const USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(MeshComponent))
	{
		return GetOrAddMesh(SkeletalMeshComponent, Materials, LODIndex);
	}

	return FGLTFJsonMeshIndex(INDEX_NONE);
}

FGLTFJsonMeshIndex FGLTFConvertBuilder::GetOrAddMesh(const UStaticMeshComponent* StaticMeshComponent, const FGLTFMaterialArray& Materials, int32 LODIndex)
{
	if (StaticMeshComponent == nullptr)
	{
		return FGLTFJsonMeshIndex(INDEX_NONE);
	}

	const UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh();
	if (StaticMesh == nullptr)
	{
		return FGLTFJsonMeshIndex(INDEX_NONE);
	}

	return StaticMeshConverter.GetOrAdd(StaticMesh, StaticMeshComponent, Materials, LODIndex);
}

FGLTFJsonMeshIndex FGLTFConvertBuilder::GetOrAddMesh(const USkeletalMeshComponent* SkeletalMeshComponent, const FGLTFMaterialArray& Materials, int32 LODIndex)
{
	if (SkeletalMeshComponent == nullptr)
	{
		return FGLTFJsonMeshIndex(INDEX_NONE);
	}

	const USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->SkeletalMesh;
	if (SkeletalMesh == nullptr)
	{
		return FGLTFJsonMeshIndex(INDEX_NONE);
	}

	return SkeletalMeshConverter.GetOrAdd(SkeletalMesh, SkeletalMeshComponent, Materials, LODIndex);
}

const FGLTFMeshData* FGLTFConvertBuilder::GetOrAddMeshData(const UStaticMesh* StaticMesh, const UStaticMeshComponent* StaticMeshComponent, int32 LODIndex)
{
	return StaticMeshDataConverter.GetOrAdd(StaticMesh, StaticMeshComponent, LODIndex);
}

const FGLTFMeshData* FGLTFConvertBuilder::GetOrAddMeshData(const USkeletalMesh* SkeletalMesh, const USkeletalMeshComponent* SkeletalMeshComponent, int32 LODIndex)
{
	return SkeletalMeshDataConverter.GetOrAdd(SkeletalMesh, SkeletalMeshComponent, LODIndex);
}

FGLTFJsonMaterialIndex FGLTFConvertBuilder::GetOrAddMaterial(const UMaterialInterface* Material, const UStaticMesh* StaticMesh, int32 LODIndex, int32 MaterialIndex)
{
	const FGLTFMeshData* MeshData = GetOrAddMeshData(StaticMesh, nullptr, LODIndex);
	const FGLTFIndexArray SectionIndices = FGLTFMeshUtility::GetSectionIndices(StaticMesh, MeshData->LODIndex, MaterialIndex);
	return GetOrAddMaterial(Material, MeshData, SectionIndices);
}

FGLTFJsonMaterialIndex FGLTFConvertBuilder::GetOrAddMaterial(const UMaterialInterface* Material, const USkeletalMesh* SkeletalMesh, int32 LODIndex, int32 MaterialIndex)
{
	const FGLTFMeshData* MeshData = GetOrAddMeshData(SkeletalMesh, nullptr, LODIndex);
	const FGLTFIndexArray SectionIndices = FGLTFMeshUtility::GetSectionIndices(SkeletalMesh, MeshData->LODIndex, MaterialIndex);
	return GetOrAddMaterial(Material, MeshData, SectionIndices);
}

FGLTFJsonMaterialIndex FGLTFConvertBuilder::GetOrAddMaterial(const UMaterialInterface* Material, const UMeshComponent* MeshComponent, int32 LODIndex, int32 MaterialIndex)
{
	if (const UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(MeshComponent))
	{
		return GetOrAddMaterial(Material, StaticMeshComponent, LODIndex, MaterialIndex);
	}

	if (const USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(MeshComponent))
	{
		return GetOrAddMaterial(Material, SkeletalMeshComponent, LODIndex, MaterialIndex);
	}

	return FGLTFJsonMaterialIndex(INDEX_NONE);
}

FGLTFJsonMaterialIndex FGLTFConvertBuilder::GetOrAddMaterial(const UMaterialInterface* Material, const UStaticMeshComponent* StaticMeshComponent, int32 LODIndex, int32 MaterialIndex)
{
	const UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh();
	const FGLTFMeshData* MeshData = GetOrAddMeshData(StaticMesh, StaticMeshComponent, LODIndex);
	const FGLTFIndexArray SectionIndices = FGLTFMeshUtility::GetSectionIndices(StaticMesh, MeshData->LODIndex, MaterialIndex);
	return GetOrAddMaterial(Material, MeshData, SectionIndices);
}

FGLTFJsonMaterialIndex FGLTFConvertBuilder::GetOrAddMaterial(const UMaterialInterface* Material, const USkeletalMeshComponent* SkeletalMeshComponent, int32 LODIndex, int32 MaterialIndex)
{
	const USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->SkeletalMesh;
	const FGLTFMeshData* MeshData = GetOrAddMeshData(SkeletalMesh, SkeletalMeshComponent, LODIndex);
	const FGLTFIndexArray SectionIndices = FGLTFMeshUtility::GetSectionIndices(SkeletalMesh, MeshData->LODIndex, MaterialIndex);
	return GetOrAddMaterial(Material, MeshData, SectionIndices);
}

FGLTFJsonMaterialIndex FGLTFConvertBuilder::GetOrAddMaterial(const UMaterialInterface* Material, const FGLTFMeshData* MeshData, const FGLTFIndexArray& SectionIndices)
{
	if (Material == nullptr)
	{
		return FGLTFJsonMaterialIndex(INDEX_NONE);
	}

	return MaterialConverter.GetOrAdd(Material, MeshData, SectionIndices);
}

FGLTFJsonSamplerIndex FGLTFConvertBuilder::GetOrAddSampler(const UTexture* Texture)
{
	if (Texture == nullptr)
	{
		return FGLTFJsonSamplerIndex(INDEX_NONE);
	}

	return SamplerConverter.GetOrAdd(Texture);
}

FGLTFJsonTextureIndex FGLTFConvertBuilder::GetOrAddTexture(const UTexture2D* Texture)
{
	return GetOrAddTexture(Texture, Texture->SRGB);
}

FGLTFJsonTextureIndex FGLTFConvertBuilder::GetOrAddTexture(const UTextureCube* Texture, ECubeFace CubeFace)
{
	return GetOrAddTexture(Texture, CubeFace, Texture->SRGB);
}

FGLTFJsonTextureIndex FGLTFConvertBuilder::GetOrAddTexture(const UTextureRenderTarget2D* Texture)
{
	return GetOrAddTexture(Texture, Texture->SRGB);
}

FGLTFJsonTextureIndex FGLTFConvertBuilder::GetOrAddTexture(const UTextureRenderTargetCube* Texture, ECubeFace CubeFace)
{
	return GetOrAddTexture(Texture, CubeFace, Texture->SRGB);
}

FGLTFJsonTextureIndex FGLTFConvertBuilder::GetOrAddTexture(const ULightMapTexture2D* Texture)
{
	if (Texture == nullptr)
	{
		return FGLTFJsonTextureIndex(INDEX_NONE);
	}

	return TextureLightMapConverter.GetOrAdd(Texture);
}

FGLTFJsonTextureIndex FGLTFConvertBuilder::GetOrAddTexture(const UTexture2D* Texture, bool bToSRGB)
{
	if (Texture == nullptr)
	{
		return FGLTFJsonTextureIndex(INDEX_NONE);
	}

	return Texture2DConverter.GetOrAdd(Texture, bToSRGB);
}

FGLTFJsonTextureIndex FGLTFConvertBuilder::GetOrAddTexture(const UTextureCube* Texture, ECubeFace CubeFace, bool bToSRGB)
{
	if (Texture == nullptr)
	{
		return FGLTFJsonTextureIndex(INDEX_NONE);
	}

	return TextureCubeConverter.GetOrAdd(Texture, CubeFace, bToSRGB);
}

FGLTFJsonTextureIndex FGLTFConvertBuilder::GetOrAddTexture(const UTextureRenderTarget2D* Texture, bool bToSRGB)
{
	if (Texture == nullptr)
	{
		return FGLTFJsonTextureIndex(INDEX_NONE);
	}

	return TextureRenderTarget2DConverter.GetOrAdd(Texture, bToSRGB);
}

FGLTFJsonTextureIndex FGLTFConvertBuilder::GetOrAddTexture(const UTextureRenderTargetCube* Texture, ECubeFace CubeFace, bool bToSRGB)
{
	if (Texture == nullptr)
	{
		return FGLTFJsonTextureIndex(INDEX_NONE);
	}

	return TextureRenderTargetCubeConverter.GetOrAdd(Texture, CubeFace, bToSRGB);
}

FGLTFJsonSkinIndex FGLTFConvertBuilder::GetOrAddSkin(FGLTFJsonNodeIndex RootNode, const USkeletalMesh* SkeletalMesh)
{
	if (RootNode == INDEX_NONE || SkeletalMesh == nullptr)
	{
		return FGLTFJsonSkinIndex(INDEX_NONE);
	}

	return SkinConverter.GetOrAdd(RootNode, SkeletalMesh);
}

FGLTFJsonSkinIndex FGLTFConvertBuilder::GetOrAddSkin(FGLTFJsonNodeIndex RootNode, const USkeletalMeshComponent* SkeletalMeshComponent)
{
	if (RootNode == INDEX_NONE || SkeletalMeshComponent == nullptr)
	{
		return FGLTFJsonSkinIndex(INDEX_NONE);
	}

	const USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->SkeletalMesh;

	if (SkeletalMesh == nullptr)
	{
		return FGLTFJsonSkinIndex(INDEX_NONE);
	}

	return GetOrAddSkin(RootNode, SkeletalMesh);
}

FGLTFJsonAnimationIndex FGLTFConvertBuilder::GetOrAddAnimation(FGLTFJsonNodeIndex RootNode, const USkeletalMesh* SkeletalMesh, const UAnimSequence* AnimSequence)
{
	if (RootNode == INDEX_NONE || SkeletalMesh == nullptr || AnimSequence == nullptr)
	{
		return FGLTFJsonAnimationIndex(INDEX_NONE);
	}

	return AnimationConverter.GetOrAdd(RootNode, SkeletalMesh, AnimSequence);
}

FGLTFJsonAnimationIndex FGLTFConvertBuilder::GetOrAddAnimation(FGLTFJsonNodeIndex RootNode, const USkeletalMeshComponent* SkeletalMeshComponent)
{
	if (RootNode == INDEX_NONE || SkeletalMeshComponent == nullptr)
	{
		return FGLTFJsonAnimationIndex(INDEX_NONE);
	}

	return AnimationDataConverter.GetOrAdd(RootNode, SkeletalMeshComponent);
}

FGLTFJsonAnimationIndex FGLTFConvertBuilder::GetOrAddAnimation(const ULevel* Level, const ULevelSequence* LevelSequence)
{
	if (Level == nullptr || LevelSequence == nullptr)
	{
		return FGLTFJsonAnimationIndex(INDEX_NONE);
	}

	return LevelSequenceConverter.GetOrAdd(Level, LevelSequence);
}

FGLTFJsonAnimationIndex FGLTFConvertBuilder::GetOrAddAnimation(const ALevelSequenceActor* LevelSequenceActor)
{
	if (LevelSequenceActor == nullptr)
	{
		return FGLTFJsonAnimationIndex(INDEX_NONE);
	}

	return LevelSequenceDataConverter.GetOrAdd(LevelSequenceActor);
}

FGLTFJsonNodeIndex FGLTFConvertBuilder::GetOrAddNode(const AActor* Actor)
{
	if (Actor == nullptr)
	{
		return FGLTFJsonNodeIndex(INDEX_NONE);
	}

	return ActorConverter.GetOrAdd(Actor);
}

FGLTFJsonNodeIndex FGLTFConvertBuilder::GetOrAddNode(const USceneComponent* SceneComponent)
{
	if (SceneComponent == nullptr)
	{
		return FGLTFJsonNodeIndex(INDEX_NONE);
	}

	return ComponentConverter.GetOrAdd(SceneComponent);
}

FGLTFJsonNodeIndex FGLTFConvertBuilder::GetOrAddNode(const USceneComponent* SceneComponent, FName SocketName)
{
	if (SceneComponent == nullptr)
	{
		return FGLTFJsonNodeIndex(INDEX_NONE);
	}

	return ComponentSocketConverter.GetOrAdd(SceneComponent, SocketName);
}

FGLTFJsonNodeIndex FGLTFConvertBuilder::GetOrAddNode(FGLTFJsonNodeIndex RootNode, const UStaticMesh* StaticMesh, FName SocketName)
{
	if (RootNode == INDEX_NONE || StaticMesh == nullptr || SocketName == NAME_None)
	{
		return FGLTFJsonNodeIndex(INDEX_NONE);
	}

	return StaticSocketConverter.GetOrAdd(RootNode, StaticMesh, SocketName);
}

FGLTFJsonNodeIndex FGLTFConvertBuilder::GetOrAddNode(FGLTFJsonNodeIndex RootNode, const USkeletalMesh* SkeletalMesh, FName SocketName)
{
	if (RootNode == INDEX_NONE || SkeletalMesh == nullptr || SocketName == NAME_None)
	{
		return FGLTFJsonNodeIndex(INDEX_NONE);
	}

	return SkeletalSocketConverter.GetOrAdd(RootNode, SkeletalMesh, SocketName);
}

FGLTFJsonNodeIndex FGLTFConvertBuilder::GetOrAddNode(FGLTFJsonNodeIndex RootNode, const USkeletalMesh* SkeletalMesh, int32 BoneIndex)
{
	if (RootNode == INDEX_NONE || SkeletalMesh == nullptr || BoneIndex == INDEX_NONE)
	{
		return FGLTFJsonNodeIndex(INDEX_NONE);
	}

	return SkeletalBoneConverter.GetOrAdd(RootNode, SkeletalMesh, BoneIndex);
}

FGLTFJsonSceneIndex FGLTFConvertBuilder::GetOrAddScene(const UWorld* World)
{
	if (World == nullptr)
	{
		return FGLTFJsonSceneIndex(INDEX_NONE);
	}

	return SceneConverter.GetOrAdd(World);
}

FGLTFJsonCameraIndex FGLTFConvertBuilder::GetOrAddCamera(const UCameraComponent* CameraComponent)
{
	if (CameraComponent == nullptr)
	{
		return FGLTFJsonCameraIndex(INDEX_NONE);
	}

	return CameraConverter.GetOrAdd(CameraComponent);
}

FGLTFJsonLightIndex FGLTFConvertBuilder::GetOrAddLight(const ULightComponent* LightComponent)
{
	if (LightComponent == nullptr)
	{
		return FGLTFJsonLightIndex(INDEX_NONE);
	}

	return LightConverter.GetOrAdd(LightComponent);
}

FGLTFJsonBackdropIndex FGLTFConvertBuilder::GetOrAddBackdrop(const AActor* BackdropActor)
{
	if (BackdropActor == nullptr)
	{
		return FGLTFJsonBackdropIndex(INDEX_NONE);
	}

	return BackdropConverter.GetOrAdd(BackdropActor);
}

FGLTFJsonLightMapIndex FGLTFConvertBuilder::GetOrAddLightMap(const UStaticMeshComponent* StaticMeshComponent)
{
	if (StaticMeshComponent == nullptr)
	{
		return FGLTFJsonLightMapIndex(INDEX_NONE);
	}

	return LightMapConverter.GetOrAdd(StaticMeshComponent);
}

FGLTFJsonHotspotIndex FGLTFConvertBuilder::GetOrAddHotspot(const AGLTFHotspotActor* HotspotActor)
{
	if (HotspotActor == nullptr)
	{
		return FGLTFJsonHotspotIndex(INDEX_NONE);
	}

	return HotspotConverter.GetOrAdd(HotspotActor);
}

FGLTFJsonSkySphereIndex FGLTFConvertBuilder::GetOrAddSkySphere(const AActor* SkySphereActor)
{
	if (SkySphereActor == nullptr)
	{
		return FGLTFJsonSkySphereIndex(INDEX_NONE);
	}

	return SkySphereConverter.GetOrAdd(SkySphereActor);
}

FGLTFJsonEpicLevelVariantSetsIndex FGLTFConvertBuilder::GetOrAddEpicLevelVariantSets(const ULevelVariantSets* LevelVariantSets)
{
	if (LevelVariantSets == nullptr)
	{
		return FGLTFJsonEpicLevelVariantSetsIndex(INDEX_NONE);
	}

	return EpicLevelVariantSetsConverter.GetOrAdd(LevelVariantSets);
}

FGLTFJsonKhrMaterialVariantIndex FGLTFConvertBuilder::GetOrAddKhrMaterialVariant(const UVariant* Variant)
{
	if (Variant == nullptr)
	{
		return FGLTFJsonKhrMaterialVariantIndex(INDEX_NONE);
	}

	return KhrMaterialVariantConverter.GetOrAdd(Variant);
}

void FGLTFConvertBuilder::RegisterObjectVariant(const UObject* Object, const UPropertyValue* Property)
{
	TArray<const UPropertyValue*>& Variants = ObjectVariants.FindOrAdd(Object);
	Variants.AddUnique(Property);
}

const TArray<const UPropertyValue*>* FGLTFConvertBuilder::GetObjectVariants(const UObject* Object) const
{
	return ObjectVariants.Find(Object);
}
