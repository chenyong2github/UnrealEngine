// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"
#include "Converters/GLTFBoneMap.h"
#include "Engine.h"

class FMultiSizeIndexContainer;

template <typename... InputTypes>
class TGLTFAccessorConverter : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonAccessorIndex, InputTypes...>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;
};

class FGLTFPositionBufferConverter final : public TGLTFAccessorConverter<const FPositionVertexBuffer*>
{
	using TGLTFAccessorConverter::TGLTFAccessorConverter;

	virtual FGLTFJsonAccessorIndex Convert(const FPositionVertexBuffer* VertexBuffer) override;
};

class FGLTFColorBufferConverter final : public TGLTFAccessorConverter<const FColorVertexBuffer*>
{
	using TGLTFAccessorConverter::TGLTFAccessorConverter;

	virtual FGLTFJsonAccessorIndex Convert(const FColorVertexBuffer* VertexBuffer) override;
};

class FGLTFNormalBufferConverter final : public TGLTFAccessorConverter<const FStaticMeshVertexBuffer*>
{
	using TGLTFAccessorConverter::TGLTFAccessorConverter;

	virtual FGLTFJsonAccessorIndex Convert(const FStaticMeshVertexBuffer* VertexBuffer) override;
};

class FGLTFTangentBufferConverter final : public TGLTFAccessorConverter<const FStaticMeshVertexBuffer*>
{
	using TGLTFAccessorConverter::TGLTFAccessorConverter;

	virtual FGLTFJsonAccessorIndex Convert(const FStaticMeshVertexBuffer* VertexBuffer) override;
};

class FGLTFUVBufferConverter final : public TGLTFAccessorConverter<const FStaticMeshVertexBuffer*, int32>
{
	using TGLTFAccessorConverter::TGLTFAccessorConverter;

	virtual FGLTFJsonAccessorIndex Convert(const FStaticMeshVertexBuffer* VertexBuffer, int32 UVIndex) override;
};

class FGLTFBoneIndexBufferConverter final : public TGLTFAccessorConverter<const FSkinWeightVertexBuffer*, int32, FGLTFBoneMap>
{
	using TGLTFAccessorConverter::TGLTFAccessorConverter;

	virtual FGLTFJsonAccessorIndex Convert(const FSkinWeightVertexBuffer* VertexBuffer, int32 InfluenceOffset, FGLTFBoneMap BoneMap) override;

	template <typename IndexType>
	FGLTFJsonAccessorIndex Convert(const FSkinWeightVertexBuffer* VertexBuffer, int32 JointsGroupIndex, FGLTFBoneMap BoneMap);
};

class FGLTFBoneWeightBufferConverter final : public TGLTFAccessorConverter<const FSkinWeightVertexBuffer*, int32>
{
	using TGLTFAccessorConverter::TGLTFAccessorConverter;

	virtual FGLTFJsonAccessorIndex Convert(const FSkinWeightVertexBuffer* VertexBuffer, int32 WeightsGroupIndex) override;
};

class FGLTFStaticMeshSectionConverter final : public TGLTFAccessorConverter<const FStaticMeshSection*, const FRawStaticIndexBuffer*>
{
	using TGLTFAccessorConverter::TGLTFAccessorConverter;

	virtual FGLTFJsonAccessorIndex Convert(const FStaticMeshSection* MeshSection, const FRawStaticIndexBuffer* IndexBuffer) override;
};

class FGLTFSkeletalMeshSectionConverter final : public TGLTFAccessorConverter<const FSkelMeshRenderSection*, const FMultiSizeIndexContainer*>
{
	using TGLTFAccessorConverter::TGLTFAccessorConverter;

	virtual FGLTFJsonAccessorIndex Convert(const FSkelMeshRenderSection* MeshSection, const FMultiSizeIndexContainer* IndexContainer) override;
};
