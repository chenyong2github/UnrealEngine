// Copyright Epic Games, Inc. All Rights Reserved.

#include "ProxyLODMeshTypes.h"
#include "MeshDescriptionOperations.h"

// --- FMeshDescriptionAdapter ----

FMeshDescriptionAdapter::FMeshDescriptionAdapter(const FMeshDescription& InRawMesh, const openvdb::math::Transform& InTransform) :
	RawMesh(&InRawMesh), Transform(InTransform)
{
	InitializeCacheData();
}

FMeshDescriptionAdapter::FMeshDescriptionAdapter(const FMeshDescriptionAdapter& other)
	: RawMesh(other.RawMesh), Transform(other.Transform)
{
	InitializeCacheData();
}

void FMeshDescriptionAdapter::InitializeCacheData()
{
	VertexPositions = RawMesh->VertexAttributes().GetAttributesRef<FVector>(MeshAttribute::Vertex::Position);
	TriangleCount = RawMesh->Triangles().Num();

	IndexBuffer.Reserve(TriangleCount * 3);

	for (const FPolygonID PolygonID : RawMesh->Polygons().GetElementIDs())
	{
		const TArray<FTriangleID>& TriangleIDs = RawMesh->GetPolygonTriangleIDs(PolygonID);
		for (const FTriangleID TriangleID : TriangleIDs)
		{
			IndexBuffer.Add(RawMesh->GetTriangleVertexInstance(TriangleID, 0));
			IndexBuffer.Add(RawMesh->GetTriangleVertexInstance(TriangleID, 1));
			IndexBuffer.Add(RawMesh->GetTriangleVertexInstance(TriangleID, 2));
		}
	}


}

size_t FMeshDescriptionAdapter::polygonCount() const
{
	return size_t(TriangleCount);
}

size_t FMeshDescriptionAdapter::pointCount() const
{
	return size_t(RawMesh->Vertices().Num());
}

void FMeshDescriptionAdapter::getIndexSpacePoint(size_t FaceNumber, size_t CornerNumber, openvdb::Vec3d& pos) const
{
	// Get the vertex position in local space.
	const FVertexInstanceID VertexInstanceID = IndexBuffer[FaceNumber * 3 + CornerNumber];
	// float3 position 
	const FVertexID VertexID = RawMesh->GetVertexInstanceVertex(VertexInstanceID);


	FVector Position = VertexPositions[VertexID];
	pos = Transform.worldToIndex(openvdb::Vec3d(Position.X, Position.Y, Position.Z));
};


// --- FMeshDescriptionArrayAdapter ----
FMeshDescriptionArrayAdapter::FMeshDescriptionArrayAdapter(const TArray<const FMeshMergeData*>& InMergeDataPtrArray)
{
	// Make a default transform.
	Transform = openvdb::math::Transform::createLinearTransform(1.);

	PointCount = 0;
	PolyCount = 0;

	PolyOffsetArray.push_back(PolyCount);
	for (int32 MeshIdx = 0, MeshCount = InMergeDataPtrArray.Num(); MeshIdx < MeshCount; ++MeshIdx)
	{
		const FMeshMergeData* MergeData = InMergeDataPtrArray[MeshIdx];
		FMeshDescription *RawMesh = MergeData->RawMesh;

		PointCount += size_t(RawMesh->Vertices().Num());
		// Sum up all the polys in this mesh.
		int32 MeshPolyCount = RawMesh->Triangles().Num();

		// Construct local index buffer for this mesh
		IndexBufferArray.push_back(std::vector<FVertexInstanceID>());
		std::vector<FVertexInstanceID>& IndexBuffer = IndexBufferArray[MeshIdx];
		IndexBuffer.reserve(MeshPolyCount * 3);
		for (const FPolygonID PolygonID : RawMesh->Polygons().GetElementIDs())
		{
			const TArray<FTriangleID>& TriangleIDs = RawMesh->GetPolygonTriangleIDs(PolygonID);
			for (const FTriangleID TriangleID : TriangleIDs)
			{
				IndexBuffer.push_back(RawMesh->GetTriangleVertexInstance(TriangleID, 0));
				IndexBuffer.push_back(RawMesh->GetTriangleVertexInstance(TriangleID, 1));
				IndexBuffer.push_back(RawMesh->GetTriangleVertexInstance(TriangleID, 2));
			}
		}


		PolyCount += MeshPolyCount;
		PolyOffsetArray.push_back(PolyCount);
		RawMeshArray.push_back(RawMesh);
		RawMeshArrayData.push_back(FMeshDescriptionAttributesGetter(RawMesh));
		MergeDataArray.push_back(MergeData);
	}

	// Compute the bbox
	ComputeAABB(this->BBox);
}

