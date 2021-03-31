// Copyright Epic Games, Inc. All Rights Reserved.

#include "Polygroups/PolygroupUtil.h"



FDynamicMeshPolygroupAttribute* UE::Geometry::FindPolygroupLayerByName(FDynamicMesh3& Mesh, FName Name)
{
	FDynamicMeshAttributeSet* AttributeSet = Mesh.Attributes();
	if (AttributeSet == nullptr) return nullptr;
	int32 NumPolygroupLayers = AttributeSet->NumPolygroupLayers();
	for (int32 k = 0; k < NumPolygroupLayers; ++k)
	{
		if (AttributeSet->GetPolygroupLayer(k)->GetName() == Name)
		{
			return AttributeSet->GetPolygroupLayer(k);
		}
	}
	return nullptr;
}


int32 UE::Geometry::FindPolygroupLayerIndexByName(const FDynamicMesh3& Mesh, FName Name)
{
	const FDynamicMeshAttributeSet* AttributeSet = Mesh.Attributes();
	if (AttributeSet == nullptr) return -1;
	int32 NumPolygroupLayers = AttributeSet->NumPolygroupLayers();
	for (int32 k = 0; k < NumPolygroupLayers; ++k)
	{
		if (AttributeSet->GetPolygroupLayer(k)->GetName() == Name)
		{
			return k;
		}
	}
	return -1;
}


int32 UE::Geometry::FindPolygroupLayerIndex(const FDynamicMesh3& Mesh, const FDynamicMeshPolygroupAttribute* Layer)
{
	const FDynamicMeshAttributeSet* AttributeSet = Mesh.Attributes();
	if (AttributeSet == nullptr) return -1;
	int32 NumPolygroupLayers = AttributeSet->NumPolygroupLayers();
	for (int32 k = 0; k < NumPolygroupLayers; ++k)
	{
		if (AttributeSet->GetPolygroupLayer(k) == Layer)
		{
			return k;
		}
	}
	return -1;
}


int32 UE::Geometry::ComputeGroupIDBound(const FDynamicMesh3& Mesh, const FDynamicMeshPolygroupAttribute* Layer)
{
	int Bound = 0;
	for (int TID : Mesh.TriangleIndicesItr())
	{
		Bound = FMath::Max(Bound, Layer->GetValue(TID) + 1);
	}
	return Bound;
}