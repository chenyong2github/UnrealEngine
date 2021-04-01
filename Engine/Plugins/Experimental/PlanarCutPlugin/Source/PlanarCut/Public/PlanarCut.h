// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"

#include "Math/Plane.h"

#include "Voronoi/Voronoi.h"
#include "GeometryCollection/GeometryCollection.h"
#include "MeshDescription.h"

struct PLANARCUT_API FNoiseSettings
{
	float Amplitude = 2;
	float Frequency = .1;
	int32 Octaves = 4;
	float PointSpacing = 1;
};

// auxiliary structure for FPlanarCells to carry material info
struct PLANARCUT_API FInternalSurfaceMaterials
{
	int32 GlobalMaterialID = 0;
	bool bGlobalVisibility = true;
	float GlobalUVScale = 1;

	TOptional<FNoiseSettings> NoiseSettings; // if unset, noise will not be added

	// TODO: add optional overrides per facet / per cell
	
	/**
	 * @param Collection	Reference collection to use for setting UV scale
	 * @param GeometryIdx	Reference geometry inside collection; if -1, use all geometries in collection
	 */
	void SetUVScaleFromCollection(const FGeometryCollection& Collection, int32 GeometryIdx = -1);

	
	int32 GetDefaultMaterialIDForGeometry(const FGeometryCollection& Collection, int32 GeometryIdx = -1) const;
};

// Stores planar facets that divide space into cells
struct PLANARCUT_API FPlanarCells
{
	FPlanarCells()
	{
	}
	FPlanarCells(const FPlane& Plane);
	FPlanarCells(const TArrayView<const FVector> Sites, FVoronoiDiagram &Voronoi);
	FPlanarCells(const TArrayView<const FBox> Boxes);
	FPlanarCells(const FBox &Region, const FIntVector& CubesPerAxis);
	FPlanarCells(const FBox &Region, const TArrayView<const FColor> Image, int32 Width, int32 Height);

	int32 NumCells;
	bool AssumeConvexCells = false; // enables optimizations in this common case (can create incorrect geometry if set to true when cells are not actually convex)
	TArray<FPlane> Planes;

	TArray<TPair<int32, int32>> PlaneCells;  // the two cells neighboring each plane, w/ the cell on the negative side of the plane first, positive side second
	TArray<TArray<int32>> PlaneBoundaries;
	TArray<FVector> PlaneBoundaryVertices;

	FInternalSurfaceMaterials InternalSurfaceMaterials;

	/**
	 * @return true if this is a single, unbounded cutting plane
	 */
	bool IsInfinitePlane() const
	{
		return NumCells == 2 && Planes.Num() == 1 && PlaneBoundaries[0].Num() == 0;
	}
	
	/**
	 * Debugging function to check that the plane boundary vertices are wound to match the orientation of the plane normal vectors
	 * @return	false if any plane boundary vertices are found in the 'wrong' orientation relative to the plane normal
	 */
	bool HasValidPlaneBoundaryOrientations() const
	{
		for (int32 PlaneIdx = 0; PlaneIdx < PlaneBoundaries.Num(); PlaneIdx++)
		{
			const TArray<int32>& Bdry = PlaneBoundaries[PlaneIdx];
			if (Bdry.Num() < 3)
			{
				continue;
			}
			const FPlane &P = Planes[PlaneIdx];
			FVector N(P.X, P.Y, P.Z);
			if (!N.IsNormalized()) // plane normals should be normalized
			{
				return false;
			}
			const FVector& A = PlaneBoundaryVertices[Bdry[0]];
			const FVector& B = PlaneBoundaryVertices[Bdry[1]];
			const FVector& C = PlaneBoundaryVertices[Bdry[2]];
			FVector E1 = B - A;
			FVector E2 = C - B;
			FVector NormalDir = E2 ^ E1;
			
			for (int32 VIdx : Bdry)
			{
				float SD = P.PlaneDot(PlaneBoundaryVertices[VIdx]);
				if (FMath::Abs(SD) > 1e-4)
				{
					return false; // vertices should be on plane!
				}
			}
			if (AssumeConvexCells && FVector::DotProduct(NormalDir, N) < 0) // vectors aren't pointing the same way at all?
			{
				return false;
			}
			float AngleMeasure = (NormalDir ^ N).SizeSquared();
			if (AngleMeasure > 1e-3) // vectors aren't directionally aligned?
			{
				return false;
			}
		}
		return true;
	}

