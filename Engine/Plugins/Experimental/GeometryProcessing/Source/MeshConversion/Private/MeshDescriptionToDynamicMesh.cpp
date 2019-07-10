// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved. 

#include "MeshDescriptionToDynamicMesh.h"
#include "MeshAttributes.h"
#include "DynamicMeshAttributeSet.h"
#include "DynamicMeshOverlay.h"
#include "MeshDescriptionBuilder.h"


struct FVertexUV
{
	int vid;
	float x;
	float y;
	bool operator==(const FVertexUV & o) const
	{
		return vid == o.vid && x == o.x && y == o.y;
	}
};
FORCEINLINE uint32 GetTypeHash(const FVertexUV& Vector)
{
	// ugh copied from FVector clearly should not be using CRC for hash!!
	return FCrc::MemCrc32(&Vector, sizeof(Vector));
}


class FUVWelder
{
public:
	TMap<FVertexUV, int> UniqueVertexUVs;
	FDynamicMeshUVOverlay* UVOverlay;

	FUVWelder(FDynamicMeshUVOverlay* UVOverlayIn)
	{
		check(UVOverlayIn);
		UVOverlay = UVOverlayIn;
	}

	int FindOrAddUnique(const FVector2D & UV, int VertexID)
	{
		FVertexUV VertUV = { VertexID, UV.X, UV.Y };

		int NewIndex = -1;
		if (UniqueVertexUVs.Contains(VertUV))
		{
			NewIndex = UniqueVertexUVs[VertUV];
		}
		else
		{
			NewIndex = UVOverlay->AppendElement(UV, VertexID);
			UniqueVertexUVs.Add(VertUV, NewIndex);
		}
		return NewIndex;
	}
};




struct FVertexNormal
{
	int vid;
	float x;
	float y;
	float z;
	bool operator==(const FVertexNormal & o) const
	{
		return vid == o.vid && x == o.x && y == o.y && z == o.z;
	}
};
FORCEINLINE uint32 GetTypeHash(const FVertexNormal& Vector)
{
	// ugh copied from FVector clearly should not be using CRC for hash!!
	return FCrc::MemCrc32(&Vector, sizeof(Vector));
}


class FNormalWelder
{
public:
	TMap<FVertexNormal, int> UniqueVertexNormals;
	FDynamicMeshNormalOverlay* NormalOverlay;

	FNormalWelder(FDynamicMeshNormalOverlay* NormalOverlayIn)
	{
		check(NormalOverlayIn);
		NormalOverlay = NormalOverlayIn;
	}

	int FindOrAddUnique(const FVector & Normal, int VertexID)
	{
		FVertexNormal VertNormal = { VertexID, Normal.X, Normal.Y, Normal.Z };

		int NewIndex = -1;
		if (UniqueVertexNormals.Contains(VertNormal))
		{
			NewIndex = UniqueVertexNormals[VertNormal];
		}
		else
		{
			NewIndex = NormalOverlay->AppendElement(Normal, VertexID);
			UniqueVertexNormals.Add(VertNormal, NewIndex);
		}
		return NewIndex;
	}
};













