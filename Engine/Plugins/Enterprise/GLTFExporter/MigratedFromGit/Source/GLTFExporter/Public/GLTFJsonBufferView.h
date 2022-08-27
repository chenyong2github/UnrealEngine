// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GLTFJsonEnums.h"
#include "GLTFJsonObject.h"
#include "GLTFJsonUtilities.h"

struct GLTFEXPORTER_API FGLTFJsonBufferView : FGLTFJsonObject
{
	FString Name;

	FGLTFJsonIndex Buffer;

	int64 ByteLength;
	int64 ByteOffset;
	int32 ByteStride;

	EGLTFJsonBufferTarget Target;

	FGLTFJsonBufferView()
		: Buffer(INDEX_NONE)
		, ByteLength(0)
		, ByteOffset(0)
		, ByteStride(0)
		, Target(EGLTFJsonBufferTarget::None)
	{
		// check that view fits completely inside the buffer
	}

	template <class CharType = TCHAR, class PrintPolicy = TPrettyJsonPrintPolicy<CharType>>
	void Write(TJsonWriter<CharType, PrintPolicy>& JsonWriter) const
	{
		JsonWriter.WriteObjectStart();

		if (!Name.IsEmpty()) JsonWriter.WriteValue(TEXT("name"), Name);

		JsonWriter.WriteValue(TEXT("buffer"), Buffer);
		JsonWriter.WriteValue(TEXT("byteLength"), ByteLength);

		if (ByteOffset != 0) JsonWriter.WriteValue(TEXT("byteOffset"), ByteOffset);
		if (ByteStride != 0) JsonWriter.WriteValue(TEXT("byteStride"), ByteStride);

		if (Target != EGLTFJsonBufferTarget::None) JsonWriter.WriteValue(TEXT("target"), BufferTargetToNumber(Target));

		JsonWriter.WriteObjectEnd();
	}
};
