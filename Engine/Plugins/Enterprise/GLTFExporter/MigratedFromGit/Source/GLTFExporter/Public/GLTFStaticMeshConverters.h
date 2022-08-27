// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GLTFJsonIndex.h"
#include "Engine.h"

struct FGLTFIndexedBuilder;

struct GLTFEXPORTER_API FGLTFPositionVertexBufferConverter
{
	static FGLTFJsonAccessorIndex Add(FGLTFIndexedBuilder& Builder, const FString& Name, const FPositionVertexBuffer* VertexBuffer);
};

struct GLTFEXPORTER_API FGLTFColorVertexBufferConverter
{
	static FGLTFJsonAccessorIndex Add(FGLTFIndexedBuilder& Builder, const FString& Name, const FColorVertexBuffer* VertexBuffer);
};

struct GLTFEXPORTER_API FGLTFStaticMeshNormalVertexBufferConverter
{
	static FGLTFJsonAccessorIndex Add(FGLTFIndexedBuilder& Builder, const FString& Name, const FStaticMeshVertexBuffer* VertexBuffer);
};

struct GLTFEXPORTER_API FGLTFStaticMeshTangentVertexBufferConverter
{
	static FGLTFJsonAccessorIndex Add(FGLTFIndexedBuilder& Builder, const FString& Name, const FStaticMeshVertexBuffer* VertexBuffer);
};

struct GLTFEXPORTER_API FGLTFStaticMeshUV0VertexBufferConverter
{
	static FGLTFJsonAccessorIndex Add(FGLTFIndexedBuilder& Builder, const FString& Name, const FStaticMeshVertexBuffer* VertexBuffer);
};

struct GLTFEXPORTER_API FGLTFStaticMeshUV1VertexBufferConverter
{
	static FGLTFJsonAccessorIndex Add(FGLTFIndexedBuilder& Builder, const FString& Name, const FStaticMeshVertexBuffer* VertexBuffer);
};

struct GLTFEXPORTER_API FGLTFStaticMeshIndexBufferConverter
{
	static FGLTFJsonBufferViewIndex Add(FGLTFIndexedBuilder& Builder, const FString& Name, const FRawStaticIndexBuffer* IndexBuffer);
};

struct GLTFEXPORTER_API FGLTFStaticMeshSectionConverter
{
	static FGLTFJsonAccessorIndex Add(FGLTFIndexedBuilder& Builder, const FString& Name, const FStaticMeshSection* MeshSection, const FRawStaticIndexBuffer* IndexBuffer);
};

struct GLTFEXPORTER_API FGLTFStaticMeshConverter
{
	static FGLTFJsonMeshIndex Add(FGLTFIndexedBuilder& Builder, const FString& Name, const FStaticMeshLODResources* StaticMeshLOD, const FColorVertexBuffer* OverrideVertexColors);
};
