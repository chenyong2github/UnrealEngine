// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GLTFJsonEnums.h"
#include "GLTFJsonObject.h"

struct GLTFEXPORTER_API FGLTFJsonAccessor : FGLTFJsonObject
{
	FString Name;

	FGLTFJsonIndex         BufferView;
	int32                  Count;
	EGLTFJsonAccessorType  Type;
	EGLTFJsonComponentType ComponentType;
	bool                   Normalized;

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
	{
	}
};
