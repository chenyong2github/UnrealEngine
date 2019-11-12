// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved. 

#include "DynamicMeshToMeshDescription.h"
#include "StaticMeshAttributes.h"
#include "DynamicMeshAttributeSet.h"
#include "DynamicMeshOverlay.h"
#include "MeshDescriptionBuilder.h"





namespace DynamicMeshToMeshDescriptionConversionHelper
{
	// NOTE: assumes the order of triangles in the MeshIn correspond to the ordering you'd get by iterating over polygons, then tris-in-polygons, on MeshOut
	// This matches conversion currently used in MeshDescriptionToDynamicMesh.cpp, but if that changes we will need to change this function to match!
	template <typename OutAttributeType, int VecLen, typename InAttributeType>
	void SetAttributesFromOverlay(
		const FDynamicMesh3* MeshInArg, const FMeshDescription& MeshOutArg,
		TVertexInstanceAttributesRef<OutAttributeType>& InstanceAttrib, const TDynamicMeshVectorOverlay<float, VecLen, InAttributeType>* Overlay, int AttribIndex=0)
	{
		const FPolygonArray& Polygons = MeshOutArg.Polygons();
		int MeshInTriIdx = 0;
		for (const FPolygonID PolygonID : Polygons.GetElementIDs())
		{
			FPolygonGroupID PolygonGroupID = MeshOutArg.GetPolygonPolygonGroup(PolygonID);

			const TArray<FTriangleID>& TriangleIDs = MeshOutArg.GetPolygonTriangleIDs(PolygonID);
			int NumTriangles = TriangleIDs.Num();
			for (int TriIdx = 0; TriIdx < NumTriangles; ++TriIdx, ++MeshInTriIdx)
			{
				const FTriangleID TriangleID = TriangleIDs[TriIdx];
				TArrayView<const FVertexInstanceID> InstanceTri = MeshOutArg.GetTriangleVertexInstances(TriangleID);

				FIndex3i OverlayVertIndices = Overlay->GetTriangle(MeshInTriIdx);
				InstanceAttrib.Set(InstanceTri[0], AttribIndex, OutAttributeType(Overlay->GetElement(OverlayVertIndices.A)));
				InstanceAttrib.Set(InstanceTri[1], AttribIndex, OutAttributeType(Overlay->GetElement(OverlayVertIndices.B)));
				InstanceAttrib.Set(InstanceTri[2], AttribIndex, OutAttributeType(Overlay->GetElement(OverlayVertIndices.C)));
			}
		}
	}
}


void FDynamicMeshToMeshDescription::Update(const FDynamicMesh3* MeshIn, FMeshDescription& MeshOut)
{
	FMeshDescriptionBuilder Builder;
	Builder.SetMeshDescription(&MeshOut);

	check(MeshIn->IsCompactV());
	check(MeshIn->VertexCount() == MeshOut.Vertices().Num());

	// update positions
	for (int VertID : MeshIn->VertexIndicesItr())
	{
		Builder.SetPosition(FVertexID(VertID), MeshIn->GetVertex(VertID));
	}

	UpdateAttributes(MeshIn, MeshOut, true, false);
}



