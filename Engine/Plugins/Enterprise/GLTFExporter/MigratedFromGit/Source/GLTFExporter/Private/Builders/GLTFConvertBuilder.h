// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Builders/GLTFImageBuilder.h"
#include "Converters/GLTFVertexBufferConverters.h"
#include "Converters/GLTFStaticMeshConverters.h"
#include "Converters/GLTFMaterialConverters.h"
#include "Converters/GLTFTextureConverters.h"
#include "Converters/GLTFLevelConverters.h"
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

	FGLTFJsonBufferViewIndex GetOrAddIndexBufferView(const FRawStaticIndexBuffer* IndexBuffer, const FString& DesiredName = TEXT(""));
	FGLTFJsonAccessorIndex GetOrAddIndexAccessor(const FStaticMeshSection* MeshSection, const FRawStaticIndexBuffer* IndexBuffer, const FString& DesiredName = TEXT(""));
	FGLTFJsonMeshIndex GetOrAddMesh(const UStaticMesh* StaticMesh, int32 LODIndex = 0, const FColorVertexBuffer* OverrideVertexColors = nullptr, const FGLTFMaterialArray& OverrideMaterials = {}, const FString& DesiredName = TEXT(""));
	FGLTFJsonMeshIndex GetOrAddMesh(const UStaticMeshComponent* StaticMeshComponent, const FString& DesiredName = TEXT(""));

	FGLTFJsonMaterialIndex GetOrAddMaterial(const UMaterialInterface* Material, const FString& DesiredName = TEXT(""));
	FGLTFJsonTextureIndex GetOrAddTexture(const UTexture2D* Texture, const FString& DesiredName = TEXT(""));
	FGLTFJsonTextureIndex GetOrAddTexture(const ULightMapTexture2D* LightMapTexture2D, const FString& DesiredName = TEXT(""));
	FGLTFJsonLightMapIndex GetOrAddLightMap(const UStaticMeshComponent* StaticMeshComponent, const FString& DesiredName = TEXT(""));

	FGLTFJsonNodeIndex GetOrAddNode(const USceneComponent* SceneComponent, const FString& DesiredName = TEXT(""));
	FGLTFJsonNodeIndex GetOrAddNode(const AActor* Actor, const FString& DesiredName = TEXT(""));
	FGLTFJsonSceneIndex GetOrAddScene(const ULevel* Level, const FString& DesiredName = TEXT(""));
	FGLTFJsonSceneIndex GetOrAddScene(const UWorld* World, const FString& DesiredName = TEXT(""));

private:

	FGLTFPositionVertexBufferConverter PositionVertexBufferConverter;
	FGLTFColorVertexBufferConverter ColorVertexBufferConverter;
	FGLTFNormalVertexBufferConverter NormalVertexBufferConverter;
	FGLTFTangentVertexBufferConverter TangentVertexBufferConverter;
	FGLTFUVVertexBufferConverter UVVertexBufferConverter;

	FGLTFIndexBufferConverter IndexBufferConverter;
	FGLTFStaticMeshSectionConverter StaticMeshSectionConverter;
	FGLTFStaticMeshConverter StaticMeshConverter;

	FGLTFMaterialConverter MaterialConverter;
	FGLTFTexture2DConverter Texture2DConverter;
	FGLTFLightMapTexture2DConverter LightMapTexture2DConverter;
	FGLTFLightMapConverter LightMapConverter;

	FGLTFSceneComponentConverter SceneComponentConverter;
	FGLTFActorConverter ActorConverter;
	FGLTFLevelConverter LevelConverter;
};
