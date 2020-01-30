// Copyright Epic Games, Inc. All Rights Reserved.

#include "SelectionSet.h"


UMeshSelectionSet::UMeshSelectionSet()
{
	//SetFlags(RF_Transactional);
}


TArray<int>& UMeshSelectionSet::GetElements(EMeshSelectionElementType ElementType)
{
	switch (ElementType)
	{
	default:
	case EMeshSelectionElementType::Vertex:
		return Vertices;
	case EMeshSelectionElementType::Edge:
		return Edges;
	case EMeshSelectionElementType::Face:
		return Faces;
	case EMeshSelectionElementType::Group:
		return Groups;
	}
	check(false);
}

const TArray<int>& UMeshSelectionSet::GetElements(EMeshSelectionElementType ElementType) const
{
	switch (ElementType)
	{
	default:
	case EMeshSelectionElementType::Vertex:
		return Vertices;
	case EMeshSelectionElementType::Edge:
		return Edges;
	case EMeshSelectionElementType::Face:
		return Faces;
	case EMeshSelectionElementType::Group:
		return Groups;
	}
	check(false);
}



void UMeshSelectionSet::AddIndices(EMeshSelectionElementType ElementType, const TArray<int32>& Indices)
{
	TArray<int32>& CurElements = GetElements(ElementType);

	int N = Indices.Num();
	for (int k = 0; k < N; ++k)
	{
		CurElements.Add(Indices[k]);
	}
	NotifySelectionSetModified();
}

void UMeshSelectionSet::AddIndices(EMeshSelectionElementType ElementType, const TSet<int32>& Indices)
{
	TArray<int>& CurElements = GetElements(ElementType);

	for ( int32 Index : Indices )
	{
		CurElements.Add(Index);
	}
	NotifySelectionSetModified();
}



void UMeshSelectionSet::RemoveIndices(EMeshSelectionElementType ElementType, const TArray<int32>& Indices)
{
	TArray<int32>& CurElements = GetElements(ElementType);

	// @todo if we are removing many elements it is maybe cheaper to make a new array...
	int N = Indices.Num();
	for (int32 k = 0; k < N; ++k)
	{
		CurElements.RemoveSwap(Indices[k]);
	}
	NotifySelectionSetModified();
}


void UMeshSelectionSet::RemoveIndices(EMeshSelectionElementType ElementType, const TSet<int32>& Indices)
{
	TArray<int32>& CurElements = GetElements(ElementType);

	// @todo if we are removing many elements it is maybe cheaper to make a new array...
	for (int32 Index : Indices)
	{
		CurElements.RemoveSwap(Index);
	}
	NotifySelectionSetModified();
}