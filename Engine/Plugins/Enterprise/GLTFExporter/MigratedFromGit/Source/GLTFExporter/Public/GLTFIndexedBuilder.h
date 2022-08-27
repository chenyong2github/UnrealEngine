// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GLTFBufferBuilder.h"
#include "GLTFIndexedObjects.h"
#include "GLTFStaticMeshConverters.h"
#include "GLTFLevelConverters.h"

struct GLTFEXPORTER_API FGLTFIndexedBuilder : public FGLTFBufferBuilder
{
	TGLTFIndexedObjects<FGLTFJsonAccessorIndex, TTuple<const FPositionVertexBuffer*>, FGLTFPositionVertexBufferConverter> PositionVertexBuffers;
	TGLTFIndexedObjects<FGLTFJsonAccessorIndex, TTuple<const FColorVertexBuffer*>, FGLTFColorVertexBufferConverter> ColorVertexBuffers;
	TGLTFIndexedObjects<FGLTFJsonAccessorIndex, TTuple<const FStaticMeshVertexBuffer*>, FGLTFStaticMeshNormalVertexBufferConverter> StaticMeshNormalVertexBuffers;
	TGLTFIndexedObjects<FGLTFJsonAccessorIndex, TTuple<const FStaticMeshVertexBuffer*>, FGLTFStaticMeshTangentVertexBufferConverter> StaticMeshTangentVertexBuffers;
	TGLTFIndexedObjects<FGLTFJsonAccessorIndex, TTuple<const FStaticMeshVertexBuffer*>, FGLTFStaticMeshUV0VertexBufferConverter> StaticMeshUV0VertexBuffers;
	TGLTFIndexedObjects<FGLTFJsonAccessorIndex, TTuple<const FStaticMeshVertexBuffer*>, FGLTFStaticMeshUV1VertexBufferConverter> StaticMeshUV1VertexBuffers;
	TGLTFIndexedObjects<FGLTFJsonBufferViewIndex, TTuple<const FRawStaticIndexBuffer*>, FGLTFStaticMeshIndexBufferConverter> StaticMeshIndexBuffers;
	TGLTFIndexedObjects<FGLTFJsonAccessorIndex, TTuple<const FStaticMeshSection*, const FRawStaticIndexBuffer*>, FGLTFStaticMeshSectionConverter> StaticMeshSections;
	TGLTFIndexedObjects<FGLTFJsonMeshIndex, TTuple<const FStaticMeshLODResources*, const FColorVertexBuffer*>, FGLTFStaticMeshConverter> StaticMeshes;

	TGLTFIndexedObjects<FGLTFJsonNodeIndex, TTuple<const USceneComponent*, bool, bool>, FGLTFSceneComponentConverter> SceneComponents;
	TGLTFIndexedObjects<FGLTFJsonSceneIndex, TTuple<const ULevel*, bool>, FGLTFLevelConverter> Levels;

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
