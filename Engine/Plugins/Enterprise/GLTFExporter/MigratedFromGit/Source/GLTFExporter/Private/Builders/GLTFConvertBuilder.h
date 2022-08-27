// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Builders/GLTFBufferBuilder.h"
#include "Converters/GLTFVertexBufferConverters.h"
#include "Converters/GLTFStaticMeshConverters.h"
#include "Converters/GLTFMaterialConverters.h"
#include "Converters/GLTFLevelConverters.h"

class FGLTFConvertBuilder : public FGLTFBufferBuilder
{
public:

	FGLTFJsonAccessorIndex GetOrAddPositionAccessor(const FPositionVertexBuffer* VertexBuffer, const FString& DesiredName = TEXT(""));
	FGLTFJsonAccessorIndex GetOrAddColorAccessor(const FColorVertexBuffer* VertexBuffer, const FString& DesiredName = TEXT(""));
	FGLTFJsonAccessorIndex GetOrAddNormalAccessor(const FStaticMeshVertexBuffer* VertexBuffer, const FString& DesiredName = TEXT(""));
	FGLTFJsonAccessorIndex GetOrAddTangentAccessor(const FStaticMeshVertexBuffer* VertexBuffer, const FString& DesiredName = TEXT(""));
	FGLTFJsonAccessorIndex GetOrAddUVAccessor(const FStaticMeshVertexBuffer* VertexBuffer, int32 UVIndex, const FString& DesiredName = TEXT(""));

	FGLTFJsonBufferViewIndex GetOrAddIndexBufferView(const FRawStaticIndexBuffer* IndexBuffer, const FString& DesiredName = TEXT(""));
	FGLTFJsonAccessorIndex GetOrAddIndexAccessor(const FStaticMeshSection* MeshSection, const FRawStaticIndexBuffer* IndexBuffer, const FString& DesiredName = TEXT(""));

	FGLTFJsonMaterialIndex GetOrAddMaterial(const UMaterialInterface* Material, const FString& DesiredName = TEXT(""));

	FGLTFJsonMeshIndex GetOrAddMesh(const UStaticMesh* StaticMesh, int32 LODIndex = 0, const FColorVertexBuffer* OverrideVertexColors = nullptr, TArray<const UMaterialInterface*> OverrideMaterials = {}, const FString& DesiredName = TEXT(""));
	FGLTFJsonMeshIndex GetOrAddMesh(const UStaticMeshComponent* StaticMeshComponent, const FString& DesiredName = TEXT(""));

	FGLTFJsonNodeIndex GetOrAddNode(const USceneComponent* SceneComponent, bool bSelectedOnly, bool bRootNode = false, const FString& DesiredName = TEXT(""));
	FGLTFJsonSceneIndex GetOrAddScene(const ULevel* Level, bool bSelectedOnly, const FString& DesiredName = TEXT(""));
	FGLTFJsonSceneIndex GetOrAddScene(const UWorld* World, bool bSelectedOnly, const FString& DesiredName = TEXT(""));

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

	FGLTFSceneComponentConverter SceneComponentConverter;
	FGLTFLevelConverter LevelConverter;
};
