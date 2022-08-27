// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Builders/GLTFJsonBuilder.h"

class FGLTFBufferBuilder : public FGLTFJsonBuilder
{
protected:

	FGLTFBufferBuilder();

	bool Serialize(FArchive& Archive, const FString& FilePath);

public:

	FGLTFJsonBufferViewIndex AddBufferView(const void* RawData, uint64 ByteLength, EGLTFJsonBufferTarget BufferTarget = EGLTFJsonBufferTarget::None, uint8 DataAlignment = 4);

	template <class ElementType>
	FGLTFJsonBufferViewIndex AddBufferView(const TArray<ElementType>& Array, EGLTFJsonBufferTarget BufferTarget = EGLTFJsonBufferTarget::None, uint8 DataAlignment = 4)
	{
		return AddBufferView(Array.GetData(), Array.Num() * sizeof(ElementType), BufferTarget, DataAlignment);
	}

private:

	void UpdateJsonBufferObject(const FString& BinaryFilePath);

	FGLTFJsonBufferIndex BufferIndex;
	TArray64<uint8> BufferData;
};
