// Copyright Epic Games, Inc. All Rights Reserved.

#include "Builders/GLTFConvertBuilder.h"
#include "Converters/GLTFMeshUtility.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureCube.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/TextureRenderTargetCube.h"

FGLTFConvertBuilder::FGLTFConvertBuilder(const FString& FilePath, const UGLTFExportOptions* ExportOptions, const TSet<AActor*>& SelectedActors)
	: FGLTFBufferBuilder(FilePath, ExportOptions)
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

FGLTFJsonAccessor* FGLTFConvertBuilder::GetOrAddPositionAccessor(const FGLTFMeshSection* MeshSection, const FPositionVertexBuffer* VertexBuffer)
{
	if (VertexBuffer == nullptr)
	{
		return nullptr;
	}

	return PositionBufferConverter->GetOrAdd(MeshSection, VertexBuffer);
}

FGLTFJsonAccessor* FGLTFConvertBuilder::GetOrAddColorAccessor(const FGLTFMeshSection* MeshSection, const FColorVertexBuffer* VertexBuffer)
{
	if (VertexBuffer == nullptr)
	{
		return nullptr;
	}

	return ColorBufferConverter->GetOrAdd(MeshSection, VertexBuffer);
}

FGLTFJsonAccessor* FGLTFConvertBuilder::GetOrAddNormalAccessor(const FGLTFMeshSection* MeshSection, const FStaticMeshVertexBuffer* VertexBuffer)
{
	if (VertexBuffer == nullptr)
	{
		return nullptr;
	}

	return NormalBufferConverter->GetOrAdd(MeshSection, VertexBuffer);
}

FGLTFJsonAccessor* FGLTFConvertBuilder::GetOrAddTangentAccessor(const FGLTFMeshSection* MeshSection, const FStaticMeshVertexBuffer* VertexBuffer)
{
	if (VertexBuffer == nullptr)
	{
		return nullptr;
	}

	return TangentBufferConverter->GetOrAdd(MeshSection, VertexBuffer);
}

FGLTFJsonAccessor* FGLTFConvertBuilder::GetOrAddUVAccessor(const FGLTFMeshSection* MeshSection, const FStaticMeshVertexBuffer* VertexBuffer, int32 UVIndex)
{
	if (VertexBuffer == nullptr)
	{
		return nullptr;
	}

	return UVBufferConverter->GetOrAdd(MeshSection, VertexBuffer, UVIndex);
}

FGLTFJsonAccessor* FGLTFConvertBuilder::GetOrAddJointAccessor(const FGLTFMeshSection* MeshSection, const FSkinWeightVertexBuffer* VertexBuffer, int32 InfluenceOffset)
{
	if (VertexBuffer == nullptr)
	{
		return nullptr;
	}

	return BoneIndexBufferConverter->GetOrAdd(MeshSection, VertexBuffer, InfluenceOffset);
}

FGLTFJsonAccessor* FGLTFConvertBuilder::GetOrAddWeightAccessor(const FGLTFMeshSection* MeshSection, const FSkinWeightVertexBuffer* VertexBuffer, int32 InfluenceOffset)
{
	if (VertexBuffer == nullptr)
	{
		return nullptr;
	}

	return BoneWeightBufferConverter->GetOrAdd(MeshSection, VertexBuffer, InfluenceOffset);
}

FGLTFJsonAccessor* FGLTFConvertBuilder::GetOrAddIndexAccessor(const FGLTFMeshSection* MeshSection)
{
	if (MeshSection == nullptr)
	{
		return nullptr;
	}

	return IndexBufferConverter->GetOrAdd(MeshSection);
}

FGLTFJsonMesh* FGLTFConvertBuilder::GetOrAddMesh(const UStaticMesh* StaticMesh, const FGLTFMaterialArray& Materials, int32 LODIndex)
{
	if (StaticMesh == nullptr)
	{
		return nullptr;
	}

	return StaticMeshConverter->GetOrAdd(StaticMesh, nullptr, Materials, LODIndex);
}

FGLTFJsonMesh* FGLTFConvertBuilder::GetOrAddMesh(const USkeletalMesh* SkeletalMesh, const FGLTFMaterialArray& Materials, int32 LODIndex)
{
	if (SkeletalMesh == nullptr)
	{
		return nullptr;
	}

	return SkeletalMeshConverter->GetOrAdd(SkeletalMesh, nullptr, Materials, LODIndex);
}

