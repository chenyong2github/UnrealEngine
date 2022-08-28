// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Builders/GLTFBufferBuilder.h"
#include "Converters/GLTFStaticMeshConverters.h"
#include "Converters/GLTFLevelConverters.h"

struct GLTFEXPORTER_API FGLTFConvertBuilder : public FGLTFBufferBuilder
{
	FGLTFPositionVertexBufferConverter PositionVertexBufferConverter;
	FGLTFColorVertexBufferConverter ColorVertexBufferConverter;
	FGLTFStaticMeshNormalVertexBufferConverter StaticMeshNormalVertexBufferConverter;
	FGLTFStaticMeshTangentVertexBufferConverter StaticMeshTangentVertexBufferConverter;
	FGLTFStaticMeshUV0VertexBufferConverter StaticMeshUV0VertexBufferConverter;
	FGLTFStaticMeshUV1VertexBufferConverter StaticMeshUV1VertexBufferConverter;
	FGLTFStaticMeshIndexBufferConverter StaticMeshIndexBufferConverter;
	FGLTFStaticMeshSectionConverter StaticMeshSectionConverter;
	FGLTFStaticMeshConverter StaticMeshConverter;

	FGLTFSceneComponentConverter SceneComponentConverter;
	FGLTFLevelConverter LevelConverter;

	FGLTFJsonAccessorIndex GetOrAddPositionAccessor(const FPositionVertexBuffer* VertexBuffer, const FString& DesiredName = TEXT(""));
	FGLTFJsonAccessorIndex GetOrAddNormalAccessor(const FStaticMeshVertexBuffer* VertexBuffer, const FString& DesiredName = TEXT(""));
	FGLTFJsonAccessorIndex GetOrAddTangentAccessor(const FStaticMeshVertexBuffer* VertexBuffer, const FString& DesiredName = TEXT(""));
	FGLTFJsonAccessorIndex GetOrAddUV0Accessor(const FStaticMeshVertexBuffer* VertexBuffer, const FString& DesiredName = TEXT(""));
	FGLTFJsonAccessorIndex GetOrAddUV1Accessor(const FStaticMeshVertexBuffer* VertexBuffer, const FString& DesiredName = TEXT(""));
	FGLTFJsonAccessorIndex GetOrAddColorAccessor(const FColorVertexBuffer* VertexBuffer, const FString& DesiredName = TEXT(""));

	FGLTFJsonBufferViewIndex GetOrAddIndexBufferView(const FRawStaticIndexBuffer* IndexBuffer, const FString& DesiredName = TEXT(""));
	FGLTFJsonAccessorIndex GetOrAddIndexAccessor(const FStaticMeshSection* MeshSection, const FRawStaticIndexBuffer* IndexBuffer, const FString& DesiredName = TEXT(""));

	FGLTFJsonMeshIndex GetOrAddMesh(const FStaticMeshLODResources* StaticMeshLOD, const FColorVertexBuffer* OverrideVertexColors = nullptr, const FString& DesiredName = TEXT(""));
	FGLTFJsonMeshIndex GetOrAddMesh(const UStaticMesh* StaticMesh, int32 LODIndex = 0, const FColorVertexBuffer* OverrideVertexColors = nullptr, const FString& DesiredName = TEXT(""));
	FGLTFJsonMeshIndex GetOrAddMesh(const UStaticMeshComponent* StaticMeshComponent, const FString& DesiredName = TEXT(""));

	FGLTFJsonNodeIndex GetOrAddNode(const USceneComponent* SceneComponent, bool bSelectedOnly, bool bRootNode = false, const FString& DesiredName = TEXT(""));
	FGLTFJsonSceneIndex GetOrAddScene(const ULevel* Level, bool bSelectedOnly, const FString& DesiredName = TEXT(""));
	FGLTFJsonSceneIndex GetOrAddScene(const UWorld* World, bool bSelectedOnly, const FString& DesiredName = TEXT(""));
};
