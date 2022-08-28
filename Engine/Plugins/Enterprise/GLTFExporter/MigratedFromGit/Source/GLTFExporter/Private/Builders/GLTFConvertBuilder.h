// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Builders/GLTFBufferBuilder.h"
#include "Converters/GLTFAccessorConverters.h"
#include "Converters/GLTFMeshConverters.h"
#include "Converters/GLTFMeshDataConverters.h"
#include "Converters/GLTFMaterialConverters.h"
#include "Converters/GLTFSamplerConverters.h"
#include "Converters/GLTFTextureConverters.h"
#include "Converters/GLTFImageConverters.h"
#include "Converters/GLTFNodeConverters.h"
#include "Converters/GLTFSkinConverters.h"
#include "Converters/GLTFAnimationConverters.h"
#include "Converters/GLTFSceneConverters.h"
#include "Converters/GLTFCameraConverters.h"
#include "Converters/GLTFLightConverters.h"
#include "Converters/GLTFBackdropConverters.h"
#include "Converters/GLTFEpicLevelVariantSetsConverters.h"
#include "Converters/GLTFLightMapConverters.h"
#include "Converters/GLTFHotspotConverters.h"
#include "Converters/GLTFKhrVariantConverters.h"
#include "Converters/GLTFSkySphereConverters.h"

class FGLTFConvertBuilder : public FGLTFBufferBuilder
{
public:

	const TSet<AActor*> SelectedActors;

	FGLTFConvertBuilder(const FString& FilePath, const UGLTFExportOptions* ExportOptions, const TSet<AActor*>& SelectedActors = {});

	bool IsSelectedActor(const AActor* Object) const;
	bool IsRootActor(const AActor* Actor) const;

	FGLTFJsonAccessor* GetOrAddPositionAccessor(const FGLTFMeshSection* MeshSection, const FPositionVertexBuffer* VertexBuffer);
	FGLTFJsonAccessor* GetOrAddColorAccessor(const FGLTFMeshSection* MeshSection, const FColorVertexBuffer* VertexBuffer);
	FGLTFJsonAccessor* GetOrAddNormalAccessor(const FGLTFMeshSection* MeshSection, const FStaticMeshVertexBuffer* VertexBuffer);
	FGLTFJsonAccessor* GetOrAddTangentAccessor(const FGLTFMeshSection* MeshSection, const FStaticMeshVertexBuffer* VertexBuffer);
	FGLTFJsonAccessor* GetOrAddUVAccessor(const FGLTFMeshSection* MeshSection, const FStaticMeshVertexBuffer* VertexBuffer, int32 UVIndex);
	FGLTFJsonAccessor* GetOrAddJointAccessor(const FGLTFMeshSection* MeshSection, const FSkinWeightVertexBuffer* VertexBuffer, int32 InfluenceOffset);
	FGLTFJsonAccessor* GetOrAddWeightAccessor(const FGLTFMeshSection* MeshSection, const FSkinWeightVertexBuffer* VertexBuffer, int32 InfluenceOffset);
	FGLTFJsonAccessor* GetOrAddIndexAccessor(const FGLTFMeshSection* MeshSection);

	FGLTFJsonMesh* GetOrAddMesh(const UStaticMesh* StaticMesh, const FGLTFMaterialArray& Materials = {}, int32 LODIndex = INDEX_NONE);
	FGLTFJsonMesh* GetOrAddMesh(const USkeletalMesh* SkeletalMesh, const FGLTFMaterialArray& Materials = {}, int32 LODIndex = INDEX_NONE);
	FGLTFJsonMesh* GetOrAddMesh(const UMeshComponent* MeshComponent, const FGLTFMaterialArray& Materials = {}, int32 LODIndex = INDEX_NONE);
	FGLTFJsonMesh* GetOrAddMesh(const UStaticMeshComponent* StaticMeshComponent, const FGLTFMaterialArray& Materials = {}, int32 LODIndex = INDEX_NONE);
	FGLTFJsonMesh* GetOrAddMesh(const USkeletalMeshComponent* SkeletalMeshComponent, const FGLTFMaterialArray& Materials = {}, int32 LODIndex = INDEX_NONE);

