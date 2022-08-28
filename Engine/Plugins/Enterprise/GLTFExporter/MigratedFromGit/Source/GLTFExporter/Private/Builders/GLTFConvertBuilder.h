// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Builders/GLTFImageBuilder.h"
#include "Converters/GLTFVertexBufferConverters.h"
#include "Converters/GLTFStaticMeshConverters.h"
#include "Converters/GLTFSkeletalMeshConverters.h"
#include "Converters/GLTFMaterialConverters.h"
#include "Converters/GLTFTextureConverters.h"
#include "Converters/GLTFLevelConverters.h"
#include "Converters/GLTFBackdropConverters.h"
#include "Converters/GLTFVariantSetConverters.h"
#include "Converters/GLTFLightMapConverters.h"

class FGLTFConvertBuilder : public FGLTFImageBuilder
{
public:

	const bool bSelectedActorsOnly;

	FGLTFConvertBuilder(const UGLTFExportOptions* ExportOptions, bool bSelectedActorsOnly);

	FGLTFJsonAccessorIndex GetOrAddPositionAccessor(const FPositionVertexBuffer* VertexBuffer, const FString& DesiredName = TEXT(""));
	FGLTFJsonAccessorIndex GetOrAddColorAccessor(const FColorVertexBuffer* VertexBuffer, const FString& DesiredName = TEXT(""));
	FGLTFJsonAccessorIndex GetOrAddNormalAccessor(const FStaticMeshVertexBuffer* VertexBuffer, const FString& DesiredName = TEXT(""));
	FGLTFJsonAccessorIndex GetOrAddTangentAccessor(const FStaticMeshVertexBuffer* VertexBuffer, const FString& DesiredName = TEXT(""));
	FGLTFJsonAccessorIndex GetOrAddUVAccessor(const FStaticMeshVertexBuffer* VertexBuffer, int32 UVIndex, const FString& DesiredName = TEXT(""));
	FGLTFJsonAccessorIndex GetOrAddJointAccessor(const FSkinWeightVertexBuffer* VertexBuffer, int32 JointsGroupIndex, FGLTFBoneMap BoneMap, const FString& DesiredName = TEXT(""));
	FGLTFJsonAccessorIndex GetOrAddWeightAccessor(const FSkinWeightVertexBuffer* VertexBuffer, int32 WeightsGroupIndex, const FString& DesiredName = TEXT(""));

	FGLTFJsonBufferViewIndex GetOrAddIndexBufferView(const FRawStaticIndexBuffer* IndexBuffer, const FString& DesiredName = TEXT(""));
	FGLTFJsonAccessorIndex GetOrAddIndexAccessor(const FStaticMeshSection* MeshSection, const FRawStaticIndexBuffer* IndexBuffer, const FString& DesiredName = TEXT(""));
	FGLTFJsonMeshIndex GetOrAddMesh(const UStaticMesh* StaticMesh, int32 LODIndex = 0, const FColorVertexBuffer* OverrideVertexColors = nullptr, const FGLTFMaterialArray& OverrideMaterials = {}, const FString& DesiredName = TEXT(""));
	FGLTFJsonMeshIndex GetOrAddMesh(const UStaticMeshComponent* StaticMeshComponent, const FString& DesiredName = TEXT(""));

	FGLTFJsonBufferViewIndex GetOrAddIndexBufferView(const FMultiSizeIndexContainer* IndexContainer, const FString& DesiredName = TEXT(""));
	FGLTFJsonAccessorIndex GetOrAddIndexAccessor(const FSkelMeshRenderSection* MeshSection, const FMultiSizeIndexContainer* IndexContainer, const FString& DesiredName = TEXT(""));
	FGLTFJsonMeshIndex GetOrAddMesh(const USkeletalMesh* SkeletalMesh, int32 LODIndex = 0, const FColorVertexBuffer* OverrideVertexColors = nullptr, const FSkinWeightVertexBuffer* OverrideSkinWeights = nullptr, const FGLTFMaterialArray& OverrideMaterials = {}, const FString& DesiredName = TEXT(""));
	FGLTFJsonMeshIndex GetOrAddMesh(const USkeletalMeshComponent* SkeletalMeshComponent, const FString& DesiredName = TEXT(""));

