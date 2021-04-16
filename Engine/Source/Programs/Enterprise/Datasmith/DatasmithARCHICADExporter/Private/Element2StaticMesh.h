// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Utils/AddonTools.h"

#include "MaterialsDatabase.h"

#include "ModelMeshBody.hpp"
#include "Polygon.hpp"
#include "AttributeIndex.hpp"
#include "Transformation3D.hpp"

#include "DatasmithMeshExporter.h"

#include <map>

BEGIN_NAMESPACE_UE_AC

#if defined(DEBUG)
	#define DUMP_GEOMETRY 1 // For debug purpose
#else
	#define DUMP_GEOMETRY 0
#endif

class FSyncContext;

// Class that will convert AC element to StaticMesh
class FElement2StaticMesh
{
  public:
	// Constructor
	FElement2StaticMesh(const FSyncContext& InSyncContext);

	// Destructor
	~FElement2StaticMesh();

	// Create a datasmith mesh element
	TSharedPtr< IDatasmithMeshElement > CreateMesh();

	// Collect geometry of the element
	void AddElementGeometry(const ModelerAPI::Element& InModelElement);

	// Collect geometry of the element
	void AddElementGeometry(const ModelerAPI::Element& InModelElement, const Geometry::Transformation3D& InWorld2Local);

	// Return the numbers of bugs detected during conversion
	unsigned int GetBugsCount() const { return BugsCount; }

	// Return true of we have at least on face.
	bool HasGeometry() const { return GlobalMaterialsUsed.size() != 0; }

#if DUMP_GEOMETRY
	// Return a mesh dump
	static utf8_string MeshAsString(const FDatasmithMesh& Mesh);

	// Dump a mesh
	static void DumpMesh(const FDatasmithMesh& Mesh);

	// Return a mesh element dump
	static utf8_string MeshElementAsString(const IDatasmithMeshElement& Mesh);

	// Dump a mesh element
	static void DumpMeshElement(const IDatasmithMeshElement& Mesh);
#endif

  private:
	// Compute name of mesh element
	FString ComputeMeshElementName(const TCHAR* InMeshFileHash);

	// Fill 3d maesh data from collected goemetry
	void FillMesh(FDatasmithMesh* OutMesh);

	// Create a triangle for polygon vertex ≈ new Triangle(first, previous, last)
	void AddVertex(GS::Int32 InBodyVertex, const Geometry::Vector3D& VertexNormal);

	// Set the material for the current polygon
	void InitPolygonMaterial();

	// Triangle data
	class FTriangle
	{
	  public:
		enum
		{
			kInvalidIndex = -1
		};

		// Default constructor
		FTriangle() {}

		bool IsValid() const { return V0 != V1 && V0 != V2 && V1 != V2; }

		int		V0 = kInvalidIndex;
		int		V1 = kInvalidIndex;
		int		V2 = kInvalidIndex;
		int		UV0 = kInvalidIndex;
		int		UV1 = kInvalidIndex;
		int		UV2 = kInvalidIndex;
		FVector Normals[3] = {};

		// Material
		int	 LocalMatID = 0;
		bool bIsCurved = false;
	};

	// Current context
	const FSyncContext& SyncContext;

	Geometry::Transformation3D World2Local;
	Geometry::Matrix33		   Matrix;
	bool					   bIsIdentity = true;

	// Working variables
	bool				 bSomeHasTextures; // True if at least one triangles need uv
	ModelerAPI::MeshBody CurrentBody; // Current body that we collect geometry from
	bool				 bIsSurfaceBody; // Current body is a surface (ie need doble side material)
	ModelerAPI::Polygon	 CurrentPolygon; // Current polygon that we collect geometry from
	const FMaterialsDatabase::FMaterialSyncData* CurrentMaterial; // Current polygon global material
	size_t										 LocalMaterialIndex = 0; // Current polygon local material
	FTriangle									 CurrentTriangle; // Current triangle data
	int											 StartVertex; // Number of vertex collected before current body
	unsigned int								 VertexCount; // Number of edges processed in the current polygon
	FVector										 CurrentNormal; // Current normal, setted before calling AddVertex
	ModelerAPI::AttributeIndex					 MaterialIndex;
	ModelerAPI::AttributeIndex					 TextureIndex;

	// Vertex value and used flag or index
	class FVertex;

	struct FCompareUV
	{
		bool operator()(const ModelerAPI::TextureCoordinate& UVA, const ModelerAPI::TextureCoordinate& UVB) const
		{
#if 1
			return UVA.u < UVB.u || (UVA.u == UVB.u && UVA.v < UVB.v);
#else
			// Using version with tolerance make Twinmotion hang in FStaticMeshOperations::ComputeTangentsAndNormals
			const double tolerance = KINDA_SMALL_NUMBER;
			if (UVA.u + tolerance < UVB.u)
			{
				return true;
			}
			if (UVA.u - tolerance > UVB.u)
			{
				return false;
			}

			if (UVA.v + tolerance < UVB.v)
			{
				return true;
			}
			return false;
#endif
		}
	};

	typedef std::vector< FVertex >										VecVertices;
	typedef std::vector< FTriangle >									VecTriangles;
	typedef std::map< ModelerAPI::TextureCoordinate, int, FCompareUV >	MapUVs;
	typedef std::vector< const FMaterialsDatabase::FMaterialSyncData* > VecMaterialSyncData;

	VecVertices			Vertices; // Vector of used vertices
	VecTriangles		Triangles; // Vector of collected triangles
	MapUVs				UVs; // Map of used UVs
	VecMaterialSyncData GlobalMaterialsUsed;

	unsigned int BugsCount; // Count of bugs during geometry conversion
};

END_NAMESPACE_UE_AC
