// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonCore.h"

struct GLTFEXPORTER_API FGLTFJsonExtensions
{
	TSet<EGLTFJsonExtension> Used;
	TSet<EGLTFJsonExtension> Required;
};
