// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DatasmithSketchUpMesh.h"

// SketchUp to Datasmith exporter classes.
#include "DatasmithSketchUpMaterial.h"
#include "DatasmithSketchUpString.h"
#include "DatasmithSketchUpSummary.h"

// SketchUp SDK.
#include "DatasmithSketchUpSDKBegins.h"
#include "SketchUpAPI/model/drawing_element.h"
#include "SketchUpAPI/model/edge.h"
#include "SketchUpAPI/model/face.h"
#include "SketchUpAPI/model/entity.h"
#include "SketchUpAPI/model/layer.h"
#include "SketchUpAPI/model/mesh_helper.h"
#include "SketchUpAPI/model/uv_helper.h"
#include "DatasmithSketchUpSDKCeases.h"

// Datasmith SDK.
#include "DatasmithMesh.h"
#include "DatasmithMeshExporter.h"
#include "DatasmithSceneExporter.h"
#include "DatasmithSceneFactory.h"
#include "DatasmithUtils.h"
#include "Misc/SecureHash.h"


TArray<TSharedPtr<FDatasmithSketchUpMesh>> FDatasmithSketchUpMesh::MeshDefinitionList;

void FDatasmithSketchUpMesh::BakeMeshes(
	TCHAR const*                                InSOwnerGUID,
	TCHAR const*                                InSOwnerName,
	SULayerRef                                  InSInheritedLayerRef,
	TArray<SUFaceRef> const&                    InSSourceFaces,
	TArray<TSharedPtr<FDatasmithSketchUpMesh>>& OutBakedMeshes
)
{
	TSet<int32> ScannedFaceIDSet;
	TSet<int32> ScannedEdgeIDSet;

	// Mesh index inside the SketchUp component definition.
	int32 MeshIndex = 0;

	for (SUFaceRef SSourceFaceRef : InSSourceFaces)
	{
		// Get the Source SketckUp face ID.
		int32 SSourceFaceID = GetFaceID(SSourceFaceRef);

		// Do not scan more than once a valid SketckUp face.
		if (SUIsInvalid(SSourceFaceRef) || ScannedFaceIDSet.Contains(SSourceFaceID))
		{
			continue;
		}

		//ADD_TRACE_LINE(TEXT("Mesh %ls %d"), InSOwnerName, MeshIndex);

		// Create a mesh combining the geometry of the SketchUp connected faces.
		TSharedPtr<FDatasmithSketchUpMesh> MeshPtr = TSharedPtr<FDatasmithSketchUpMesh>(new FDatasmithSketchUpMesh(InSOwnerGUID, InSOwnerName, ++MeshIndex));

		// The source SketchUp face needs to be scanned once.
		TArray<SUFaceRef> FacesToScan;
		FacesToScan.Add(SSourceFaceRef);
		ScannedFaceIDSet.Add(SSourceFaceID);

		while (FacesToScan.Num() > 0)
		{
			SUFaceRef SScannedFaceRef = FacesToScan.Pop(/*bAllowShrinking*/ false);

			int64 SFacePID = 0;
			SUEntityGetPersistentID(SUFaceToEntity(SScannedFaceRef), &SFacePID);
			//ADD_TRACE_LINE(TEXT("   Face %lld"), SFacePID);

			// Get whether or not the SketckUp face is visible in the current SketchUp scene.
			if (IsVisible(SScannedFaceRef, InSInheritedLayerRef))
			{
				// Tessellate the SketchUp face into a triangle mesh merged into the combined mesh.
				MeshPtr->AddFace(SScannedFaceRef);
			}

			// Get the number of SketchUp face edges.
			size_t SEdgeCount = 0;
			SUFaceGetNumEdges(SScannedFaceRef, &SEdgeCount); // we can ignore the returned SU_RESULT

			// Retrieve the SketchUp face edges.
			TArray<SUEdgeRef> SEdges;
			SEdges.Init(SU_INVALID, SEdgeCount);
			SUFaceGetEdges(SScannedFaceRef, SEdgeCount, SEdges.GetData(), &SEdgeCount); // we can ignore the returned SU_RESULT
			SEdges.SetNum(SEdgeCount);

			for (SUEdgeRef SEdgeRef : SEdges)
			{
				// Get the SketckUp edge ID.
				int32 SEdgeID = GetEdgeID(SEdgeRef);

				// Avoid scanning more than once this SketckUp edge.
				if (!ScannedEdgeIDSet.Contains(SEdgeID))
				{
					ScannedEdgeIDSet.Add(SEdgeID);

					// Get the number of SketchUp faces associated with the edge.
					size_t SFaceCount = 0;
					SUEdgeGetNumFaces(SEdgeRef, &SFaceCount); // we can ignore the returned SU_RESULT

					// Retrieve the SketchUp faces associated with the edge.
					TArray<SUFaceRef> SFaces;
					SFaces.Init(SU_INVALID, SFaceCount);
					SUEdgeGetFaces(SEdgeRef, SFaceCount, SFaces.GetData(), &SFaceCount); // we can ignore the returned SU_RESULT
					SFaces.SetNum(SFaceCount);

					for (SUFaceRef SFaceRef : SFaces)
					{
						// Get the SketckUp face ID.
						int32 SFaceID = GetFaceID(SFaceRef);

						// Avoid scanning more than once this SketckUp face.
						if (!ScannedFaceIDSet.Contains(SFaceID))
						{
							ScannedFaceIDSet.Add(SFaceID);

							// This SketchUp face is connected and needs to be scanned further.
							FacesToScan.Add(SFaceRef);
						}
					}
				}
			}
		}

		if (MeshPtr->ContainsGeometry())
		{
			// Add the combined mesh into our list of mesh definitions.
			MeshDefinitionList.Add(MeshPtr);

			// Add the combined mesh into the list of baked component meshes.
			OutBakedMeshes.Add(MeshPtr);
		}
	}
}