FMeshDescriptionArrayAdapter::FMeshDescriptionArrayAdapter(const TArray<FMeshMergeData>& InMergeDataArray)
{
	// Make a default transform.
	Transform = openvdb::math::Transform::createLinearTransform(1.);
	
	PointCount = 0;
	PolyCount = 0;

	PolyOffsetArray.push_back(PolyCount);
	for (int32 MeshIdx = 0, MeshCount = InMergeDataArray.Num(); MeshIdx < MeshCount; ++MeshIdx)
	{
		const FMeshMergeData* MergeData = &InMergeDataArray[MeshIdx];
		
		FMeshDescription *RawMesh = MergeData->RawMesh;
		PointCount += size_t(RawMesh->Vertices().Num());

		// Sum up all the polys in this mesh.
		int32 MeshPolyCount = RawMesh->Triangles().Num();
		PolyCount += MeshPolyCount;
		// Construct local index buffer for this mesh
		IndexBufferArray.push_back(std::vector<FVertexInstanceID>());
		std::vector<FVertexInstanceID>& IndexBuffer = IndexBufferArray[MeshIdx];
		IndexBuffer.reserve(MeshPolyCount * 3);
		for (const FPolygonID PolygonID : RawMesh->Polygons().GetElementIDs())
		{
			const TArray<FTriangleID>& TriangleIDs = RawMesh->GetPolygonTriangleIDs(PolygonID);
			for (const FTriangleID TriangleID : TriangleIDs)
			{
				IndexBuffer.push_back(RawMesh->GetTriangleVertexInstance(TriangleID, 0));
				IndexBuffer.push_back(RawMesh->GetTriangleVertexInstance(TriangleID, 1));
				IndexBuffer.push_back(RawMesh->GetTriangleVertexInstance(TriangleID, 2));
			}
		}


		PolyOffsetArray.push_back(PolyCount);
		RawMeshArray.push_back(RawMesh);
		RawMeshArrayData.push_back(FMeshDescriptionAttributesGetter(RawMesh));
		MergeDataArray.push_back(MergeData);
	}

	// Compute the bbox
	ComputeAABB(this->BBox);
}

FMeshDescriptionArrayAdapter::FMeshDescriptionArrayAdapter(const TArray<FMeshMergeData>& InMergeDataArray, const openvdb::math::Transform::Ptr InTransform)
	:Transform(InTransform)
{
	PointCount = 0;
	PolyCount = 0;

	PolyOffsetArray.push_back(PolyCount);
	for (int32 MeshIdx = 0, MeshCount = InMergeDataArray.Num(); MeshIdx < MeshCount; ++MeshIdx)
	{
		const FMeshMergeData* MergeData = &InMergeDataArray[MeshIdx];
		
		FMeshDescription *RawMesh = MergeData->RawMesh;
		PointCount += size_t(RawMesh->Vertices().Num());
		
		// Sum up all the polys in this mesh.
		int32 MeshPolyCount = RawMesh->Triangles().Num();

		// Construct local index buffer for this mesh
		IndexBufferArray.push_back(std::vector<FVertexInstanceID>());
		std::vector<FVertexInstanceID>& IndexBuffer = IndexBufferArray[MeshIdx];
		IndexBuffer.reserve(MeshPolyCount * 3);
		for (const FPolygonID& PolygonID : RawMesh->Polygons().GetElementIDs())
		{
			const TArray<FTriangleID>& TriangleIDs = RawMesh->GetPolygonTriangleIDs(PolygonID);
			for (const FTriangleID TriangleID : TriangleIDs)
			{
				IndexBuffer.push_back(RawMesh->GetTriangleVertexInstance(TriangleID, 0));
				IndexBuffer.push_back(RawMesh->GetTriangleVertexInstance(TriangleID, 1));
				IndexBuffer.push_back(RawMesh->GetTriangleVertexInstance(TriangleID, 2));
			}
		}

		PolyCount += MeshPolyCount;
		PolyOffsetArray.push_back(PolyCount);
		RawMeshArray.push_back(RawMesh);
		RawMeshArrayData.push_back(FMeshDescriptionAttributesGetter(RawMesh));
		MergeDataArray.push_back(MergeData);
	}

	// Compute the bbox
	ComputeAABB(this->BBox);
}