	inline void AddPlane(const FPlane &P, int32 CellIdxBehind, int32 CellIdxInFront)
	{
		Planes.Add(P);
		PlaneCells.Emplace(CellIdxBehind, CellIdxInFront);
		PlaneBoundaries.Emplace();
	}

	inline void AddPlane(const FPlane &P, int32 CellIdxBehind, int32 CellIdxInFront, const TArray<int32>& PlaneBoundary)
	{
		Planes.Add(P);
		PlaneCells.Emplace(CellIdxBehind, CellIdxInFront);
		PlaneBoundaries.Add(PlaneBoundary);
	}

	void SetNoise(FNoiseSettings Noise = FNoiseSettings())
	{
		InternalSurfaceMaterials.NoiseSettings = Noise;
	}
};


/**
 * Cut a Geometry inside a GeometryCollection with PlanarCells, and add each cut cell back to the GeometryCollection as a new child of the input Geometry.  For geometries that would not be cut, nothing is added.
 * 
 * @param Cells				Defines the cutting planes and division of space
 * @param Collection		The collection to be cut
 * @param TransformIdx		Which transform inside the collection to cut
 * @param Grout				Separation to leave between cutting cells
 * @param CollisionSampleSpacing	Target spacing between collision sample vertices	
 * @param TransformCollection		Optional transform of the whole geometry collection; if unset, defaults to Identity
 * @param bIncludeOutsideCellInOutput	If true, geometry that was not inside any of the cells (e.g. was outside of the bounds of all cutting geometry) will still be included in the output; if false, it will be discarded.
 * @param CheckDistanceAcrossOutsideCellForProximity	If > 0, when a plane is neighboring the "outside" cell, instead of setting proximity to the outside cell, the algo will sample a point this far outside the cell in the normal direction of the plane to see if there is actually a non-outside cell there.  (Useful for bricks w/out mortar)
 * @return	index of first new geometry in the Output GeometryCollection, or -1 if no geometry was added
 */
int32 PLANARCUT_API CutWithPlanarCells(
	FPlanarCells &Cells,
	FGeometryCollection& Collection,
	int32 TransformIdx,
	double Grout,
	double CollisionSampleSpacing,
	const TOptional<FTransform>& TransformCollection = TOptional<FTransform>(),
	bool bIncludeOutsideCellInOutput = true,
	float CheckDistanceAcrossOutsideCellForProximity = 0,
	bool bSetDefaultInternalMaterialsFromCollection = true
);

/**
 * Cut multiple Geometry groups inside a GeometryCollection with PlanarCells, and add each cut cell back to the GeometryCollection as a new child of their source Geometry.  For geometries that would not be cut, nothing is added.
 *
 * @param Cells				Defines the cutting planes and division of space
 * @param Collection		The collection to be cut
 * @param TransformIndices	Which transform groups inside the collection to cut
 * @param Grout				Separation to leave between cutting cells
 * @param CollisionSampleSpacing	Target spacing between collision sample vertices
 * @param TransformCollection		Optional transform of the whole geometry collection; if unset, defaults to Identity
 * @param bIncludeOutsideCellInOutput	If true, geometry that was not inside any of the cells (e.g. was outside of the bounds of all cutting geometry) will still be included in the output; if false, it will be discarded.
 * @param CheckDistanceAcrossOutsideCellForProximity	If > 0, when a plane is neighboring the "outside" cell, instead of setting proximity to the outside cell, the algo will sample a point this far outside the cell in the normal direction of the plane to see if there is actually a non-outside cell there.  (Useful for bricks w/out mortar)
 * @return	index of first new geometry in the Output GeometryCollection, or -1 if no geometry was added
 */
int32 PLANARCUT_API CutMultipleWithPlanarCells(
	FPlanarCells &Cells,
	FGeometryCollection& Collection,
	const TArrayView<const int32>& TransformIndices,
	double Grout,
	double CollisionSampleSpacing,
	const TOptional<FTransform>& TransformCollection = TOptional<FTransform>(),
	bool bIncludeOutsideCellInOutput = true,
	float CheckDistanceAcrossOutsideCellForProximity = 0,  // TODO: < this param does nothing in the new mode; is only needed in special cases that aren't possible in the UI currently
	bool bSetDefaultInternalMaterialsFromCollection = true
);

