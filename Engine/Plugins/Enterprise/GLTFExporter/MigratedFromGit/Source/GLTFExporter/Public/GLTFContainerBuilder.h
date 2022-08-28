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

	FGLTFJsonAccessorIndex AddAccessor(const FGLTFJsonAccessor& JsonAccessor);
	FGLTFJsonBufferIndex AddBuffer(const FGLTFJsonBuffer& JsonBuffer);
	FGLTFJsonBufferViewIndex AddBufferView(const FGLTFJsonBufferView& JsonBufferView);
	FGLTFJsonMeshIndex AddMesh(const FGLTFJsonMesh& JsonMesh);
	FGLTFJsonNodeIndex AddNode(const FGLTFJsonNode& JsonNode);
	FGLTFJsonSceneIndex AddScene(const FGLTFJsonScene& JsonScene);

	FGLTFJsonBufferViewIndex AddBufferView(const void* RawData, uint64 ByteLength, const FString& Name = TEXT(""), EGLTFJsonBufferTarget BufferTarget = EGLTFJsonBufferTarget::ArrayBuffer);

	template <class ElementType>
	FGLTFJsonBufferViewIndex AddBufferView(const TArray<ElementType>& Array, const FString& Name = TEXT(""), EGLTFJsonBufferTarget BufferTarget = EGLTFJsonBufferTarget::ArrayBuffer)
	{
		return AddBufferView(Array.GetData(), Array.Num() * sizeof(ElementType), Name, BufferTarget);
	}

	FGLTFJsonMeshIndex AddMesh(const UStaticMesh* StaticMesh, int32 LODIndex = 0, const FColorVertexBuffer* OverrideVertexColors = nullptr);
	FGLTFJsonMeshIndex AddMesh(const UStaticMeshComponent* StaticMeshComponent);

	FGLTFJsonSceneIndex AddScene(const UWorld* World, bool bSelectedOnly = false);

	void Serialize(FArchive& Archive);
};