FMeshDescriptionArrayAdapter::FMeshDescriptionArrayAdapter(const FMeshDescriptionArrayAdapter& other)
	:Transform(other.Transform), PointCount(other.PointCount), PolyCount(other.PolyCount), BBox(other.BBox)
{
	RawMeshArray = other.RawMeshArray;
	PolyOffsetArray = other.PolyOffsetArray;
	MergeDataArray = other.MergeDataArray;

	for (const FMeshDescription* RawMesh : RawMeshArray)
	{
		RawMeshArrayData.push_back(FMeshDescriptionAttributesGetter(RawMesh));
	}

	IndexBufferArray.reserve(other.IndexBufferArray.size());
	for (const auto& IndexBuffer : other.IndexBufferArray)
	{
		IndexBufferArray.push_back(IndexBuffer);
	}

}

FMeshDescriptionArrayAdapter::~FMeshDescriptionArrayAdapter()
{
	RawMeshArray.clear();

	RawMeshArrayData.clear();
	MergeDataArray.clear();
	PolyOffsetArray.clear();
}

void FMeshDescriptionArrayAdapter::getWorldSpacePoint(size_t FaceNumber, size_t CornerNumber, openvdb::Vec3d& pos) const
{
	int32 MeshIdx, LocalFaceNumber;

	const FMeshDescriptionAttributesGetter* AttributesGetter = nullptr;
	const FMeshDescription& RawMesh = GetRawMesh(FaceNumber, MeshIdx, LocalFaceNumber, &AttributesGetter);
	check(AttributesGetter);

	const auto& IndexBuffer = IndexBufferArray[MeshIdx];
	// Get the vertex position in local space.
	const FVertexInstanceID VertexInstanceID = IndexBuffer[3 * LocalFaceNumber + int32(CornerNumber)];
	// float3 position 
	FVector Position = AttributesGetter->VertexPositions[RawMesh.GetVertexInstanceVertex(VertexInstanceID)];
	pos = openvdb::Vec3d(Position.X, Position.Y, Position.Z);
};

void FMeshDescriptionArrayAdapter::getIndexSpacePoint(size_t FaceNumber, size_t CornerNumber, openvdb::Vec3d& pos) const
{
	openvdb::Vec3d Position;
	getWorldSpacePoint(FaceNumber, CornerNumber, Position);
	pos = Transform->worldToIndex(Position);

};

const FMeshMergeData& FMeshDescriptionArrayAdapter::GetMeshMergeData(uint32 Idx) const
{
	checkSlow(Idx < MergeDataArray.size());
	return *MergeDataArray[Idx];
}

void FMeshDescriptionArrayAdapter::UpdateMaterialsID()
{
	for (int32 MeshIdx = 0; MeshIdx < MergeDataArray.size(); ++MeshIdx)
	{
		FMeshDescription* MeshDescription = RawMeshArray[MeshIdx];

		check(MergeDataArray[MeshIdx]->RawMesh->Polygons().Num() == MeshDescription->Polygons().Num());
		TMap<FPolygonGroupID, FPolygonGroupID> RemapGroup;
		TArray<int32> UniqueMaterials;
		for (const FPolygonID PolygonID : MeshDescription->Polygons().GetElementIDs())
		{
			FPolygonGroupID NewPolygonGroupID = MergeDataArray[MeshIdx]->RawMesh->GetPolygonPolygonGroup(PolygonID);
			if (!UniqueMaterials.Contains(NewPolygonGroupID.GetValue()))
			{
				UniqueMaterials.Add(NewPolygonGroupID.GetValue());
				FPolygonGroupID OriginalPolygonGroupID = MeshDescription->GetPolygonPolygonGroup(PolygonID);
				RemapGroup.Add(OriginalPolygonGroupID, NewPolygonGroupID);
			}
		}
		//Remap the polygon group with the correct ID
		FMeshDescriptionOperations::RemapPolygonGroups(*MeshDescription, RemapGroup);
	}
}