FGLTFJsonMesh* FGLTFConvertBuilder::GetOrAddMesh(const UMeshComponent* MeshComponent, const FGLTFMaterialArray& Materials, int32 LODIndex)
{
	if (const UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(MeshComponent))
	{
		return GetOrAddMesh(StaticMeshComponent, Materials, LODIndex);
	}

	if (const USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(MeshComponent))
	{
		return GetOrAddMesh(SkeletalMeshComponent, Materials, LODIndex);
	}

	return nullptr;
}

FGLTFJsonMesh* FGLTFConvertBuilder::GetOrAddMesh(const UStaticMeshComponent* StaticMeshComponent, const FGLTFMaterialArray& Materials, int32 LODIndex)
{
	if (StaticMeshComponent == nullptr)
	{
		return nullptr;
	}

	const UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh();
	if (StaticMesh == nullptr)
	{
		return nullptr;
	}

	return StaticMeshConverter->GetOrAdd(StaticMesh, StaticMeshComponent, Materials, LODIndex);
}

FGLTFJsonMesh* FGLTFConvertBuilder::GetOrAddMesh(const USkeletalMeshComponent* SkeletalMeshComponent, const FGLTFMaterialArray& Materials, int32 LODIndex)
{
	if (SkeletalMeshComponent == nullptr)
	{
		return nullptr;
	}

	const USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->SkeletalMesh;
	if (SkeletalMesh == nullptr)
	{
		return nullptr;
	}

	return SkeletalMeshConverter->GetOrAdd(SkeletalMesh, SkeletalMeshComponent, Materials, LODIndex);
}

const FGLTFMeshData* FGLTFConvertBuilder::GetOrAddMeshData(const UStaticMesh* StaticMesh, const UStaticMeshComponent* StaticMeshComponent, int32 LODIndex)
{
	return StaticMeshDataConverter->GetOrAdd(StaticMesh, StaticMeshComponent, LODIndex);
}

const FGLTFMeshData* FGLTFConvertBuilder::GetOrAddMeshData(const USkeletalMesh* SkeletalMesh, const USkeletalMeshComponent* SkeletalMeshComponent, int32 LODIndex)
{
	return SkeletalMeshDataConverter->GetOrAdd(SkeletalMesh, SkeletalMeshComponent, LODIndex);
}

FGLTFJsonMaterial* FGLTFConvertBuilder::GetOrAddMaterial(const UMaterialInterface* Material, const UStaticMesh* StaticMesh, int32 LODIndex, int32 MaterialIndex)
{
	// TODO: optimize by skipping mesh data if material doesn't need it
	const FGLTFMeshData* MeshData = GetOrAddMeshData(StaticMesh, nullptr, LODIndex);
	const FGLTFIndexArray SectionIndices = FGLTFMeshUtility::GetSectionIndices(StaticMesh, MeshData->LODIndex, MaterialIndex);
	return GetOrAddMaterial(Material, MeshData, SectionIndices);
}

FGLTFJsonMaterial* FGLTFConvertBuilder::GetOrAddMaterial(const UMaterialInterface* Material, const USkeletalMesh* SkeletalMesh, int32 LODIndex, int32 MaterialIndex)
{
	// TODO: optimize by skipping mesh data if material doesn't need it
	const FGLTFMeshData* MeshData = GetOrAddMeshData(SkeletalMesh, nullptr, LODIndex);
	const FGLTFIndexArray SectionIndices = FGLTFMeshUtility::GetSectionIndices(SkeletalMesh, MeshData->LODIndex, MaterialIndex);
	return GetOrAddMaterial(Material, MeshData, SectionIndices);
}

FGLTFJsonMaterial* FGLTFConvertBuilder::GetOrAddMaterial(const UMaterialInterface* Material, const UMeshComponent* MeshComponent, int32 LODIndex, int32 MaterialIndex)
{
	if (const UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(MeshComponent))
	{
		return GetOrAddMaterial(Material, StaticMeshComponent, LODIndex, MaterialIndex);
	}

	if (const USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(MeshComponent))
	{
		return GetOrAddMaterial(Material, SkeletalMeshComponent, LODIndex, MaterialIndex);
	}

	return nullptr;
}

