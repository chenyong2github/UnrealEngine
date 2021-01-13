// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CleaningOps/RemoveOccludedTrianglesOp.h"

#include "DynamicMesh3.h"
#include "DynamicMeshAABBTree3.h"

#include "Operations/RemoveOccludedTriangles.h"
#include "Selections/MeshConnectedComponents.h"
#include "Selections/MeshFaceSelection.h"
#include "Polygroups/PolygroupSet.h"

#include "Async/ParallelFor.h"


void IndexMeshWithAcceleration::AddMesh(const FDynamicMesh3& MeshIn, FTransform3d Transform)
{
	int VertexIndexStart = Vertices.Num();
	Vertices.SetNum(VertexIndexStart + MeshIn.MaxVertexID());
	ParallelFor(MeshIn.MaxVertexID(), [this, &MeshIn, &Transform, &VertexIndexStart](int VID)
	{
		if (MeshIn.IsVertex(VID))
		{
			Vertices[VID + VertexIndexStart] = Transform.TransformPosition(MeshIn.GetVertex(VID));
		}
	}
	, false);

	// don't parallelize triangles b/c we want need them compact
	for (int TID = 0; TID < MeshIn.MaxTriangleID(); TID++)
	{
		if (MeshIn.IsTriangle(TID))
		{
			FIndex3i Triangle = MeshIn.GetTriangle(TID);
			Triangles.Add(Triangle.A + VertexIndexStart);
			Triangles.Add(Triangle.B + VertexIndexStart);
			Triangles.Add(Triangle.C + VertexIndexStart);
		}
	}
}


void FRemoveOccludedTrianglesOp::SetTransform(const FTransform& Transform)
{
	ResultTransform = (FTransform3d)Transform;
}

void FRemoveOccludedTrianglesOp::CalculateResult(FProgressCancel* Progress)
{
	if (Progress && Progress->Cancelled())
	{
		return;
	}

	bool bDiscardAttributes = false;
	ResultMesh->Copy(*OriginalMesh, true, true, true, !bDiscardAttributes);
	
	auto ShrinkSelection = [](FDynamicMesh3& Mesh, TArray<int>& SelectedTris, int NumShrinks)
	{
		FMeshFaceSelection Selection(&Mesh);
		Selection.Select(SelectedTris);
		Selection.ContractBorderByOneRingNeighbours(NumShrinks);
		SelectedTris = Selection.AsArray();
	};

	auto SetNewGroupSelection = [](FDynamicMesh3& Mesh, const TArray<int>& SelectedTris, FName LayerName, bool bActiveGroupIsDefault)
	{
		// don't add any new groups if there's nothing to select
		if (SelectedTris.Num() == 0)
		{
			return FIndex2i::Invalid();
		}

		auto SetGroup = [](UE::Geometry::FPolygroupSet& ActiveGroupSet, const TArray<int>& SelectedTris)
		{
			int32 JacketGroupID = ActiveGroupSet.AllocateNewGroupID();

			for (int TID : SelectedTris)
			{
				ActiveGroupSet.SetGroup(TID, JacketGroupID);
			}

			return FIndex2i(JacketGroupID, ActiveGroupSet.GroupLayerIndex);
		};

		if (!bActiveGroupIsDefault)
		{
			UE::Geometry::FPolygroupSet ActiveGroupSet(&Mesh, LayerName);
			return SetGroup(ActiveGroupSet, SelectedTris);
		}
		else
		{
			UE::Geometry::FPolygroupSet ActiveGroupSet(&Mesh);
			return SetGroup(ActiveGroupSet, SelectedTris);
		}
	};

	if (bOnlySelfOcclude)
	{
		TRemoveOccludedTriangles<FDynamicMesh3> SelfJacket(ResultMesh.Get());
		FDynamicMeshAABBTree3 SelfAABB(ResultMesh.Get());
		TFastWindingTree<FDynamicMesh3> SelfFWTree(&SelfAABB);
		if (Progress && Progress->Cancelled())
		{
			return;
		}
		SelfJacket.InsideMode = InsideMode;
		SelfJacket.TriangleSamplingMethod = TriangleSamplingMethod;
		SelfJacket.WindingIsoValue = WindingIsoValue;
		SelfJacket.NormalOffset = NormalOffset;
		SelfJacket.AddRandomRays = AddRandomRays;
		SelfJacket.AddTriangleSamples = AddTriangleSamples;
		SelfJacket.Select(FTransform3d::Identity(), &SelfAABB, &SelfFWTree);
		if (ShrinkRemoval > 0)
		{
			ShrinkSelection(*ResultMesh.Get(), SelfJacket.RemovedT, ShrinkRemoval);
		}

		if (bSetTriangleGroupInsteadOfRemoving)
		{
			FIndex2i GroupIDAndLayerIndex = SetNewGroupSelection(*ResultMesh.Get(), SelfJacket.RemovedT, ActiveGroupLayer, bActiveGroupLayerIsDefault);
			CreatedGroupID = GroupIDAndLayerIndex.A;
			CreatedGroupLayerIndex = GroupIDAndLayerIndex.B;
		}
		else
		{
			SelfJacket.RemoveSelected();
		}
	}
	else
	{
		TRemoveOccludedTriangles<TIndexMeshArrayAdapter<int, double, FVector3d>> Jacket(ResultMesh.Get());

		if (Progress && Progress->Cancelled())
		{
			return;
		}

		Jacket.InsideMode = InsideMode;
		Jacket.TriangleSamplingMethod = TriangleSamplingMethod;
		Jacket.WindingIsoValue = WindingIsoValue;
		Jacket.NormalOffset = NormalOffset;
		Jacket.AddRandomRays = AddRandomRays;
		Jacket.AddTriangleSamples = AddTriangleSamples;
		Jacket.Select(MeshTransforms, &CombinedMeshTrees->AABB, &CombinedMeshTrees->FastWinding);
		if (ShrinkRemoval > 0)
		{
			ShrinkSelection(*ResultMesh.Get(), Jacket.RemovedT, ShrinkRemoval);
		}

		if (bSetTriangleGroupInsteadOfRemoving)
		{
			FIndex2i GroupIDAndLayerIndex = SetNewGroupSelection(*ResultMesh.Get(), Jacket.RemovedT, ActiveGroupLayer, bActiveGroupLayerIsDefault);
			CreatedGroupID = GroupIDAndLayerIndex.A;
			CreatedGroupLayerIndex = GroupIDAndLayerIndex.B;
		}
		else
		{
			Jacket.RemoveSelected();
		}
	}

	if (MinTriCountConnectedComponent > 0 || MinAreaConnectedComponent > 0)
	{
		FDynamicMeshEditor Editor(ResultMesh.Get());
		Editor.RemoveSmallComponents(0, MinAreaConnectedComponent, MinTriCountConnectedComponent);
	}
}