	const FGLTFMeshData* GetOrAddMeshData(const UStaticMesh* StaticMesh, const UStaticMeshComponent* StaticMeshComponent = nullptr, int32 LODIndex = INDEX_NONE);
	const FGLTFMeshData* GetOrAddMeshData(const USkeletalMesh* SkeletalMesh, const USkeletalMeshComponent* SkeletalMeshComponent = nullptr, int32 LODIndex = INDEX_NONE);

	FGLTFJsonMaterial* GetOrAddMaterial(const UMaterialInterface* Material, const UStaticMesh* StaticMesh, int32 LODIndex = INDEX_NONE, int32 MaterialIndex = INDEX_NONE);
	FGLTFJsonMaterial* GetOrAddMaterial(const UMaterialInterface* Material, const USkeletalMesh* SkeletalMesh, int32 LODIndex = INDEX_NONE, int32 MaterialIndex = INDEX_NONE);
	FGLTFJsonMaterial* GetOrAddMaterial(const UMaterialInterface* Material, const UMeshComponent* MeshComponent, int32 LODIndex = INDEX_NONE, int32 MaterialIndex = INDEX_NONE);
	FGLTFJsonMaterial* GetOrAddMaterial(const UMaterialInterface* Material, const UStaticMeshComponent* StaticMeshComponent, int32 LODIndex = INDEX_NONE, int32 MaterialIndex = INDEX_NONE);
	FGLTFJsonMaterial* GetOrAddMaterial(const UMaterialInterface* Material, const USkeletalMeshComponent* SkeletalMeshComponent, int32 LODIndex = INDEX_NONE, int32 MaterialIndex = INDEX_NONE);
	FGLTFJsonMaterial* GetOrAddMaterial(const UMaterialInterface* Material, const FGLTFMeshData* MeshData = nullptr, const FGLTFIndexArray& SectionIndices = {});

	FGLTFJsonSampler* GetOrAddSampler(const UTexture* Texture);
	FGLTFJsonTexture* GetOrAddTexture(const UTexture* Texture);
	FGLTFJsonTexture* GetOrAddTexture(const UTexture2D* Texture);
	FGLTFJsonTexture* GetOrAddTexture(const UTextureCube* Texture, ECubeFace CubeFace);
	FGLTFJsonTexture* GetOrAddTexture(const UTextureRenderTarget2D* Texture);
	FGLTFJsonTexture* GetOrAddTexture(const UTextureRenderTargetCube* Texture, ECubeFace CubeFace);
	FGLTFJsonTexture* GetOrAddTexture(const ULightMapTexture2D* Texture);
	FGLTFJsonTexture* GetOrAddTexture(const UTexture* Texture, bool bToSRGB);
	FGLTFJsonTexture* GetOrAddTexture(const UTexture2D* Texture, bool bToSRGB);
	FGLTFJsonTexture* GetOrAddTexture(const UTextureCube* Texture, ECubeFace CubeFace, bool bToSRGB);
	FGLTFJsonTexture* GetOrAddTexture(const UTextureRenderTarget2D* Texture, bool bToSRGB);
	FGLTFJsonTexture* GetOrAddTexture(const UTextureRenderTargetCube* Texture, ECubeFace CubeFace, bool bToSRGB);
	FGLTFJsonImage* GetOrAddImage(TGLTFSharedArray<FColor>& Pixels, FIntPoint Size, bool bIgnoreAlpha, EGLTFTextureType Type, const FString& Name);

	FGLTFJsonSkin* GetOrAddSkin(FGLTFJsonNode* RootNode, const USkeletalMesh* SkeletalMesh);
	FGLTFJsonSkin* GetOrAddSkin(FGLTFJsonNode* RootNode, const USkeletalMeshComponent* SkeletalMeshComponent);
	FGLTFJsonAnimation* GetOrAddAnimation(FGLTFJsonNode* RootNode, const USkeletalMesh* SkeletalMesh, const UAnimSequence* AnimSequence);
	FGLTFJsonAnimation* GetOrAddAnimation(FGLTFJsonNode* RootNode, const USkeletalMeshComponent* SkeletalMeshComponent);
	FGLTFJsonAnimation* GetOrAddAnimation(const ULevel* Level, const ULevelSequence* LevelSequence);
	FGLTFJsonAnimation* GetOrAddAnimation(const ALevelSequenceActor* LevelSequenceActor);