void FDatasmithSketchUpMesh::ClearMeshDefinitionList()
{
	// Remove all entries from our list of mesh definitions.
	MeshDefinitionList.Empty();
}

void FDatasmithSketchUpMesh::ExportDefinitions(
	TSharedRef<IDatasmithScene> IODSceneRef,
	TCHAR const*                InMeshElementFolder
)
{
	// Export the mesh definitions used by some components.
	for (auto const& MeshDefinitionPtr : MeshDefinitionList)
	{
		// Export the combined mesh into a Datasmith mesh element file.
		MeshDefinitionPtr->ExportMesh(IODSceneRef, InMeshElementFolder);
	}
}

bool FDatasmithSketchUpMesh::UsesInheritedMaterialID() const
{
	return MeshTriangleMaterialIDSet.Contains(FDatasmithSketchUpMaterial::INHERITED_MATERIAL_ID);
}

int32 FDatasmithSketchUpMesh::GetFaceID(
	SUFaceRef InSFaceRef
)
{
	// Get the SketckUp face ID.
	int32 SFaceID = 0;
	SUEntityGetID(SUFaceToEntity(InSFaceRef), &SFaceID); // we can ignore the returned SU_RESULT

	return SFaceID;
}

int32 FDatasmithSketchUpMesh::GetEdgeID(
	SUEdgeRef InSEdgeRef
)
{
	// Get the SketckUp edge ID.
	int32 SEdgeID = 0;
	SUEntityGetID(SUEdgeToEntity(InSEdgeRef), &SEdgeID); // we can ignore the returned SU_RESULT

	return SEdgeID;
}

bool FDatasmithSketchUpMesh::IsVisible(
	SUFaceRef  InSFaceRef,
	SULayerRef InSInheritedLayerRef
)
{
	// Get the flag indicating whether or not the SketchUp face is hidden.
	bool bFaceHidden = false;
	SUDrawingElementGetHidden(SUFaceToDrawingElement(InSFaceRef), &bFaceHidden); // we can ignore the returned SU_RESULT

	// Retrieve the SketckUp face layer.
	SULayerRef SFaceLayerRef = SU_INVALID;
	SUDrawingElementGetLayer(SUFaceToDrawingElement(InSFaceRef), &SFaceLayerRef); // we can ignore the returned SU_RESULT

	// Retrieve the SketchUp face layer name.
	FString SFaceLayerName;
	SU_GET_STRING(SULayerGetName, SFaceLayerRef, SFaceLayerName);

	// Get the SketchUp face effective layer.
	SULayerRef SFaceEffectiveLayerRef = SFaceLayerName.Equals(TEXT("Layer0")) ? InSInheritedLayerRef : SFaceLayerRef;

	// Get the flag indicating whether or not the SketchUp face effective layer is visible.
	bool bSFaceEffectiveLayerVisible = true;
	SULayerGetVisibility(SFaceEffectiveLayerRef, &bSFaceEffectiveLayerVisible); // we can ignore the returned SU_RESULT

	return (!bFaceHidden && bSFaceEffectiveLayerVisible);
}

