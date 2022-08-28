// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GLTFJsonEnums.h"
#include "GLTFJsonIndex.h"
#include "GLTFJsonUtilities.h"
#include "Serialization/JsonSerializer.h"

struct GLTFEXPORTER_API FGLTFJsonAccessor
{
	FString Name;

	FGLTFJsonBufferViewIndex BufferView;
	int32                    Count;
	EGLTFJsonAccessorType    Type;
	EGLTFJsonComponentType   ComponentType;
	bool                     Normalized;

	int32 MinMaxLength;
	float Min[16];
	float Max[16];

	FGLTFJsonAccessor()
		: BufferView(INDEX_NONE)
		, Count(0)
		, Type(EGLTFJsonAccessorType::None)
		, ComponentType(EGLTFJsonComponentType::None)
		, Normalized(false)
		, MinMaxLength(0)
		, Min{0}
		, Max{0}
	{
	}

	template <class CharType = TCHAR, class PrintPolicy = TPrettyJsonPrintPolicy<CharType>>
	void WriteObject(TJsonWriter<CharType, PrintPolicy>& JsonWriter) const
	{
		JsonWriter.WriteObjectStart();

		if (!Name.IsEmpty()) JsonWriter.WriteValue(TEXT("name"), Name);

		JsonWriter.WriteValue(TEXT("bufferView"), BufferView);
		JsonWriter.WriteValue(TEXT("count"), Count);
		JsonWriter.WriteValue(TEXT("type"), AccessorTypeToString(Type));
		JsonWriter.WriteValue(TEXT("componentType"), ComponentTypeToNumber(ComponentType));

		if (MinMaxLength > 0)
		{
			JsonWriter.WriteArrayStart(TEXT("min"));
			for (int32 ComponentIndex = 0; ComponentIndex < MinMaxLength; ++ComponentIndex)
			{
				JsonWriter.WriteValue(Min[ComponentIndex]);
			}
			JsonWriter.WriteArrayEnd();

			JsonWriter.WriteArrayStart(TEXT("max"));
			for (int32 ComponentIndex = 0; ComponentIndex < MinMaxLength; ++ComponentIndex)
			{
				JsonWriter.WriteValue(Max[ComponentIndex]);
			}
			JsonWriter.WriteArrayEnd();
		}

		JsonWriter.WriteObjectEnd();
	}
};
