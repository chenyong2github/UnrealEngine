// Copyright Epic Games, Inc. All Rights Reserved.

#include "Polygroups/PolygroupSet.h"
#include "Polygroups/PolygroupUtil.h"

using namespace UE::Geometry;


FPolygroupSet::FPolygroupSet(const FPolygroupSet* CopyIn)
{
	Mesh = CopyIn->Mesh;
	PolygroupAttrib = CopyIn->PolygroupAttrib;
	GroupLayerIndex = CopyIn->GroupLayerIndex;
	MaxGroupID = CopyIn->MaxGroupID;
}

FPolygroupSet::FPolygroupSet(FDynamicMesh3* MeshIn)
{
	Mesh = MeshIn;
	GroupLayerIndex = -1;
	RecalculateMaxGroupID();
}

FPolygroupSet::FPolygroupSet(FDynamicMesh3* MeshIn, FDynamicMeshPolygroupAttribute* PolygroupAttribIn)
{
	Mesh = MeshIn;
	PolygroupAttrib = PolygroupAttribIn;
	GroupLayerIndex = UE::Geometry::FindPolygroupLayerIndex(*MeshIn, PolygroupAttrib);
	RecalculateMaxGroupID();
}

FPolygroupSet::FPolygroupSet(FDynamicMesh3* MeshIn, int32 PolygroupLayerIndex)
{
	Mesh = MeshIn;
	if (ensure(Mesh->Attributes()))
	{
		if (PolygroupLayerIndex < Mesh->Attributes()->NumPolygroupLayers())
		{
			PolygroupAttrib = Mesh->Attributes()->GetPolygroupLayer(PolygroupLayerIndex);
			GroupLayerIndex = PolygroupLayerIndex;
			return;
		}
	}
	RecalculateMaxGroupID();
	ensureMsgf(false, TEXT("FPolygroupSet: Attribute index missing!"));
}


FPolygroupSet::FPolygroupSet(FDynamicMesh3* MeshIn, FName AttribName)
{
	Mesh = MeshIn;
	PolygroupAttrib = UE::Geometry::FindPolygroupLayerByName(*MeshIn, AttribName);
	GroupLayerIndex = UE::Geometry::FindPolygroupLayerIndex(*MeshIn, PolygroupAttrib);
	RecalculateMaxGroupID();
	ensureMsgf(PolygroupAttrib != nullptr, TEXT("FPolygroupSet: Attribute set missing!"));
}


void FPolygroupSet::RecalculateMaxGroupID()
{
	MaxGroupID = 0;
	if (PolygroupAttrib)
	{
		for (int32 tid : Mesh->TriangleIndicesItr())
		{
			MaxGroupID = FMath::Max(MaxGroupID, PolygroupAttrib->GetValue(tid) + 1);
		}
	}
	else
	{
		for (int32 tid : Mesh->TriangleIndicesItr())
		{
			MaxGroupID = FMath::Max(MaxGroupID, Mesh->GetTriangleGroup(tid) + 1);
		}
	}
}