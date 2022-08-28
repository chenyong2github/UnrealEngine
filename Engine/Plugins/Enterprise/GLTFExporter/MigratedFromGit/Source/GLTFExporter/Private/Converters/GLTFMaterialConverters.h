// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonCore.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"
#include "Converters/GLTFMeshData.h"
#include "Converters/GLTFIndexArray.h"
#include "Converters/GLTFUVOverlapChecker.h"
#include "Converters/GLTFUVDegenerateChecker.h"

typedef TGLTFConverter<FGLTFJsonMaterial*, const UMaterialInterface*, const FGLTFMeshData*, FGLTFIndexArray> IGLTFMaterialConverter;

class FGLTFMaterialConverter final : public FGLTFBuilderContext, public IGLTFMaterialConverter
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual void Sanitize(const UMaterialInterface*& Material, const FGLTFMeshData*& MeshData, FGLTFIndexArray& SectionIndices) override;

	virtual FGLTFJsonMaterial* Convert(const UMaterialInterface* Material, const FGLTFMeshData* MeshData, FGLTFIndexArray SectionIndices) override;

	FGLTFUVOverlapChecker UVOverlapChecker;
	FGLTFUVDegenerateChecker UVDegenerateChecker;
};
