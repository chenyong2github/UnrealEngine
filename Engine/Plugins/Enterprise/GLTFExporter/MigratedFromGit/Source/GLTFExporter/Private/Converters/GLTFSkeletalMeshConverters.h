// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFMaterialArray.h"
#include "Engine.h"

class FMultiSizeIndexContainer;

class FGLTFIndexContainerConverter final : public TGLTFConverter<FGLTFJsonBufferViewIndex, const FMultiSizeIndexContainer*>
{
	using TGLTFConverter::TGLTFConverter;

	FGLTFJsonBufferViewIndex Convert(const FMultiSizeIndexContainer* IndexContainer) override;
};

class FGLTFSkeletalMeshSectionConverter final : public TGLTFConverter<FGLTFJsonAccessorIndex, const FSkelMeshRenderSection*, const FMultiSizeIndexContainer*>
{
	using TGLTFConverter::TGLTFConverter;

	FGLTFJsonAccessorIndex Convert(const FSkelMeshRenderSection* MeshSection, const FMultiSizeIndexContainer* IndexContainer) override;
};

class FGLTFSkeletalMeshConverter final : public TGLTFConverter<FGLTFJsonMeshIndex, const USkeletalMesh*, int32, const FColorVertexBuffer*, const FSkinWeightVertexBuffer*, FGLTFMaterialArray>
{
	using TGLTFConverter::TGLTFConverter;

	FGLTFJsonMeshIndex Convert(const USkeletalMesh* SkeletalMesh, int32 LODIndex, const FColorVertexBuffer* OverrideVertexColors, const FSkinWeightVertexBuffer* OverrideSkinWeights, FGLTFMaterialArray OverrideMaterials) override;
};
