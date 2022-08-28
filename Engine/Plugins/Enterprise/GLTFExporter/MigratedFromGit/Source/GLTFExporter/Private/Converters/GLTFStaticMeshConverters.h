// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFMaterialArray.h"
#include "Engine.h"

class FGLTFIndexBufferConverter final : public TGLTFConverter<FGLTFJsonBufferViewIndex, const FRawStaticIndexBuffer*>
{
	using TGLTFConverter::TGLTFConverter;

	FGLTFJsonBufferViewIndex Convert(const FString& Name, const FRawStaticIndexBuffer* IndexBuffer) override;
};

class FGLTFStaticMeshSectionConverter final : public TGLTFConverter<FGLTFJsonAccessorIndex, const FStaticMeshSection*, const FRawStaticIndexBuffer*>
{
	using TGLTFConverter::TGLTFConverter;

	FGLTFJsonAccessorIndex Convert(const FString& Name, const FStaticMeshSection* MeshSection, const FRawStaticIndexBuffer* IndexBuffer) override;
};

class FGLTFStaticMeshConverter final : public TGLTFConverter<FGLTFJsonMeshIndex, const UStaticMesh*, int32, const FColorVertexBuffer*, FGLTFMaterialArray>
{
	using TGLTFConverter::TGLTFConverter;

	FGLTFJsonMeshIndex Convert(const FString& Name, const UStaticMesh* StaticMesh, int32 LODIndex, const FColorVertexBuffer* OverrideVertexColors, FGLTFMaterialArray OverrideMaterials) override;
};
