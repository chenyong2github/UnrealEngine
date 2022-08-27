// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFIndexBuilder.h"
#include "GLTFContainerBuilder.h"
#include "GLTFMeshConverter.h"

FGLTFJsonMeshIndex FGLTFIndexBuilder::FindMesh(const FGLTFStaticMeshKey& Key) const
{
	const FGLTFJsonMeshIndex* Index = StaticMeshes.Find(Key);
	return Index != nullptr ? *Index : FGLTFJsonMeshIndex(INDEX_NONE);
}

FGLTFJsonAccessorIndex FGLTFIndexBuilder::FindPositionAccessor(const FGLTFPositionVertexBufferKey& Key) const
{
	const FGLTFJsonAccessorIndex* Index = PositionVertexBuffers.Find(Key);
	return Index != nullptr ? *Index : FGLTFJsonAccessorIndex(INDEX_NONE);
}

FGLTFJsonAccessorIndex FGLTFIndexBuilder::FindNormalAccessor(const FGLTFStaticMeshVertexBufferKey& Key) const
{
	const FGLTFJsonAccessorIndex* Index = StaticNormalVertexBuffers.Find(Key);
	return Index != nullptr ? *Index : FGLTFJsonAccessorIndex(INDEX_NONE);
}

FGLTFJsonAccessorIndex FGLTFIndexBuilder::FindTangentAccessor(const FGLTFStaticMeshVertexBufferKey& Key) const
{
	const FGLTFJsonAccessorIndex* Index = StaticTangentVertexBuffers.Find(Key);
	return Index != nullptr ? *Index : FGLTFJsonAccessorIndex(INDEX_NONE);
}

FGLTFJsonAccessorIndex FGLTFIndexBuilder::FindUV0Accessor(const FGLTFStaticMeshVertexBufferKey& Key) const
{
	const FGLTFJsonAccessorIndex* Index = StaticUV0VertexBuffers.Find(Key);
	return Index != nullptr ? *Index : FGLTFJsonAccessorIndex(INDEX_NONE);
}

FGLTFJsonAccessorIndex FGLTFIndexBuilder::FindUV1Accessor(const FGLTFStaticMeshVertexBufferKey& Key) const
{
	const FGLTFJsonAccessorIndex* Index = StaticUV1VertexBuffers.Find(Key);
	return Index != nullptr ? *Index : FGLTFJsonAccessorIndex(INDEX_NONE);
}

FGLTFJsonAccessorIndex FGLTFIndexBuilder::FindColorAccessor(const FGLTFColorVertexBufferKey& Key) const
{
	const FGLTFJsonAccessorIndex* Index = ColorVertexBuffers.Find(Key);
	return Index != nullptr ? *Index : FGLTFJsonAccessorIndex(INDEX_NONE);
}

FGLTFJsonBufferViewIndex FGLTFIndexBuilder::FindIndexBufferView(const FGLTFRawStaticIndexBufferKey& Key) const
{
	const FGLTFJsonBufferViewIndex* Index = StaticIndexBuffers.Find(Key);
	return Index != nullptr ? *Index : FGLTFJsonBufferViewIndex(INDEX_NONE);
}

FGLTFJsonAccessorIndex FGLTFIndexBuilder::FindIndexAccessor(const FGLTFStaticMeshSectionKey& Key) const
{
	const FGLTFJsonAccessorIndex* Index = StaticMeshSections.Find(Key);
	return Index != nullptr ? *Index : FGLTFJsonAccessorIndex(INDEX_NONE);
}


FGLTFJsonMeshIndex FGLTFIndexBuilder::FindOrConvertMesh(const FGLTFStaticMeshKey& Key, FGLTFContainerBuilder& Container)
{
	FGLTFJsonMeshIndex Index = FindMesh(Key);

	if (Index == INDEX_NONE)
	{
		Index = FGLTFMeshConverter::ConvertMesh(Container, Key.StaticMesh, Key.LODIndex, Key.OverrideVertexColors);
		StaticMeshes.Add(Key, Index);
	}
	
	return Index;
}

