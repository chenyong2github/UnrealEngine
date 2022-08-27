// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GLTFJsonRoot.h"
#include "Engine.h"

struct GLTFEXPORTER_API FGLTFBuilder
{
	FGLTFJsonRoot JsonRoot;

	const FGLTFJsonBufferIndex MergedBufferIndex;
	TArray<uint8> MergedBufferData;

	FGLTFBuilder();

	template <class ElementType>
	FGLTFJsonBufferViewIndex AppendBufferView(const TArray<ElementType>& Array, const FString& Name = TEXT(""), EGLTFJsonBufferTarget BufferTarget = EGLTFJsonBufferTarget::ArrayBuffer)
	{
		return AppendBufferView(Array.GetData(), Array.Num() * sizeof(ElementType), Name, BufferTarget);
	}

	FGLTFJsonBufferViewIndex AppendBufferView(const void* RawData, uint64 ByteLength, const FString& Name = TEXT(""), EGLTFJsonBufferTarget BufferTarget = EGLTFJsonBufferTarget::ArrayBuffer);

	FGLTFJsonMeshIndex AppendMesh(const UStaticMesh* StaticMesh, int32 LODIndex);

	void UpdateMergedBuffer();
	void Serialize(FArchive& Archive);
};