void FDynamicMeshToMeshDescription::UpdateAttributes(const FDynamicMesh3* MeshIn, FMeshDescription& MeshOut, bool bUpdateNormals, bool bUpdateUVs)
{
	check(MeshIn->IsCompactV());

	if (bUpdateNormals)
	{
		TVertexInstanceAttributesRef<FVector> InstanceAttrib =
			MeshOut.VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Normal);
		ensureMsgf(InstanceAttrib.IsValid(), TEXT("Trying to update normals on a MeshDescription that has no normal attributes"));
		if (InstanceAttrib.IsValid())
		{
			const FDynamicMeshNormalOverlay* Overlay = MeshIn->HasAttributes() ? MeshIn->Attributes()->PrimaryNormals() : nullptr;
			if (Overlay)
			{
				check(MeshIn->TriangleCount() == MeshOut.Triangles().Num())
				DynamicMeshToMeshDescriptionConversionHelper::SetAttributesFromOverlay(MeshIn, MeshOut, InstanceAttrib, Overlay);
			}
			else
			{
				check(MeshIn->VertexCount() == MeshOut.Vertices().Num());
				for (int VertID : MeshIn->VertexIndicesItr())
				{
					FVector Normal = (FVector)MeshIn->GetVertexNormal(VertID);
					for (FVertexInstanceID InstanceID : MeshOut.GetVertexVertexInstances(FVertexID(VertID)))
					{
						InstanceAttrib.Set(InstanceID, Normal);
					}
				}
			}
		}
	}

	if (bUpdateUVs)
	{
		TVertexInstanceAttributesRef<FVector2D> InstanceAttrib =
			MeshOut.VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);
		ensureMsgf(InstanceAttrib.IsValid(), TEXT("Trying to update UVs on a MeshDescription that has no texture coordinate attributes"));
		if (InstanceAttrib.IsValid())
		{
			if (MeshIn->HasAttributes())
			{
				for (int UVLayerIndex = 0, NumLayers = MeshIn->Attributes()->NumUVLayers(); UVLayerIndex < NumLayers; UVLayerIndex++)
				{
					DynamicMeshToMeshDescriptionConversionHelper::SetAttributesFromOverlay(MeshIn, MeshOut, InstanceAttrib, MeshIn->Attributes()->GetUVLayer(UVLayerIndex), UVLayerIndex);
				}
			}
			else
			{
				for (int VertID : MeshIn->VertexIndicesItr())
				{
					FVector2D UV = (FVector2D)MeshIn->GetVertexUV(VertID);
					for (FVertexInstanceID InstanceID : MeshOut.GetVertexVertexInstances(FVertexID(VertID)))
					{
						InstanceAttrib.Set(InstanceID, UV);
					}
				}
			}
		}
	}
}




void FDynamicMeshToMeshDescription::Convert(const FDynamicMesh3* MeshIn, FMeshDescription& MeshOut)
{
	if (MeshIn->HasAttributes())
	{
		Convert_SharedInstances(MeshIn, MeshOut);
	}
	else
	{
		Convert_NoAttributes(MeshIn, MeshOut);
	}
}


void FDynamicMeshToMeshDescription::Convert_NoAttributes(const FDynamicMesh3* MeshIn, FMeshDescription& MeshOut)
{
	MeshOut.Empty();

	FMeshDescriptionBuilder Builder;
	Builder.SetMeshDescription(&MeshOut);

	bool bCopyGroupToPolyGroup = false;
	if (ConversionOptions.bSetPolyGroups && MeshIn->HasTriangleGroups())
	{
		Builder.EnablePolyGroups();
		bCopyGroupToPolyGroup = true;
	}

	// create vertices
	TArray<FVertexID> MapV; 
	MapV.SetNum(MeshIn->MaxVertexID());
	for (int VertID : MeshIn->VertexIndicesItr())
	{
		MapV[VertID] = Builder.AppendVertex(MeshIn->GetVertex(VertID));
	}

	FPolygonGroupID AllGroupID = Builder.AppendPolygonGroup();

	// create new instances when seen
	TMap<FIndex3i, FVertexInstanceID> InstanceList;
	for (int TriID : MeshIn->TriangleIndicesItr())
	{
		FIndex3i Triangle = MeshIn->GetTriangle(TriID);
		FIndex3i UVTriangle(-1, -1, -1);
		FIndex3i NormalTriangle = Triangle;
		FVertexInstanceID InstanceTri[3];
		for (int j = 0; j < 3; ++j)
		{
			FIndex3i InstanceElem(Triangle[j], UVTriangle[j], NormalTriangle[j]);
			if (InstanceList.Contains(InstanceElem) == false)
			{
				FVertexInstanceID NewInstanceID = Builder.AppendInstance(MapV[Triangle[j]]);
				InstanceList.Add(InstanceElem, NewInstanceID);
				FVector2D UV = MeshIn->HasVertexUVs() ? FVector2D(MeshIn->GetVertexUV(Triangle[j])) : FVector2D::ZeroVector;
				FVector Normal = MeshIn->HasVertexNormals() ? FVector(MeshIn->GetVertexNormal(Triangle[j])) : FVector::UpVector;
				Builder.SetInstance(NewInstanceID, UV, Normal);
			}
			InstanceTri[j] = InstanceList[InstanceElem];
		}

		FPolygonID NewPolygonID = Builder.AppendTriangle(InstanceTri[0], InstanceTri[1], InstanceTri[2], AllGroupID);
		if (bCopyGroupToPolyGroup)
		{
			Builder.SetPolyGroupID(NewPolygonID, MeshIn->GetTriangleGroup(TriID));
		}
	}
}