FGLTFJsonAccessorIndex FGLTFIndexBuilder::FindOrConvertPositionAccessor(const FGLTFPositionVertexBufferKey& Key, FGLTFContainerBuilder& Container)
{
	FGLTFJsonAccessorIndex Index = FindPositionAccessor(Key);

	if (Index == INDEX_NONE)
	{
		Index = FGLTFMeshConverter::ConvertPositionAccessor(Container, Key.VertexBuffer, Key.Name);
		PositionVertexBuffers.Add(Key, Index);
	}
	
	return Index;
}

FGLTFJsonAccessorIndex FGLTFIndexBuilder::FindOrConvertNormalAccessor(const FGLTFStaticMeshVertexBufferKey& Key, FGLTFContainerBuilder& Container)
{
	FGLTFJsonAccessorIndex Index = FindNormalAccessor(Key);

	if (Index == INDEX_NONE)
	{
		Index = FGLTFMeshConverter::ConvertNormalAccessor(Container, Key.VertexBuffer, Key.Name);
		StaticNormalVertexBuffers.Add(Key, Index);
	}
	
	return Index;
}

FGLTFJsonAccessorIndex FGLTFIndexBuilder::FindOrConvertTangentAccessor(const FGLTFStaticMeshVertexBufferKey& Key, FGLTFContainerBuilder& Container)
{
	FGLTFJsonAccessorIndex Index = FindTangentAccessor(Key);

	if (Index == INDEX_NONE)
	{
		Index = FGLTFMeshConverter::ConvertTangentAccessor(Container, Key.VertexBuffer, Key.Name);
		StaticTangentVertexBuffers.Add(Key, Index);
	}
	
	return Index;
}

FGLTFJsonAccessorIndex FGLTFIndexBuilder::FindOrConvertUV0Accessor(const FGLTFStaticMeshVertexBufferKey& Key, FGLTFContainerBuilder& Container)
{
	FGLTFJsonAccessorIndex Index = FindUV0Accessor(Key);

	if (Index == INDEX_NONE)
	{
		Index = FGLTFMeshConverter::ConvertUV0Accessor(Container, Key.VertexBuffer, Key.Name);
		StaticUV0VertexBuffers.Add(Key, Index);
	}
	
	return Index;
}

FGLTFJsonAccessorIndex FGLTFIndexBuilder::FindOrConvertUV1Accessor(const FGLTFStaticMeshVertexBufferKey& Key, FGLTFContainerBuilder& Container)
{
	FGLTFJsonAccessorIndex Index = FindUV1Accessor(Key);

	if (Index == INDEX_NONE)
	{
		Index = FGLTFMeshConverter::ConvertUV1Accessor(Container, Key.VertexBuffer, Key.Name);
		StaticUV1VertexBuffers.Add(Key, Index);
	}
	
	return Index;
}

FGLTFJsonAccessorIndex FGLTFIndexBuilder::FindOrConvertColorAccessor(const FGLTFColorVertexBufferKey& Key, FGLTFContainerBuilder& Container)
{
	FGLTFJsonAccessorIndex Index = FindColorAccessor(Key);

	if (Index == INDEX_NONE)
	{
		Index = FGLTFMeshConverter::ConvertColorAccessor(Container, Key.VertexBuffer, Key.Name);
		ColorVertexBuffers.Add(Key, Index);
	}
	
	return Index;
}

FGLTFJsonBufferViewIndex FGLTFIndexBuilder::FindOrConvertIndexBufferView(const FGLTFRawStaticIndexBufferKey& Key, FGLTFContainerBuilder& Container)
{
	FGLTFJsonBufferViewIndex Index = FindIndexBufferView(Key);

	if (Index == INDEX_NONE)
	{
		Index = FGLTFMeshConverter::ConvertIndexBufferView(Container, Key.IndexBuffer, Key.Name);
		StaticIndexBuffers.Add(Key, Index);
	}
	
	return Index;
}

FGLTFJsonAccessorIndex FGLTFIndexBuilder::FindOrConvertIndexAccessor(const FGLTFStaticMeshSectionKey& Key, FGLTFContainerBuilder& Container)
{
	FGLTFJsonAccessorIndex Index = FindIndexAccessor(Key);

	if (Index == INDEX_NONE)
	{
		Index = FGLTFMeshConverter::ConvertIndexAccessor(Container, Key.MeshSection, Key.IndexBuffer, Key.Name);
		StaticMeshSections.Add(Key, Index);
	}
	
	return Index;
}
