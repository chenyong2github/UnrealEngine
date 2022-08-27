// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GLTFJsonRoot.h"
#include "GLTFBufferBuilder.h"
#include "GLTFIndexBuilder.h"
#include "Engine.h"

struct GLTFEXPORTER_API FGLTFContainerBuilder
{
	FGLTFJsonRoot JsonRoot;
	FGLTFBufferBuilder BufferBuilder;
	FGLTFIndexBuilder IndexBuilder;

	FGLTFContainerBuilder();

	FGLTFJsonAccessorIndex CreateAccessor(const FGLTFJsonAccessor& JsonAccessor);
	FGLTFJsonBufferIndex CreateBuffer(const FGLTFJsonBuffer& JsonBuffer);
	FGLTFJsonBufferViewIndex CreateBufferView(const FGLTFJsonBufferView& JsonBufferView);
	FGLTFJsonMeshIndex CreateMesh(const FGLTFJsonMesh& JsonMesh);
	FGLTFJsonNodeIndex CreateNode(const FGLTFJsonNode& JsonNode);
	FGLTFJsonSceneIndex CreateScene(const FGLTFJsonScene& JsonScene);

	FGLTFJsonBufferViewIndex CreateBufferView(const void* RawData, uint64 ByteLength, const FString& Name = TEXT(""), EGLTFJsonBufferTarget BufferTarget = EGLTFJsonBufferTarget::ArrayBuffer);

	template <class ElementType>
	FGLTFJsonBufferViewIndex CreateBufferView(const TArray<ElementType>& Array, const FString& Name = TEXT(""), EGLTFJsonBufferTarget BufferTarget = EGLTFJsonBufferTarget::ArrayBuffer)
	{
		return CreateBufferView(Array.GetData(), Array.Num() * sizeof(ElementType), Name, BufferTarget);
	}

	FGLTFJsonAccessorIndex AddPositionAccessor(const FPositionVertexBuffer* VertexBuffer, const FString& Name = TEXT(""));
	FGLTFJsonAccessorIndex AddNormalAccessor(const FStaticMeshVertexBuffer* VertexBuffer, const FString& Name = TEXT(""));
	FGLTFJsonAccessorIndex AddTangentAccessor(const FStaticMeshVertexBuffer* VertexBuffer, const FString& Name = TEXT(""));
	FGLTFJsonAccessorIndex AddUV0Accessor(const FStaticMeshVertexBuffer* VertexBuffer, const FString& Name = TEXT(""));
	FGLTFJsonAccessorIndex AddUV1Accessor(const FStaticMeshVertexBuffer* VertexBuffer, const FString& Name = TEXT(""));
	FGLTFJsonAccessorIndex AddColorAccessor(const FColorVertexBuffer* VertexBuffer, const FString& Name = TEXT(""));

	FGLTFJsonBufferViewIndex AddIndexBufferView(const FRawStaticIndexBuffer* IndexBuffer, const FString& Name = TEXT(""));
	FGLTFJsonAccessorIndex AddIndexAccessor(const FStaticMeshSection* MeshSection, const FRawStaticIndexBuffer* IndexBuffer, const FString& Name = TEXT(""));

	FGLTFJsonMeshIndex AddMesh(const UStaticMesh* StaticMesh, int32 LODIndex = 0, const FColorVertexBuffer* OverrideVertexColors = nullptr);
	FGLTFJsonMeshIndex AddMesh(const UStaticMeshComponent* StaticMeshComponent);

	FGLTFJsonSceneIndex AddScene(const UWorld* World, bool bSelectedOnly = false);

	void Serialize(FArchive& Archive);
};
