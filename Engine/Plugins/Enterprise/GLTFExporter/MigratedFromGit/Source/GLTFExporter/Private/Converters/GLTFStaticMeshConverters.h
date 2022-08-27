// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"
#include "Converters/GLTFMaterialArray.h"
#include "Engine.h"

class FGLTFIndexBufferConverter : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonBufferViewIndex, const FRawStaticIndexBuffer*>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	FGLTFJsonBufferViewIndex Convert(const FRawStaticIndexBuffer* IndexBuffer) override final;
};

class FGLTFStaticMeshSectionConverter : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonAccessorIndex, const FStaticMeshSection*, const FRawStaticIndexBuffer*>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	FGLTFJsonAccessorIndex Convert(const FStaticMeshSection* MeshSection, const FRawStaticIndexBuffer* IndexBuffer) override final;
};

class FGLTFStaticMeshConverter : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonMeshIndex, const UStaticMesh*, int32, const FColorVertexBuffer*, FGLTFMaterialArray>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	FGLTFJsonMeshIndex Convert(const UStaticMesh* StaticMesh, int32 LODIndex, const FColorVertexBuffer* OverrideVertexColors, FGLTFMaterialArray OverrideMaterials) override final;
};