FGLTFJsonMaterial* FGLTFConvertBuilder::GetOrAddMaterial(const UMaterialInterface* Material, const UStaticMeshComponent* StaticMeshComponent, int32 LODIndex, int32 MaterialIndex)
{
	// TODO: optimize by skipping mesh data if material doesn't need it
	const UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh();
	const FGLTFMeshData* MeshData = GetOrAddMeshData(StaticMesh, StaticMeshComponent, LODIndex);
	const FGLTFIndexArray SectionIndices = FGLTFMeshUtility::GetSectionIndices(StaticMesh, MeshData->LODIndex, MaterialIndex);
	return GetOrAddMaterial(Material, MeshData, SectionIndices);
}

FGLTFJsonMaterial* FGLTFConvertBuilder::GetOrAddMaterial(const UMaterialInterface* Material, const USkeletalMeshComponent* SkeletalMeshComponent, int32 LODIndex, int32 MaterialIndex)
{
	// TODO: optimize by skipping mesh data if material doesn't need it
	const USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->SkeletalMesh;
	const FGLTFMeshData* MeshData = GetOrAddMeshData(SkeletalMesh, SkeletalMeshComponent, LODIndex);
	const FGLTFIndexArray SectionIndices = FGLTFMeshUtility::GetSectionIndices(SkeletalMesh, MeshData->LODIndex, MaterialIndex);
	return GetOrAddMaterial(Material, MeshData, SectionIndices);
}

FGLTFJsonMaterial* FGLTFConvertBuilder::GetOrAddMaterial(const UMaterialInterface* Material, const FGLTFMeshData* MeshData, const FGLTFIndexArray& SectionIndices)
{
	if (Material == nullptr)
	{
		return nullptr;
	}

	return MaterialConverter->GetOrAdd(Material, MeshData, SectionIndices);
}

FGLTFJsonSampler* FGLTFConvertBuilder::GetOrAddSampler(const UTexture* Texture)
{
	if (Texture == nullptr)
	{
		return nullptr;
	}

	return SamplerConverter->GetOrAdd(Texture);
}

FGLTFJsonTexture* FGLTFConvertBuilder::GetOrAddTexture(const UTexture* Texture)
{
	return GetOrAddTexture(Texture, Texture->SRGB);
}

FGLTFJsonTexture* FGLTFConvertBuilder::GetOrAddTexture(const UTexture2D* Texture)
{
	return GetOrAddTexture(Texture, Texture->SRGB);
}

FGLTFJsonTexture* FGLTFConvertBuilder::GetOrAddTexture(const UTextureCube* Texture, ECubeFace CubeFace)
{
	return GetOrAddTexture(Texture, CubeFace, Texture->SRGB);
}

FGLTFJsonTexture* FGLTFConvertBuilder::GetOrAddTexture(const UTextureRenderTarget2D* Texture)
{
	return GetOrAddTexture(Texture, Texture->SRGB);
}

FGLTFJsonTexture* FGLTFConvertBuilder::GetOrAddTexture(const UTextureRenderTargetCube* Texture, ECubeFace CubeFace)
{
	return GetOrAddTexture(Texture, CubeFace, Texture->SRGB);
}

FGLTFJsonTexture* FGLTFConvertBuilder::GetOrAddTexture(const ULightMapTexture2D* Texture)
{
	if (Texture == nullptr)
	{
		return nullptr;
	}

	return TextureLightMapConverter->GetOrAdd(Texture);
}

FGLTFJsonTexture* FGLTFConvertBuilder::GetOrAddTexture(const UTexture* Texture, bool bToSRGB)
{
	if (const UTexture2D* Texture2D = Cast<UTexture2D>(Texture))
	{
		return GetOrAddTexture(Texture2D, bToSRGB);
	}

	if (const UTextureRenderTarget2D* RenderTarget2D = Cast<UTextureRenderTarget2D>(Texture))
	{
		return GetOrAddTexture(RenderTarget2D, bToSRGB);
	}

	return nullptr;
}

FGLTFJsonTexture* FGLTFConvertBuilder::GetOrAddTexture(const UTexture2D* Texture, bool bToSRGB)
{
	if (Texture == nullptr)
	{
		return nullptr;
	}

	return Texture2DConverter->GetOrAdd(Texture, bToSRGB);
}

