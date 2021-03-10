// Copyright Epic Games, Inc. All Rights Reserved. 

#include "DynamicMeshToMeshDescription.h"
#include "StaticMeshAttributes.h"
#include "DynamicMeshAttributeSet.h"
#include "DynamicMeshOverlay.h"
#include "MeshDescriptionBuilder.h"
#include "MeshTangents.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;


namespace DynamicMeshToMeshDescriptionConversionHelper
{
	// NOTE: assumes the order of triangles in the MeshIn correspond to the ordering over tris on MeshOut
	// This matches conversion currently used in MeshDescriptionToDynamicMesh.cpp, but if that changes we will need to change this function to match!
	template <typename OutAttributeType, int VecLen, typename InAttributeType>
	void SetAttributesFromOverlay(
		const FDynamicMesh3* MeshInArg, const FMeshDescription& MeshOutArg,
		TVertexInstanceAttributesRef<OutAttributeType>& InstanceAttrib, const TDynamicMeshVectorOverlay<float, VecLen, InAttributeType>* Overlay, int AttribIndex=0)
	{
		for (const FTriangleID TriangleID : MeshOutArg.Triangles().GetElementIDs())
		{
			TArrayView<const FVertexInstanceID> InstanceTri = MeshOutArg.GetTriangleVertexInstances(TriangleID);

			int32 MeshInTriIdx = TriangleID.GetValue();   

			FIndex3i OverlayVertIndices = Overlay->GetTriangle(MeshInTriIdx);
			InstanceAttrib.Set(InstanceTri[0], AttribIndex, OutAttributeType(Overlay->GetElement(OverlayVertIndices.A)));
			InstanceAttrib.Set(InstanceTri[1], AttribIndex, OutAttributeType(Overlay->GetElement(OverlayVertIndices.B)));
			InstanceAttrib.Set(InstanceTri[2], AttribIndex, OutAttributeType(Overlay->GetElement(OverlayVertIndices.C)));
		}
	}
}


void FDynamicMeshToMeshDescription::Update(const FDynamicMesh3* MeshIn, FMeshDescription& MeshOut, bool bUpdateNormals, bool bUpdateTangents, bool bUpdateUVs)
{
	FMeshDescriptionBuilder Builder;
	Builder.SetMeshDescription(&MeshOut);

	check(MeshIn->IsCompactV());

	// can't currently update the shared UV connectivity data structure on the MeshDescription :(
	//  -- see FDynamicMeshToMeshDescription::UpdateAttributes()
	// after this has been fixed, please update USimpleDynamicMeshComponent::Bake() to use the Update() path accordingly  
	check(bUpdateUVs == false);

	// update positions
	int32 NumVertices = MeshOut.Vertices().Num();
	check(NumVertices <= MeshIn->VertexCount());
	for (int32 VertID = 0; VertID < NumVertices; ++VertID)
	{
		Builder.SetPosition(FVertexID(VertID), (FVector)MeshIn->GetVertex(VertID));
	}

	UpdateAttributes(MeshIn, MeshOut, bUpdateNormals, bUpdateTangents, bUpdateUVs);
}