	FGLTFJsonMaterialIndex GetOrAddMaterial(const UMaterialInterface* Material, const FString& DesiredName = TEXT(""));
	FGLTFJsonSamplerIndex GetOrAddSampler(const UTexture* Texture, const FString& DesiredName = TEXT(""));
	FGLTFJsonTextureIndex GetOrAddTexture(const UTexture2D* Texture, const FString& DesiredName = TEXT(""));
	FGLTFJsonTextureIndex GetOrAddTexture(const UTextureCube* Texture, ECubeFace CubeFace, const FString& DesiredName = TEXT(""));
	FGLTFJsonTextureIndex GetOrAddTexture(const UTextureRenderTarget2D* Texture, const FString& DesiredName = TEXT(""));
	FGLTFJsonTextureIndex GetOrAddTexture(const UTextureRenderTargetCube* Texture, ECubeFace CubeFace, const FString& DesiredName = TEXT(""));

	FGLTFJsonNodeIndex GetOrAddNode(const USceneComponent* SceneComponent, const FString& DesiredName = TEXT(""));
	FGLTFJsonNodeIndex GetOrAddNode(const AActor* Actor, const FString& DesiredName = TEXT(""));
	FGLTFJsonSceneIndex GetOrAddScene(const ULevel* Level, const FString& DesiredName = TEXT(""));
	FGLTFJsonSceneIndex GetOrAddScene(const UWorld* World, const FString& DesiredName = TEXT(""));

	FGLTFJsonCameraIndex GetOrAddCamera(const UCameraComponent* CameraComponent, const FString& DesiredName = TEXT(""));
	FGLTFJsonLightIndex GetOrAddLight(const ULightComponent* LightComponent, const FString& DesiredName = TEXT(""));

	FGLTFJsonBackdropIndex GetOrAddBackdrop(const AActor* Actor, const FString& DesiredName = TEXT(""));
	FGLTFJsonLevelVariantSetsIndex GetOrAddLevelVariantSets(const ALevelVariantSetsActor* LevelVariantSetsActor, const FString& DesiredName = TEXT(""));
	FGLTFJsonLightMapIndex GetOrAddLightMap(const UStaticMeshComponent* StaticMeshComponent, const FString& DesiredName = TEXT(""));

private:

	FGLTFPositionVertexBufferConverter PositionVertexBufferConverter;
	FGLTFColorVertexBufferConverter ColorVertexBufferConverter;
	FGLTFNormalVertexBufferConverter NormalVertexBufferConverter;
	FGLTFTangentVertexBufferConverter TangentVertexBufferConverter;
	FGLTFUVVertexBufferConverter UVVertexBufferConverter;
	FGLTFBoneIndexVertexBufferConverter BoneIndexVertexBufferConverter;
	FGLTFBoneWeightVertexBufferConverter BoneWeightVertexBufferConverter;

	FGLTFIndexBufferConverter IndexBufferConverter;
	FGLTFStaticMeshSectionConverter StaticMeshSectionConverter;
	FGLTFStaticMeshConverter StaticMeshConverter;

	FGLTFIndexContainerConverter IndexContainerConverter;
	FGLTFSkeletalMeshSectionConverter SkeletalMeshSectionConverter;
	FGLTFSkeletalMeshConverter SkeletalMeshConverter;

	FGLTFMaterialConverter MaterialConverter;
	FGLTFTextureSamplerConverter TextureSamplerConverter;
	FGLTFTexture2DConverter Texture2DConverter;
	FGLTFTextureCubeConverter TextureCubeConverter;
	FGLTFTextureRenderTarget2DConverter TextureRenderTarget2DConverter;
	FGLTFTextureRenderTargetCubeConverter TextureRenderTargetCubeConverter;

	FGLTFSceneComponentConverter SceneComponentConverter;
	FGLTFActorConverter ActorConverter;
	FGLTFLevelConverter LevelConverter;

	FGLTFCameraComponentConverter CameraComponentConverter;
	FGLTFLightComponentConverter LightComponentConverter;

	FGLTFBackdropConverter BackdropConverter;
	FGLTFLevelVariantSetsConverter LevelVariantSetsConverter;
	FGLTFLightMapConverter LightMapConverter;
};