FDatasmithSketchUpMesh::FDatasmithSketchUpMesh(
	TCHAR const* InSOwnerGUID,
	TCHAR const* InSOwnerName,
	int32        InMeshIndex
) :
	SOwnerName(InSOwnerName),
	MeshIndex(InMeshIndex)
{
	// Make a unique Datasmith mesh element file name.
	// Prefix the file name with "M" in order to keep all the Datasmith mesh element files contiguous in the asset folder.
	MeshElementName = FString::Printf(TEXT("M%ls_%d"), *FMD5::HashAnsiString(InSOwnerGUID), MeshIndex);
}

void FDatasmithSketchUpMesh::AddFace(
	SUFaceRef InSFaceRef
)
{
	// Retrieve the SketchUp face front material.
	SUMaterialRef SFrontMaterialRef = SU_INVALID;
	SUFaceGetFrontMaterial(InSFaceRef, &SFrontMaterialRef); // we can ignore the returned SU_RESULT

	// Retrieve the SketchUp face back material.
	SUMaterialRef SBackMaterialRef = SU_INVALID;
	SUFaceGetBackMaterial(InSFaceRef, &SBackMaterialRef); // we can ignore the returned SU_RESULT

	// Fall back on the back material when there is no face front material, but there is a face back material.
	bool bUseBackMaterial = SUIsInvalid(SFrontMaterialRef) && SUIsValid(SBackMaterialRef);
	bool bUseFrontMaterial = !bUseBackMaterial;

	// Create a UV helper for the SketchUp face.
	SUTextureWriterRef STextureWriterRef = SU_INVALID;
	SUUVHelperRef SUVHelperRef = SU_INVALID;
	SUFaceGetUVHelper(InSFaceRef, bUseFrontMaterial, bUseBackMaterial, STextureWriterRef, &SUVHelperRef); // we can ignore the returned SU_RESULT

	// Tessellate the SketchUp face into a SketchUp triangle mesh.
	SUMeshHelperRef SMeshRef = SU_INVALID;
	SUMeshHelperCreateWithUVHelper(&SMeshRef, InSFaceRef, SUVHelperRef); // we can ignore the returned SU_RESULT

	// Get the number of mesh vertices.
	size_t SVertexCount = 0;
	SUMeshHelperGetNumVertices(SMeshRef, &SVertexCount); // we can ignore the returned SU_RESULT

	// Get the number of mesh triangles.
	size_t STriangleCount = 0;
	SUMeshHelperGetNumTriangles(SMeshRef, &STriangleCount); // we can ignore the returned SU_RESULT

	if (SVertexCount == 0 || STriangleCount == 0)
	{
		// Release the SketchUp triangle mesh.
		SUMeshHelperRelease(&SMeshRef); // we can ignore the returned SU_RESULT

		// Release SketchUp face UV helper.
		SUUVHelperRelease(&SUVHelperRef); // we can ignore the returned SU_RESULT

		return;
	}

	// Retrieve the mesh vertex points.
	size_t SVertexPointCount = SVertexCount;
	TArray<SUPoint3D> SVertexPoints;
	SVertexPoints.SetNumUninitialized(SVertexPointCount);
	SUMeshHelperGetVertices(SMeshRef, SVertexPointCount, SVertexPoints.GetData(), &SVertexPointCount); // we can ignore the returned SU_RESULT
	SVertexPoints.SetNum(SVertexPointCount);

	// Retrieve the mesh vertex normals.
	size_t SVertexNormalCount = SVertexCount;
	TArray<SUVector3D> SVertexNormals;
	SVertexNormals.SetNumUninitialized(SVertexNormalCount);
	SUMeshHelperGetNormals(SMeshRef, SVertexNormalCount, SVertexNormals.GetData(), &SVertexNormalCount); // we can ignore the returned SU_RESULT
	SVertexNormals.SetNum(SVertexNormalCount);

	// Retrieve the mesh triangle vertex indices, by strides of three indices per triangle.
	size_t STriangleVertexIndexCount = STriangleCount * 3;
	TArray<size_t> STriangleVertexIndices;
	STriangleVertexIndices.SetNumUninitialized(STriangleVertexIndexCount);
	SUMeshHelperGetVertexIndices(SMeshRef, STriangleVertexIndexCount, STriangleVertexIndices.GetData(), &STriangleVertexIndexCount); // we can ignore the returned SU_RESULT
	STriangleVertexIndices.SetNum(STriangleVertexIndexCount);

	// Get the SketchUp triangle mesh vertex offset into the combined mesh vertex vector.
	int32 MeshVertexIndexOffset = MeshVertexPoints.Num();

	// Combine the mesh vertex points.
	MeshVertexPoints.Append(SVertexPoints);

	MeshVertexNormals.Reserve(MeshVertexNormals.Num() + SVertexNormalCount);
	MeshVertexUVQs.Reserve(MeshVertexUVQs.Num() + SVertexPointCount);
	MeshTriangleIndices.Reserve(MeshTriangleIndices.Num() + STriangleVertexIndexCount);

	if (bUseFrontMaterial)
	{
		// Combine the mesh vertex normals.
		MeshVertexNormals.Append(SVertexNormals);

		// Combine the mesh vertex front UVQ texture coordinates.
		for (size_t SVertexPointNo = 0; SVertexPointNo < SVertexPointCount; SVertexPointNo++)
		{
			// Retrieve the front UVQ texture coordinate of the mesh vertex.
			SUUVQ SUVQ;
			SUUVHelperGetFrontUVQ(SUVHelperRef, &SVertexPoints[SVertexPointNo], &SUVQ); // we can ignore the returned SU_RESULT

			MeshVertexUVQs.Add(SUVQ);
		}

		// Combine the mesh front-facing triangle vertex indices.
		for (size_t STriangleVertexIndexNo = 0; STriangleVertexIndexNo < STriangleVertexIndexCount;)
		{
			size_t IndexA = MeshVertexIndexOffset + STriangleVertexIndices[STriangleVertexIndexNo++];
			size_t IndexB = MeshVertexIndexOffset + STriangleVertexIndices[STriangleVertexIndexNo++];
			size_t IndexC = MeshVertexIndexOffset + STriangleVertexIndices[STriangleVertexIndexNo++];

			SMeshTriangleIndices TriangleIndices = { IndexA, IndexB, IndexC };

			MeshTriangleIndices.Add(TriangleIndices);
		}
	}
	else
	{
		// Combine the mesh vertex normals.
		for (size_t SVertexNormalNo = 0; SVertexNormalNo < SVertexNormalCount; SVertexNormalNo++)
		{
			// Reverse the mesh vertex normal.
			SUVector3D SVertexNormal = SVertexNormals[SVertexNormalNo];
			SVertexNormal.x = -SVertexNormal.x;
			SVertexNormal.y = -SVertexNormal.y;
			SVertexNormal.z = -SVertexNormal.z;

			MeshVertexNormals.Add(SVertexNormal);
		}

		// Combine the mesh vertex back UVQ texture coordinates.
		for (size_t SVertexPointNo = 0; SVertexPointNo < SVertexPointCount; SVertexPointNo++)
		{
			// Retrieve the back UVQ texture coordinate of the mesh vertex.
			SUUVQ SUVQ;
			SUUVHelperGetBackUVQ(SUVHelperRef, &SVertexPoints[SVertexPointNo], &SUVQ); // we can ignore the returned SU_RESULT

			MeshVertexUVQs.Add(SUVQ);
		}

		// Combine the mesh back-facing triangle vertex indices.
		for (size_t STriangleVertexIndexNo = 0; STriangleVertexIndexNo < STriangleVertexIndexCount;)
		{
			size_t IndexA = MeshVertexIndexOffset + STriangleVertexIndices[STriangleVertexIndexNo++];
			size_t IndexB = MeshVertexIndexOffset + STriangleVertexIndices[STriangleVertexIndexNo++];
			size_t IndexC = MeshVertexIndexOffset + STriangleVertexIndices[STriangleVertexIndexNo++];

			SMeshTriangleIndices TriangleIndices = { IndexC, IndexB, IndexA };

			MeshTriangleIndices.Add(TriangleIndices);
		}
	}

	// Release the SketchUp triangle mesh.
	SUMeshHelperRelease(&SMeshRef); // we can ignore the returned SU_RESULT

	// Release SketchUp face UV helper.
	SUUVHelperRelease(&SUVHelperRef); // we can ignore the returned SU_RESULT

	// Inherit SketchUp material by default.
	int32 SMaterialID = FDatasmithSketchUpMaterial::INHERITED_MATERIAL_ID;

	// Get the SketckUp material ID.
	if (bUseFrontMaterial)
	{
		if (SUIsValid(SFrontMaterialRef))
		{
			// Get the front material ID of the SketckUp front material.
			SMaterialID = FDatasmithSketchUpMaterial::GetMaterialID(SFrontMaterialRef);
		}
	}
	else // bUseBackMaterial
	{
		if (SUIsValid(SBackMaterialRef))
		{
			// Get the back material ID of the SketckUp back material.
			SMaterialID = FDatasmithSketchUpMaterial::GetMaterialID(SBackMaterialRef);
		}
	}

	// Combine the mesh triangle material IDs.
	TArray<int32> SMaterialIDs;
	SMaterialIDs.Init(SMaterialID, STriangleCount);
	MeshTriangleMaterialIDs.Append(SMaterialIDs);

	// Add the material ID to the set of all the material IDs used by the combined mesh triangles.
	MeshTriangleMaterialIDSet.Add(SMaterialID);
}

