// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GLTFJsonObject.h"

struct GLTFEXPORTER_API FGLTFJsonNode : FGLTFJsonObject
{
	FString Name;

	FVector Translation;
	FQuat   Rotation;
	FVector Scale;

	FGLTFJsonIndex Camera;
	FGLTFJsonIndex Skin;
	FGLTFJsonIndex Mesh;

	TArray<FGLTFJsonIndex> Children;

	FGLTFJsonNode()
		: Translation(FVector::ZeroVector)
		, Rotation(FQuat::Identity)
		, Scale(FVector::OneVector)
		, Camera(INDEX_NONE)
		, Skin(INDEX_NONE)
		, Mesh(INDEX_NONE)
	{
	}
};
