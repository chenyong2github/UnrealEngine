// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh3.h"

#include "MeshElementLinearizations.h"

#include "FSparseMatrixD.h"

#include "MeshSmoothingUtilities.h" // for ELaplacianWeightScheme


/**
* Construct a sparse matrix representation of a uniform weighted Laplacian.
* The uniform weighted Laplacian is defined solely in terms of the connectivity
* of the mesh.  Note, by construction this should be a symmetric matrix.
*
* Row i represents the Laplacian at vert_i, the non-zero entries correspond to
* the incident one-ring vertices vert_j.
*
* L_{ij} = 1                      if vert_j is in the one-ring of vert_i
* L_{ii} = -Sum{ L_{ij}, j != i}
*
*
* @param DynamicMesh3   The triangle mesh
* @param VertexMap      Additional arrays used to map between vertexID and offset in a linear array (i.e. the row)
* @param BoundaryVerts  Optionally capture the verts that are on mesh edges.
*/
TUniquePtr<FSparseMatrixD> ConstructUniformLaplacian(const FDynamicMesh3& DynamicMesh, FVertexLinearization& VertexMap, TArray<int32>* BoundaryVerts = NULL);

/**
* Construct a sparse matrix representation of an umbrella weighted Laplacian.
* This Laplacian is defined solely in terms of the connectivity
* of the mesh.  Note, there is no expectation that the resulting matrix will be symmetric.
*
* Row i represents the Laplacian at vert_i, the non-zero entries correspond to
* the incident one-ring vertices vert_j.
*
* L_{ij} = 1 / valence(of i)                      if vert_j is in the one-ring of vert_i
* L_{ii} = -Sum{ L_{ij}, j != i} = -1
*
*
* @param DynamicMesh3   The triangle mesh
* @param VertexMap      Additional arrays used to map between vertexID and offset in a linear array (i.e. the row)
* @param BoundaryVerts  Optionally capture the verts that are on mesh edges.
*/
TUniquePtr<FSparseMatrixD> ConstructUmbrellaLaplacian(const FDynamicMesh3& DynamicMesh, FVertexLinearization& VertexMap, TArray<int32>* BoundaryVerts = NULL);

/**
* Construct a sparse matrix representation of a valence-weighted Laplacian.
* The valence weighted Laplacian is defined solely in terms of the connectivity
* of the mesh.  Note, by construction this should be a symmetric matrix.
*
* Row i represents the Laplacian at vert_i, the non-zero entries correspond to
* the incident one-ring vertices vert_j.
*
* L_{ij} = 1/\sqrt(valence(i) + valence(j))   if vert_j is in the one-ring of vert_i
* L_{ii} = -Sum{ L_{ij}, j != i}
*
*
* @param DynamicMesh3   The triangle mesh
* @param VertexMap      Additional arrays used to map between vertexID and offset in a linear array (i.e. the row)
* @param BoundaryVerts  Optionally capture the verts that are on mesh edges.
*/
TUniquePtr<FSparseMatrixD>  ConstructValenceWeightedLaplacian(const FDynamicMesh3& DynamicMesh3, FVertexLinearization& VertexMap, TArray<int32>* BoundaryVerts = NULL);

/**
* Construct a sparse matrix representation using a cotangent-weighted  Laplacian.
* NB: there is no reason to expect this to be a symmetric matrix.
*
* @param DynamicMesh3   The triangle mesh
* @param VertexMap      Additional arrays used to map between vertexID and offset in a linear array (i.e. the row)
* @param bClampWeights  Indicates if the off-diagonal weights should be clamped on construction.
*                       in practice this is desirable when creating the biharmonic operator, but not the mean curvature flow operator
* @param BoundaryVerts  Optionally capture the verts that are on mesh edges.
*/

TUniquePtr<FSparseMatrixD> ConstructCotangentLaplacian(const FDynamicMesh3& DynamicMesh, FVertexLinearization& VertexMap, const bool bClampWeights = false, TArray<int32>* BoundaryVerts = NULL);

/**
* Construct a sparse matrix representation using a cotangent-weighted  Laplacian.
* but returns the result in two symmetric parts.
*
*  (AreaMatrix^{-1}) * L_hat  = Cotangent weighted Laplacian.
*
* @return L_hat = Laplacian Without Area weighting. - this will be symmetric
*
* @param On return - diagonal matrix holding the voronoi areas for each vertex.  Area 1 is assigned to any edge vertex
*
*
* @param DynamicMesh3   The triangle mesh
* @param VertexMap      Additional arrays used to map between vertexID and offset in a linear array (i.e. the row)
* @param BoundaryVerts  Optionally capture the verts that are on mesh edges.
*/

TUniquePtr<FSparseMatrixD> ConstructCotangentLaplacian(const FDynamicMesh3& DynamicMesh, FVertexLinearization& VertexMap, FSparseMatrixD& AreaMatrix, TArray<int32>* BoundaryVerts = NULL);



/**
* Construct a sparse matrix representation using a meanvalue-weighted  Laplacian.
* NB: there is no reason to expect this to be a symmetric matrix.
*
* @param DynamicMesh3   The triangle mesh
* @param VertexMap      Additional arrays used to map between vertexID and offset in a linear array (i.e. the row)
* @param bClampWeights  Indicates if the off-diagonal weights should be clamped on construction.
*                       in practice this is desirable when creating the biharmonic operator, but not the mean curvature flow operator
* @param BoundaryVerts  Optionally capture the verts that are on mesh edges.
*/

TUniquePtr<FSparseMatrixD> ConstructMeanValueWeightLaplacian(const FDynamicMesh3& DynamicMesh, FVertexLinearization& VertexMap, TArray<int32>* BoundaryVerts = NULL);

TUniquePtr<FSparseMatrixD> ConstructLaplacian(const ELaplacianWeightScheme Scheme, const FDynamicMesh3& DynamicMesh, FVertexLinearization& VertexMap, TArray<int32>* BoundaryVerts = NULL);


/**
* Utility to map the enum names for debuging etc.
*/
FString LaplacianSchemeName(const ELaplacianWeightScheme Scheme);


/**
* Only certian laplacian operators are symmetric...
*/
static bool bIsSymmetricLaplacian(const ELaplacianWeightScheme Scheme)
{
	bool bSymmetric = false;
	switch (Scheme)
	{
	case ELaplacianWeightScheme::ClampedCotangent:
		bSymmetric = false;
		break;
	case ELaplacianWeightScheme::Cotangent:
		bSymmetric = false;
		break;
	case ELaplacianWeightScheme::Umbrella:
		bSymmetric = false;
		break;
	case ELaplacianWeightScheme::MeanValue:
		bSymmetric = false;
		break;
	case ELaplacianWeightScheme::Uniform:
		bSymmetric = true;
		break;
	case ELaplacianWeightScheme::Valence:
		bSymmetric = true;
		break;
	default:
		check(0);
	}
	return bSymmetric;
}


