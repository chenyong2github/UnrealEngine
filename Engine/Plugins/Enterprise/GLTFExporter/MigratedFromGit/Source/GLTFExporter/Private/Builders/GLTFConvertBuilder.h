// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GLTFExportOptions.h"
#include "Builders/GLTFImageBuilder.h"
#include "Converters/GLTFAccessorConverters.h"
#include "Converters/GLTFMeshConverters.h"
#include "Converters/GLTFMaterialConverters.h"
#include "Converters/GLTFSamplerConverters.h"
#include "Converters/GLTFTextureConverters.h"
#include "Converters/GLTFNodeConverters.h"
#include "Converters/GLTFSkinConverters.h"
#include "Converters/GLTFSceneConverters.h"
#include "Converters/GLTFCameraConverters.h"
#include "Converters/GLTFLightConverters.h"
#include "Converters/GLTFBackdropConverters.h"
#include "Converters/GLTFVarationConverters.h"
#include "Converters/GLTFLightMapConverters.h"
#include "Converters/GLTFHotspotConverters.h"

class FGLTFConvertBuilder : public FGLTFImageBuilder
{
protected:

	FGLTFConvertBuilder(const UGLTFExportOptions* ExportOptions, bool bSelectedActorsOnly);

public:

	const UGLTFExportOptions* const ExportOptions;
	const bool bSelectedActorsOnly;

	FGLTFJsonAccessorIndex GetOrAddPositionAccessor(const FGLTFMeshSection* MeshSection, const FPositionVertexBuffer* VertexBuffer);
	FGLTFJsonAccessorIndex GetOrAddColorAccessor(const FGLTFMeshSection* MeshSection, const FColorVertexBuffer* VertexBuffer);
	FGLTFJsonAccessorIndex GetOrAddNormalAccessor(const FGLTFMeshSection* MeshSection, const FStaticMeshVertexBuffer* VertexBuffer);
	FGLTFJsonAccessorIndex GetOrAddTangentAccessor(const FGLTFMeshSection* MeshSection, const FStaticMeshVertexBuffer* VertexBuffer);
	FGLTFJsonAccessorIndex GetOrAddUVAccessor(const FGLTFMeshSection* MeshSection, const FStaticMeshVertexBuffer* VertexBuffer, int32 UVIndex);
	FGLTFJsonAccessorIndex GetOrAddJointAccessor(const FGLTFMeshSection* MeshSection, const FSkinWeightVertexBuffer* VertexBuffer, int32 InfluenceOffset);
	FGLTFJsonAccessorIndex GetOrAddWeightAccessor(const FGLTFMeshSection* MeshSection, const FSkinWeightVertexBuffer* VertexBuffer, int32 InfluenceOffset);
	FGLTFJsonAccessorIndex GetOrAddIndexAccessor(const FGLTFMeshSection* MeshSection);

	FGLTFJsonMeshIndex GetOrAddMesh(const UStaticMesh* StaticMesh, int32 LODIndex = -1, const FColorVertexBuffer* OverrideVertexColors = nullptr, const FGLTFMaterialArray& OverrideMaterials = {});
	FGLTFJsonMeshIndex GetOrAddMesh(const UStaticMeshComponent* StaticMeshComponent);
	FGLTFJsonMeshIndex GetOrAddMesh(const USkeletalMesh* SkeletalMesh, int32 LODIndex = -1, const FColorVertexBuffer* OverrideVertexColors = nullptr, const FSkinWeightVertexBuffer* OverrideSkinWeights = nullptr, const FGLTFMaterialArray& OverrideMaterials = {});
	FGLTFJsonMeshIndex GetOrAddMesh(const USkeletalMeshComponent* SkeletalMeshComponent);