void FMeshDescriptionToDynamicMesh::Convert(const FMeshDescription* MeshIn, FDynamicMesh3& MeshOut)
{
	// look up vertex positions
	const FVertexArray& VertexIDs = MeshIn->Vertices();
	TVertexAttributesConstRef<FVector> VertexPositions =
		MeshIn->VertexAttributes().GetAttributesRef<FVector>(MeshAttribute::Vertex::Position);

	// copy vertex positions
	for (const FVertexID VertexID : VertexIDs.GetElementIDs())
	{
		const FVector Position = VertexPositions.Get(VertexID);
		int NewVertIdx = MeshOut.AppendVertex(Position);
		check(NewVertIdx == VertexID.GetValue());
	}

	// look up vertex-instance UVs and normals
	// @todo: does the MeshDescription always have UVs and Normals?
	TVertexInstanceAttributesConstRef<FVector2D> InstanceUVs =
		MeshIn->VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);
	TVertexInstanceAttributesConstRef<FVector> InstanceNormals =
		MeshIn->VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Normal);

	// enable attributes on output mesh
	MeshOut.EnableAttributes();
	FDynamicMeshUVOverlay* UVOverlay = MeshOut.Attributes()->PrimaryUV();
	FDynamicMeshNormalOverlay* NormalOverlay = MeshOut.Attributes()->PrimaryNormals();

	TPolygonAttributesConstRef<int> PolyGroups =
		MeshIn->PolygonAttributes().GetAttributesRef<int>(ExtendedMeshAttribute::PolyTriGroups);

	// base triangle groups will track polygons
	if (bEnableOutputGroups)
	{
		MeshOut.EnableTriangleGroups(0);
	}

	int NumVertices = MeshIn->Vertices().Num();
	int NumPolygons = MeshIn->Polygons().Num();
	int NumVtxInstances = MeshIn->VertexInstances().Num();
	if (bPrintDebugMessages)
	{
		UE_LOG(LogTemp, Warning, TEXT("FMeshDescriptionToDynamicMesh: MeshDescription verts %d polys %d instances %d"), NumVertices, NumPolygons, NumVtxInstances);
	}

	// reserve space in MeshOut?

	// used to merge coincident elements so that we get actual topology
	FUVWelder UVWelder(UVOverlay);
	FNormalWelder NormalWelder(NormalOverlay);

	const FPolygonArray& Polygons = MeshIn->Polygons();
	for (const FPolygonID PolygonID : Polygons.GetElementIDs())
	{
		FPolygonGroupID PolygonGroupID = MeshIn->GetPolygonPolygonGroup(PolygonID);

		const TArray<FMeshTriangle>& Triangles = MeshIn->GetPolygonTriangles(PolygonID);
		int NumTriangles = Triangles.Num();
		for ( int TriIdx = 0; TriIdx < NumTriangles; ++TriIdx)
		{
			const FMeshTriangle& Triangle = Triangles[TriIdx];
				
			FVertexInstanceID InstanceTri[3];
			InstanceTri[0] = Triangle.VertexInstanceID0; 
			InstanceTri[1] = Triangle.VertexInstanceID1; 
			InstanceTri[2] = Triangle.VertexInstanceID2;

			int GroupID = 0;
			if (GroupMode == EPrimaryGroupMode::SetToPolyGroup)
			{
				if (PolyGroups.IsValid())
				{
					GroupID = PolyGroups.Get(PolygonID, 0);
				}
			}
			else if (GroupMode == EPrimaryGroupMode::SetToPolygonID)
			{
				GroupID = PolygonID.GetValue();
			}
			else if (GroupMode == EPrimaryGroupMode::SetToPolygonGroupID)
			{
				GroupID = PolygonGroupID.GetValue();
			}
			

			// append triangle
			int VertexID0 = MeshIn->GetVertexInstance(Triangle.VertexInstanceID0).VertexID.GetValue();
			int VertexID1 = MeshIn->GetVertexInstance(Triangle.VertexInstanceID1).VertexID.GetValue();
			int VertexID2 = MeshIn->GetVertexInstance(Triangle.VertexInstanceID2).VertexID.GetValue();
			int NewTriangleID = MeshOut.AppendTriangle(VertexID0, VertexID1, VertexID2, GroupID);
			
			// if append failed due to non-manifold, duplicate verts
			if (NewTriangleID == FDynamicMesh3::NonManifoldID)
			{
				int e0 = MeshOut.FindEdge(VertexID0, VertexID1);
				int e1 = MeshOut.FindEdge(VertexID1, VertexID2);
				int e2 = MeshOut.FindEdge(VertexID2, VertexID0);

				// determine which verts need to be duplicated
				bool bDuplicate[3] = { false, false, false };
				if (e0 != FDynamicMesh3::InvalidID && MeshOut.IsBoundaryEdge(e0) == false)
				{
					bDuplicate[0] = true;
					bDuplicate[1] = true;
				}
				if (e1 != FDynamicMesh3::InvalidID && MeshOut.IsBoundaryEdge(e1) == false)
				{
					bDuplicate[1] = true;
					bDuplicate[2] = true;
				}
				if (e2 != FDynamicMesh3::InvalidID && MeshOut.IsBoundaryEdge(e2) == false)
				{
					bDuplicate[2] = true;
					bDuplicate[0] = true;
				}
				if (bDuplicate[0])
				{
					FVertexID VertexID = MeshIn->GetVertexInstance(Triangle.VertexInstanceID0).VertexID;
					const FVector Position = VertexPositions.Get(VertexID);
					int NewVertIdx = MeshOut.AppendVertex(Position);
					VertexID0 = NewVertIdx;
				}
				if (bDuplicate[1])
				{
					FVertexID VertexID = MeshIn->GetVertexInstance(Triangle.VertexInstanceID1).VertexID;
					const FVector Position = VertexPositions.Get(VertexID);
					int NewVertIdx = MeshOut.AppendVertex(Position);
					VertexID1 = NewVertIdx;
				}
				if (bDuplicate[2])
				{
					FVertexID VertexID = MeshIn->GetVertexInstance(Triangle.VertexInstanceID2).VertexID;
					const FVector Position = VertexPositions.Get(VertexID);
					int NewVertIdx = MeshOut.AppendVertex(Position);
					VertexID2 = NewVertIdx;
				}

				NewTriangleID = MeshOut.AppendTriangle(VertexID0, VertexID1, VertexID2, GroupID);
				checkSlow(NewTriangleID != FDynamicMesh3::NonManifoldID);
			}

			FIndex3i Tri(VertexID0, VertexID1, VertexID2);

			if (bCalculateMaps)
			{
				TriToPolyTriMap.Insert(FIndex2i(PolygonID.GetValue(), TriIdx), NewTriangleID);
			}

			if (UVOverlay != nullptr)
			{
				FIndex3i TriUV;
				for (int j = 0; j < 3; ++j)
				{
					FVector2D UV = InstanceUVs.Get(InstanceTri[j]);
					TriUV[j] = UVWelder.FindOrAddUnique(UV, Tri[j]);
				}
				UVOverlay->SetTriangle(NewTriangleID, TriUV);
			}

			if (NormalOverlay != nullptr)
			{
				FIndex3i TriNormals;
				for (int j = 0; j < 3; ++j)
				{
					FVector Normal = InstanceNormals.Get(InstanceTri[j]);
					TriNormals[j] = NormalWelder.FindOrAddUnique(Normal, Tri[j]);
				}
				NormalOverlay->SetTriangle(NewTriangleID, TriNormals);
			}

		}
	}

	if (bPrintDebugMessages)
	{
		int NumUVs = (UVOverlay != nullptr) ? UVOverlay->MaxElementID() : 0;
		int NumNormals = (NormalOverlay != nullptr) ? NormalOverlay->MaxElementID() : 0;
		UE_LOG(LogTemp, Warning, TEXT("FMeshDescriptionToDynamicMesh:  FDynamicMesh verts %d triangles %d uvs %d normals %d"), MeshOut.MaxVertexID(), MeshOut.MaxTriangleID(), NumUVs, NumNormals);
	}

}





