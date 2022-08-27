// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"
#include "Engine.h"

class FGLTFMaterialConverter final : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonMaterialIndex, const UMaterialInterface*, const UStaticMesh*, int32>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual void Sanitize(const UMaterialInterface*& Material, const UStaticMesh*& Mesh, int32& LODIndex) override;

	virtual FGLTFJsonMaterialIndex Convert(const UMaterialInterface* Material, const UStaticMesh* Mesh, int32 LODIndex) override;

	static bool MaterialNeedsVertexData(UMaterialInterface* Material);
};
