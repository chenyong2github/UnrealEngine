// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GLTFJsonBuilder.h"

struct GLTFEXPORTER_API FGLTFBufferBuilder : public FGLTFJsonBuilder
{
	FGLTFJsonBufferIndex BufferIndex;
	TArray<uint8> BufferData;

	FGLTFBufferBuilder();

	FGLTFJsonBufferViewIndex AddBufferView(const void* RawData, uint64 ByteLength, const FString& Name = TEXT(""), EGLTFJsonBufferTarget BufferTarget = EGLTFJsonBufferTarget::ArrayBuffer);

	template <class ElementType>
	FGLTFJsonBufferViewIndex AddBufferView(const TArray<ElementType>& Array, const FString& Name = TEXT(""), EGLTFJsonBufferTarget BufferTarget = EGLTFJsonBufferTarget::ArrayBuffer)
	{
		return AddBufferView(Array.GetData(), Array.Num() * sizeof(ElementType), Name, BufferTarget);
	}

	void UpdateJsonBufferObject(const FString& BinaryFilePath);
	virtual bool Serialize(FArchive& Archive, const FString& FilePath) override;
};
