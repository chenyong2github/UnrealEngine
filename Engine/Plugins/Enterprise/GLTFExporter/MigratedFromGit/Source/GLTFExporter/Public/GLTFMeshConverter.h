// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GLTFJsonRoot.h"
#include "GLTFContainerBuilder.h"
#include "Engine.h"

struct GLTFEXPORTER_API FGLTFMeshConverter
{
	static FGLTFJsonAccessorIndex ConvertPositionAccessor(FGLTFContainerBuilder& Container, const FPositionVertexBuffer* VertexBuffer, const FString& Name);
	static FGLTFJsonAccessorIndex ConvertNormalAccessor(FGLTFContainerBuilder& Container, const FStaticMeshVertexBuffer* VertexBuffer, const FString& Name);
	static FGLTFJsonAccessorIndex ConvertTangentAccessor(FGLTFContainerBuilder& Container, const FStaticMeshVertexBuffer* VertexBuffer, const FString& Name);
	static FGLTFJsonAccessorIndex ConvertUV0Accessor(FGLTFContainerBuilder& Container, const FStaticMeshVertexBuffer* VertexBuffer, const FString& Name);
	static FGLTFJsonAccessorIndex ConvertUV1Accessor(FGLTFContainerBuilder& Container, const FStaticMeshVertexBuffer* VertexBuffer, const FString& Name);
	static FGLTFJsonAccessorIndex ConvertColorAccessor(FGLTFContainerBuilder& Container, const FColorVertexBuffer* VertexBuffer, const FString& Name);

	static FGLTFJsonBufferViewIndex ConvertIndexBufferView(FGLTFContainerBuilder& Container, const FRawStaticIndexBuffer* IndexBuffer, const FString& Name);
	static FGLTFJsonAccessorIndex ConvertIndexAccessor(FGLTFContainerBuilder& Container, const FStaticMeshSection* MeshSection, const FRawStaticIndexBuffer* IndexBuffer, const FString& Name);

	static FGLTFJsonMeshIndex ConvertMesh(FGLTFContainerBuilder& Container, const UStaticMesh* StaticMesh, int32 LODIndex, const FColorVertexBuffer* OverrideVertexColors);
};
