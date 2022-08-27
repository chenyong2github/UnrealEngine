// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GLTFJsonIndex.h"
#include "Engine.h"

struct FGLTFContainerBuilder;

struct FGLTFStaticMeshKey
{
	const UStaticMesh* StaticMesh;
	int32 LODIndex;
	const FColorVertexBuffer* OverrideVertexColors;

	FGLTFStaticMeshKey(const UStaticMesh* StaticMesh, int32 LODIndex, const FColorVertexBuffer* OverrideVertexColors)
		: StaticMesh(StaticMesh)
		, LODIndex(LODIndex)
		, OverrideVertexColors(OverrideVertexColors)
	{
	}

	FORCEINLINE bool operator==(const FGLTFStaticMeshKey& Other) const
	{
		return StaticMesh           == Other.StaticMesh
			&& LODIndex             == Other.LODIndex
			&& OverrideVertexColors == Other.OverrideVertexColors;
	}

	FORCEINLINE friend uint32 GetTypeHash(const FGLTFStaticMeshKey& Other)
	{
		return HashCombine(GetTypeHash(Other.StaticMesh), HashCombine(GetTypeHash(Other.LODIndex), GetTypeHash(Other.OverrideVertexColors)));
	}
};

struct FGLTFPositionVertexBufferKey
{
	const FPositionVertexBuffer* VertexBuffer;
	const FString Name;

	FGLTFPositionVertexBufferKey(const FPositionVertexBuffer* VertexBuffer, const FString& Name)
		: VertexBuffer(VertexBuffer)
		, Name(Name)
	{
	}

	FORCEINLINE bool operator==(const FGLTFPositionVertexBufferKey& Other) const
	{
		return VertexBuffer == Other.VertexBuffer;
	}

	FORCEINLINE friend uint32 GetTypeHash(const FGLTFPositionVertexBufferKey& Other)
	{
		return GetTypeHash(Other.VertexBuffer);
	}
};

struct FGLTFStaticMeshVertexBufferKey
{
	const FStaticMeshVertexBuffer* VertexBuffer;
	const FString Name;

	FGLTFStaticMeshVertexBufferKey(const FStaticMeshVertexBuffer* VertexBuffer, const FString& Name)
		: VertexBuffer(VertexBuffer)
		, Name(Name)
	{
	}

	FORCEINLINE bool operator==(const FGLTFStaticMeshVertexBufferKey& Other) const
	{
		return VertexBuffer == Other.VertexBuffer;
	}

	FORCEINLINE friend uint32 GetTypeHash(const FGLTFStaticMeshVertexBufferKey& Other)
	{
		return GetTypeHash(Other.VertexBuffer);
	}
};

struct FGLTFColorVertexBufferKey
{
	const FColorVertexBuffer* VertexBuffer;
	const FString Name;

	FGLTFColorVertexBufferKey(const FColorVertexBuffer* VertexBuffer, const FString& Name)
		: VertexBuffer(VertexBuffer)
		, Name(Name)
	{
	}

	FORCEINLINE bool operator==(const FGLTFColorVertexBufferKey& Other) const
	{
		return VertexBuffer == Other.VertexBuffer;
	}

	FORCEINLINE friend uint32 GetTypeHash(const FGLTFColorVertexBufferKey& Other)
	{
		return GetTypeHash(Other.VertexBuffer);
	}
};

struct FGLTFRawStaticIndexBufferKey
{
	const FRawStaticIndexBuffer* IndexBuffer;
	const FString Name;

	FGLTFRawStaticIndexBufferKey(const FRawStaticIndexBuffer* IndexBuffer, const FString& Name)
		: IndexBuffer(IndexBuffer)
		, Name(Name)
	{
	}

	FORCEINLINE bool operator==(const FGLTFRawStaticIndexBufferKey& Other) const
	{
		return IndexBuffer == Other.IndexBuffer;
	}

	FORCEINLINE friend uint32 GetTypeHash(const FGLTFRawStaticIndexBufferKey& Other)
	{
		return GetTypeHash(Other.IndexBuffer);
	}
};

struct FGLTFStaticMeshSectionKey
{
	const FStaticMeshSection* MeshSection;
	const FRawStaticIndexBuffer* IndexBuffer;
	const FString Name;

	FGLTFStaticMeshSectionKey(const FStaticMeshSection* MeshSection, const FRawStaticIndexBuffer* IndexBuffer, const FString& Name)
		: MeshSection(MeshSection)
		, IndexBuffer(IndexBuffer)
		, Name(Name)
	{
	}

