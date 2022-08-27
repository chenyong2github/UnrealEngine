// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"
#include "Converters/GLTFMeshSection.h"
#include "Engine.h"

template <typename... InputTypes>
class TGLTFAccessorConverter : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonAccessorIndex, InputTypes...>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;
};

class FGLTFPositionBufferConverter final : public TGLTFAccessorConverter<const FGLTFMeshSection*, const FPositionVertexBuffer*>
{
	using TGLTFAccessorConverter::TGLTFAccessorConverter;

	virtual FGLTFJsonAccessorIndex Convert(const FGLTFMeshSection* MeshSection, const FPositionVertexBuffer* VertexBuffer) override;
};

class FGLTFColorBufferConverter final : public TGLTFAccessorConverter<const FGLTFMeshSection*, const FColorVertexBuffer*>
{
	using TGLTFAccessorConverter::TGLTFAccessorConverter;

	virtual FGLTFJsonAccessorIndex Convert(const FGLTFMeshSection* MeshSection, const FColorVertexBuffer* VertexBuffer) override;
};

class FGLTFNormalBufferConverter final : public TGLTFAccessorConverter<const FGLTFMeshSection*, const FStaticMeshVertexBuffer*>
{
	using TGLTFAccessorConverter::TGLTFAccessorConverter;

	virtual FGLTFJsonAccessorIndex Convert(const FGLTFMeshSection* MeshSection, const FStaticMeshVertexBuffer* VertexBuffer) override;

	template <typename DestinationType, typename SourceType>
	FGLTFJsonBufferViewIndex ConvertBufferView(const FGLTFMeshSection* MeshSection, const void* TangentData);
};

class FGLTFTangentBufferConverter final : public TGLTFAccessorConverter<const FGLTFMeshSection*, const FStaticMeshVertexBuffer*>
{
	using TGLTFAccessorConverter::TGLTFAccessorConverter;

	virtual FGLTFJsonAccessorIndex Convert(const FGLTFMeshSection* MeshSection, const FStaticMeshVertexBuffer* VertexBuffer) override;

	template <typename DestinationType, typename SourceType>
	FGLTFJsonBufferViewIndex ConvertBufferView(const FGLTFMeshSection* MeshSection, const void* TangentData);
};

class FGLTFUVBufferConverter final : public TGLTFAccessorConverter<const FGLTFMeshSection*, const FStaticMeshVertexBuffer*, uint32>
{
	using TGLTFAccessorConverter::TGLTFAccessorConverter;

	virtual FGLTFJsonAccessorIndex Convert(const FGLTFMeshSection* MeshSection, const FStaticMeshVertexBuffer* VertexBuffer, uint32 UVIndex) override;

	template <typename SourceType>
	FGLTFJsonAccessorIndex Convert(const FGLTFMeshSection* MeshSection, const FStaticMeshVertexBuffer* VertexBuffer, uint32 UVIndex, const uint8* SourceData) const;
};

class FGLTFBoneIndexBufferConverter final : public TGLTFAccessorConverter<const FGLTFMeshSection*, const FSkinWeightVertexBuffer*, uint32>
{
	using TGLTFAccessorConverter::TGLTFAccessorConverter;

	virtual FGLTFJsonAccessorIndex Convert(const FGLTFMeshSection* MeshSection, const FSkinWeightVertexBuffer* VertexBuffer, uint32 InfluenceOffset) override;

	template <typename DestinationType>
	FGLTFJsonAccessorIndex Convert(const FGLTFMeshSection* MeshSection, const FSkinWeightVertexBuffer* VertexBuffer, uint32 InfluenceOffset, const uint8* SourceData) const;

	template <typename DestinationType, typename SourceType>
	FGLTFJsonAccessorIndex Convert(const FGLTFMeshSection* MeshSection, const FSkinWeightVertexBuffer* VertexBuffer, uint32 InfluenceOffset, const uint8* SourceData) const;

	template <typename DestinationType, typename SourceType, typename CallbackType>
	FGLTFJsonAccessorIndex Convert(const FGLTFMeshSection* MeshSection, const FSkinWeightVertexBuffer* VertexBuffer, uint32 InfluenceOffset, const uint8* SourceData, CallbackType GetVertexInfluenceOffsetCount) const;
};

class FGLTFBoneWeightBufferConverter final : public TGLTFAccessorConverter<const FGLTFMeshSection*, const FSkinWeightVertexBuffer*, uint32>
{
	using TGLTFAccessorConverter::TGLTFAccessorConverter;

	virtual FGLTFJsonAccessorIndex Convert(const FGLTFMeshSection* MeshSection, const FSkinWeightVertexBuffer* VertexBuffer, uint32 InfluenceOffset) override;

	template <typename BoneIndexType>
	FGLTFJsonAccessorIndex Convert(const FGLTFMeshSection* MeshSection, const FSkinWeightVertexBuffer* VertexBuffer, uint32 InfluenceOffset, const uint8* SourceData) const;

	template <typename BoneIndexType, typename CallbackType>
	FGLTFJsonAccessorIndex Convert(const FGLTFMeshSection* MeshSection, const FSkinWeightVertexBuffer* VertexBuffer, uint32 InfluenceOffset, const uint8* SourceData, CallbackType GetVertexInfluenceOffsetCount) const;
};

class FGLTFIndexBufferConverter final : public TGLTFAccessorConverter<const FGLTFMeshSection*>
{
	using TGLTFAccessorConverter::TGLTFAccessorConverter;

	virtual FGLTFJsonAccessorIndex Convert(const FGLTFMeshSection* MeshSection) override;

	template <typename IndexType>
	FGLTFJsonAccessorIndex Convert(const FGLTFMeshSection* MeshSection) const;
};
