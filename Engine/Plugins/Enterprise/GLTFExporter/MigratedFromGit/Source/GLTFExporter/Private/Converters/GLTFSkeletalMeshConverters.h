// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"
#include "Converters/GLTFMaterialArray.h"
#include "Engine.h"

class FMultiSizeIndexContainer;

class FGLTFIndexContainerConverter : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonBufferViewIndex, const FMultiSizeIndexContainer*>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	FGLTFJsonBufferViewIndex Convert(const FMultiSizeIndexContainer* IndexContainer) override final;
};

class FGLTFSkeletalMeshSectionConverter : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonAccessorIndex, const FSkelMeshRenderSection*, const FMultiSizeIndexContainer*>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	FGLTFJsonAccessorIndex Convert(const FSkelMeshRenderSection* MeshSection, const FMultiSizeIndexContainer* IndexContainer) override final;
};

class FGLTFSkeletalMeshConverter : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonMeshIndex, const USkeletalMesh*, int32, const FColorVertexBuffer*, const FSkinWeightVertexBuffer*, FGLTFMaterialArray>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	FGLTFJsonMeshIndex Convert(const USkeletalMesh* SkeletalMesh, int32 LODIndex, const FColorVertexBuffer* OverrideVertexColors, const FSkinWeightVertexBuffer* OverrideSkinWeights, FGLTFMaterialArray OverrideMaterials) override final;
};
