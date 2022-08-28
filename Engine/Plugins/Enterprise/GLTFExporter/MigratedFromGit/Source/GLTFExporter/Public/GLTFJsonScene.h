// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GLTFJsonObject.h"

struct GLTFEXPORTER_API FGLTFJsonScene: FGLTFJsonObject
{
	FString Name;

	TArray<FGLTFJsonIndex> Nodes;
};
