// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"
#include "Converters/GLTFMaterialArray.h"
#include "Engine.h"

class FGLTFMaterialConverter final : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonMaterialIndex, const UMaterialInterface*, const UObject*, int32, FGLTFMaterialArray>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual void Sanitize(const UMaterialInterface*& Material, const UObject*& MeshOrComponent, int32& LODIndex, FGLTFMaterialArray& OverrideMaterials) override;

	virtual FGLTFJsonMaterialIndex Convert(const UMaterialInterface* Material, const UObject* MeshOrComponent, int32 LODIndex, const FGLTFMaterialArray OverrideMaterials) override;
};
