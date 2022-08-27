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

	FGLTFJsonAccessorIndex ConvertPositionAccessor(const FPositionVertexBuffer* VertexBuffer, const FString& DesiredName = TEXT(""));
	FGLTFJsonAccessorIndex ConvertNormalAccessor(const FStaticMeshVertexBuffer* VertexBuffer, const FString& DesiredName = TEXT(""));
	FGLTFJsonAccessorIndex ConvertTangentAccessor(const FStaticMeshVertexBuffer* VertexBuffer, const FString& DesiredName = TEXT(""));
	FGLTFJsonAccessorIndex ConvertUV0Accessor(const FStaticMeshVertexBuffer* VertexBuffer, const FString& DesiredName = TEXT(""));
	FGLTFJsonAccessorIndex ConvertUV1Accessor(const FStaticMeshVertexBuffer* VertexBuffer, const FString& DesiredName = TEXT(""));
	FGLTFJsonAccessorIndex ConvertColorAccessor(const FColorVertexBuffer* VertexBuffer, const FString& DesiredName = TEXT(""));

	FGLTFJsonBufferViewIndex ConvertIndexBufferView(const FRawStaticIndexBuffer* IndexBuffer, const FString& DesiredName = TEXT(""));
	FGLTFJsonAccessorIndex ConvertIndexAccessor(const FStaticMeshSection* MeshSection, const FRawStaticIndexBuffer* IndexBuffer, const FString& DesiredName = TEXT(""));

	FGLTFJsonMeshIndex ConvertMesh(const FStaticMeshLODResources* StaticMeshLOD, const FColorVertexBuffer* OverrideVertexColors = nullptr, const FString& DesiredName = TEXT(""));
	FGLTFJsonMeshIndex ConvertMesh(const UStaticMesh* StaticMesh, int32 LODIndex = 0, const FColorVertexBuffer* OverrideVertexColors = nullptr, const FString& DesiredName = TEXT(""));
	FGLTFJsonMeshIndex ConvertMesh(const UStaticMeshComponent* StaticMeshComponent, const FString& DesiredName = TEXT(""));

	FGLTFJsonNodeIndex ConvertNode(const USceneComponent* SceneComponent, bool bSelectedOnly, bool bRootNode = false, const FString& DesiredName = TEXT(""));
	FGLTFJsonSceneIndex ConvertScene(const ULevel* Level, bool bSelectedOnly, const FString& DesiredName = TEXT(""));
	FGLTFJsonSceneIndex ConvertScene(const UWorld* World, bool bSelectedOnly, const FString& DesiredName = TEXT(""));
};
