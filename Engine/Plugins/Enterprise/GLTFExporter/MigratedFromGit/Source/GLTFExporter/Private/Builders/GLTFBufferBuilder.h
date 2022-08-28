// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Builders/GLTFJsonBuilder.h"

class FGLTFBufferBuilder : public FGLTFJsonBuilder
{
protected:

	FGLTFBufferBuilder();

public:

	FGLTFJsonBufferViewIndex AddBufferView(const void* RawData, uint64 ByteLength, uint8 DataAlignment = 4, EGLTFJsonBufferTarget BufferTarget = EGLTFJsonBufferTarget::ArrayBuffer);

	template <class ElementType>
	FGLTFJsonBufferViewIndex AddBufferView(const TArray<ElementType>& Array, uint8 DataAlignment = 4, EGLTFJsonBufferTarget BufferTarget = EGLTFJsonBufferTarget::ArrayBuffer)
	{
		return AddBufferView(Array.GetData(), Array.Num() * sizeof(ElementType), DataAlignment, BufferTarget);
	}

	virtual bool Serialize(FArchive& Archive, const FString& FilePath) override;

private:

	void UpdateJsonBufferObject(const FString& BinaryFilePath);

	FGLTFJsonBufferIndex BufferIndex;
	TArray64<uint8> BufferData;
};