/**
 * Cut multiple Geometry groups inside a GeometryCollection with Planes, and add each cut cell back to the GeometryCollection as a new child of their source Geometry.  For geometries that would not be cut, nothing is added.
 *
 * @param Planes				Defines the cutting planes and division of space
 * @param InternalSurfaceMaterials	Defines material properties for any added internal surfaces
 * @param Collection			The collection to be cut
 * @param TransformIndices	Which transform groups inside the collection to cut
 * @param Grout				Separation to leave between cutting cells
 * @param CollisionSampleSpacing	Target spacing between collision sample vertices
 * @param TransformCollection		Optional transform of the whole geometry collection; if unset, defaults to Identity
 * @return	index of first new geometry in the Output GeometryCollection, or -1 if no geometry was added
 */
int32 PLANARCUT_API CutMultipleWithMultiplePlanes(
	const TArrayView<const FPlane>& Planes,
	FInternalSurfaceMaterials& InternalSurfaceMaterials,
	FGeometryCollection& Collection,
	const TArrayView<const int32>& TransformIndices,
	double Grout,
	double CollisionSampleSpacing,
	const TOptional<FTransform>& TransformCollection = TOptional<FTransform>(),
	bool bSetDefaultInternalMaterialsFromCollection = true
);

/**
 * Recompute normals and tangents of selected geometry, optionally restricted to faces with odd or given material IDs (i.e. to target internal faces)
 *
 * @param bOnlyTangents		If true, leave normals unchanged and only recompute tangent&bitangent vectors
 * @param Collection		The Geometry Collection to be updated
 * @param TransformIndices	Which transform groups on the Geometry Collection to be updated.  If empty, all groups are updated.
 * @param bOnlyOddMaterials	If true, restrict recomputation to odd-numbered material IDs
 * @param WhichMaterials	If non-empty, restrict recomputation to only the listed material IDs
 */
void PLANARCUT_API RecomputeNormalsAndTangents(bool bOnlyTangents, FGeometryCollection& Collection, const TArrayView<const int32>& TransformIndices = TArrayView<const int32>(),
	bool bOnlyOddMaterials = true, const TArrayView<const int32>& WhichMaterials = TArrayView<const int32>());

/**
 * Scatter additional vertices (w/ no associated triangle) as needed to satisfy minimum point spacing
 * 
 * @param TargetSpacing		The desired spacing between collision sample vertices
 * @param Collection		The Geometry Collection to be updated
 * @param TransformIndices	Which transform groups on the Geometry Collection to be updated.  If empty, all groups are updated.
 * @return Index of first transform group w/ updated geometry.  (To update geometry we delete and re-add, because geometry collection isn't designed for in-place updates)
 */
int32 PLANARCUT_API AddCollisionSampleVertices(double TargetSpacing, FGeometryCollection& Collection, const TArrayView<const int32>& TransformIndices = TArrayView<const int32>());

/**
 * Cut multiple Geometry groups inside a GeometryCollection with a mesh, and add each cut cell back to the GeometryCollection as a new child of their source Geometry.  For geometries that would not be cut, nothing is added.
 * 
 * @param CuttingMesh				Mesh to be used to cut the geometry collection
 * @param CuttingMeshTransform		Position of cutting mesh
 * @param InternalSurfaceMaterials	Defines material properties for any added internal surfaces
 * @param Collection				The collection to be cut
 * @param TransformIndices			Which transform groups inside the collection to cut
 * @param CollisionSampleSpacing	Target spacing between collision sample vertices
 * @param TransformCollection		Optional transform of the collection; if unset, defaults to Identity
 * @return index of first new geometry in the Output GeometryCollection, or -1 if no geometry was added
 */
int32 PLANARCUT_API CutWithMesh(
	FMeshDescription* CuttingMesh,
	FTransform CuttingMeshTransform,
	FInternalSurfaceMaterials& InternalSurfaceMaterials,
	FGeometryCollection& Collection,
	const TArrayView<const int32>& TransformIndices,
	double CollisionSampleSpacing,
	const TOptional<FTransform>& TransformCollection = TOptional<FTransform>(),
	bool bSetDefaultInternalMaterialsFromCollection = true
);

/**
 * Convert chosen Geometry groups inside a GeometryCollection to a single Mesh Description.
 *
 * @param OutputMesh				Mesh to be filled with the geometry collection geometry
 * @param TransformOut				Transform taking output mesh geometry to local space of geometry collection
 * @param bCenterPivot				Whether to center the geometry at the origin
 * @param Collection				The collection to be converted
 * @param TransformIndices			Which transform groups inside the collection to convert
 */
void PLANARCUT_API ConvertToMeshDescription(
	FMeshDescription& OutputMesh,
	FTransform& TransformOut,
	bool bCenterPivot,
	FGeometryCollection& Collection,
	const TArrayView<const int32>& TransformIndices
);