	FORCEINLINE bool operator==(const FGLTFStaticMeshSectionKey& Other) const
	{
		return MeshSection == Other.MeshSection
			&& IndexBuffer == Other.IndexBuffer;
	}

	FORCEINLINE friend uint32 GetTypeHash(const FGLTFStaticMeshSectionKey& Other)
	{
		return HashCombine(GetTypeHash(Other.MeshSection), GetTypeHash(Other.IndexBuffer));
	}
};

struct GLTFEXPORTER_API FGLTFIndexBuilder
{
	TMap<FGLTFStaticMeshKey, FGLTFJsonMeshIndex> StaticMeshes;
	TMap<FGLTFPositionVertexBufferKey, FGLTFJsonAccessorIndex> PositionVertexBuffers;
	TMap<FGLTFStaticMeshVertexBufferKey, FGLTFJsonAccessorIndex> StaticNormalVertexBuffers;
	TMap<FGLTFStaticMeshVertexBufferKey, FGLTFJsonAccessorIndex> StaticTangentVertexBuffers;
	TMap<FGLTFStaticMeshVertexBufferKey, FGLTFJsonAccessorIndex> StaticUV0VertexBuffers;
	TMap<FGLTFStaticMeshVertexBufferKey, FGLTFJsonAccessorIndex> StaticUV1VertexBuffers;
	TMap<FGLTFColorVertexBufferKey, FGLTFJsonAccessorIndex> ColorVertexBuffers;
	TMap<FGLTFRawStaticIndexBufferKey, FGLTFJsonBufferViewIndex> StaticIndexBuffers;
	TMap<FGLTFStaticMeshSectionKey, FGLTFJsonAccessorIndex> StaticMeshSections;

	FGLTFJsonMeshIndex FindMesh(const FGLTFStaticMeshKey& Key) const;
	FGLTFJsonAccessorIndex FindPositionAccessor(const FGLTFPositionVertexBufferKey& Key) const;
	FGLTFJsonAccessorIndex FindNormalAccessor(const FGLTFStaticMeshVertexBufferKey& Key) const;
	FGLTFJsonAccessorIndex FindTangentAccessor(const FGLTFStaticMeshVertexBufferKey& Key) const;
	FGLTFJsonAccessorIndex FindUV0Accessor(const FGLTFStaticMeshVertexBufferKey& Key) const;
	FGLTFJsonAccessorIndex FindUV1Accessor(const FGLTFStaticMeshVertexBufferKey& Key) const;
	FGLTFJsonAccessorIndex FindColorAccessor(const FGLTFColorVertexBufferKey& Key) const;
	FGLTFJsonBufferViewIndex FindIndexBufferView(const FGLTFRawStaticIndexBufferKey& Key) const;
	FGLTFJsonAccessorIndex FindIndexAccessor(const FGLTFStaticMeshSectionKey& Key) const;

	FGLTFJsonMeshIndex FindOrConvertMesh(const FGLTFStaticMeshKey& Key, FGLTFContainerBuilder& Container);
	FGLTFJsonAccessorIndex FindOrConvertPositionAccessor(const FGLTFPositionVertexBufferKey& Key, FGLTFContainerBuilder& Container);
	FGLTFJsonAccessorIndex FindOrConvertNormalAccessor(const FGLTFStaticMeshVertexBufferKey& Key, FGLTFContainerBuilder& Container);
	FGLTFJsonAccessorIndex FindOrConvertTangentAccessor(const FGLTFStaticMeshVertexBufferKey& Key, FGLTFContainerBuilder& Container);
	FGLTFJsonAccessorIndex FindOrConvertUV0Accessor(const FGLTFStaticMeshVertexBufferKey& Key, FGLTFContainerBuilder& Container);
	FGLTFJsonAccessorIndex FindOrConvertUV1Accessor(const FGLTFStaticMeshVertexBufferKey& Key, FGLTFContainerBuilder& Container);
	FGLTFJsonAccessorIndex FindOrConvertColorAccessor(const FGLTFColorVertexBufferKey& Key, FGLTFContainerBuilder& Container);
	FGLTFJsonBufferViewIndex FindOrConvertIndexBufferView(const FGLTFRawStaticIndexBufferKey& Key, FGLTFContainerBuilder& Container);
	FGLTFJsonAccessorIndex FindOrConvertIndexAccessor(const FGLTFStaticMeshSectionKey& Key, FGLTFContainerBuilder& Container);
};