void FMeshDescriptionToDynamicMesh::CopyTangents(const FMeshDescription* SourceMesh, const FDynamicMesh3* TargetMesh, FMeshTangentsf& TangentsOut)
{
	check(bCalculateMaps == true);
	check(TriToPolyTriMap.Num() == TargetMesh->TriangleCount());

	TVertexInstanceAttributesConstRef<FVector> InstanceNormals =
		SourceMesh->VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Normal);
	TVertexInstanceAttributesConstRef<FVector> InstanceTangents =
		SourceMesh->VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Tangent);
	TVertexInstanceAttributesConstRef<float> InstanceSigns =
		SourceMesh->VertexInstanceAttributes().GetAttributesRef<float>(MeshAttribute::VertexInstance::BinormalSign);
	check(InstanceNormals.IsValid());
	check(InstanceTangents.IsValid());
	check(InstanceSigns.IsValid());

	TangentsOut.SetMesh(TargetMesh);
	TangentsOut.InitializePerTriangleTangents(false);
	
	for (int TriID : TargetMesh->TriangleIndicesItr())
	{
		FIndex2i PolyTriIdx = TriToPolyTriMap[TriID];
		const TArray<FMeshTriangle>& Triangles = SourceMesh->GetPolygonTriangles( FPolygonID(PolyTriIdx.A) );
		const FMeshTriangle& Triangle = Triangles[PolyTriIdx.B];
		FVertexInstanceID InstanceTri[3];
		InstanceTri[0] = Triangle.VertexInstanceID0;
		InstanceTri[1] = Triangle.VertexInstanceID1;
		InstanceTri[2] = Triangle.VertexInstanceID2;
		for (int j = 0; j < 3; ++j)
		{
			FVector Normal = InstanceNormals.Get(InstanceTri[j], 0);
			FVector Tangent = InstanceTangents.Get(InstanceTri[j], 0);
			float BinormalSign = InstanceSigns.Get(InstanceTri[j], 0);
			FVector3f Bitangent = VectorUtil::Binormal((FVector3f)Normal, (FVector3f)Tangent, (float)BinormalSign);
			Tangent.Normalize(); Bitangent.Normalize();
			TangentsOut.SetPerTriangleTangent(TriID, j, Tangent, Bitangent);
		}
	}

}