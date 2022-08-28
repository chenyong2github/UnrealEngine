// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GLTFJsonIndex.h"
#include "GLTFStaticMeshConverters.h"
#include "Engine.h"

struct FGLTFContainerBuilder;

template <class IndexType, class KeyType, class ConverterType>
struct GLTFEXPORTER_API TGLTFIndexedConvert
{
	TMap<KeyType, IndexType> IndexLookup;
	
	template <typename... ArgsType>
    FORCEINLINE IndexType Find(ArgsType&&... Args) const
	{
		const KeyType Key(Forward<ArgsType>(Args)...);
		return IndexLookup.FindRef(Key);
	}

	template <typename... ArgsType>
    FORCEINLINE IndexType Convert(FGLTFContainerBuilder& Container, const FString& DesiredName, ArgsType&&... Args)
	{
		const KeyType Key(Forward<ArgsType>(Args)...);

		IndexType Index = IndexLookup.FindRef(Key);
		if (Index == INDEX_NONE)
		{
			Index = ConverterType::Convert(Container, DesiredName, Forward<ArgsType>(Args)...);
			IndexLookup.Add(Key, Index);
		}

		return Index;
	}
};

struct GLTFEXPORTER_API FGLTFIndexedConverts
{
	TGLTFIndexedConvert<FGLTFJsonAccessorIndex, TTuple<const FPositionVertexBuffer*>, FGLTFPositionVertexBufferConverter> PositionVertexBuffers;
	TGLTFIndexedConvert<FGLTFJsonAccessorIndex, TTuple<const FColorVertexBuffer*>, FGLTFColorVertexBufferConverter> ColorVertexBuffers;
	TGLTFIndexedConvert<FGLTFJsonAccessorIndex, TTuple<const FStaticMeshVertexBuffer*>, FGLTFStaticMeshNormalVertexBufferConverter> StaticMeshNormalVertexBuffers;
	TGLTFIndexedConvert<FGLTFJsonAccessorIndex, TTuple<const FStaticMeshVertexBuffer*>, FGLTFStaticMeshTangentVertexBufferConverter> StaticMeshTangentVertexBuffers;
	TGLTFIndexedConvert<FGLTFJsonAccessorIndex, TTuple<const FStaticMeshVertexBuffer*>, FGLTFStaticMeshUV0VertexBufferConverter> StaticMeshUV0VertexBuffers;
	TGLTFIndexedConvert<FGLTFJsonAccessorIndex, TTuple<const FStaticMeshVertexBuffer*>, FGLTFStaticMeshUV1VertexBufferConverter> StaticMeshUV1VertexBuffers;
	TGLTFIndexedConvert<FGLTFJsonBufferViewIndex, TTuple<const FRawStaticIndexBuffer*>, FGLTFStaticMeshIndexBufferConverter> StaticMeshIndexBuffers;
	TGLTFIndexedConvert<FGLTFJsonAccessorIndex, TTuple<const FStaticMeshSection*, const FRawStaticIndexBuffer*>, FGLTFStaticMeshSectionConverter> StaticMeshSections;
	TGLTFIndexedConvert<FGLTFJsonMeshIndex, TTuple<const FStaticMeshLODResources*, const FColorVertexBuffer*>, FGLTFStaticMeshConverter> StaticMeshes;
};