FGLTFJsonTexture* FGLTFConvertBuilder::GetOrAddTexture(const UTextureCube* Texture, ECubeFace CubeFace, bool bToSRGB)
{
	if (Texture == nullptr)
	{
		return nullptr;
	}

	return TextureCubeConverter->GetOrAdd(Texture, CubeFace, bToSRGB);
}

FGLTFJsonTexture* FGLTFConvertBuilder::GetOrAddTexture(const UTextureRenderTarget2D* Texture, bool bToSRGB)
{
	if (Texture == nullptr)
	{
		return nullptr;
	}

	return TextureRenderTarget2DConverter->GetOrAdd(Texture, bToSRGB);
}

FGLTFJsonTexture* FGLTFConvertBuilder::GetOrAddTexture(const UTextureRenderTargetCube* Texture, ECubeFace CubeFace, bool bToSRGB)
{
	if (Texture == nullptr)
	{
		return nullptr;
	}

	return TextureRenderTargetCubeConverter->GetOrAdd(Texture, CubeFace, bToSRGB);
}

FGLTFJsonImage* FGLTFConvertBuilder::GetOrAddImage(TGLTFSharedArray<FColor>& Pixels, FIntPoint Size, bool bIgnoreAlpha, EGLTFTextureType Type, const FString& Name)
{
	return ImageConverter->GetOrAdd(Name, Type, bIgnoreAlpha, Size, Pixels);
}

FGLTFJsonSkin* FGLTFConvertBuilder::GetOrAddSkin(FGLTFJsonNode* RootNode, const USkeletalMesh* SkeletalMesh)
{
	if (RootNode == nullptr || SkeletalMesh == nullptr)
	{
		return nullptr;
	}

	return SkinConverter->GetOrAdd(RootNode, SkeletalMesh);
}

FGLTFJsonSkin* FGLTFConvertBuilder::GetOrAddSkin(FGLTFJsonNode* RootNode, const USkeletalMeshComponent* SkeletalMeshComponent)
{
	if (RootNode == nullptr || SkeletalMeshComponent == nullptr)
	{
		return nullptr;
	}

	const USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->SkeletalMesh;

	if (SkeletalMesh == nullptr)
	{
		return nullptr;
	}

	return GetOrAddSkin(RootNode, SkeletalMesh);
}

FGLTFJsonAnimation* FGLTFConvertBuilder::GetOrAddAnimation(FGLTFJsonNode* RootNode, const USkeletalMesh* SkeletalMesh, const UAnimSequence* AnimSequence)
{
	if (RootNode == nullptr || SkeletalMesh == nullptr || AnimSequence == nullptr)
	{
		return nullptr;
	}

	return AnimationConverter->GetOrAdd(RootNode, SkeletalMesh, AnimSequence);
}

FGLTFJsonAnimation* FGLTFConvertBuilder::GetOrAddAnimation(FGLTFJsonNode* RootNode, const USkeletalMeshComponent* SkeletalMeshComponent)
{
	if (RootNode == nullptr || SkeletalMeshComponent == nullptr)
	{
		return nullptr;
	}

	return AnimationDataConverter->GetOrAdd(RootNode, SkeletalMeshComponent);
}

FGLTFJsonAnimation* FGLTFConvertBuilder::GetOrAddAnimation(const ULevel* Level, const ULevelSequence* LevelSequence)
{
	if (Level == nullptr || LevelSequence == nullptr)
	{
		return nullptr;
	}

	return LevelSequenceConverter->GetOrAdd(Level, LevelSequence);
}

FGLTFJsonAnimation* FGLTFConvertBuilder::GetOrAddAnimation(const ALevelSequenceActor* LevelSequenceActor)
{
	if (LevelSequenceActor == nullptr)
	{
		return nullptr;
	}

	return LevelSequenceDataConverter->GetOrAdd(LevelSequenceActor);
}

FGLTFJsonNode* FGLTFConvertBuilder::GetOrAddNode(const AActor* Actor)
{
	if (Actor == nullptr)
	{
		return nullptr;
	}

	return ActorConverter->GetOrAdd(Actor);
}

FGLTFJsonNode* FGLTFConvertBuilder::GetOrAddNode(const USceneComponent* SceneComponent)
{
	if (SceneComponent == nullptr)
	{
		return nullptr;
	}

	return ComponentConverter->GetOrAdd(SceneComponent);
}

