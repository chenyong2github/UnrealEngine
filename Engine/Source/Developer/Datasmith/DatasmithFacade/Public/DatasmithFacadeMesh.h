// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// Datasmith facade.
#include "DatasmithFacadeElement.h"

// Datasmith SDK classes.
class FDatasmithMesh;


class DATASMITHFACADE_API FDatasmithFacadeMesh :
	public FDatasmithFacadeElement
{
public:

	FDatasmithFacadeMesh(
		const TCHAR* InElementName, // Datasmith element name
		const TCHAR* InElementLabel // Datasmith element label
	);

	virtual ~FDatasmithFacadeMesh() {}

	// Add a vertex point (in right-handed Z-up coordinates) to the Datasmith static mesh.
	void AddVertex(
		float InX, // vertex position on the X axis
		float InY, // vertex position on the Y axis
		float InZ  // vertex position on the Z axis
	);

	// Add a vertex UV texture coordinate to the Datasmith static mesh.
	void AddUV(
		int   InChannel, // UV channel number
		float InU,       // U coordinate value
		float InV        // V coordinate value
	);

	// Add a triangle vertex index to the Datasmith static mesh.
	void AddTriangle(
		int InVertex1,       // index of the first triangle vertex
		int InVertex2,       // index of the second triangle vertex
		int InVertex3,       // index of the third triangle vertex
		int InMaterialID = 0 // triangle material ID
	);

	// Add a triangle vertex index to the Datasmith static mesh.
	void AddTriangle(
		int          InVertex1,     // index of the first triangle vertex
		int          InVertex2,     // index of the second triangle vertex
		int          InVertex3,     // index of the third triangle vertex
		const TCHAR* InMaterialName // triangle material name
	);

	// Add a triangle vertex normal (in right-handed Z-up coordinates) to the Datasmith static mesh.
	// Each stride of 3 normals corresponds to the 3 vertices of a triangle.
	void AddNormal(
		float InX, // normal direction on the X axis
		float InY, // normal direction on the Y axis
		float InZ  // normal direction on the Z axis
	);

	// Add a material name to the dictionary of material names utilized by the mesh.
	void AddMaterial(
		int          InMaterialId,  // utilized material ID
		const TCHAR* InMaterialName // utilized material name
	);

	// Return the number of vertices in the Datasmith static mesh.
	int GetVertexCount() const;

	// Return the number of triangles in the Datasmith static mesh.
	int GetTriangleCount() const;

	// Add a metadata string property to the Datasmith static mesh.
	virtual void AddMetadataString(
		const TCHAR* InPropertyName, // property name
		const TCHAR* InPropertyValue // property value
	);

#ifdef SWIG_FACADE
protected:
#endif

	// Return the built Datasmith static mesh asset.
	TSharedPtr<FDatasmithMesh> GetAsset() const;

	// Return the Datasmith static mesh element.
	TSharedPtr<IDatasmithMeshElement> GetMeshElement() const;

	// Build the Datasmith static mesh asset.
	virtual void BuildAsset() override;

	// Build and export the Datasmith static mesh asset.
	// This must be done before building a Datasmith static mesh element.
	virtual void ExportAsset(
		FString const& InAssetFolder // Datasmith asset folder path
	) override;

	// Build a Datasmith static mesh element and add it to the Datasmith scene.
	virtual void BuildScene(
		TSharedRef<IDatasmithScene> IOSceneRef // Datasmith scene
	) override;

private:

	// Definition of a mesh triangle.
	struct MeshTriangle
	{
		int Vertex1;    // index of the first triangle vertex
		int Vertex2;    // index of the second triangle vertex
		int Vertex3;    // index of the third triangle vertex
		int MaterialID; // triangle material ID
	};

private:

	// Mesh vertex points.
	TArray<FVector> VertexPointArray;

	// Mesh vertex UV texture coordinate channels.
	TArray<TArray<FVector2D>> VertexUVChannelArray;

	// Mesh triangles.
	TArray<MeshTriangle> TriangleArray;

	// Mesh triangle vertex normals.
	// Each stride of 3 normals corresponds to the 3 vertices of a triangle.
	TArray<FVector> TriangleNormalArray;

	// Set of material names utilized by the mesh.
	TSet<FString> MaterialNameSet;

	// Dictionary of material names utilized by the mesh indexed by material IDs.
	TMap<int, FString> MaterialNameMap;

	// Array of Datasmith metadata properties.
	TArray<TSharedPtr<IDatasmithKeyValueProperty>> MetadataPropertyArray;

	// Datasmith static mesh.
	TSharedPtr<FDatasmithMesh> MeshPtr;

	// Datasmith static mesh element.
	TSharedPtr<IDatasmithMeshElement> MeshElementPtr;
};
