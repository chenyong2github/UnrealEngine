// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GLTFJsonRoot.h"
#include "CoreMinimal.h"

struct GLTFEXPORTER_API FGLTFBufferBuilder
{
	FGLTFJsonRoot& JsonRoot;
	const FGLTFJsonIndex BufferIndex;

	TArray<uint8> Data;

	FGLTFBufferBuilder(FGLTFJsonRoot& JsonRoot, const FString& Name = TEXT(""));

	template <class ElementType>
	FGLTFJsonIndex AppendBufferView(const TArray<ElementType>& Array, const FString& Name = TEXT(""), EGLTFJsonBufferTarget BufferTarget = EGLTFJsonBufferTarget::ArrayBuffer)
	{
		return AppendBufferView(Array.GetData(), Array.Num() * sizeof(ElementType), Name, BufferTarget);
	}

	FGLTFJsonIndex AppendBufferView(const void* RawData, uint64 ByteLength, const FString& Name = TEXT(""), EGLTFJsonBufferTarget BufferTarget = EGLTFJsonBufferTarget::ArrayBuffer);

	void Close();
};