void FDynamicMeshToMeshDescription::UpdateAttributes(const FDynamicMesh3* MeshIn, FMeshDescription& MeshOut, bool bUpdateNormals, bool bUpdateTangents, bool bUpdateUVs)
{
	check(MeshIn->IsCompactV());


	FStaticMeshAttributes Attributes(MeshOut);


	if (bUpdateNormals)
	{
		TVertexInstanceAttributesRef<FVector> InstanceAttrib = Attributes.GetVertexInstanceNormals();
		bool bIsValidDst = InstanceAttrib.IsValid();
		ensureMsgf(bIsValidDst, TEXT("Trying to update normals on a MeshDescription that has no normal attributes"));
		if (bIsValidDst)
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
					for (FVertexInstanceID InstanceID : MeshOut.GetVertexVertexInstanceIDs(FVertexID(VertID)))
					{
						InstanceAttrib.Set(InstanceID, Normal);
					}
				}
			}
		}
	}

	if (bUpdateTangents)
	{
		UpdateTangents(MeshIn, MeshOut);
	}
	

	if (bUpdateUVs)
	{
		TVertexInstanceAttributesRef<FVector2D> InstanceAttrib = Attributes.GetVertexInstanceUVs();
		ensureMsgf(InstanceAttrib.IsValid(), TEXT("Trying to update UVs on a MeshDescription that has no texture coordinate attributes"));
		if (InstanceAttrib.IsValid())
		{
			if (MeshIn->HasAttributes())
			{
				check(MeshIn->TriangleCount() == MeshOut.Triangles().Num())
				int32 NumLayers = MeshIn->Attributes()->NumUVLayers();			
				MeshOut.SetNumUVChannels(NumLayers); // This resets MeshDescription's internal TriangleUV array
				
				for (int UVLayerIndex = 0; UVLayerIndex < NumLayers; UVLayerIndex++)
				{
					const FDynamicMeshUVOverlay* UVOverlay = MeshIn->Attributes()->GetUVLayer(UVLayerIndex);
					// update the vertex Attribute UVs
					DynamicMeshToMeshDescriptionConversionHelper::SetAttributesFromOverlay(MeshIn, MeshOut, InstanceAttrib, UVOverlay, UVLayerIndex);

					// rebuild the shared UVs
					FUVArray& UVArray = MeshOut.UVs(UVLayerIndex);
					UVArray.Reset(); // delete existing UV buffer
					UVArray.Reserve(UVOverlay->ElementCount());
					
					// rebuild the UV buffer
					TArray<FUVID> ElIDToUVIDMap;  
					ElIDToUVIDMap.Reserve(UVOverlay->ElementCount());
					for (int32 ElID = 0, MaxID = UVOverlay->MaxElementID(); ElID < MaxID; ++ElID)
					{
						if (!UVOverlay->IsElement(ElID))
						{
							continue;
						}

						FVector2f UVValue = UVOverlay->GetElement(ElID);
						FVector2D UVValue2D(UVValue.X, UVValue.Y);
						FUVID UVID = UVArray.Add();
						ElIDToUVIDMap.Insert(UVID, ElID);
						UVArray.GetAttributes().GetAttributesRef<FVector2D>(MeshAttribute::UV::UVCoordinate)[UVID] = UVValue2D;
					}

					for (const FTriangleID TriangleID : MeshOut.Triangles().GetElementIDs())
					{
						int TriID = TriangleID.GetValue(); // assumes the same TriIDs in both meshes.
						FIndex3i ElIDs = UVOverlay->GetTriangle(TriID);
						TArray<FUVID, TFixedAllocator<3>> MDTri; 
						MDTri.Add(ElIDToUVIDMap[ElIDs[0]]);
						MDTri.Add(ElIDToUVIDMap[ElIDs[1]]);
						MDTri.Add(ElIDToUVIDMap[ElIDs[2]]);
						MeshOut.SetTriangleUVIndices(TriangleID, MDTri, UVLayerIndex);
					}
				}
				if (0)
				{ 
					// Verify the shared UVs and per-vertexinstance UVs match
					for (int UVLayerIndex = 0; UVLayerIndex < NumLayers; UVLayerIndex++)
					{ 
						for (const FTriangleID TriangleID : MeshOut.Triangles().GetElementIDs())
						{
							TArrayView<const FVertexInstanceID> TriWedges = MeshOut.GetTriangleVertexInstances(TriangleID);
							TArrayView<FUVID> UVTri = MeshOut.GetTriangleUVIndices(TriangleID, UVLayerIndex);

							for (int32 i = 0; i < 3; ++i)
							{
								// UV from shared
								FVector2D SharedUV = MeshOut.UVs(UVLayerIndex).GetAttributes().GetAttributesRef<FVector2D>(MeshAttribute::UV::UVCoordinate)[UVTri[i]];
								FVector2D WedgeUV = InstanceAttrib.Get(TriWedges[i], UVLayerIndex);
								check(SharedUV == WedgeUV);
							}
						}
					}
				}
			}
			else
			{
				// [todo] correctly build shared UVs?
				check(MeshIn->VertexCount() == MeshOut.Vertices().Num());
				for (int VertID : MeshIn->VertexIndicesItr())
				{
					FVector2D UV = (FVector2D)MeshIn->GetVertexUV(VertID);
					for (FVertexInstanceID InstanceID : MeshOut.GetVertexVertexInstanceIDs(FVertexID(VertID)))
					{
						InstanceAttrib.Set(InstanceID, UV);
					}
				}
			}
		}
	}
}


