// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved. 

#include "DynamicMeshToMeshDescription.h"
#include "MeshAttributes.h"
#include "DynamicMeshAttributeSet.h"
#include "DynamicMeshOverlay.h"
#include "MeshDescriptionBuilder.h"
#include "MeshNormals.h"



void FDynamicMeshToMeshDescription::Update(const FDynamicMesh3* MeshIn, FMeshDescription& MeshOut, bool bRecomputeNormals)
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

	if (bRecomputeNormals)
	{
		Builder.RecalculateInstanceNormals();
	}
	else
	{
		UpdateAttributes(MeshIn, MeshOut, true, false);
	}
}


namespace DynamicMeshToMeshDescriptionConversionHelper
{
	// NOTE: assumes the order of triangles in the MeshIn correspond to the ordering you'd get by iterating over polygons, then tris-in-polygons, on MeshOut
	// This matches conversion currently used in MeshDescriptionToDynamicMesh.cpp, but if that changes we will need to change this function to match!
	template <typename OutAttributeType, int VecLen, typename InAttributeType>
	void SetAttributesFromOverlay(
		const FDynamicMesh3* MeshInArg, const FMeshDescription& MeshOutArg,
		TVertexInstanceAttributesRef<OutAttributeType>& InstanceAttrib, const TDynamicMeshVectorOverlay<float, VecLen, InAttributeType>* Overlay)
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
				InstanceAttrib.Set(InstanceTri[0], OutAttributeType(Overlay->GetElement(OverlayVertIndices.A)));
				InstanceAttrib.Set(InstanceTri[1], OutAttributeType(Overlay->GetElement(OverlayVertIndices.B)));
				InstanceAttrib.Set(InstanceTri[2], OutAttributeType(Overlay->GetElement(OverlayVertIndices.C)));
			}
		}
	}
}


void FDynamicMeshToMeshDescription::UpdateAttributes(const FDynamicMesh3* MeshIn, FMeshDescription& MeshOut, bool bUpdateNormals, bool bUpdateUVs)
{
	check(MeshIn->IsCompactV());
	check(MeshIn->VertexCount() == MeshOut.Vertices().Num());
	check(MeshIn->TriangleCount() == MeshOut.Triangles().Num())

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
				DynamicMeshToMeshDescriptionConversionHelper::SetAttributesFromOverlay(MeshIn, MeshOut, InstanceAttrib, Overlay);
			}
			else
			{
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
		const FDynamicMeshUVOverlay* Overlay = MeshIn->HasAttributes() ? MeshIn->Attributes()->PrimaryUV() : nullptr;

		TVertexInstanceAttributesRef<FVector2D> InstanceAttrib =
			MeshOut.VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);
		ensureMsgf(InstanceAttrib.IsValid(), TEXT("Trying to update UVs on a MeshDescription that has no texture coordinate attributes"));
		if (InstanceAttrib.IsValid())
		{
			if (Overlay)
			{
				DynamicMeshToMeshDescriptionConversionHelper::SetAttributesFromOverlay(MeshIn, MeshOut, InstanceAttrib, Overlay);
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

	FMeshNormals VertexNormals(MeshIn);
	VertexNormals.ComputeVertexNormals();

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
				Builder.SetInstance(NewInstanceID, FVector2f::Zero(), VertexNormals[Triangle[j]]);
			}
			InstanceTri[j] = InstanceList[InstanceElem];
		}

		FPolygonID NewPolygonID = Builder.AppendTriangle(InstanceTri[0], InstanceTri[1], InstanceTri[2], AllGroupID);
		if (bCopyGroupToPolyGroup)
		{
			Builder.SetPolyGroupID(NewPolygonID, MeshIn->GetTriangleGroup(TriID));
		}
	}

	Builder.RecalculateInstanceNormals();
}







void FDynamicMeshToMeshDescription::Convert_SharedInstances(const FDynamicMesh3* MeshIn, FMeshDescription& MeshOut)
{
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

	// create vertices
	TArray<FVertexID> MapV; MapV.SetNum(MeshIn->MaxVertexID());
	for (int VertID : MeshIn->VertexIndicesItr())
	{
		MapV[VertID] = Builder.AppendVertex(MeshIn->GetVertex(VertID));
	}


	FPolygonGroupID AllGroupID = Builder.AppendPolygonGroup();


	// create new instances when seen
	TMap<FIndex3i, FVertexInstanceID> InstanceList;
	for (int TriID : MeshIn->TriangleIndicesItr())
	{
		// @todo support additional overlays (requires IndexNi...)
		FIndex3i Triangle = MeshIn->GetTriangle(TriID);
		FIndex3i UVTriangle = (UVOverlay != nullptr) ? UVOverlay->GetTriangle(TriID) : FIndex3i(-1, -1, -1);
		FIndex3i NormalTriangle = (NormalOverlay != nullptr) ? NormalOverlay->GetTriangle(TriID) : FIndex3i(-1, -1, -1);
		FVertexInstanceID InstanceTri[3];
		for (int j = 0; j < 3; ++j)
		{
			FIndex3i InstanceElem(Triangle[j], UVTriangle[j], NormalTriangle[j]);
			if (InstanceList.Contains(InstanceElem) == false)
			{
				FVertexInstanceID NewInstanceID = Builder.AppendInstance(MapV[Triangle[j]]);
				InstanceList.Add(InstanceElem, NewInstanceID);
				Builder.SetInstance(NewInstanceID,
					(UVTriangle[j] == -1 || UVOverlay == nullptr) ? FVector2f::Zero() : UVOverlay->GetElement(UVTriangle[j]),
					(NormalTriangle[j] == -1 || NormalOverlay == nullptr) ? FVector3f::UnitY() : NormalOverlay->GetElement(NormalTriangle[j]));
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





void FDynamicMeshToMeshDescription::Convert_NoSharedInstances(const FDynamicMesh3* MeshIn, FMeshDescription& MeshOut)
{
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

	FPolygonGroupID AllGroupID = Builder.AppendPolygonGroup();

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

		FPolygonID NewPolygonID = Builder.AppendTriangle(TriVertices, AllGroupID, UseUVs, UseNormals);

		if (bCopyGroupToPolyGroup)
		{
			Builder.SetPolyGroupID(NewPolygonID, MeshIn->GetTriangleGroup(TriID));
		}
	}

	// set to hard edges
	//PolyMeshIn->SetAllEdgesHardness(true);
}