void FDynamicMeshToMeshDescription::Convert_SharedInstances(const FDynamicMesh3* MeshIn, FMeshDescription& MeshOut)
{
	const FDynamicMeshNormalOverlay* NormalOverlay = MeshIn->HasAttributes() ? MeshIn->Attributes()->PrimaryNormals() : nullptr;

	MeshOut.Empty();

	FMeshDescriptionBuilder Builder;
	Builder.SetMeshDescription(&MeshOut);

	bool bCopyGroupToPolyGroup = false;
	if (ConversionOptions.bSetPolyGroups && MeshIn->HasTriangleGroups())
	{
		Builder.EnablePolyGroups();
		bCopyGroupToPolyGroup = true;
	}

	// create vertices
	TArray<FVertexID> MapV; MapV.SetNum(MeshIn->MaxVertexID());
	for (int VertID : MeshIn->VertexIndicesItr())
	{
		MapV[VertID] = Builder.AppendVertex(MeshIn->GetVertex(VertID));
	}


	FPolygonGroupID ZeroPolygonGroupID = Builder.AppendPolygonGroup();

	// check if we have per-triangle material ID
	const FDynamicMeshMaterialAttribute* MaterialIDAttrib =
		(MeshIn->HasAttributes() && MeshIn->Attributes()->HasMaterialID()) ?
		MeshIn->Attributes()->GetMaterialID() : nullptr;

	// need to know max material index value so we can reserve groups in MeshDescription
	int32 MaxPolygonGroupID = 0;
	if (MaterialIDAttrib)
	{
		for (int TriID : MeshIn->TriangleIndicesItr())
		{
			int32 MaterialID;
			MaterialIDAttrib->GetValue(TriID, &MaterialID);
			MaxPolygonGroupID = FMath::Max(MaterialID, MaxPolygonGroupID);
		}
		if (MaxPolygonGroupID == 0)
		{
			MaterialIDAttrib = nullptr;
		}
		else
		{
			for (int k = 0; k < MaxPolygonGroupID; ++k)
			{
				Builder.AppendPolygonGroup();
			}
		}
	}

	// build all vertex instances (splitting as needed)
	// store per-triangle instance ids
	struct Tri
	{
		FVertexInstanceID V[3];

		Tri() : V{ FVertexInstanceID::Invalid,FVertexInstanceID::Invalid,FVertexInstanceID::Invalid }
		{}
	};
	TArray<Tri> TriVertInstances;
	TriVertInstances.SetNum(MeshIn->MaxTriangleID());
	TArray<int32> KnownInstanceIDs;
	int NumUVLayers = MeshIn->HasAttributes() ? MeshIn->Attributes()->NumUVLayers() : 0;
	Builder.SetNumUVLayers(NumUVLayers);
	int KIItemLen = 1 + (NormalOverlay ? 1 : 0) + NumUVLayers;
	for (int VertID : MeshIn->VertexIndicesItr())
	{
		KnownInstanceIDs.Reset();
		for (int TriID : MeshIn->VtxTrianglesItr(VertID))
		{
			FIndex3i Tri = MeshIn->GetTriangle(TriID);
			int SubIdx = IndexUtil::FindTriIndex(VertID, Tri);

			int32 FoundInstance = FVertexInstanceID::Invalid.GetValue();
			for (int KIItemIdx = 0; KIItemIdx < KnownInstanceIDs.Num(); KIItemIdx += KIItemLen)
			{
				int KIItemInternalIdx = KIItemIdx;

				if (NormalOverlay && KnownInstanceIDs[KIItemInternalIdx++] != NormalOverlay->GetTriangle(TriID)[SubIdx])
				{
					continue;
				}

				bool FoundInUVs = true;
				for (int UVLayerIndex = 0; UVLayerIndex < NumUVLayers; UVLayerIndex++)
				{
					const FDynamicMeshUVOverlay* Overlay = MeshIn->Attributes()->GetUVLayer(UVLayerIndex);
					if (KnownInstanceIDs[KIItemInternalIdx++] != Overlay->GetTriangle(TriID)[SubIdx])
					{
						FoundInUVs = false;
						break;
					}
				}
				if (!FoundInUVs)
				{
					continue;
				}

				FoundInstance = KnownInstanceIDs[KIItemInternalIdx++];
				check(KIItemInternalIdx == KIItemIdx + KIItemLen);
				break;
			}
			if (FoundInstance == FVertexInstanceID::Invalid.GetValue())
			{
				FVertexInstanceID NewInstanceID = Builder.AppendInstance(MapV[VertID]);
				if (NormalOverlay)
				{
					int ElID = NormalOverlay->GetTriangle(TriID)[SubIdx];
					KnownInstanceIDs.Add(int32(ElID));
					Builder.SetInstanceNormal(NewInstanceID, ElID != -1 ? NormalOverlay->GetElement(ElID) : FVector3f::UnitZ());
				}
				else
				{
					Builder.SetInstanceNormal(NewInstanceID, FVector::UpVector);
				}
				for (int UVLayerIndex = 0; UVLayerIndex < NumUVLayers; UVLayerIndex++)
				{
					const FDynamicMeshUVOverlay* Overlay = MeshIn->Attributes()->GetUVLayer(UVLayerIndex);
					int ElID = Overlay->GetTriangle(TriID)[SubIdx];
					KnownInstanceIDs.Add(int32(ElID));

					Builder.SetInstanceUV(NewInstanceID, ElID != -1 ? Overlay->GetElement(ElID) : FVector2f::Zero(), UVLayerIndex);
				}
				FoundInstance = NewInstanceID.GetValue();
				KnownInstanceIDs.Add(FoundInstance);
			}
			TriVertInstances[TriID].V[SubIdx] = FVertexInstanceID(FoundInstance);
		}
	}

	// build the polygons
	for (int TriID : MeshIn->TriangleIndicesItr())
	{
		// transfer material index to MeshDescription polygon group (by convention)
		FPolygonGroupID UsePolygonGroupID = ZeroPolygonGroupID;
		if (MaterialIDAttrib)
		{
			int32 MaterialID;
			MaterialIDAttrib->GetValue(TriID, &MaterialID);
			UsePolygonGroupID = FPolygonGroupID(MaterialID);
		}

		FPolygonID NewPolygonID = Builder.AppendTriangle(TriVertInstances[TriID].V[0], TriVertInstances[TriID].V[1], TriVertInstances[TriID].V[2], UsePolygonGroupID);

		if (bCopyGroupToPolyGroup)
		{
			Builder.SetPolyGroupID(NewPolygonID, MeshIn->GetTriangleGroup(TriID));
		}
	}
}