void FDynamicMeshToMeshDescription::UpdateTangents(const FDynamicMesh3* MeshIn, FMeshDescription& MeshOut, const FMeshTangentsd* Tangents)
{
	if (!ensureMsgf(MeshIn->TriangleCount() == MeshOut.Triangles().Num(), TEXT("Trying to update MeshDescription Tangents from Mesh that does not have same triangle count"))) return;
	if (!ensureMsgf(MeshIn->IsCompactT(), TEXT("Trying to update MeshDescription Tangents from a non-compact DynamicMesh"))) return;
	if (!ensureMsgf(MeshIn->HasAttributes(), TEXT("Trying to update MeshDescription Tangents from a DynamicMesh that has no Normals attribute"))) return;

	FStaticMeshAttributes Attributes(MeshOut);

	const FDynamicMeshNormalOverlay* Normals = MeshIn->Attributes()->PrimaryNormals();
	TVertexInstanceAttributesRef<FVector> TangentAttrib = Attributes.GetVertexInstanceTangents();
	TVertexInstanceAttributesRef<float> BinormalSignAttrib = Attributes.GetVertexInstanceBinormalSigns();

	if (!ensureMsgf(TangentAttrib.IsValid(), TEXT("Trying to update Tangents on a MeshDescription that has no Tangent Vertex Instance attribute"))) return;
	if (!ensureMsgf(BinormalSignAttrib.IsValid(), TEXT("Trying to update Tangents on a MeshDescription that has no BinormalSign Vertex Instance attribute"))) return;

	if (TangentAttrib.IsValid() && BinormalSignAttrib.IsValid())
	{
		int32 NumTriangles = MeshIn->TriangleCount();
		for (int32 k = 0; k < NumTriangles; ++k)
		{
			FVector3f TriNormals[3];
			Normals->GetTriElements(k, TriNormals[0], TriNormals[1], TriNormals[2]);

			TArrayView<const FVertexInstanceID> TriInstances = MeshOut.GetTriangleVertexInstances(FTriangleID(k));
			for (int j = 0; j < 3; ++j)
			{
				FVector3d Tangent, Bitangent;
				Tangents->GetPerTriangleTangent(k, j, Tangent, Bitangent);
				float BitangentSign = (float)VectorUtil::BitangentSign((FVector3d)TriNormals[j], Tangent, Bitangent);
				TangentAttrib.Set(TriInstances[j], (FVector)Tangent);
				BinormalSignAttrib.Set(TriInstances[j], BitangentSign);
			}
		}
	}
}