	FGLTFJsonNode* GetOrAddNode(const AActor* Actor);
	FGLTFJsonNode* GetOrAddNode(const USceneComponent* SceneComponent);
	FGLTFJsonNode* GetOrAddNode(const USceneComponent* SceneComponent, FName SocketName);
	FGLTFJsonNode* GetOrAddNode(FGLTFJsonNode* RootNode, const UStaticMesh* StaticMesh, FName SocketName);
	FGLTFJsonNode* GetOrAddNode(FGLTFJsonNode* RootNode, const USkeletalMesh* SkeletalMesh, FName SocketName);
	FGLTFJsonNode* GetOrAddNode(FGLTFJsonNode* RootNode, const USkeletalMesh* SkeletalMesh, int32 BoneIndex);
	FGLTFJsonScene* GetOrAddScene(const UWorld* World);

	FGLTFJsonCamera* GetOrAddCamera(const UCameraComponent* CameraComponent);
	FGLTFJsonLight* GetOrAddLight(const ULightComponent* LightComponent);
	FGLTFJsonBackdrop* GetOrAddBackdrop(const AActor* BackdropActor);
	FGLTFJsonLightMap* GetOrAddLightMap(const UStaticMeshComponent* StaticMeshComponent);
	FGLTFJsonHotspot* GetOrAddHotspot(const AGLTFHotspotActor* HotspotActor);
	FGLTFJsonSkySphere* GetOrAddSkySphere(const AActor* SkySphereActor);

	FGLTFJsonEpicLevelVariantSets* GetOrAddEpicLevelVariantSets(const ULevelVariantSets* LevelVariantSets);
	FGLTFJsonKhrMaterialVariant* GetOrAddKhrMaterialVariant(const UVariant* Variant);

	void RegisterObjectVariant(const UObject* Object, const UPropertyValue* Property);
	const TArray<const UPropertyValue*>* GetObjectVariants(const UObject* Object) const;

	TUniquePtr<IGLTFPositionBufferConverter> PositionBufferConverter = MakeUnique<FGLTFPositionBufferConverter>(*this);
	TUniquePtr<IGLTFColorBufferConverter> ColorBufferConverter = MakeUnique<FGLTFColorBufferConverter>(*this);
	TUniquePtr<IGLTFNormalBufferConverter> NormalBufferConverter = MakeUnique<FGLTFNormalBufferConverter>(*this);
	TUniquePtr<IGLTFTangentBufferConverter> TangentBufferConverter = MakeUnique<FGLTFTangentBufferConverter>(*this);
	TUniquePtr<IGLTFUVBufferConverter> UVBufferConverter = MakeUnique<FGLTFUVBufferConverter>(*this);
	TUniquePtr<IGLTFBoneIndexBufferConverter> BoneIndexBufferConverter = MakeUnique<FGLTFBoneIndexBufferConverter>(*this);
	TUniquePtr<IGLTFBoneWeightBufferConverter> BoneWeightBufferConverter = MakeUnique<FGLTFBoneWeightBufferConverter>(*this);
	TUniquePtr<IGLTFIndexBufferConverter> IndexBufferConverter = MakeUnique<FGLTFIndexBufferConverter>(*this);

	TUniquePtr<IGLTFStaticMeshConverter> StaticMeshConverter = MakeUnique<FGLTFStaticMeshConverter>(*this);
	TUniquePtr<IGLTFSkeletalMeshConverter> SkeletalMeshConverter = MakeUnique<FGLTFSkeletalMeshConverter>(*this);

	TUniquePtr<IGLTFMaterialConverter> MaterialConverter = MakeUnique<FGLTFMaterialConverter>(*this);
	TUniquePtr<IGLTFStaticMeshDataConverter> StaticMeshDataConverter = MakeUnique<FGLTFStaticMeshDataConverter>(*this);
	TUniquePtr<IGLTFSkeletalMeshDataConverter> SkeletalMeshDataConverter = MakeUnique<FGLTFSkeletalMeshDataConverter>(*this);