FGLTFJsonNode* FGLTFConvertBuilder::GetOrAddNode(const USceneComponent* SceneComponent, FName SocketName)
{
	if (SceneComponent == nullptr)
	{
		return nullptr;
	}

	return ComponentSocketConverter->GetOrAdd(SceneComponent, SocketName);
}

FGLTFJsonNode* FGLTFConvertBuilder::GetOrAddNode(FGLTFJsonNode* RootNode, const UStaticMesh* StaticMesh, FName SocketName)
{
	if (RootNode == nullptr || StaticMesh == nullptr || SocketName == NAME_None)
	{
		return nullptr;
	}

	return StaticSocketConverter->GetOrAdd(RootNode, StaticMesh, SocketName);
}

FGLTFJsonNode* FGLTFConvertBuilder::GetOrAddNode(FGLTFJsonNode* RootNode, const USkeletalMesh* SkeletalMesh, FName SocketName)
{
	if (RootNode == nullptr || SkeletalMesh == nullptr || SocketName == NAME_None)
	{
		return nullptr;
	}

	return SkeletalSocketConverter->GetOrAdd(RootNode, SkeletalMesh, SocketName);
}

FGLTFJsonNode* FGLTFConvertBuilder::GetOrAddNode(FGLTFJsonNode* RootNode, const USkeletalMesh* SkeletalMesh, int32 BoneIndex)
{
	if (RootNode == nullptr || SkeletalMesh == nullptr || BoneIndex == INDEX_NONE)
	{
		return nullptr;
	}

	return SkeletalBoneConverter->GetOrAdd(RootNode, SkeletalMesh, BoneIndex);
}

FGLTFJsonScene* FGLTFConvertBuilder::GetOrAddScene(const UWorld* World)
{
	if (World == nullptr)
	{
		return nullptr;
	}

	return SceneConverter->GetOrAdd(World);
}

FGLTFJsonCamera* FGLTFConvertBuilder::GetOrAddCamera(const UCameraComponent* CameraComponent)
{
	if (CameraComponent == nullptr)
	{
		return nullptr;
	}

	return CameraConverter->GetOrAdd(CameraComponent);
}

FGLTFJsonLight* FGLTFConvertBuilder::GetOrAddLight(const ULightComponent* LightComponent)
{
	if (LightComponent == nullptr)
	{
		return nullptr;
	}

	return LightConverter->GetOrAdd(LightComponent);
}

FGLTFJsonBackdrop* FGLTFConvertBuilder::GetOrAddBackdrop(const AActor* BackdropActor)
{
	if (BackdropActor == nullptr)
	{
		return nullptr;
	}

	return BackdropConverter->GetOrAdd(BackdropActor);
}

FGLTFJsonLightMap* FGLTFConvertBuilder::GetOrAddLightMap(const UStaticMeshComponent* StaticMeshComponent)
{
	if (StaticMeshComponent == nullptr)
	{
		return nullptr;
	}

	return LightMapConverter->GetOrAdd(StaticMeshComponent);
}

FGLTFJsonHotspot* FGLTFConvertBuilder::GetOrAddHotspot(const AGLTFHotspotActor* HotspotActor)
{
	if (HotspotActor == nullptr)
	{
		return nullptr;
	}

	return HotspotConverter->GetOrAdd(HotspotActor);
}

FGLTFJsonSkySphere* FGLTFConvertBuilder::GetOrAddSkySphere(const AActor* SkySphereActor)
{
	if (SkySphereActor == nullptr)
	{
		return nullptr;
	}

	return SkySphereConverter->GetOrAdd(SkySphereActor);
}

FGLTFJsonEpicLevelVariantSets* FGLTFConvertBuilder::GetOrAddEpicLevelVariantSets(const ULevelVariantSets* LevelVariantSets)
{
	if (LevelVariantSets == nullptr)
	{
		return nullptr;
	}

	return EpicLevelVariantSetsConverter->GetOrAdd(LevelVariantSets);
}

FGLTFJsonKhrMaterialVariant* FGLTFConvertBuilder::GetOrAddKhrMaterialVariant(const UVariant* Variant)
{
	if (Variant == nullptr)
	{
		return nullptr;
	}

	return KhrMaterialVariantConverter->GetOrAdd(Variant);
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
