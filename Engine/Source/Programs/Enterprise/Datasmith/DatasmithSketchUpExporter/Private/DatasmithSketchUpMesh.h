// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// SketchUp SDK.
#include "DatasmithSketchUpSDKBegins.h"
#include "SketchUpAPI/geometry.h"
#include "SketchUpAPI/model/uv_helper.h"
#include "DatasmithSketchUpSDKCeases.h"

// Datasmith SDK.
#include "Containers/Array.h"
#include "Containers/Set.h"
#include "Containers/UnrealString.h"
#include "Templates/SharedPointer.h"

class FDatasmithMesh;
class IDatasmithScene;


// The vertex indices of a mesh triangle in a tessellated SketchUp face.
struct SMeshTriangleIndices
{
	size_t IndexA; // index of the first triangle vertex
	size_t IndexB; // index of the second triangle vertex
	size_t IndexC; // index of the third triangle vertex
};

// The vertex normals of a mesh triangle in a tessellated SketchUp face.
struct SMeshTriangleNormals
{
	SUVector3D NormalA; // normal of the first triangle vertex
	SUVector3D NormalB; // normal of the second triangle vertex
	SUVector3D NormalC; // normal of the third triangle vertex
};


class FDatasmithSketchUpMesh
{
public:

	// Bake SketchUp component definition faces into a list of component meshes.
	static void BakeMeshes(
		TCHAR const*                                InSOwnerGUID,         // SketchUp owner component GUID
		TCHAR const*                                InSOwnerName,         // SketchUp owner component name
		SULayerRef                                  InSInheritedLayerRef, // SketchUp inherited layer
		TArray<SUFaceRef> const&                    InSSourceFaces,       // source SketchUp faces
		TArray<TSharedPtr<FDatasmithSketchUpMesh>>& OutBakedMeshes        // baked component meshes
	);

	// Clear the list of mesh definitions.
	static void ClearMeshDefinitionList();

	// Export the mesh definitions into Datasmith mesh element files.
	static void ExportDefinitions(
		TSharedRef<IDatasmithScene> IODSceneRef,        // Datasmith scene to populate
		TCHAR const*                InMeshElementFolder // Datasmith mesh element folder path
	);

	// Return the mesh index.
	int32 GetMeshIndex() const;

	// Return the Datasmith mesh element file name (without any path or extension).
	FString const& GetMeshElementName() const;

	// Return whether or not the set of all the mesh material IDs contains the inherited materiel ID.
	bool UsesInheritedMaterialID() const;

private:

	// Get the face ID of a SketckUp face.
	static int32 GetFaceID(
		SUFaceRef InSFaceRef // valid SketckUp face
	);

	// Get the edge ID of a SketckUp edge.
	static int32 GetEdgeID(
		SUEdgeRef InSEdgeRef // valid SketckUp edge
	);

	// Return whether or not a SketckUp face is visible in the current SketchUp scene.
	static bool IsVisible(
		SUFaceRef  InSFaceRef,          // valid SketchUp face
		SULayerRef InSInheritedLayerRef // SketchUp inherited layer
	);

	FDatasmithSketchUpMesh(
		TCHAR const* InSOwnerGUID, // SketchUp owner component GUID
		TCHAR const* InSOwnerName, // SketchUp owner component name
		int32        InMeshIndex   // mesh index inside the SketchUp component
	);

	// No copying or copy assignment allowed for this class.
	FDatasmithSketchUpMesh(FDatasmithSketchUpMesh const&) = delete;
	FDatasmithSketchUpMesh& operator=(FDatasmithSketchUpMesh const&) = delete;

	// Tessellate a SketchUp face into a triangle mesh merged into the combined mesh.
	void AddFace(
		SUFaceRef InSFaceRef // valid source SketchUp face to tessellate and combine
	);

	// Return whether or not the combined mesh contains geometry.
	bool ContainsGeometry() const;

	// Export the combined mesh into a Datasmith mesh element file.
	void ExportMesh(
		TSharedRef<IDatasmithScene> IODSceneRef,        // Datasmith scene to populate
		TCHAR const*                InMeshElementFolder // Datasmith mesh element folder path
	);

	// Convert the combined mesh into a Datasmith mesh.
	void ConvertMesh(
		FDatasmithMesh& OutDMesh // Datasmith mesh to populate
	) const;

private:

	// List of mesh definitions.
	static TArray<TSharedPtr<FDatasmithSketchUpMesh>> MeshDefinitionList;

	// SketchUp component name of the mesh owner component.
	FString SOwnerName;

	// Index of the mesh inside the SketchUp component.
	int32 MeshIndex;

	// Combined mesh vertex points.
	TArray<SUPoint3D> MeshVertexPoints;

	// Combined mesh vertex normals.
	TArray<SUVector3D> MeshVertexNormals;

	// Combined mesh vertex UVQ texture coordinates.
	TArray<SUUVQ> MeshVertexUVQs;

	// Combined mesh triangle vertex indices.
	TArray<SMeshTriangleIndices> MeshTriangleIndices;

	// Combined mesh triangle material IDs.
	TArray<int32> MeshTriangleMaterialIDs;

	// Set of all the material IDs used by the combined mesh triangles.
	TSet<int32> MeshTriangleMaterialIDSet;

	// Datasmith mesh element file name (without any path or extension).
	FString MeshElementName;
};


inline int32 FDatasmithSketchUpMesh::GetMeshIndex() const
{
	return MeshIndex;
}

inline FString const& FDatasmithSketchUpMesh::GetMeshElementName() const
{
	return MeshElementName;
}

inline bool FDatasmithSketchUpMesh::ContainsGeometry() const
{
	return (MeshVertexPoints.Num() > 0 && MeshTriangleIndices.Num() > 0);
}
