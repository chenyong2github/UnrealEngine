// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/KDTree.h"

namespace UE { namespace PoseSearch
{

FKDTree::FKDTree(int32 Count, int32 Dim, const float* Data, int32 MaxLeafSize)
: DataSrc(Count, Dim, Data)
, Impl(nullptr)
{
}

FKDTree::FKDTree()
: DataSrc(0, 0, nullptr)
, Impl(nullptr)
{
}

FKDTree::~FKDTree()
{
}

FKDTree::FKDTree(const FKDTree& r)
: DataSrc(r.DataSrc.PointCount, r.DataSrc.PointDim, r.DataSrc.Data)
, Impl(nullptr)
{
}

FKDTree& FKDTree::operator=(const FKDTree& r)
{
	this->~FKDTree();
	new(this)FKDTree(r);
	return *this;
}

void FKDTree::Construct(int32 Count, int32 Dim, const float* Data, int32 MaxLeafSize)
{
	this->~FKDTree();
	new(this)FKDTree(Count, Dim, Data, MaxLeafSize);
}

bool FKDTree::FindNeighbors(KNNResultSet& Result, const float* Query) const
{
	check(false); // unimplemented
	return false;
}

FArchive& Serialize(FArchive& Ar, FKDTree& KDTree, const float* KDTreeData)
{
	return Ar;
}

} }