void FDynamicMeshToMeshDescription::UpdateTangents(const FDynamicMesh3* MeshIn, FMeshDescription& MeshOut)
{
	if (!ensureMsgf(MeshIn->IsCompactT(), TEXT("Trying to update MeshDescription Tangents from a non-compact DynamicMesh"))) return;
	if (!ensureMsgf(MeshIn->TriangleCount() == MeshOut.Triangles().Num(), TEXT("Trying to update MeshDescription Tangents from Mesh that does not have same triangle count"))) return;
	if (!ensureMsgf(MeshIn->HasAttributes(), TEXT("Trying to update MeshDescription Tangents from a DynamicMesh that has no attributes, e.g. normals"))) return;

	
	// src
	const FDynamicMeshNormalOverlay* NormalOverlay = MeshIn->Attributes()->PrimaryNormals();
	const FDynamicMeshNormalOverlay* TangentOverlay = MeshIn->Attributes()->PrimaryTangents();
	const FDynamicMeshNormalOverlay* BiTangentOverlay = MeshIn->Attributes()->PrimaryBiTangents();

	bool bHasValidSrc = (NormalOverlay != nullptr) && (TangentOverlay != nullptr) && (BiTangentOverlay != nullptr);

	if(!ensureMsgf(bHasValidSrc, TEXT("Trying to update MeshDescription Tangents from a DynamicMesh that does not have all three tangent space attributes"))) return;

	// dst
	FStaticMeshAttributes Attributes(MeshOut);
	TVertexInstanceAttributesRef<FVector> TangentAttrib = Attributes.GetVertexInstanceTangents();
	TVertexInstanceAttributesRef<float> BiTangentSignAttrib = Attributes.GetVertexInstanceBinormalSigns();

	if (!ensureMsgf(TangentAttrib.IsValid(), TEXT("Trying to update Tangents on a MeshDescription that has no Tangent Vertex Instance attribute"))) return;
	if (!ensureMsgf(BiTangentSignAttrib.IsValid(), TEXT("Trying to update Tangents on a MeshDescription that has no BinormalSign Vertex Instance attribute"))) return;

	const int32 NumTriangles = MeshIn->TriangleCount();
	for (int32 t = 0; t < NumTriangles; ++t)
	{
		const bool bTriHasTangentSpace = NormalOverlay->IsSetTriangle(t) && TangentOverlay->IsSetTriangle(t) && BiTangentOverlay->IsSetTriangle(t);

		if (!bTriHasTangentSpace) continue;
		
		// get data from dynamic mesh overlays
		FVector3f TriNormals[3];
		NormalOverlay->GetTriElements(t, TriNormals[0], TriNormals[1], TriNormals[2]);
	

		FVector3f TriTangents[3];
		TangentOverlay->GetTriElements(t, TriTangents[0], TriTangents[1], TriTangents[2]);
		

		FVector3f TriBiTangents[3];
		BiTangentOverlay->GetTriElements(t, TriBiTangents[0], TriBiTangents[1], TriBiTangents[2]);
		
		// set data in the mesh description per-vertex attributes
		TArrayView<const FVertexInstanceID> TriInstances = MeshOut.GetTriangleVertexInstances(FTriangleID(t));
		for (int32 i = 0; i < 3; ++i)
		{
			const float BiTangentSign = VectorUtil::BitangentSign(TriNormals[i], TriTangents[i], TriBiTangents[i]);

			TangentAttrib.Set(TriInstances[i], (FVector)TriTangents[i]);
			BiTangentSignAttrib.Set(TriInstances[i], BiTangentSign);
		}
	}
}


void FDynamicMeshToMeshDescription::UpdateVertexColors(const FDynamicMesh3* MeshIn, FMeshDescription& MeshOut)
{
	check(MeshIn->IsCompactV() && MeshIn->HasVertexColors());

	FStaticMeshAttributes Attributes(MeshOut);
	TVertexInstanceAttributesRef<FVector4> InstanceColors = Attributes.GetVertexInstanceColors();
	bool bIsValidDst = InstanceColors.IsValid();
	ensureMsgf(bIsValidDst, TEXT("Trying to update colors on a MeshDescription that has no color attributes"));
	if (bIsValidDst)
	{
		check(MeshIn->VertexCount() == MeshOut.Vertices().Num());
		for (int VertID : MeshIn->VertexIndicesItr())
		{
			FVector3f Color3f = MeshIn->GetVertexColor(VertID);
			FVector4 Color4(Color3f.X, Color3f.Y, Color3f.Z, 1.0f);
			for (FVertexInstanceID InstanceID : MeshOut.GetVertexVertexInstanceIDs(FVertexID(VertID)))
			{
				InstanceColors.Set(InstanceID, 0, Color4);
			}
		}
	}
}