void FDynamicMeshToMeshDescription::Convert_NoSharedInstances(const FDynamicMesh3* MeshIn, FMeshDescription& MeshOut)
{
	// TODO: update this path to support multiple UV layers

	const FDynamicMeshUVOverlay* UVOverlay = MeshIn->HasAttributes() ? MeshIn->Attributes()->PrimaryUV() : nullptr;
	const FDynamicMeshNormalOverlay* NormalOverlay = MeshIn->HasAttributes() ? MeshIn->Attributes()->PrimaryNormals() : nullptr;

	MeshOut.Empty();

	FMeshDescriptionBuilder Builder;
	Builder.SetMeshDescription(&MeshOut);

	bool bCopyGroupToPolyGroup = false;
	if (ConversionOptions.bSetPolyGroups && MeshIn->HasTriangleGroups())
	{
		Builder.EnablePolyGroups();
		bCopyGroupToPolyGroup = true;
	}

	TArray<FVertexID> MapV; MapV.SetNum(MeshIn->MaxVertexID());
	for (int VertID : MeshIn->VertexIndicesItr())
	{
		MapV[VertID] = Builder.AppendVertex(MeshIn->GetVertex(VertID));
	}

	FPolygonGroupID ZeroPolygonGroupID = Builder.AppendPolygonGroup();

	// check if we have per-triangle material ID
	const FDynamicMeshMaterialAttribute* MaterialIDAttrib =
		(MeshIn->HasAttributes() && MeshIn->Attributes()->HasMaterialID()) ?
		MeshIn->Attributes()->GetMaterialID() : nullptr;

	// need to know max material index value so we can reserve groups in MeshDescription
	int32 MaxPolygonGroupID = 0;
	if (MaterialIDAttrib)
	{
		for (int TriID : MeshIn->TriangleIndicesItr())
		{
			int32 MaterialID;
			MaterialIDAttrib->GetValue(TriID, &MaterialID);
			MaxPolygonGroupID = FMath::Max(MaterialID, MaxPolygonGroupID);
		}
		if (MaxPolygonGroupID == 0)
		{
			MaterialIDAttrib = nullptr;
		}
		else
		{
			for (int k = 0; k < MaxPolygonGroupID; ++k)
			{
				Builder.AppendPolygonGroup();
			}
		}
	}


	FVertexID TriVertices[3];
	FVector2D TriUVs[3];
	FVector TriNormals[3];
	for (int TriID : MeshIn->TriangleIndicesItr())
	{
		FIndex3i Triangle = MeshIn->GetTriangle(TriID);

		FVector2D* UseUVs = nullptr;
		if (UVOverlay != nullptr)
		{
			FIndex3i UVTriangle = UVOverlay->GetTriangle(TriID);
			TriUVs[0] = UVOverlay->GetElement(UVTriangle[0]);
			TriUVs[1] = UVOverlay->GetElement(UVTriangle[1]);
			TriUVs[2] = UVOverlay->GetElement(UVTriangle[2]);
			UseUVs = TriUVs;
		}

		FVector* UseNormals = nullptr;
		if (NormalOverlay != nullptr)
		{
			FIndex3i NormalTriangle = NormalOverlay->GetTriangle(TriID);
			TriNormals[0] = NormalOverlay->GetElement(NormalTriangle[0]);
			TriNormals[1] = NormalOverlay->GetElement(NormalTriangle[1]);
			TriNormals[2] = NormalOverlay->GetElement(NormalTriangle[2]);
			UseNormals = TriNormals;
		}

		TriVertices[0] = MapV[Triangle[0]];
		TriVertices[1] = MapV[Triangle[1]];
		TriVertices[2] = MapV[Triangle[2]];

		// transfer material index to MeshDescription polygon group (by convention)
		FPolygonGroupID UsePolygonGroupID = ZeroPolygonGroupID;
		if (MaterialIDAttrib)
		{
			int32 MaterialID;
			MaterialIDAttrib->GetValue(TriID, &MaterialID);
			UsePolygonGroupID = FPolygonGroupID(MaterialID);
		}

		FPolygonID NewPolygonID = Builder.AppendTriangle(TriVertices, UsePolygonGroupID, UseUVs, UseNormals);

		if (bCopyGroupToPolyGroup)
		{
			Builder.SetPolyGroupID(NewPolygonID, MeshIn->GetTriangleGroup(TriID));
		}
	}

	// set to hard edges
	//PolyMeshIn->SetAllEdgesHardness(true);
}