void FDatasmithSketchUpMesh::ExportMesh(
	TSharedRef<IDatasmithScene> IODSceneRef,
	TCHAR const*                InMeshElementFolder
)
{
	// Convert the combined mesh into a Datasmith mesh.
	FDatasmithMesh DMesh;
	ConvertMesh(DMesh);

	FString MeshLabel = FDatasmithUtils::SanitizeObjectName(SOwnerName);

	// Export the Datasmith mesh into a Datasmith mesh element and its Unreal object file.
	FDatasmithMeshExporter DMeshExporter;
	TSharedPtr<IDatasmithMeshElement> DMeshElementPtr = DMeshExporter.ExportToUObject(InMeshElementFolder, *MeshElementName, DMesh, nullptr, FDatasmithExportOptions::LightmapUV);

	// Check for any Unreal object file export errors.
	FString DMeshExporterError = DMeshExporter.GetLastError();
	if (DMeshExporterError.IsEmpty())
	{
		// Set the mesh element label used in the Unreal UI.
		DMeshElementPtr->SetLabel(*MeshLabel);

		// Add the non-inherited materiels used by the combined mesh triangles.
		for (int32 MeshMaterialID : MeshTriangleMaterialIDSet)
		{
			if (MeshMaterialID != FDatasmithSketchUpMaterial::INHERITED_MATERIAL_ID)
			{
				// Get the material name sanitized for Datasmith.
				FString const& MeshMaterialName = FDatasmithSketchUpMaterial::GetLocalizedMaterialName(MeshMaterialID);

				// Add the material to the Datasmith mesh element.
				DMeshElementPtr->SetMaterial(*MeshMaterialName, MeshMaterialID);
			}
		}

		// Add the Datasmith mesh element to the Datasmith scene.
		IODSceneRef->AddMesh(DMeshElementPtr);

		// ADD_TRACE_LINE(TEXT("Export mesh %ls (%ls)"), *MeshElementName, *MeshLabel);
	}
	else
	{
		// Append the error message to the export summary.
		ADD_SUMMARY_LINE(TEXT("WARNING: Cannot export mesh %ls (%ls): %ls"), *MeshElementName, *MeshLabel, *DMeshExporterError);
	}
}

