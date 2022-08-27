// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"
#include "Converters/GLTFBoneMap.h"
#include "Engine.h"

class FMultiSizeIndexContainer;

class FGLTFPositionVertexBufferConverter : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonAccessorIndex, const FPositionVertexBuffer*>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	FGLTFJsonAccessorIndex Convert(const FPositionVertexBuffer* VertexBuffer) override final;
};

class FGLTFColorVertexBufferConverter : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonAccessorIndex, const FColorVertexBuffer*>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	FGLTFJsonAccessorIndex Convert(const FColorVertexBuffer* VertexBuffer) override final;
};

class FGLTFNormalVertexBufferConverter : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonAccessorIndex, const FStaticMeshVertexBuffer*>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	FGLTFJsonAccessorIndex Convert(const FStaticMeshVertexBuffer* VertexBuffer) override final;
};

class FGLTFTangentVertexBufferConverter : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonAccessorIndex, const FStaticMeshVertexBuffer*>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	FGLTFJsonAccessorIndex Convert(const FStaticMeshVertexBuffer* VertexBuffer) override final;
};

class FGLTFUVVertexBufferConverter : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonAccessorIndex, const FStaticMeshVertexBuffer*, int32>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	FGLTFJsonAccessorIndex Convert(const FStaticMeshVertexBuffer* VertexBuffer, int32 UVIndex) override final;
};

class FGLTFBoneIndexVertexBufferConverter : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonAccessorIndex, const FSkinWeightVertexBuffer*, int32, FGLTFBoneMap>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	FGLTFJsonAccessorIndex Convert(const FSkinWeightVertexBuffer* VertexBuffer, int32 InfluenceOffset, FGLTFBoneMap BoneMap) override final;

	template <typename IndexType>
	FGLTFJsonAccessorIndex Convert(const FSkinWeightVertexBuffer* VertexBuffer, int32 JointsGroupIndex, FGLTFBoneMap BoneMap);
};

class FGLTFBoneWeightVertexBufferConverter : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonAccessorIndex, const FSkinWeightVertexBuffer*, int32>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	FGLTFJsonAccessorIndex Convert(const FSkinWeightVertexBuffer* VertexBuffer, int32 WeightsGroupIndex) override final;
};

class FGLTFStaticMeshSectionConverter : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonAccessorIndex, const FStaticMeshSection*, const FRawStaticIndexBuffer*>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	FGLTFJsonAccessorIndex Convert(const FStaticMeshSection* MeshSection, const FRawStaticIndexBuffer* IndexBuffer) override final;
};

class FGLTFSkeletalMeshSectionConverter : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonAccessorIndex, const FSkelMeshRenderSection*, const FMultiSizeIndexContainer*>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	FGLTFJsonAccessorIndex Convert(const FSkelMeshRenderSection* MeshSection, const FMultiSizeIndexContainer* IndexContainer) override final;
};