void FDynamicMeshToMeshDescription::Convert(const FDynamicMesh3* MeshIn, FMeshDescription& MeshOut, bool bCopyTangents)
{
	if (MeshIn->HasAttributes())
	{
		//Convert_SharedInstances(MeshIn, MeshOut);
		Convert_NoSharedInstances(MeshIn, MeshOut, bCopyTangents);
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

	Builder.SuspendMeshDescriptionIndexing();
	const int32 UVLayerIndex = 0;
	Builder.SetNumUVLayers(1);
	Builder.ReserveNewUVs(MeshIn->VertexCount(), UVLayerIndex);

	bool bCopyGroupToPolyGroup = false;
	if (ConversionOptions.bSetPolyGroups && MeshIn->HasTriangleGroups())
	{
		Builder.EnablePolyGroups();
		bCopyGroupToPolyGroup = true;
	}

	// create vertices
	TArray<FVertexID> MapV; 
	MapV.SetNum(MeshIn->MaxVertexID());
	Builder.ReserveNewVertices(MeshIn->VertexCount());
	for (int VertID : MeshIn->VertexIndicesItr())
	{
		MapV[VertID] = Builder.AppendVertex((FVector)MeshIn->GetVertex(VertID));
	}

	FPolygonGroupID AllGroupID = Builder.AppendPolygonGroup();

	// create new instances when seen
	TMap<FIndex3i, FVertexInstanceID> InstanceList;
	TMap<int32, FUVID> InstanceUVIDMap;
	for (int TriID : MeshIn->TriangleIndicesItr())
	{
		FIndex3i Triangle = MeshIn->GetTriangle(TriID);
		FIndex3i UVTriangle(-1, -1, -1);
		FIndex3i NormalTriangle = Triangle;
		FVertexInstanceID InstanceTri[3];
		FUVID UVIDs[3];
		for (int j = 0; j < 3; ++j)
		{
			FIndex3i InstanceElem(Triangle[j], UVTriangle[j], NormalTriangle[j]);
			if (InstanceList.Contains(InstanceElem) == false)
			{
				FVertexInstanceID NewInstanceID = Builder.AppendInstance(MapV[Triangle[j]]);
				InstanceList.Add(InstanceElem, NewInstanceID);
				
				FVector Normal = MeshIn->HasVertexNormals() ? FVector(MeshIn->GetVertexNormal(Triangle[j])) : FVector::UpVector;
				Builder.SetInstanceNormal(NewInstanceID, Normal);

				// Add UV to MeshDescription UVvertex buffer
				FVector2D UV = MeshIn->HasVertexUVs() ? FVector2D(MeshIn->GetVertexUV(Triangle[j])) : FVector2D::ZeroVector;
				FUVID UVID = Builder.AppendUV(UV, UVLayerIndex);
				
				// associate UVID with this instance
				InstanceUVIDMap.Add(NewInstanceID.GetValue(), UVID);				
			}
			InstanceTri[j] = InstanceList[InstanceElem];
			UVIDs[j] = InstanceUVIDMap[InstanceTri[j].GetValue()];
		}

		FTriangleID NewTriangleID = Builder.AppendTriangle(InstanceTri[0], InstanceTri[1], InstanceTri[2], AllGroupID);
		
		// append the UV triangle - builder takes care of the rest
		Builder.AppendUVTriangle(NewTriangleID, UVIDs[0], UVIDs[1], UVIDs[2], UVLayerIndex);
		
		if (bCopyGroupToPolyGroup)
		{
			Builder.SetPolyGroupID(NewTriangleID, MeshIn->GetTriangleGroup(TriID));
		}
	}

	Builder.ResumeMeshDescriptionIndexing();
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
		MapV[VertID] = Builder.AppendVertex((FVector)MeshIn->GetVertex(VertID));
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

		Tri() : V{ INDEX_NONE,INDEX_NONE,INDEX_NONE }
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

			int32 FoundInstance = INDEX_NONE;
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
			if (FoundInstance == INDEX_NONE)
			{
				FVertexInstanceID NewInstanceID = Builder.AppendInstance(MapV[VertID]);
				if (NormalOverlay)
				{
					int ElID = NormalOverlay->GetTriangle(TriID)[SubIdx];
					KnownInstanceIDs.Add(int32(ElID));
					FVector3f ElementNormal = ElID != -1 ? NormalOverlay->GetElement(ElID) : FVector3f::UnitZ();
					Builder.SetInstanceNormal(NewInstanceID, (FVector)ElementNormal);
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

					FVector2f ElementUV = ElID != -1 ? Overlay->GetElement(ElID) : FVector2f::Zero();
					Builder.SetInstanceUV(NewInstanceID, (FVector2D)ElementUV, UVLayerIndex);
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

		FTriangleID NewTriangleID = Builder.AppendTriangle(TriVertInstances[TriID].V[0], TriVertInstances[TriID].V[1], TriVertInstances[TriID].V[2], UsePolygonGroupID);

		if (bCopyGroupToPolyGroup)
		{
			Builder.SetPolyGroupID(NewTriangleID, MeshIn->GetTriangleGroup(TriID));
		}
	}
}





void FDynamicMeshToMeshDescription::Convert_NoSharedInstances(const FDynamicMesh3* MeshIn, FMeshDescription& MeshOut, bool bCopyTangents)
{
	
	const bool bHasAttributes = MeshIn->HasAttributes();

	// check if we have per-triangle material ID
	const FDynamicMeshMaterialAttribute* MaterialIDAttrib = (bHasAttributes && MeshIn->Attributes()->HasMaterialID()) ? MeshIn->Attributes()->GetMaterialID() : nullptr;

	// cache tangent space and UV overlay info
	const FDynamicMeshNormalOverlay* NormalOverlay = bHasAttributes ? MeshIn->Attributes()->PrimaryNormals() : nullptr;
	const FDynamicMeshNormalOverlay* TangentOverlay = bHasAttributes ? MeshIn->Attributes()->PrimaryTangents() : nullptr;
	const FDynamicMeshNormalOverlay* BiTangentOverlay = bHasAttributes ? MeshIn->Attributes()->PrimaryBiTangents() : nullptr;

	const int32 NumUVLayers = bHasAttributes ? MeshIn->Attributes()->NumUVLayers() : 0;
	
	// cache the UV layers
	TArray<const FDynamicMeshUVOverlay*> UVLayers;
	for (int32 k = 0; k < NumUVLayers; ++k)
	{
		UVLayers.Add(MeshIn->Attributes()->GetUVLayer(k));
	}


	MeshOut.Empty();

	FMeshDescriptionBuilder Builder;
	Builder.SetMeshDescription(&MeshOut);

	bool bCopyGroupToPolyGroup = false;
	if (ConversionOptions.bSetPolyGroups && MeshIn->HasTriangleGroups())
	{
		Builder.EnablePolyGroups();
		bCopyGroupToPolyGroup = true;
	}

	bool bCopyVertexColors = MeshIn->HasVertexColors();		// always copy when we are baking new mesh? should this be a config option?

	// disable indexing during the full build of the mesh
	Builder.SuspendMeshDescriptionIndexing();

	// allocate
	Builder.ReserveNewVertices(MeshIn->VertexCount());

	// create "vertex buffer" in MeshDescription
	TArray<FVertexID> MapV; MapV.SetNum(MeshIn->MaxVertexID());
	for (int VertID : MeshIn->VertexIndicesItr())
	{
		MapV[VertID] = Builder.AppendVertex((FVector)MeshIn->GetVertex(VertID));
	}

	// create UV vertex buffer in MeshDescription
	Builder.SetNumUVLayers(NumUVLayers);

	TArray<TArray<FUVID>> MapUVArray; MapUVArray.SetNum(NumUVLayers);
	for (int32 k = 0; k < NumUVLayers; ++k)
	{
		const FDynamicMeshUVOverlay* UVOverlay = UVLayers[k];
		TArray<FUVID>& MapUV = MapUVArray[k];

		MapUV.SetNum(UVOverlay->MaxElementID());
		Builder.ReserveNewUVs(UVOverlay->ElementCount(), k); 

		for (int32 ElementID : UVOverlay->ElementIndicesItr())
		{
			const FVector2D UVvalue = (FVector2D)UVOverlay->GetElement(ElementID); 
			MapUV[ElementID] = Builder.AppendUV(UVvalue, k);
		}
	}
	


	FPolygonGroupID ZeroPolygonGroupID = Builder.AppendPolygonGroup();

	
	// construct a function that will transfer tangent space data. 
	// if the DynamicMesh has a full tangent space: a normal, tangent and bitangent sign will be transfered
	//                                            otherwise just transfer the normal if it exists
	const bool bCopyFullTangentSpace = bCopyTangents && (NormalOverlay != nullptr && TangentOverlay != nullptr && BiTangentOverlay != nullptr);

	TFunction<void(int TriID, FVertexInstanceID TriVertInstances[3])> TangetSpaceInstanceSetter;
	if (bCopyFullTangentSpace) // create function that sets the tangent space
	{
		TangetSpaceInstanceSetter = [NormalOverlay, TangentOverlay, BiTangentOverlay, &Builder](int TriID, FVertexInstanceID TriVertInstances[3])
		{
			FIndex3i NormalTri = NormalOverlay->GetTriangle(TriID);
			FIndex3i TangentTri = TangentOverlay->GetTriangle(TriID);
			FIndex3i BiTangentTri = BiTangentOverlay->GetTriangle(TriID);

			for (int32 j = 0; j < 3; ++j)
			{
				const FVertexInstanceID CornerInstanceID = TriVertInstances[j];
				
				const FVector3f TriVertNormal = (NormalOverlay->IsElement(NormalTri[j]))  ? NormalOverlay->GetElement(NormalTri[j]) : FVector3f(FVector::UpVector);
				const FVector3f TriVertTangent = (TangentOverlay->IsElement(TangentTri[j])) ? TangentOverlay->GetElement(TangentTri[j]) :  FVector3f(FVector::ForwardVector);
				const FVector3f TriVertBiTangent = (BiTangentOverlay->IsElement(BiTangentTri[j])) ? BiTangentOverlay->GetElement(BiTangentTri[j]) : FVector3f(FVector::RightVector);

				// infer sign
				float BiTangentSign = VectorUtil::BitangentSign(TriVertNormal, TriVertTangent, TriVertBiTangent);

				// set the tangent space
				Builder.SetInstanceTangentSpace(CornerInstanceID, (FVector)TriVertNormal, (FVector)TriVertTangent, BiTangentSign);
			}
			
		};
	}
	else if (NormalOverlay != nullptr) // create function that just sets the normals
	{
		TangetSpaceInstanceSetter = [NormalOverlay, &Builder](int TriID, FVertexInstanceID TriVertInstances[3])
		{
			FIndex3i NormalTri = NormalOverlay->GetTriangle(TriID);
			for (int32 j = 0; j < 3; ++j)
			{
				FVertexInstanceID CornerInstanceID = TriVertInstances[j];
				FVector TriVertNormal = (NormalOverlay->IsElement(NormalTri[j])) ? (FVector)NormalOverlay->GetElement(NormalTri[j]) : FVector::UpVector;
				Builder.SetInstanceNormal(CornerInstanceID, TriVertNormal);
			}
		};
	}
	else  // dummy function that does nothing 
	{
		TangetSpaceInstanceSetter = [](int TriID, FVertexInstanceID TriVertInstances[3]){ return;};
	}


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

	

	TArray<FIndex3i> UVTris;
	UVTris.SetNum(NumUVLayers);

	TArray<FTriangleID> IndexToTriangleIDMap;
	IndexToTriangleIDMap.SetNum(MeshIn->MaxTriangleID());

	for (int TriID : MeshIn->TriangleIndicesItr())
	{
		FIndex3i Triangle = MeshIn->GetTriangle(TriID);

		// create new vtx instances for each triangle
		FVertexInstanceID TriVertInstances[3];
		for (int32 j = 0; j < 3; ++j)
		{
			const FVertexID TriVertex = MapV[Triangle[j]];
			TriVertInstances[j] = Builder.AppendInstance(TriVertex);
		}

		// copy vertex colors. This writes each color multiple times, but that is not expensive in this context
		if (bCopyVertexColors)
		{
			for (int32 j = 0; j < 3; ++j)
			{
				FVector3f Color = MeshIn->GetVertexColor(Triangle[j]);
				Builder.SetInstanceColor(TriVertInstances[j], FVector4(Color.X, Color.Y, Color.Z, 1.0f));
			}
		}

		// transfer material index to MeshDescription polygon group (by convention)
		FPolygonGroupID UsePolygonGroupID = ZeroPolygonGroupID;
		if (MaterialIDAttrib)
		{
			int32 MaterialID;
			MaterialIDAttrib->GetValue(TriID, &MaterialID);
			UsePolygonGroupID = FPolygonGroupID(MaterialID);
		}

		// add the triangle to MeshDescription
		FTriangleID NewTriangleID = Builder.AppendTriangle(TriVertInstances[0], TriVertInstances[1], TriVertInstances[2], UsePolygonGroupID); 
		IndexToTriangleIDMap[TriID] = NewTriangleID;

		// transfer UVs.  Note the Builder sets both the shared and per-instance UVs from this
		for (int32 k = 0; k < NumUVLayers; ++k)
		{ 
			FUVID UVIDs[3] = {FUVID(-1), FUVID(-1), FUVID(-1)};

			// add zero uvs for unset triangles.  this mimics the old version of this code.
			if (!UVLayers[k]->IsSetTriangle(TriID))
			{
				for (int32 j = 0; j < 3; ++j)
				{ 
					UVIDs[j] = Builder.AppendUV(FVector2D::ZeroVector, k);
				}
			}
			else
			{ 
				const TArray<FUVID>& MapUV = MapUVArray[k];
		
				// triangle of UV element ids from dynamic mesh.  references values already stored in MeshDescription.
				FIndex3i UVTri = UVLayers[k]->GetTriangle(TriID);

				// translate to MeshDescription Ids
				for (int32 j = 0; j < 3; ++j)
				{ 
					UVIDs[j] = MapUV[ UVTri[j] ];
				}
			}
			
			// append the UV triangle - builder takes care of the rest
			Builder.AppendUVTriangle(NewTriangleID, UVIDs[0], UVIDs[1], UVIDs[2], k); 

		}
		
		// transfer tangent space. 
		// NB: MeshDescription doesn't store and explicit bitangent, so this conversion isn't perfect.
		// NB: only per-instance normals , tangents, bitangent sign are supported in MeshDescription at this time
		// NB: will need to be updated to follow the pattern used in UVs above when MeshDescription supports shared tangent space elements. 
		TangetSpaceInstanceSetter(TriID, TriVertInstances);


		if (bCopyGroupToPolyGroup)
		{
			Builder.SetPolyGroupID(NewTriangleID, MeshIn->GetTriangleGroup(TriID));
		}
	}

	// convert polygroup layers
	ConvertPolygroupLayers(MeshIn, MeshOut, IndexToTriangleIDMap);

	Builder.ResumeMeshDescriptionIndexing();
}



void FDynamicMeshToMeshDescription::ConvertPolygroupLayers(const FDynamicMesh3* MeshIn, FMeshDescription& MeshOut, const TArray<FTriangleID>& IndexToTriangleIDMap)
{
	if (MeshIn->Attributes() == nullptr) return;

	TAttributesSet<FTriangleID>& TriAttribsSet = MeshOut.TriangleAttributes();

	for (int32 li = 0; li < MeshIn->Attributes()->NumPolygroupLayers(); ++li)
	{
		const FDynamicMeshPolygroupAttribute* Polygroups = MeshIn->Attributes()->GetPolygroupLayer(li);
		FName LayerName = Polygroups->GetName();
	
		// Find existing attribute with the same name. If not found, create a new one.
		TTriangleAttributesRef<int32> Attribute;
		if (TriAttribsSet.HasAttribute(LayerName))
		{
			Attribute = TriAttribsSet.GetAttributesRef<int32>(LayerName);
		}
		else
		{
			TriAttribsSet.RegisterAttribute<int32>(LayerName, 1, 0, EMeshAttributeFlags::AutoGenerated);
			Attribute = TriAttribsSet.GetAttributesRef<int32>(LayerName);
		}
		if (ensure(Attribute.IsValid()))
		{
			for (int32 tid : MeshIn->TriangleIndicesItr())
			{
				FTriangleID TriangleID = IndexToTriangleIDMap[tid];
				int32 GroupID = Polygroups->GetValue(tid);
				Attribute.Set(TriangleID, GroupID);
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("FDynamicMeshToMeshDescription::ConvertPolygroupLayers - could not create attribute named %s"), *LayerName.ToString());
		}
	}
}