	TUniquePtr<IGLTFSamplerConverter> SamplerConverter = MakeUnique<FGLTFSamplerConverter>(*this);
	TUniquePtr<IGLTFTexture2DConverter> Texture2DConverter = MakeUnique<FGLTFTexture2DConverter>(*this);
	TUniquePtr<IGLTFTextureCubeConverter> TextureCubeConverter = MakeUnique<FGLTFTextureCubeConverter>(*this);
	TUniquePtr<IGLTFTextureRenderTarget2DConverter> TextureRenderTarget2DConverter = MakeUnique<FGLTFTextureRenderTarget2DConverter>(*this);
	TUniquePtr<IGLTFTextureRenderTargetCubeConverter> TextureRenderTargetCubeConverter = MakeUnique<FGLTFTextureRenderTargetCubeConverter>(*this);
	TUniquePtr<IGLTFTextureLightMapConverter> TextureLightMapConverter = MakeUnique<FGLTFTextureLightMapConverter>(*this);
	TUniquePtr<IGLTFImageConverter> ImageConverter = MakeUnique<FGLTFImageConverter>(*this);

	TUniquePtr<IGLTFSkinConverter> SkinConverter = MakeUnique<FGLTFSkinConverter>(*this);
	TUniquePtr<IGLTFAnimationConverter> AnimationConverter = MakeUnique<FGLTFAnimationConverter>(*this);
	TUniquePtr<IGLTFAnimationDataConverter> AnimationDataConverter = MakeUnique<FGLTFAnimationDataConverter>(*this);
	TUniquePtr<IGLTFLevelSequenceConverter> LevelSequenceConverter = MakeUnique<FGLTFLevelSequenceConverter>(*this);
	TUniquePtr<IGLTFLevelSequenceDataConverter> LevelSequenceDataConverter = MakeUnique<FGLTFLevelSequenceDataConverter>(*this);

	TUniquePtr<IGLTFActorConverter> ActorConverter = MakeUnique<FGLTFActorConverter>(*this);
	TUniquePtr<IGLTFComponentConverter> ComponentConverter = MakeUnique<FGLTFComponentConverter>(*this);
	TUniquePtr<IGLTFComponentSocketConverter> ComponentSocketConverter = MakeUnique<FGLTFComponentSocketConverter>(*this);
	TUniquePtr<IGLTFStaticSocketConverter> StaticSocketConverter = MakeUnique<FGLTFStaticSocketConverter>(*this);
	TUniquePtr<IGLTFSkeletalSocketConverter> SkeletalSocketConverter = MakeUnique<FGLTFSkeletalSocketConverter>(*this);
	TUniquePtr<IGLTFSkeletalBoneConverter> SkeletalBoneConverter = MakeUnique<FGLTFSkeletalBoneConverter>(*this);
	TUniquePtr<IGLTFSceneConverter> SceneConverter = MakeUnique<FGLTFSceneConverter>(*this);

	TUniquePtr<IGLTFCameraConverter> CameraConverter = MakeUnique<FGLTFCameraConverter>(*this);
	TUniquePtr<IGLTFLightConverter> LightConverter = MakeUnique<FGLTFLightConverter>(*this);
	TUniquePtr<IGLTFBackdropConverter> BackdropConverter = MakeUnique<FGLTFBackdropConverter>(*this);
	TUniquePtr<IGLTFLightMapConverter> LightMapConverter = MakeUnique<FGLTFLightMapConverter>(*this);
	TUniquePtr<IGLTFHotspotConverter> HotspotConverter = MakeUnique<FGLTFHotspotConverter>(*this);
	TUniquePtr<IGLTFSkySphereConverter> SkySphereConverter = MakeUnique<FGLTFSkySphereConverter>(*this);

	TUniquePtr<IGLTFEpicLevelVariantSetsConverter> EpicLevelVariantSetsConverter = MakeUnique<FGLTFEpicLevelVariantSetsConverter>(*this);
	TUniquePtr<IGLTFKhrMaterialVariantConverter> KhrMaterialVariantConverter = MakeUnique<FGLTFKhrMaterialVariantConverter>(*this);

private:

	TMap<const UObject*, TArray<const UPropertyValue*>> ObjectVariants;
};
