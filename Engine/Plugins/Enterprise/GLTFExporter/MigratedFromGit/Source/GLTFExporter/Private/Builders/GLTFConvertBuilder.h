// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Builders/GLTFImageBuilder.h"
#include "Converters/GLTFAccessorConverters.h"
#include "Converters/GLTFMeshConverters.h"
#include "Converters/GLTFMaterialConverters.h"
#include "Converters/GLTFSamplerConverters.h"
#include "Converters/GLTFTextureConverters.h"
#include "Converters/GLTFNodeConverters.h"
#include "Converters/GLTFSceneConverters.h"
#include "Converters/GLTFCameraConverters.h"
#include "Converters/GLTFLightConverters.h"
#include "Converters/GLTFBackdropConverters.h"
#include "Converters/GLTFVarationConverters.h"
#include "Converters/GLTFLightMapConverters.h"
#include "Converters/GLTFHotspotConverters.h"

class FGLTFConvertBuilder : public FGLTFImageBuilder
{
public:

	const bool bSelectedActorsOnly;

	FGLTFConvertBuilder(const UGLTFExportOptions* ExportOptions, bool bSelectedActorsOnly);

	FGLTFJsonAccessorIndex GetOrAddPositionAccessor(const FPositionVertexBuffer* VertexBuffer);
	FGLTFJsonAccessorIndex GetOrAddColorAccessor(const FColorVertexBuffer* VertexBuffer);
	FGLTFJsonAccessorIndex GetOrAddNormalAccessor(const FStaticMeshVertexBuffer* VertexBuffer);
	FGLTFJsonAccessorIndex GetOrAddTangentAccessor(const FStaticMeshVertexBuffer* VertexBuffer);
	FGLTFJsonAccessorIndex GetOrAddUVAccessor(const FStaticMeshVertexBuffer* VertexBuffer, int32 UVIndex);
	FGLTFJsonAccessorIndex GetOrAddJointAccessor(const FSkinWeightVertexBuffer* VertexBuffer, int32 JointsGroupIndex, FGLTFBoneMap BoneMap);
	FGLTFJsonAccessorIndex GetOrAddWeightAccessor(const FSkinWeightVertexBuffer* VertexBuffer, int32 WeightsGroupIndex);

	FGLTFJsonAccessorIndex GetOrAddIndexAccessor(const FStaticMeshSection* MeshSection, const FRawStaticIndexBuffer* IndexBuffer);
	FGLTFJsonMeshIndex GetOrAddMesh(const UStaticMesh* StaticMesh, int32 LODIndex = -1, const FColorVertexBuffer* OverrideVertexColors = nullptr, const FGLTFMaterialArray& OverrideMaterials = {});
	FGLTFJsonMeshIndex GetOrAddMesh(const UStaticMeshComponent* StaticMeshComponent);

	FGLTFJsonAccessorIndex GetOrAddIndexAccessor(const FSkelMeshRenderSection* MeshSection, const FMultiSizeIndexContainer* IndexContainer);
	FGLTFJsonMeshIndex GetOrAddMesh(const USkeletalMesh* SkeletalMesh, int32 LODIndex = -1, const FColorVertexBuffer* OverrideVertexColors = nullptr, const FSkinWeightVertexBuffer* OverrideSkinWeights = nullptr, const FGLTFMaterialArray& OverrideMaterials = {});
	FGLTFJsonMeshIndex GetOrAddMesh(const USkeletalMeshComponent* SkeletalMeshComponent);

	FGLTFJsonMaterialIndex GetOrAddMaterial(const UMaterialInterface* Material);
	FGLTFJsonSamplerIndex GetOrAddSampler(const UTexture* Texture);
	FGLTFJsonTextureIndex GetOrAddTexture(const UTexture2D* Texture);
	FGLTFJsonTextureIndex GetOrAddTexture(const UTextureCube* Texture, ECubeFace CubeFace);
	FGLTFJsonTextureIndex GetOrAddTexture(const UTextureRenderTarget2D* Texture);
	FGLTFJsonTextureIndex GetOrAddTexture(const UTextureRenderTargetCube* Texture, ECubeFace CubeFace);

	FGLTFJsonNodeIndex GetOrAddNode(const USceneComponent* SceneComponent);
	FGLTFJsonNodeIndex GetOrAddNode(const AActor* Actor);
	FGLTFJsonSceneIndex GetOrAddScene(const ULevel* Level);
	FGLTFJsonSceneIndex GetOrAddScene(const UWorld* World);

	FGLTFJsonCameraIndex GetOrAddCamera(const UCameraComponent* CameraComponent);
	FGLTFJsonLightIndex GetOrAddLight(const ULightComponent* LightComponent);

	FGLTFJsonBackdropIndex GetOrAddBackdrop(const AActor* Actor);
	FGLTFJsonVariationIndex GetOrAddVariation(const ALevelVariantSetsActor* LevelVariantSetsActor);
	FGLTFJsonLightMapIndex GetOrAddLightMap(const UStaticMeshComponent* StaticMeshComponent);
	FGLTFJsonHotspotIndex GetOrAddHotspot(const UGLTFInteractionHotspotComponent* HotspotComponent);

private:

	FGLTFPositionVertexBufferConverter PositionVertexBufferConverter = *this;
	FGLTFColorVertexBufferConverter ColorVertexBufferConverter = *this;
	FGLTFNormalVertexBufferConverter NormalVertexBufferConverter = *this;
	FGLTFTangentVertexBufferConverter TangentVertexBufferConverter = *this;
	FGLTFUVVertexBufferConverter UVVertexBufferConverter = *this;
	FGLTFBoneIndexVertexBufferConverter BoneIndexVertexBufferConverter = *this;
	FGLTFBoneWeightVertexBufferConverter BoneWeightVertexBufferConverter = *this;

	FGLTFStaticMeshSectionConverter StaticMeshSectionConverter = *this;
	FGLTFStaticMeshConverter StaticMeshConverter = *this;

	FGLTFSkeletalMeshSectionConverter SkeletalMeshSectionConverter = *this;
	FGLTFSkeletalMeshConverter SkeletalMeshConverter = *this;

	FGLTFMaterialConverter MaterialConverter = *this;
	FGLTFTextureSamplerConverter TextureSamplerConverter = *this;
	FGLTFTexture2DConverter Texture2DConverter = *this;
	FGLTFTextureCubeConverter TextureCubeConverter = *this;
	FGLTFTextureRenderTarget2DConverter TextureRenderTarget2DConverter = *this;
	FGLTFTextureRenderTargetCubeConverter TextureRenderTargetCubeConverter = *this;

	FGLTFSceneComponentConverter SceneComponentConverter = *this;
	FGLTFActorConverter ActorConverter = *this;
	FGLTFLevelConverter LevelConverter = *this;

	FGLTFCameraComponentConverter CameraComponentConverter = *this;
	FGLTFLightComponentConverter LightComponentConverter = *this;

	FGLTFBackdropConverter BackdropConverter = *this;
	FGLTFVariationConverter VariationConverter = *this;
	FGLTFLightMapConverter LightMapConverter = *this;
	FGLTFHotspotComponentConverter HotspotComponentConverter = *this;
};