FMeshDescriptionArrayAdapter::FRawPoly FMeshDescriptionArrayAdapter::GetRawPoly(const size_t FaceNumber, int32& OutMeshIdx, int32& OutLocalFaceNumber) const
{
	checkSlow(FaceNumber < PolyCount);

	int32 MeshIdx, LocalFaceNumber;

	const FMeshDescriptionAttributesGetter* AttributesGetter = nullptr;
	const FMeshDescription& RawMesh = GetRawMesh(FaceNumber, MeshIdx, LocalFaceNumber, &AttributesGetter);
	check(AttributesGetter);
	OutMeshIdx = MeshIdx;
	OutLocalFaceNumber = LocalFaceNumber;

	checkSlow(LocalFaceNumber < AttributesGetter->TriangleCount);

	FVertexInstanceID WedgeIdx[3];
	WedgeIdx[0] = FVertexInstanceID(3 * LocalFaceNumber);
	WedgeIdx[1] = FVertexInstanceID((3 * LocalFaceNumber) + 1);
	WedgeIdx[2] = FVertexInstanceID((3 * LocalFaceNumber) + 2);


	FRawPoly RawPoly;
	RawPoly.MeshIdx = MeshIdx;
	FPolygonID PolygonID(LocalFaceNumber);
	RawPoly.FaceMaterialIndex = RawMesh.GetPolygonPolygonGroup(PolygonID).GetValue();
	RawPoly.FaceSmoothingMask = AttributesGetter->FaceSmoothingMasks[LocalFaceNumber];

	for (const FTriangleID TriangleID : RawMesh.GetPolygonTriangleIDs(PolygonID))
	{
		TArrayView<const FVertexInstanceID> VertexInstanceIDs = RawMesh.GetTriangleVertexInstances(TriangleID);
		for (int32 i = 0; i < 3; ++i)
		{
			RawPoly.VertexPositions[i] = AttributesGetter->VertexPositions[RawMesh.GetVertexInstanceVertex(VertexInstanceIDs[i])];
		}


		for (int32 i = 0; i < 3; ++i)
		{
			RawPoly.WedgeTangentX[i] = AttributesGetter->VertexInstanceTangents[VertexInstanceIDs[i]];
			RawPoly.WedgeTangentY[i] = FVector::CrossProduct(AttributesGetter->VertexInstanceNormals[VertexInstanceIDs[i]], AttributesGetter->VertexInstanceTangents[VertexInstanceIDs[i]]).GetSafeNormal() * AttributesGetter->VertexInstanceBinormalSigns[VertexInstanceIDs[i]];
			RawPoly.WedgeTangentZ[i] = AttributesGetter->VertexInstanceNormals[VertexInstanceIDs[i]];
			
			RawPoly.WedgeColors[i] = FLinearColor(AttributesGetter->VertexInstanceColors[VertexInstanceIDs[i]]).ToFColor(true);
			// Copy Texture coords
			for (int Idx = 0; Idx < MAX_MESH_TEXTURE_COORDS_MD; ++Idx)
			{
				if (AttributesGetter->VertexInstanceUVs.GetNumIndices() > Idx)
				{
					RawPoly.WedgeTexCoords[Idx][i] = AttributesGetter->VertexInstanceUVs.Get(VertexInstanceIDs[i], Idx);
				}
				else
				{
					RawPoly.WedgeTexCoords[Idx][i] = FVector2D(0.f, 0.f);
				}
			}
		}
	}
	return RawPoly;
}

