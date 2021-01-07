// Copyright Epic Games, Inc. All Rights Reserved. 

#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMeshAttributeSet.h"
#include "DynamicMeshOverlay.h"
#include "MeshTangents.h"
#include "MeshDescriptionBuilder.h"
#include "StaticMeshAttributes.h"
#include "Async/Async.h"


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

	FUVWelder() : UVOverlay(nullptr)
	{
	}

	FUVWelder(FDynamicMeshUVOverlay* UVOverlayIn)
	{
		check(UVOverlayIn);
		UVOverlay = UVOverlayIn;
	}

	int FindOrAddUnique(const FVector2D& UV, int VertexID)
	{
		FVertexUV VertUV = { VertexID, UV.X, UV.Y };

		const int32* FoundIndex = UniqueVertexUVs.Find(VertUV);
		if (FoundIndex != nullptr)
		{
			return *FoundIndex;
		}

		int32 NewIndex = UVOverlay->AppendElement(FVector2f(UV));
		UniqueVertexUVs.Add(VertUV, NewIndex);
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

	FNormalWelder() : NormalOverlay(nullptr)
	{
	}

	FNormalWelder(FDynamicMeshNormalOverlay* NormalOverlayIn)
	{
		check(NormalOverlayIn);
		NormalOverlay = NormalOverlayIn;
	}

	int FindOrAddUnique(const FVector & Normal, int VertexID)
	{
		FVertexNormal VertNormal = { VertexID, Normal.X, Normal.Y, Normal.Z };

		const int32* FoundIndex = UniqueVertexNormals.Find(VertNormal);
		if (FoundIndex != nullptr)
		{
			return *FoundIndex;
		}

		int32 NewIndex = NormalOverlay->AppendElement(Normal);
		UniqueVertexNormals.Add(VertNormal, NewIndex);
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

	TPolygonAttributesConstRef<int> PolyGroups =
		MeshIn->PolygonAttributes().GetAttributesRef<int>(ExtendedMeshAttribute::PolyTriGroups);
	if (bEnableOutputGroups)
	{
		MeshOut.EnableTriangleGroups(0);
	}

	// base triangle groups will track polygons
	int NumVertices = MeshIn->Vertices().Num();
	int NumPolygons = MeshIn->Polygons().Num();
	int NumVtxInstances = MeshIn->VertexInstances().Num();
	if (bPrintDebugMessages)
	{
		UE_LOG(LogTemp, Warning, TEXT("FMeshDescriptionToDynamicMesh: MeshDescription verts %d polys %d instances %d"), NumVertices, NumPolygons, NumVtxInstances);
	}

	FDateTime Time_AfterVertices = FDateTime::Now();

	// Although it is slightly redundant, we will build up this list of MeshDescription data
	// so that it is easier to index into it below (profile whether extra memory here hurts us?)
	struct FTriData
	{
		FPolygonID PolygonID;
		int32 PolygonGroupID;
		int32 TriIndex;
		FVertexInstanceID TriInstances[3];
	};
	TArray<FTriData> AddedTriangles;
	AddedTriangles.SetNum(MeshIn->Triangles().Num());

	// NOTE: If you change the iteration order here, please update the corresponding iteration in FDynamicMeshToMeshDescription::UpdateAttributes, 
	//	which assumes the iteration order here is polygons -> triangles, to correspond the triangles when writing updated attributes back!
	const FPolygonArray& Polygons = MeshIn->Polygons();
	for (const FPolygonID PolygonID : Polygons.GetElementIDs())
	{
		int32 PolygonGroupID = MeshIn->GetPolygonPolygonGroup(PolygonID).GetValue();

		const TArray<FTriangleID>& TriangleIDs = MeshIn->GetPolygonTriangleIDs(PolygonID);
		int NumTriangles = TriangleIDs.Num();
		for (int TriIdx = 0; TriIdx < NumTriangles; ++TriIdx)
		{
			FTriData TriData;
			TriData.PolygonID = PolygonID;
			TriData.PolygonGroupID = PolygonGroupID;
			TriData.TriIndex = TriIdx;

			TArrayView<const FVertexInstanceID> InstanceTri = MeshIn->GetTriangleVertexInstances(TriangleIDs[TriIdx]);
			TriData.TriInstances[0] = InstanceTri[0];
			TriData.TriInstances[1] = InstanceTri[1];
			TriData.TriInstances[2] = InstanceTri[2];

			int GroupID = 0;
			switch (GroupMode)
			{
			case EPrimaryGroupMode::SetToPolyGroup:
				if (PolyGroups.IsValid())
				{
					GroupID = PolyGroups.Get(PolygonID, 0);
				}
				break;
			case EPrimaryGroupMode::SetToPolygonID:
				GroupID = PolygonID.GetValue() + 1; // Shift IDs up by 1 to leave ID 0 as a default/unassigned group
				break;
			case EPrimaryGroupMode::SetToPolygonGroupID:
				GroupID = PolygonGroupID + 1; // Shift IDs up by 1 to leave ID 0 as a default/unassigned group
				break;
			case EPrimaryGroupMode::SetToZero:
				break; // keep at 0
			}

			// append triangle
			int32 VertexID0 = MeshIn->GetVertexInstanceVertex(InstanceTri[0]).GetValue();
			int32 VertexID1 = MeshIn->GetVertexInstanceVertex(InstanceTri[1]).GetValue();
			int32 VertexID2 = MeshIn->GetVertexInstanceVertex(InstanceTri[2]).GetValue();
			int NewTriangleID = MeshOut.AppendTriangle(VertexID0, VertexID1, VertexID2, GroupID);

			if (NewTriangleID == FDynamicMesh3::DuplicateTriangleID)
			{
				continue;
			}

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
					FVertexID VertexID = MeshIn->GetVertexInstanceVertex(InstanceTri[0]);
					const FVector Position = VertexPositions.Get(VertexID);
					int NewVertIdx = MeshOut.AppendVertex(Position);
					VertexID0 = NewVertIdx;
				}
				if (bDuplicate[1])
				{
					FVertexID VertexID = MeshIn->GetVertexInstanceVertex(InstanceTri[1]);
					const FVector Position = VertexPositions.Get(VertexID);
					int NewVertIdx = MeshOut.AppendVertex(Position);
					VertexID1 = NewVertIdx;
				}
				if (bDuplicate[2])
				{
					FVertexID VertexID = MeshIn->GetVertexInstanceVertex(InstanceTri[2]);
					const FVector Position = VertexPositions.Get(VertexID);
					int NewVertIdx = MeshOut.AppendVertex(Position);
					VertexID2 = NewVertIdx;
				}

				NewTriangleID = MeshOut.AppendTriangle(VertexID0, VertexID1, VertexID2, GroupID);
				checkSlow(NewTriangleID != FDynamicMesh3::NonManifoldID);
			}

			checkSlow(NewTriangleID >= 0);
			AddedTriangles[NewTriangleID] = TriData;
		}
	}

	FDateTime Time_AfterTriangles = FDateTime::Now();

	//
	// Enable relevant attributes and initialize UV/Normal welders
	// 

	int NumUVLayers = InstanceUVs.GetNumIndices();
	TArray<FDynamicMeshUVOverlay*> UVOverlays;
	TArray<FUVWelder> UVWelders;
	FDynamicMeshNormalOverlay* NormalOverlay = nullptr;
	FNormalWelder NormalWelder;
	FDynamicMeshMaterialAttribute* MaterialIDAttrib = nullptr;
	if (!bDisableAttributes)
	{
		MeshOut.EnableAttributes();

		MeshOut.Attributes()->SetNumUVLayers(NumUVLayers);
		UVOverlays.SetNum(NumUVLayers);
		UVWelders.SetNum(NumUVLayers);
		for (int j = 0; j < NumUVLayers; ++j)
		{
			UVOverlays[j] = MeshOut.Attributes()->GetUVLayer(j);
			UVWelders[j].UVOverlay = UVOverlays[j];
		}

		NormalOverlay = MeshOut.Attributes()->PrimaryNormals();
		NormalWelder.NormalOverlay = NormalOverlay;

		// always enable Material ID if there are any attributes
		MeshOut.Attributes()->EnableMaterialID();
		MaterialIDAttrib = MeshOut.Attributes()->GetMaterialID();
	}


	// we will weld/populate all the attributes simultaneously, hold on to futures in this array and then Wait for them at the end
	TArray<TFuture<void>> Pending;

	if (bCalculateMaps)
	{
		auto Future = Async(EAsyncExecution::ThreadPool, [&]()
		{
			for (int32 TriangleID : MeshOut.TriangleIndicesItr())
			{
				const FTriData& TriData = AddedTriangles[TriangleID];
				TriToPolyTriMap.Insert(FIndex2i(TriData.PolygonID.GetValue(), TriData.TriIndex), TriangleID);
			}
		});
		Pending.Add(MoveTemp(Future));
	}

	if (!bDisableAttributes)
	{
		for (int UVLayerIndex = 0; UVLayerIndex < NumUVLayers; UVLayerIndex++)
		{
			auto UVFuture = Async(EAsyncExecution::ThreadPool, [&, UVLayerIndex]() // must copy UVLayerIndex here!
			{
				for (int32 TriangleID : MeshOut.TriangleIndicesItr())
				{
					FIndex3i Tri = MeshOut.GetTriangle(TriangleID);
					const FTriData& TriData = AddedTriangles[TriangleID];
					FIndex3i TriUV;
					for (int j = 0; j < 3; ++j)
					{
						FVector2D UV = InstanceUVs.Get(TriData.TriInstances[j], UVLayerIndex);
						TriUV[j] = UVWelders[UVLayerIndex].FindOrAddUnique(UV, Tri[j]);
					}
					UVOverlays[UVLayerIndex]->SetTriangle(TriangleID, TriUV);
				}
			});
			Pending.Add(MoveTemp(UVFuture));
		}


		if (NormalOverlay != nullptr)
		{
			auto NormalFuture = Async(EAsyncExecution::ThreadPool, [&]()
			{
				for (int32 TriangleID : MeshOut.TriangleIndicesItr())
				{
					FIndex3i Tri = MeshOut.GetTriangle(TriangleID);
					const FTriData& TriData = AddedTriangles[TriangleID];
					FIndex3i TriNormals;
					for (int j = 0; j < 3; ++j)
					{
						FVector Normal = InstanceNormals.Get(TriData.TriInstances[j]);
						TriNormals[j] = NormalWelder.FindOrAddUnique(Normal, Tri[j]);
					}
					NormalOverlay->SetTriangle(TriangleID, TriNormals);
				}
			});
			Pending.Add(MoveTemp(NormalFuture));
		}


		if (MaterialIDAttrib != nullptr)
		{
			auto MaterialFuture = Async(EAsyncExecution::ThreadPool, [&]()
			{
				for (int32 TriangleID : MeshOut.TriangleIndicesItr())
				{
					const FTriData& TriData = AddedTriangles[TriangleID];
					MaterialIDAttrib->SetValue(TriangleID, &TriData.PolygonGroupID);
				}
			});
			Pending.Add(MoveTemp(MaterialFuture));
		}
	}

	// wait for all work to be done
	for (TFuture<void>& Future : Pending)
	{
		Future.Wait();
	}

	FDateTime Time_AfterAttribs = FDateTime::Now();

	if (bPrintDebugMessages)
	{
		UE_LOG(LogTemp, Warning, TEXT("FMeshDescriptionToDynamicMesh:  Conversion Timing: Triangles %fs   Attributbes %fs"),
			(Time_AfterTriangles - Time_AfterVertices).GetTotalSeconds(), (Time_AfterAttribs - Time_AfterTriangles).GetTotalSeconds());

		int NumUVs = (MeshOut.HasAttributes() && NumUVLayers > 0) ? MeshOut.Attributes()->PrimaryUV()->MaxElementID() : 0;
		int NumNormals = (NormalOverlay != nullptr) ? NormalOverlay->MaxElementID() : 0;
		UE_LOG(LogTemp, Warning, TEXT("FMeshDescriptionToDynamicMesh:  FDynamicMesh verts %d triangles %d (primary) uvs %d normals %d"), MeshOut.MaxVertexID(), MeshOut.MaxTriangleID(), NumUVs, NumNormals);
	}

}





