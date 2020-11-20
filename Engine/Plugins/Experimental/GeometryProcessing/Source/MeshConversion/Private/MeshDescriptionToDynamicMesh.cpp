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
	TriIDMap.Reset();
	VertIDMap.Reset();

	// allocate the VertIDMap.  Unfortunately the array will need to grow more if MeshIn has non-manifold edges that need to be split
	VertIDMap.SetNumUninitialized(MeshIn->Vertices().Num());

	// look up vertex positions
	TVertexAttributesConstRef<FVector> VertexPositions = MeshIn->GetVertexPositions();

	// copy vertex positions. Later we may have to append duplicate vertices to resolve non-manifold structures.
	for (const FVertexID VertexID : MeshIn->Vertices().GetElementIDs())
	{
		const FVector Position = VertexPositions.Get(VertexID);
		int NewVertIdx = MeshOut.AppendVertex(Position);
		VertIDMap[NewVertIdx] = VertexID;
	}

	// look up vertex-instance UVs and normals
	// @todo: does the MeshDescription always have UVs and Normals?
	FStaticMeshConstAttributes Attributes(*MeshIn);
	TVertexInstanceAttributesConstRef<FVector2D> InstanceUVs = Attributes.GetVertexInstanceUVs();
	TVertexInstanceAttributesConstRef<FVector> InstanceNormals = Attributes.GetVertexInstanceNormals();

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
		FVertexInstanceID TriInstances[3];
	};
	TArray<FTriData> AddedTriangles;
	AddedTriangles.SetNum(MeshIn->Triangles().Num());

	// allocate the TriIDMap
	TriIDMap.SetNumUninitialized(MeshIn->Triangles().Num());


	// Iterate over triangles in the Mesh Description
	// NOTE: If you change the iteration order here, please update the corresponding iteration in FDynamicMeshToMeshDescription::UpdateAttributes, 
	//	which assumes the iteration order here is the same as here to correspond the triangles when writing updated attributes back!
	for (const FTriangleID TriangleID : MeshIn->Triangles().GetElementIDs())
	{

		// Get the PolygonID, PolygonGroupID and general GroupID
		//---
		FPolygonID PolygonID = MeshIn->GetTrianglePolygon(TriangleID);
		FPolygonGroupID PolygonGroupID = MeshIn->GetTrianglePolygonGroup(TriangleID);

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
				GroupID = PolygonGroupID.GetValue() + 1; // Shift IDs up by 1 to leave ID 0 as a default/unassigned group
				break;
			case EPrimaryGroupMode::SetToZero:
				break; // keep at 0
		}
		
		FTriData TriData;
		TriData.PolygonID = PolygonID;
		TriData.PolygonGroupID = PolygonGroupID;
		

		// stash the vertex instance IDs for this triangle.  potentially needed for per-instance attribute welding
		TArrayView<const FVertexInstanceID> InstanceTri = MeshIn->GetTriangleVertexInstances(TriangleID);
		TriData.TriInstances[0] = InstanceTri[0];
		TriData.TriInstances[1] = InstanceTri[1];
		TriData.TriInstances[2] = InstanceTri[2];

		//---

		// Get the vertex IDs for this triangle.
		TArrayView<const FVertexID> TriangleVertexIDs = MeshIn->GetTriangleVertices(TriangleID);

		// sanity
		checkSlow(TriangleVertexIDs.Num() == 3);

		FIndex3i VertexIDs; 
		VertexIDs[0] = TriangleVertexIDs[0].GetValue();
		VertexIDs[1] = TriangleVertexIDs[1].GetValue(); 
		VertexIDs[2] = TriangleVertexIDs[2].GetValue();

		int NewTriangleID = MeshOut.AppendTriangle(VertexIDs, GroupID);

		// Deal with potential failure cases

		//-- already seen this triangle for some reason.. or the MeshDecription had a degenerate tri
		if (NewTriangleID == FDynamicMesh3::DuplicateTriangleID || NewTriangleID == FDynamicMesh3::InvalidID)
		{
			continue;
		}

		//-- non manifold 
		// if append failed due to non-manifold, duplicate verts
		if (NewTriangleID == FDynamicMesh3::NonManifoldID)
		{
			int e0 = MeshOut.FindEdge(VertexIDs[0], VertexIDs[1]);
			int e1 = MeshOut.FindEdge(VertexIDs[1], VertexIDs[2]);
			int e2 = MeshOut.FindEdge(VertexIDs[2], VertexIDs[0]);

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
				const FVector Position = VertexPositions[TriangleVertexIDs[0]];
				const int32 NewVertIdx = MeshOut.AppendVertex(Position);
				VertexIDs[0] = NewVertIdx;
				VertIDMap.Insert(TriangleVertexIDs[0], NewVertIdx); // note 'insert', because the array may need to grow
			}
			if (bDuplicate[1])
			{
				const FVector Position = VertexPositions[TriangleVertexIDs[1]];
				const int32 NewVertIdx = MeshOut.AppendVertex(Position);
				VertexIDs[1] = NewVertIdx;
				VertIDMap.Insert(TriangleVertexIDs[1], NewVertIdx);
			}
			if (bDuplicate[2])
			{
				const FVector Position = VertexPositions[TriangleVertexIDs[2]];
				const int32 NewVertIdx = MeshOut.AppendVertex(Position);
				VertexIDs[2] = NewVertIdx;
				VertIDMap.Insert(TriangleVertexIDs[2], NewVertIdx);
			}

			NewTriangleID = MeshOut.AppendTriangle(VertexIDs, GroupID);
			checkSlow(NewTriangleID != FDynamicMesh3::NonManifoldID);
		}

		checkSlow(NewTriangleID >= 0);
		AddedTriangles[NewTriangleID] = TriData;
		TriIDMap[NewTriangleID] = TriangleID;
	
	}

	FDateTime Time_AfterTriangles = FDateTime::Now();


	//
	// Enable relevant attributes and initialize UV/Normal welders
	// 

	// the shared UV representation.  
	const int32 NumUVElementChannels = MeshIn->GetNumUVElementChannels();

	// the instanced UV representation.
	const int NumUVLayers = InstanceUVs.GetNumChannels();

	// determine if we really have shared UVS. Legacy geo might not have them 
	// - at the time of this writing MeshDescription has not been updated
	// to always populated the shared UVs during load time for legacy geometry that only has per-instance UVs. 
	// but that is a promised improvement. 

	bool bUseSharedUVs =  (NumUVLayers == NumUVElementChannels);
	if (bUseSharedUVs)
	{
		for (int UVLayerIndex = 0; UVLayerIndex < NumUVLayers; ++UVLayerIndex)
		{
			const int32 NumSharedUVs   = MeshIn->UVs(UVLayerIndex).GetArraySize();
			bUseSharedUVs = bUseSharedUVs && (NumSharedUVs != 0);
		}
	}


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


	

	if (!bDisableAttributes)
	{
		// we will weld/populate all the attributes simultaneously, hold on to futures in this array and then Wait for them at the end
		TArray<TFuture<void>> Pending;

		for (int UVLayerIndex = 0; UVLayerIndex < NumUVLayers; UVLayerIndex++)
		{
			auto UVFuture = Async(EAsyncExecution::ThreadPool, [&, UVLayerIndex, bUseSharedUVs]() // must copy UVLayerIndex here!
			{

				if (!bUseSharedUVs) // have to rely on welding the per-instance uvs 
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
				}
				else
				{
					// the overlay to fill.
					FDynamicMeshUVOverlay* UVOverlay = UVOverlays[UVLayerIndex];

					// copy uv "vertex buffer"
					const FUVArray& UVs = MeshIn->UVs(UVLayerIndex);
					TUVAttributesRef<const FVector2D> UVCoordinates = UVs.GetAttributes().GetAttributesRef<FVector2D>(MeshAttribute::UV::UVCoordinate);
						
					// map to translate UVIds from FUVID::int32() to DynamicOverlay Index.
					TArray<int32> UVIndexMap;
					UVIndexMap.Reserve(UVs.GetArraySize() + 1);

					for (FUVID UVID : UVs.GetElementIDs())
					{
						const FVector2D UVvalue = UVCoordinates[UVID];
						int32 NewIndex = UVOverlay->AppendElement(FVector2f(UVvalue));

						// forced to use Insert since this array might have to resize (unfortunately we can only guess the max UVID in the mesh description)
						UVIndexMap.Insert(NewIndex, UVID.GetValue());

					}

					// copy uv "index buffer"
					for (int32 TriID : MeshOut.TriangleIndicesItr())
					{
						FTriangleID TriangleID = TriIDMap[TriID];
						// NB: the mesh description lacks a const method variant of this function, hence the const_cast. 
						// Don't change the UVIndices values!
						TArrayView<FUVID> UVIndices = const_cast<FMeshDescription*>(MeshIn)->GetTriangleUVIndices(TriangleID, UVLayerIndex);

						// translate to Overlay indicies 
						FIndex3i TriUV( UVIndexMap[UVIndices[0].GetValue()],
							            UVIndexMap[UVIndices[1].GetValue()],
							            UVIndexMap[UVIndices[2].GetValue()] );
							
						///--  We have to do some clean-up on the shared UVs that come from MeshDecription --///
						// This clean up should go away if MeshDescription can solve these problems during import from the source fbx files
						{
							// MeshDescription can attach multiple vertices to the same UV element.  DynamicMesh does not.
							// if we have already used this element for a different mesh vertex, split it.
							const FIndex3i ParentTriangle = MeshOut.GetTriangle(TriID);
							for (int i = 0; i < 3; ++i)
							{
								int32 ParentVID = UVOverlay->GetParentVertex(TriUV[i]);
								if (ParentVID != FDynamicMesh3::InvalidID && ParentVID != ParentTriangle[i])
								{
									const FVector2D UVvalue = UVCoordinates[UVIndices[1]];
									TriUV[i] = UVIndexMap.Add(UVOverlay->AppendElement(FVector2f(UVvalue)));
								}
							}

							// MeshDescription allows for degenerate UV tris.  Dynamic Mesh does not.
							// if the UV tri is degenerate we split the degenerate UV edge by adding two new UVs
							// in its place, or if it is totally degenerate we add 3 new UVs

							if (TriUV[0] == TriUV[1] && TriUV[0] == TriUV[2])
							{
								const FVector2D UVvalue = UVCoordinates[UVIndices[1]];
								TriUV[0] = UVIndexMap.Add(UVOverlay->AppendElement(FVector2f(UVvalue)));
								TriUV[1] = UVIndexMap.Add(UVOverlay->AppendElement(FVector2f(UVvalue)));
								TriUV[2] = UVIndexMap.Add(UVOverlay->AppendElement(FVector2f(UVvalue)));

							}
							else
							{
								if (TriUV[0] == TriUV[1])
								{
									const FVector2D UVvalue = UVCoordinates[UVIndices[0]];
									TriUV[0] = UVIndexMap.Add(UVOverlay->AppendElement(FVector2f(UVvalue)));
									TriUV[1] = UVIndexMap.Add(UVOverlay->AppendElement(FVector2f(UVvalue)));
								}
								if (TriUV[0] == TriUV[2])
								{
									const FVector2D UVvalue = UVCoordinates[UVIndices[0]];
									TriUV[0] = UVIndexMap.Add(UVOverlay->AppendElement(FVector2f(UVvalue)));
									TriUV[2] = UVIndexMap.Add(UVOverlay->AppendElement(FVector2f(UVvalue)));
								}
								if (TriUV[1] == TriUV[2])
								{
									const FVector2D UVvalue = UVCoordinates[UVIndices[1]];
									TriUV[1] = UVIndexMap.Add(UVOverlay->AppendElement(FVector2f(UVvalue)));
									TriUV[2] = UVIndexMap.Add(UVOverlay->AppendElement(FVector2f(UVvalue)));
								}
							}
						}

						// set the triangle in the overlay
						UVOverlay->SetTriangle(TriID, TriUV);
					}
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

		// wait for all work to be done
		for (TFuture<void>& Future : Pending)
		{
			Future.Wait();
		}
	}

	// free maps if no longer needed
	if (!bCalculateMaps)
	{
		TriIDMap.Empty();
		VertIDMap.Empty();
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



template<typename RealType>
static void CopyTangents_Internal(const FMeshDescription* SourceMesh, const FDynamicMesh3* TargetMesh, TMeshTangents<RealType>* TangentsOut, const TArray<FTriangleID>& TriIDMap)
{

	FStaticMeshConstAttributes Attributes(*SourceMesh);

	TArrayView<const FVector> InstanceNormals = Attributes.GetVertexInstanceNormals().GetRawArray();
	TArrayView<const FVector> InstanceTangents = Attributes.GetVertexInstanceTangents().GetRawArray();
	TArrayView<const float> InstanceSigns = Attributes.GetVertexInstanceBinormalSigns().GetRawArray();

	if (!ensureMsgf(InstanceNormals.Num() != 0, TEXT("Cannot CopyTangents from MeshDescription with invalid Instance Normals"))) return;
	if (!ensureMsgf(InstanceTangents.Num() != 0, TEXT("Cannot CopyTangents from MeshDescription with invalid Instance Tangents"))) return;
	if (!ensureMsgf(InstanceSigns.Num() != 0, TEXT("Cannot CopyTangents from MeshDescription with invalid Instance BinormalSigns"))) return;

	TangentsOut->SetMesh(TargetMesh);
	TangentsOut->InitializeTriVertexTangents(false);
	
	for (int32 TriID : TargetMesh->TriangleIndicesItr())
	{

		const FTriangleID TriangleID = TriIDMap[TriID];
		TArrayView<const FVertexInstanceID> InstanceTri = SourceMesh->GetTriangleVertexInstances(TriangleID);
		for (int32 j = 0; j < 3; ++j)
		{
			FVector Normal = InstanceNormals[InstanceTri[j]];
			FVector Tangent = InstanceTangents[InstanceTri[j]];
			float BitangentSign = InstanceSigns[InstanceTri[j]];
			FVector3<RealType> Bitangent = VectorUtil::Bitangent((FVector3<RealType>)Normal, (FVector3<RealType>)Tangent, (RealType)BitangentSign);
			Tangent.Normalize(); Bitangent.Normalize();
			TangentsOut->SetPerTriangleTangent(TriID, j, Tangent, Bitangent);
		}
	}
}



void FMeshDescriptionToDynamicMesh::CopyTangents(const FMeshDescription* SourceMesh, const FDynamicMesh3* TargetMesh, TMeshTangents<float>* TangentsOut)
{
	if (!ensureMsgf(bCalculateMaps, TEXT("Cannot CopyTangents unless Maps were calculated"))) return;
	if (!ensureMsgf(TriIDMap.Num() == TargetMesh->TriangleCount(), TEXT("Tried to CopyTangents to mesh with different triangle count"))) return;
	CopyTangents_Internal<float>(SourceMesh, TargetMesh, TangentsOut, TriIDMap);
}



void FMeshDescriptionToDynamicMesh::CopyTangents(const FMeshDescription* SourceMesh, const FDynamicMesh3* TargetMesh, TMeshTangents<double>* TangentsOut)
{
	if (!ensureMsgf(bCalculateMaps, TEXT("Cannot CopyTangents unless Maps were calculated"))) return;
	if (!ensureMsgf(TriIDMap.Num() == TargetMesh->TriangleCount(), TEXT("Tried to CopyTangents to mesh with different triangle count"))) return;
	CopyTangents_Internal<double>(SourceMesh, TargetMesh, TangentsOut, TriIDMap);
}


