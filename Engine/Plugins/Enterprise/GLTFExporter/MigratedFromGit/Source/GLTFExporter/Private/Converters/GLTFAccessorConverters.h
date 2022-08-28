// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"
#include "Converters/GLTFBoneMap.h"
#include "Engine.h"

class FMultiSizeIndexContainer;

template <typename... InputTypes>
class FGLTFAccessorConverter : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonAccessorIndex, InputTypes...>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;
};

class FGLTFPositionVertexBufferConverter : public FGLTFAccessorConverter<const FPositionVertexBuffer*>
{
	using FGLTFAccessorConverter::FGLTFAccessorConverter;

	virtual FGLTFJsonAccessorIndex Convert(const FPositionVertexBuffer* VertexBuffer) override final;
};

class FGLTFColorVertexBufferConverter : public FGLTFAccessorConverter<const FColorVertexBuffer*>
{
	using FGLTFAccessorConverter::FGLTFAccessorConverter;

	virtual FGLTFJsonAccessorIndex Convert(const FColorVertexBuffer* VertexBuffer) override final;
};

class FGLTFNormalVertexBufferConverter : public FGLTFAccessorConverter<const FStaticMeshVertexBuffer*>
{
	using FGLTFAccessorConverter::FGLTFAccessorConverter;

	virtual FGLTFJsonAccessorIndex Convert(const FStaticMeshVertexBuffer* VertexBuffer) override final;
};

class FGLTFTangentVertexBufferConverter : public FGLTFAccessorConverter<const FStaticMeshVertexBuffer*>
{
	using FGLTFAccessorConverter::FGLTFAccessorConverter;

	virtual FGLTFJsonAccessorIndex Convert(const FStaticMeshVertexBuffer* VertexBuffer) override final;
};

class FGLTFUVVertexBufferConverter : public FGLTFAccessorConverter<const FStaticMeshVertexBuffer*, int32>
{
	using FGLTFAccessorConverter::FGLTFAccessorConverter;

	virtual FGLTFJsonAccessorIndex Convert(const FStaticMeshVertexBuffer* VertexBuffer, int32 UVIndex) override final;
};

class FGLTFBoneIndexVertexBufferConverter : public FGLTFAccessorConverter<const FSkinWeightVertexBuffer*, int32, FGLTFBoneMap>
{
	using FGLTFAccessorConverter::FGLTFAccessorConverter;

	virtual FGLTFJsonAccessorIndex Convert(const FSkinWeightVertexBuffer* VertexBuffer, int32 InfluenceOffset, FGLTFBoneMap BoneMap) override final;

	template <typename IndexType>
	FGLTFJsonAccessorIndex Convert(const FSkinWeightVertexBuffer* VertexBuffer, int32 JointsGroupIndex, FGLTFBoneMap BoneMap);
};

class FGLTFBoneWeightVertexBufferConverter : public FGLTFAccessorConverter<const FSkinWeightVertexBuffer*, int32>
{
	using FGLTFAccessorConverter::FGLTFAccessorConverter;

	virtual FGLTFJsonAccessorIndex Convert(const FSkinWeightVertexBuffer* VertexBuffer, int32 WeightsGroupIndex) override final;
};

class FGLTFStaticMeshSectionConverter : public FGLTFAccessorConverter<const FStaticMeshSection*, const FRawStaticIndexBuffer*>
{
	using FGLTFAccessorConverter::FGLTFAccessorConverter;

	virtual FGLTFJsonAccessorIndex Convert(const FStaticMeshSection* MeshSection, const FRawStaticIndexBuffer* IndexBuffer) override final;
};

class FGLTFSkeletalMeshSectionConverter : public FGLTFAccessorConverter<const FSkelMeshRenderSection*, const FMultiSizeIndexContainer*>
{
	using FGLTFAccessorConverter::FGLTFAccessorConverter;

	virtual FGLTFJsonAccessorIndex Convert(const FSkelMeshRenderSection* MeshSection, const FMultiSizeIndexContainer* IndexContainer) override final;
};