void FMeshDescriptionToDynamicMesh::CopyTangents(const FMeshDescription* SourceMesh, const FDynamicMesh3* TargetMesh, TMeshTangents<float>* TangentsOut)
{
	if (!ensureMsgf(bCalculateMaps, TEXT("Cannot CopyTangents unless Maps were calculated"))) return;
	if (!ensureMsgf(TriToPolyTriMap.Num() == TargetMesh->TriangleCount(), TEXT("Tried to CopyTangents to mesh with different triangle count"))) return;

	TVertexInstanceAttributesConstRef<FVector> InstanceNormals =
		SourceMesh->VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Normal);
	TVertexInstanceAttributesConstRef<FVector> InstanceTangents =
		SourceMesh->VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Tangent);
	TVertexInstanceAttributesConstRef<float> InstanceSigns =
		SourceMesh->VertexInstanceAttributes().GetAttributesRef<float>(MeshAttribute::VertexInstance::BinormalSign);

	if (!ensureMsgf(InstanceNormals.IsValid(), TEXT("Cannot CopyTangents from MeshDescription with invalid Instance Normals"))) return;
	if (!ensureMsgf(InstanceTangents.IsValid(), TEXT("Cannot CopyTangents from MeshDescription with invalid Instance Tangents"))) return;
	if (!ensureMsgf(InstanceSigns.IsValid(), TEXT("Cannot CopyTangents from MeshDescription with invalid Instance BinormalSigns"))) return;

	TangentsOut->SetMesh(TargetMesh);
	TangentsOut->InitializeTriVertexTangents(false);
	
	for (int32 TriID : TargetMesh->TriangleIndicesItr())
	{
		FIndex2i PolyTriIdx = TriToPolyTriMap[TriID];
		const TArray<FTriangleID>& TriangleIDs = SourceMesh->GetPolygonTriangleIDs( FPolygonID(PolyTriIdx.A) );
		const FTriangleID TriangleID = TriangleIDs[PolyTriIdx.B];
		TArrayView<const FVertexInstanceID> InstanceTri = SourceMesh->GetTriangleVertexInstances(TriangleID);
		for (int32 j = 0; j < 3; ++j)
		{
			FVector Normal = InstanceNormals.Get(InstanceTri[j], 0);
			FVector Tangent = InstanceTangents.Get(InstanceTri[j], 0);
			float BitangentSign = InstanceSigns.Get(InstanceTri[j], 0);
			FVector3f Bitangent = VectorUtil::Bitangent((FVector3f)Normal, (FVector3f)Tangent, (float)BitangentSign);
			Tangent.Normalize(); Bitangent.Normalize();
			TangentsOut->SetPerTriangleTangent(TriID, j, Tangent, Bitangent);
		}
	}

}