	FGLTFJsonMaterialIndex GetOrAddMaterial(const UMaterialInterface* Material);
	FGLTFJsonSamplerIndex GetOrAddSampler(const UTexture* Texture);
	FGLTFJsonTextureIndex GetOrAddTexture(const UTexture2D* Texture);
	FGLTFJsonTextureIndex GetOrAddTexture(const UTextureCube* Texture, ECubeFace CubeFace);
	FGLTFJsonTextureIndex GetOrAddTexture(const UTextureRenderTarget2D* Texture);
	FGLTFJsonTextureIndex GetOrAddTexture(const UTextureRenderTargetCube* Texture, ECubeFace CubeFace);

	FGLTFJsonNodeIndex GetOrAddNode(FGLTFJsonNodeIndex RootNode, const USkeletalMesh* SkeletalMesh, FName SocketName);
	FGLTFJsonNodeIndex GetOrAddNode(FGLTFJsonNodeIndex RootNode, const USkeletalMesh* SkeletalMesh, int32 BoneIndex);
	FGLTFJsonSkinIndex GetOrAddSkin(FGLTFJsonNodeIndex RootNode, const USkeletalMesh* SkeletalMesh);

	FGLTFJsonNodeIndex GetOrAddNode(const USceneComponent* SceneComponent);
	FGLTFJsonNodeIndex GetOrAddNode(const AActor* Actor);
	FGLTFJsonSceneIndex GetOrAddScene(const ULevel* Level);
	FGLTFJsonSceneIndex GetOrAddScene(const UWorld* World);

	FGLTFJsonCameraIndex GetOrAddCamera(const UCameraComponent* CameraComponent);
	FGLTFJsonLightIndex GetOrAddLight(const ULightComponent* LightComponent);
	FGLTFJsonBackdropIndex GetOrAddBackdrop(const AActor* BackdropActor);
	FGLTFJsonVariationIndex GetOrAddVariation(const ALevelVariantSetsActor* LevelVariantSetsActor);
	FGLTFJsonLightMapIndex GetOrAddLightMap(const UStaticMeshComponent* StaticMeshComponent);
	FGLTFJsonHotspotIndex GetOrAddHotspot(const AGLTFInteractionHotspotActor* HotspotActor);

private:

	FGLTFPositionBufferConverter PositionBufferConverter{ *this };
	FGLTFColorBufferConverter ColorBufferConverter{ *this };
	FGLTFNormalBufferConverter NormalBufferConverter{ *this };
	FGLTFTangentBufferConverter TangentBufferConverter{ *this };
	FGLTFUVBufferConverter UVBufferConverter{ *this };
	FGLTFBoneIndexBufferConverter BoneIndexBufferConverter{ *this };
	FGLTFBoneWeightBufferConverter BoneWeightBufferConverter{ *this };
	FGLTFIndexBufferConverter IndexBufferConverter{ *this };

	FGLTFStaticMeshConverter StaticMeshConverter{ *this };
	FGLTFSkeletalMeshConverter SkeletalMeshConverter{ *this };

	FGLTFMaterialConverter MaterialConverter{ *this };
	FGLTFSamplerConverter SamplerConverter{ *this };
	FGLTFTexture2DConverter Texture2DConverter{ *this };
	FGLTFTextureCubeConverter TextureCubeConverter{ *this };
	FGLTFTextureRenderTarget2DConverter TextureRenderTarget2DConverter{ *this };
	FGLTFTextureRenderTargetCubeConverter TextureRenderTargetCubeConverter{ *this };

	FGLTFSkeletalSocketConverter SkeletalSocketConverter{ *this };
	FGLTFSkeletalBoneConverter SkeletalBoneConverter{ *this };
	FGLTFSkinConverter SkinConverter{ *this };

	FGLTFComponentConverter ComponentConverter{ *this };
	FGLTFActorConverter ActorConverter{ *this };
	FGLTFSceneConverter SceneConverter{ *this };

	FGLTFCameraConverter CameraConverter{ *this };
	FGLTFLightConverter LightConverter{ *this };
	FGLTFBackdropConverter BackdropConverter{ *this };
	FGLTFVariationConverter VariationConverter{ *this };
	FGLTFLightMapConverter LightMapConverter{ *this };
	FGLTFHotspotConverter HotspotConverter{ *this };
};
