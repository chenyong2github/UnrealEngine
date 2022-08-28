// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonObject.h"
#include "Json/GLTFJsonEnums.h"
#include "Json/GLTFJsonIndex.h"

struct GLTFEXPORTER_API FGLTFJsonAccessor : IGLTFJsonObject
{
	FString Name;

	FGLTFJsonBufferViewIndex BufferView;
	int64                    ByteOffset;
	int32                    Count;
	EGLTFJsonAccessorType    Type;
	EGLTFJsonComponentType   ComponentType;
	bool                     bNormalized;

	int32 MinMaxLength;
	float Min[16];
	float Max[16];

	FGLTFJsonAccessor()
		: ByteOffset(0)
		, Count(0)
		, Type(EGLTFJsonAccessorType::None)
		, ComponentType(EGLTFJsonComponentType::None)
		, bNormalized(false)
		, MinMaxLength(0)
		, Min{0}
		, Max{0}
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};