FMeshDescriptionArrayAdapter::FRawPoly FMeshDescriptionArrayAdapter::GetRawPoly(const size_t FaceNumber) const
{
	int32 IgnoreMeshId, IgnoreLocalFaceNumber;
	return GetRawPoly(FaceNumber, IgnoreMeshId, IgnoreLocalFaceNumber);
}

// protected functions

const FMeshDescription& FMeshDescriptionArrayAdapter::GetRawMesh(const size_t FaceNumber, int32& MeshIdx, int32& LocalFaceNumber, const FMeshDescriptionAttributesGetter** OutAttributesGetter) const
{
	// Find the correct raw mesh
	MeshIdx = 0;
	while (FaceNumber >= PolyOffsetArray[MeshIdx + 1])
	{
		MeshIdx++;
	}

	// Offset the face number to get the correct index into this mesh.
	LocalFaceNumber = int32(FaceNumber) - PolyOffsetArray[MeshIdx];

	const FMeshDescription* MeshDescription = RawMeshArray[MeshIdx];

	*OutAttributesGetter = &RawMeshArrayData[MeshIdx];
	
	return *MeshDescription;
}

void FMeshDescriptionArrayAdapter::ComputeAABB(ProxyLOD::FBBox& InOutBBox)
{
	uint32 NumTris = this->polygonCount();
	InOutBBox = ProxyLOD::Parallel_Reduce(ProxyLOD::FIntRange(0, NumTris), ProxyLOD::FBBox(),
		[this](const ProxyLOD::FIntRange& Range, ProxyLOD::FBBox TargetBBox)->ProxyLOD::FBBox
	{
		// loop over faces
		for (int32 f = Range.begin(), F = Range.end(); f < F; ++f)
		{
			openvdb::Vec3d Pos;
			// loop over verts
			for (int32 v = 0; v < 3; ++v)
			{
				this->getWorldSpacePoint(f, v, Pos);

				TargetBBox.expand(Pos);
			}

		}

		return TargetBBox;

	}, [](const ProxyLOD::FBBox& BBoxA, const ProxyLOD::FBBox& BBoxB)->ProxyLOD::FBBox
	{
		ProxyLOD::FBBox Result(BBoxA);
		Result.expand(BBoxB);

		return Result;
	}

	);
}

// --- FClosestPolyField ----
FClosestPolyField::FClosestPolyField(const FMeshDescriptionArrayAdapter& MeshArray, const openvdb::Int32Grid::Ptr& SrcPolyIndexGrid) :
	RawMeshArrayAdapter(&MeshArray),
	ClosestPolyGrid(SrcPolyIndexGrid) 
{}

FClosestPolyField::FClosestPolyField(const FClosestPolyField& other) :
	RawMeshArrayAdapter(other.RawMeshArrayAdapter),
	ClosestPolyGrid(other.ClosestPolyGrid) 
{}

FClosestPolyField::FPolyConstAccessor::FPolyConstAccessor(const openvdb::Int32Grid* PolyIndexGrid, const FMeshDescriptionArrayAdapter* MeshArrayAdapter) :
	MeshArray(MeshArrayAdapter),
	CAccessor(PolyIndexGrid->getConstAccessor()),
	XForm(&(PolyIndexGrid->transform()))
{
}

FMeshDescriptionArrayAdapter::FRawPoly FClosestPolyField::FPolyConstAccessor::Get(const openvdb::Vec3d& WorldPos, bool& bSuccess) const
{
	checkSlow(MeshArray != NULL);
	const openvdb::Coord ijk = XForm->worldToIndexCellCentered(WorldPos);
	openvdb::Int32 SrcPolyId;
	bSuccess = CAccessor.probeValue(ijk, SrcPolyId);
	// return the first poly if this failed..
	SrcPolyId = (bSuccess) ? SrcPolyId : 0;
	return MeshArray->GetRawPoly(SrcPolyId);
}


FClosestPolyField::FPolyConstAccessor FClosestPolyField::GetPolyConstAccessor() const
{
	checkSlow(RawMeshArrayAdapter != NULL);
	checkSlow(ClosestPolyGrid != NULL);

	return FPolyConstAccessor(ClosestPolyGrid.get(), RawMeshArrayAdapter);
}