void FDatasmithSketchUpMesh::ConvertMesh(
	FDatasmithMesh& OutDMesh
) const
{
	// Get the number of mesh vertices (must be > 0).
	int32 VertexCount = MeshVertexPoints.Num();

	// Set the number of vertices of the exported Datasmith mesh.
	OutDMesh.SetVerticesCount(VertexCount);

	// Convert vertex points from SketchUp right-handed Z-up coordinates to Unreal left-handed Z-up coordinates.
	// To avoid perturbating X, which is forward in Unreal, the handedness conversion is done by flipping the side vector Y.
	// SketchUp uses inches as internal system unit for all 3D coordinates in the model while Unreal uses centimeters.
	const float UnitScale = 2.54; // centimeters per inch
	for (int32 VertexNo = 0; VertexNo < VertexCount; VertexNo++)
	{
		// Set the vertex point in the exported Datasmith mesh.
		SUPoint3D const& VertexPoint = MeshVertexPoints[VertexNo];
		OutDMesh.SetVertex(VertexNo, float(VertexPoint.x * UnitScale), float(-VertexPoint.y * UnitScale), float(VertexPoint.z * UnitScale));
	}

	// Set the number of Datasmith mesh UV channels.
	OutDMesh.SetUVChannelsCount(1);

	// Set the number of UV texture coordinates in Datasmith mesh UV channel 0.
	OutDMesh.SetUVCount(0, VertexCount);

	// Convert SketchUp UVQ texture coordinates to Datasmith UV texture coordinates.
	for (int32 VertexNo = 0; VertexNo < VertexCount; VertexNo++)
	{
		// Set and flip vertically the UV texture coordinates in Datasmith mesh UV channel 0.
		SUUVQ const& SUVQ = MeshVertexUVQs[VertexNo];
		OutDMesh.SetUV(0, VertexNo, SUVQ.u / SUVQ.q, -SUVQ.v / SUVQ.q);
	}

	// Get the number of mesh triangles (must be > 0).
	int32 TriangleCount = MeshTriangleIndices.Num();

	// Set the number of triangles of the exported Datasmith mesh.
	OutDMesh.SetFacesCount(TriangleCount);

	// Convert triangle vertex indices and normals from SketchUp right-handed Z-up coordinates to Unreal left-handed Z-up coordinates.
	// To avoid perturbating X, which is forward in Unreal, the handedness conversion is done by flipping the side vector Y.
	for (int32 TriangleNo = 0, NormalNo = 0; TriangleNo < TriangleCount; TriangleNo++)
	{
		// Set the triangle smoothing mask in the exported Datasmith mesh.
		uint32 SmoothingMask = 0; // no smoothing
		OutDMesh.SetFaceSmoothingMask(TriangleNo, SmoothingMask);

		// Set the triangle vertex indices in the exported Datasmith mesh.
		SMeshTriangleIndices const& TriangleIndices = MeshTriangleIndices[TriangleNo];
		OutDMesh.SetFace(TriangleNo, int32(TriangleIndices.IndexA), int32(TriangleIndices.IndexB), int32(TriangleIndices.IndexC), MeshTriangleMaterialIDs[TriangleNo]);

		// Set the triangle vertex normals in the exported Datasmith mesh.
		SMeshTriangleNormals TriangleNormals = { MeshVertexNormals[TriangleIndices.IndexA],
		                                         MeshVertexNormals[TriangleIndices.IndexB],
		                                         MeshVertexNormals[TriangleIndices.IndexC] };
		OutDMesh.SetNormal(NormalNo++, float(TriangleNormals.NormalA.x), float(-TriangleNormals.NormalA.y), float(TriangleNormals.NormalA.z));
		OutDMesh.SetNormal(NormalNo++, float(TriangleNormals.NormalB.x), float(-TriangleNormals.NormalB.y), float(TriangleNormals.NormalB.z));
		OutDMesh.SetNormal(NormalNo++, float(TriangleNormals.NormalC.x), float(-TriangleNormals.NormalC.y), float(TriangleNormals.NormalC.z));

		// Set the triangle UV coordinate indices in the exported Datasmith mesh.
		OutDMesh.SetFaceUV(TriangleNo, 0, int32(TriangleIndices.IndexA), int32(TriangleIndices.IndexB), int32(TriangleIndices.IndexC));
	}
}
