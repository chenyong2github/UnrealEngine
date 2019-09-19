// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "LaplacianOperators.h"

#include <cmath> // double version of sqrt
#include <vector> // used by eigen to initialize sparse matrix

#if defined(_MSC_VER) && USING_CODE_ANALYSIS
#pragma warning(push)
#pragma warning(disable : 6011)
#pragma warning(disable : 6387)
#pragma warning(disable : 6313)
#pragma warning(disable : 6294)
#endif
THIRD_PARTY_INCLUDES_START
#include <Eigen/Dense> // for Matrix4d in testing
THIRD_PARTY_INCLUDES_END
#if defined(_MSC_VER) && USING_CODE_ANALYSIS
#pragma warning(pop)
#endif


#define LAPLACIAN_SKIP_BOUNDARY 0



FString LaplacianSchemeName(const ELaplacianWeightScheme Scheme)
{
	FString LaplacianName;
	switch (Scheme)
	{
	case ELaplacianWeightScheme::ClampedCotangent:
		LaplacianName = FString(TEXT("Clamped Cotangent Laplacian"));
		break;
	case ELaplacianWeightScheme::Cotangent:
		LaplacianName = FString(TEXT("Cotangent Laplacian"));
		break;
	case ELaplacianWeightScheme::Umbrella:
		LaplacianName = FString(TEXT("Umbrella Laplacian"));
		break;
	case ELaplacianWeightScheme::MeanValue:
		LaplacianName = FString(TEXT("MeanValue Laplacian"));
		break;
	case ELaplacianWeightScheme::Uniform:
		LaplacianName = FString(TEXT("Uniform Laplacian"));
		break;
	case ELaplacianWeightScheme::Valence:
		LaplacianName = FString(TEXT("Valence Laplacian"));
		break;
	default:
		check(0 && "Unknown Laplacian Weight Scheme Enum");
	}

	return LaplacianName;
}


// Utility to compute the number of elements in the sparse laplacian matrix
int32 ComputeNumMatrixElements(const FDynamicMesh3& DynamicMesh, const TArray<int32>& ToVtxId)
{
	const int32 NumVerts = ToVtxId.Num();
	TArray<int32> OneRingSize;
	{
		OneRingSize.SetNumUninitialized(NumVerts);

		for (int32 i = 0; i < NumVerts; ++i)
		{
			const int32 VertId = ToVtxId[i];
			OneRingSize[i] = DynamicMesh.GetVtxEdgeCount(VertId);
		}
	}

	// Compute the total number of entries in the sparse matrix
	int32 NumMatrixEntries = 0;
	{
		for (int32 i = 0; i < NumVerts; ++i)
		{
			NumMatrixEntries += 1 + OneRingSize[i]; // myself plus my neighbors
		}

	}

	return NumMatrixEntries;
}

/**
* The per-triangle data used in constructing the cotangent weighted laplacian.
*
*/
class CotanTriangleData
{
public:

	typedef FVector3d TriangleVertices[3];

	CotanTriangleData() = default;

	CotanTriangleData(const CotanTriangleData& Other)
	{

		Cotangent[0] = Other.Cotangent[0];
		Cotangent[1] = Other.Cotangent[1];
		Cotangent[2] = Other.Cotangent[2];

		VoronoiArea[0] = Other.VoronoiArea[0];
		VoronoiArea[1] = Other.VoronoiArea[1];
		VoronoiArea[2] = Other.VoronoiArea[2];

		OppositeEdge[0] = Other.OppositeEdge[0];
		OppositeEdge[1] = Other.OppositeEdge[1];
		OppositeEdge[2] = Other.OppositeEdge[2];

	}


	CotanTriangleData(const FDynamicMesh3& DynamicMesh, int32 TriId)
	{
		Initialize(DynamicMesh, TriId);
	}

	void Initialize(const FDynamicMesh3& DynamicMesh, int32 SrcTriId)
	{
		TriId = SrcTriId;

		// edges: ab, bc, ca
		FIndex3i EdgeIds = DynamicMesh.GetTriEdges(TriId);

		FVector3d VertA, VertB, VertC;
		DynamicMesh.GetTriVertices(TriId, VertA, VertB, VertC);

		const FVector3d EdgeAB(VertB - VertA);
		const FVector3d EdgeAC(VertC - VertA);
		const FVector3d EdgeBC(VertC - VertB);


		OppositeEdge[0] = EdgeIds[1]; // EdgeBC is opposite vert A
		OppositeEdge[1] = EdgeIds[2]; // EdgeAC is opposite vert B
		OppositeEdge[2] = EdgeIds[0]; // EdgeAB is opposite vert C


		// NB: didn't use VectorUtil::Area() so we can re-use the Edges.
		//     also this formulation of area is always positive.

		const double TwiceArea = EdgeAB.Cross(EdgeAC).Length();

		// NB: Area = 1/2 || EdgeA X EdgeB ||  where EdgeA and EdgeB are any two edges in the triangle.

		// Compute the Voronoi areas
		// 
		// From Discrete Differential-Geometry Operators for Triangulated 2-Manifolds (Meyer, Desbrun, Schroder, Barr)
		// http://www.geometry.caltech.edu/pubs/DMSB_III.pdf
		//    Given triangle P,Q, R the voronoi area at P is given by
		//    Area = (1/8) * ( |PR|**2 Cot<Q  + |PQ|**2 Cot<R ) 

		if (TwiceArea > 2. * SmallTriangleArea)
		{



			// Compute the cotangent of the angle between V1 and V2 
			// as the ratio  V1.Dot.V2 / || V1 Cross V2 ||

			// Cotangent[i] is cos(theta)/sin(theta) at the i'th vertex.

			Cotangent[0] = EdgeAB.Dot(EdgeAC) / TwiceArea;
			Cotangent[1] = -EdgeAB.Dot(EdgeBC) / TwiceArea;
			Cotangent[2] = EdgeAC.Dot(EdgeBC) / TwiceArea;

			if (bIsObtuse())
			{
				const double Area = 0.5 * TwiceArea;

				// Voronoi inappropriate case.  Instead use Area(T)/2 at obtuse corner
				// and Area(T) / 4 at the other corners.

				VoronoiArea[0] = 0.25 * Area;
				VoronoiArea[1] = 0.25 * Area;
				VoronoiArea[2] = 0.25 * Area;

				for (int i = 0; i < 3; ++i)
				{
					if (Cotangent[i] < 0.)
					{
						VoronoiArea[i] = 0.5 * Area;
					}
				}
			}
			else
			{
				// If T is non-obtuse.

				const double EdgeABSqLength = EdgeAB.SquaredLength();
				const double EdgeACSqLength = EdgeAC.SquaredLength();
				const double EdgeBCSqLength = EdgeBC.SquaredLength();

				VoronoiArea[0] = EdgeABSqLength * Cotangent[1] + EdgeACSqLength * Cotangent[2];
				VoronoiArea[1] = EdgeABSqLength * Cotangent[0] + EdgeBCSqLength * Cotangent[2];
				VoronoiArea[2] = EdgeACSqLength * Cotangent[0] + EdgeBCSqLength * Cotangent[1];

				const double Inv8 = .125; // 1/8

				VoronoiArea[0] *= Inv8;
				VoronoiArea[1] *= Inv8;
				VoronoiArea[2] *= Inv8;
			}
		}
		else
		{
			// default small triangle - equilateral 
			double CotOf60 = 1. / FMath::Sqrt(3.f);
			Cotangent[0] = CotOf60;
			Cotangent[1] = CotOf60;
			Cotangent[2] = CotOf60;

			VoronoiArea[0] = SmallTriangleArea / 3.;
			VoronoiArea[1] = SmallTriangleArea / 3.;
			VoronoiArea[2] = SmallTriangleArea / 3.;
		}
	}

	int32 GetLocalEdgeIdx(const int32 DynamicsMeshEdgeId) const
	{
		int32 Result = -1;
		if (DynamicsMeshEdgeId == OppositeEdge[0])
		{
			Result = 0;
		}
		else if (DynamicsMeshEdgeId == OppositeEdge[1])
		{
			Result = 1;
		}
		else if (DynamicsMeshEdgeId == OppositeEdge[2])
		{
			Result = 2;
		}
		return Result;
	}

	/** helper to return the cotangent of the angle opposite the given edge
	*
	*   @param DynamicsMeshEdgeId is the id used by FDynamicMesh3 for this edge.
	*   @param bValid will false on return if the requested edge is not part of this triangle
	*   @return Cotangent of the opposite angle.
	*/
	double GetOpposingCotangent(const int32 DynamicsMeshEdgeId, bool& bValid) const
	{

		double OpposingCotangent = -1.;
		bValid = false;
		int32 LocalEdgeIdx = GetLocalEdgeIdx(DynamicsMeshEdgeId);
		if (LocalEdgeIdx > -1)
		{
			OpposingCotangent = Cotangent[LocalEdgeIdx];
			bValid = true;
		}

		return OpposingCotangent;
	}

	double GetOpposingCotangent(const int32 DynamicsMeshEdgeId) const
	{
		bool bValid;
		double OpposingCotangent = GetOpposingCotangent(DynamicsMeshEdgeId, bValid);
		checkSlow(bValid);
		return OpposingCotangent;
	}

	bool bIsObtuse() const
	{
		return (Cotangent[0] < 0.f || Cotangent[1] < 0.f || Cotangent[2] < 0.f);
	}


	// The "floor" for triangle area.
	// NB: the cotan laplacian has terms ~ 1/TriArea
	//     and the deformation matrix has terms ~ 1/TriArea**2

	static constexpr double SmallTriangleArea = 1.e-4;

	// testing
	int32 TriId = -1;

	/** Total byte count: 6 double + 3 int32 = 60 bytes.   */

	// Cotangent[i] is cos(theta)/sin(theta) at the i'th vertex.

	double Cotangent[3] = { 0. };

	// VoronoiArea[i] is the voronoi area about the i'th vertex in this triangle.

	double VoronoiArea[3] = { 0. };

	// OppositeEdge[i] = Corresponding DynamicsMesh3::EdgeId for the edge that is opposite
	// the i'th vertex in this triangle

	int32 OppositeEdge[3] = { -1 };

};


/**
* The per-triangle data used in constructing the mean-value weighted laplacian.
*
*/
class MeanValueTriangleData
{

public:

	MeanValueTriangleData(const MeanValueTriangleData& Other)
		: TriId(Other.TriId)
		, TriVtxIds(Other.TriVtxIds)
		, TriEdgeIds(Other.TriEdgeIds)
		, bDegenerate(Other.bDegenerate)
	{
		EdgeLength[0] = Other.EdgeLength[0];
		EdgeLength[1] = Other.EdgeLength[1];
		EdgeLength[2] = Other.EdgeLength[2];

		TanHalfAngle[0] = Other.TanHalfAngle[0];
		TanHalfAngle[1] = Other.TanHalfAngle[1];
		TanHalfAngle[2] = Other.TanHalfAngle[2];
	}

	void Initialize(const FDynamicMesh3& DynamicMesh, int32 SrcTriId)
	{
		TriId = SrcTriId;

		// VertAId, VertBId, VertCId
		TriVtxIds = DynamicMesh.GetTriangle(TriId);
		TriEdgeIds = DynamicMesh.GetTriEdges(TriId);

		FVector3d VertA, VertB, VertC;
		DynamicMesh.GetTriVertices(TriId, VertA, VertB, VertC);

		const FVector3d EdgeAB(VertB - VertA);
		const FVector3d EdgeAC(VertC - VertA);
		const FVector3d EdgeBC(VertC - VertB);

		EdgeLength[0] = EdgeAB.Length();
		EdgeLength[1] = EdgeAC.Length();
		EdgeLength[2] = EdgeBC.Length();

		constexpr double SmallEdge = 1e-4;

		bDegenerate = (EdgeLength[0] < SmallEdge || EdgeLength[1] < SmallEdge || EdgeLength[2] < SmallEdge);

		// Compute tan(angle/2) = Sqrt[ (1-cos) / (1 + cos)]

		const double ABdotAC = EdgeAB.Dot(EdgeAC);
		const double BCdotBA = -EdgeBC.Dot(EdgeAB);
		const double CAdotCB = EdgeAC.Dot(EdgeBC);


		const double RegularizingConst = 1.e-6; // keeps us from dividing by zero when making tan[180/2] = sin[90]/cos[90] = inf
		TanHalfAngle[0] = (EdgeLength[0] * EdgeLength[1] - ABdotAC) / (EdgeLength[0] * EdgeLength[1] + ABdotAC + RegularizingConst);
		TanHalfAngle[1] = (EdgeLength[0] * EdgeLength[2] - BCdotBA) / (EdgeLength[0] * EdgeLength[2] + BCdotBA + RegularizingConst);
		TanHalfAngle[2] = (EdgeLength[1] * EdgeLength[2] - CAdotCB) / (EdgeLength[1] * EdgeLength[2] + CAdotCB + RegularizingConst);

		// The ABS is just a precaution.. mathematically these should all be positive, but very small angles may result in negative values.

		TanHalfAngle[0] = std::sqrt(FMath::Abs(TanHalfAngle[0])); // at vertA
		TanHalfAngle[1] = std::sqrt(FMath::Abs(TanHalfAngle[1])); // at vertB
		TanHalfAngle[2] = std::sqrt(FMath::Abs(TanHalfAngle[2])); // at vertC
#if 0
		// testing
		double totalAngle = 2. * (std::atan(TanHalfAngle[0]) + std::atan(TanHalfAngle[1]) + std::atan(TanHalfAngle[2]));

		double angleError = M_PI - totalAngle;
#endif
	}

	// return Tan(angle / 2) for the corner indicated by this vert id.
	double GetTanHalfAngle(int32 VtxId) const
	{
		int32 Offset = 0;

		while (VtxId != TriVtxIds[Offset])
		{
			Offset++;
			checkSlow(Offset < 3);
		}

		return TanHalfAngle[Offset];
	}

	// return the length of the indicated edge
	double GetEdgeLenght(int32 EdgeId) const
	{
		int32 Offset = 0;

		while (EdgeId != TriEdgeIds[Offset])
		{
			Offset++;
			checkSlow(Offset < 3);
		}

		return EdgeLength[Offset];
	}


	int32 TriId = -1;
	FIndex3i TriVtxIds;
	FIndex3i TriEdgeIds;

	bool bDegenerate = true;

	double EdgeLength[3] = { 0. };
	double TanHalfAngle[3] = { 0. };


};

/**
* Return and array in triangle order that holds the per-triangle derived data needed
*/
template <typename TriangleDataType>
TArray<TriangleDataType>  ConstructTriangleDataArray(const FDynamicMesh3& DynamicMesh, const FTriangleLinearization& TriangleLinearization)
{
	TArray< TriangleDataType > TriangleDataArray;

	const int32 NumTris = TriangleLinearization.NumTris();
	TriangleDataArray.SetNumUninitialized(NumTris);

	const auto& ToTriIdx = TriangleLinearization.ToIndex();

	for (int32 i = 0; i < NumTris; ++i)
	{
		// Current triangle

		const int32 TriIdx = ToTriIdx[i];

		// Compute all the geometric data needed for this triangle.

		TriangleDataArray[i].Initialize(DynamicMesh, TriIdx);
	}

	return TriangleDataArray;
}

void ConstructUniformLaplacian(const FDynamicMesh3& DynamicMesh, FVertexLinearization& VertexMap, FSparseMatrixD& LaplacianInterior, FSparseMatrixD& LaplacianBoundary)
{
	typedef FSparseMatrixD::Scalar    ScalarT;
	typedef Eigen::Triplet<ScalarT>  MatrixTripletT;

	// Sync the mapping between the mesh vertex ids and their offsets in a nominal linear array.
	VertexMap.Reset(DynamicMesh);

	const TArray<int32>& ToMeshV = VertexMap.ToId();
	const TArray<int32>& ToIndex = VertexMap.ToIndex();
	const int32 NumVerts = VertexMap.NumVerts();

	const int32 NumBoundaryVerts = VertexMap.NumBoundaryVerts();
	const int32 NumInteriorVerts = NumVerts - NumBoundaryVerts;


	// Eigen constructs a sparse matrix from a linearized array of matrix entries.
	// pre-allocate space for the temporary triplet list 
	std::vector<MatrixTripletT> LaplacianAsTripletList;
	std::vector<MatrixTripletT> BoundaryAsTripletList;
	{
		int32 NumMatrixEntries = ComputeNumMatrixElements(DynamicMesh, ToMeshV);
		LaplacianAsTripletList.reserve(NumMatrixEntries);
	}

	
	// Construct Laplacian Matrix: loop over verts constructing the corresponding matrix row.
	// NB: the vertices are ordered with the interior verts first.
	for (int32 i = 0; i < NumInteriorVerts; ++i)
	{
		const int32 VertId = ToMeshV[i];
		ScalarT CenterWeight = ScalarT(0); // equal and opposite the sum of the neighbor weights

		checkSlow(!DynamicMesh.IsBoundaryVertex(VertId));  // we should only be looping over the internal verts

		for (int NeighborVertId : DynamicMesh.VtxVerticesItr(VertId))
		{
			const int32 j = ToIndex[NeighborVertId];


			ScalarT NeighborWeight = ScalarT(1);
			CenterWeight += NeighborWeight;

			if (j < NumInteriorVerts)
			{
				// add the neighbor 
				LaplacianAsTripletList.push_back(MatrixTripletT(i, j, NeighborWeight));
			}
			else
			{
				int32 jBoundary = j - NumInteriorVerts;
				BoundaryAsTripletList.push_back(MatrixTripletT(i, jBoundary, NeighborWeight));
			}
		}
		// add the center
		LaplacianAsTripletList.push_back(MatrixTripletT(i, i, -CenterWeight));

	}

	// populate the Laplacian for the interior.
	{
		// [djh] - can we construct with (0,0) ? or do we need to check for it?
		FSparseMatrixD InteriorMatrix(NumInteriorVerts, NumInteriorVerts);
		InteriorMatrix.setFromTriplets(LaplacianAsTripletList.begin(), LaplacianAsTripletList.end());
		LaplacianInterior.swap(InteriorMatrix);
		LaplacianInterior.makeCompressed();
	}
	
	// populate the boundary matrix
	{
		FSparseMatrixD BoundaryMatrix(NumInteriorVerts, NumBoundaryVerts);
		BoundaryMatrix.setFromTriplets(BoundaryAsTripletList.begin(), BoundaryAsTripletList.end());
		LaplacianBoundary.swap(BoundaryMatrix);
		LaplacianBoundary.makeCompressed();
	}

}


void ConstructUmbrellaLaplacian(const FDynamicMesh3& DynamicMesh, FVertexLinearization& VertexMap, FSparseMatrixD& LaplacianInterior, FSparseMatrixD& LaplacianBoundary)
{

	typedef FSparseMatrixD::Scalar    ScalarT;
	typedef Eigen::Triplet<ScalarT>  MatrixTripletT;

	// Sync the mapping between the mesh vertex ids and their offsets in a nominal linear array.
	VertexMap.Reset(DynamicMesh);

	const TArray<int32>& ToMeshV = VertexMap.ToId();
	const TArray<int32>& ToIndex = VertexMap.ToIndex();
	const int32 NumVerts = VertexMap.NumVerts();

	const int32 NumBoundaryVerts = VertexMap.NumBoundaryVerts();
	const int32 NumInteriorVerts = NumVerts - NumBoundaryVerts;

	// Eigen constructs a sparse matrix from a linearized array of matrix entries.
	// pre-allocate space for the temporary triplet list 
	std::vector<MatrixTripletT> LaplacianAsTripletList;
	std::vector<MatrixTripletT> BoundaryAsTripletList;
	{
		int32 NumMatrixEntries = ComputeNumMatrixElements(DynamicMesh, ToMeshV);
		LaplacianAsTripletList.reserve(NumMatrixEntries);
	}

	// Cache valency of each vertex.
	// Number of non-zero elements in the i'th row = 1 + OneRingSize(i)
	TArray<int32> OneRingSize;
	{
		OneRingSize.SetNumUninitialized(NumVerts);

		for (int32 i = 0; i < NumVerts; ++i)
		{
			const int32 VertId = ToMeshV[i];
			OneRingSize[i] = DynamicMesh.GetVtxEdgeCount(VertId);
		}
	}

	// Construct Laplacian Matrix: loop over verts constructing the corresponding matrix row.

	for (int32 i = 0; i < NumInteriorVerts; ++i)
	{
		const int32 VertId = ToMeshV[i];
		const int32 Valence = OneRingSize[i];
		double InvValence = (Valence != 0) ? 1. / double(Valence) : 0.;

		checkSlow(!DynamicMesh.IsBoundaryVertex(VertId));

		for (int NeighborVertId : DynamicMesh.VtxVerticesItr(VertId))
		{
			const int32 j = ToIndex[NeighborVertId];

			// add the neighbor 
			if (j < NumInteriorVerts)
			{
				LaplacianAsTripletList.push_back(MatrixTripletT(i, j, InvValence));
			}
			else
			{
				int32 jBoundary = j - NumInteriorVerts;
				BoundaryAsTripletList.push_back(MatrixTripletT(i, jBoundary, InvValence));
			}
		}
		// add the center
		LaplacianAsTripletList.push_back(MatrixTripletT(i, i, -ScalarT(1)));

	}

	// populate the Laplacian for the interior.
	{
		// [djh] - can we construct with (0,0) ? or do we need to check for it?
		FSparseMatrixD InteriorMatrix(NumInteriorVerts, NumInteriorVerts);
		InteriorMatrix.setFromTriplets(LaplacianAsTripletList.begin(), LaplacianAsTripletList.end());
		LaplacianInterior.swap(InteriorMatrix);
		LaplacianInterior.makeCompressed();
	}

	// populate the boundary matrix
	{
		FSparseMatrixD BoundaryMatrix(NumInteriorVerts, NumBoundaryVerts);
		BoundaryMatrix.setFromTriplets(BoundaryAsTripletList.begin(), BoundaryAsTripletList.end());
		LaplacianBoundary.swap(BoundaryMatrix);
		LaplacianBoundary.makeCompressed();
	}

}


void ConstructValenceWeightedLaplacian(const FDynamicMesh3& DynamicMesh, FVertexLinearization& VertexMap, FSparseMatrixD& LaplacianInterior, FSparseMatrixD& LaplacianBoundary)
{


	typedef FSparseMatrixD::Scalar    ScalarT;
	typedef Eigen::Triplet<ScalarT>  MatrixTripletT;


	// Sync the mapping between the mesh vertex ids and their offsets in a nomimal linear array.
	VertexMap.Reset(DynamicMesh);

	const TArray<int32>& ToMeshV = VertexMap.ToId();
	const TArray<int32>& ToIndex = VertexMap.ToIndex();
	const int32 NumVerts = VertexMap.NumVerts();

	const int32 NumBoundaryVerts = VertexMap.NumBoundaryVerts();
	const int32 NumInteriorVerts = NumVerts - NumBoundaryVerts;

	// Eigen constructs a sparse matrix from a linearized array of matrix entries.
	// pre-allocate space for the temporary triplet list 
	std::vector<MatrixTripletT> LaplacianAsTripletList;
	std::vector<MatrixTripletT> BoundaryAsTripletList;

	// Cache valency of each vertex.
	// Number of non-zero elements in the i'th row = 1 + OneRingSize(i)
	TArray<int32> OneRingSize;
	{
		OneRingSize.SetNumUninitialized(NumVerts);

		for (int32 i = 0; i < NumVerts; ++i)
		{
			const int32 VertId = ToMeshV[i];
			OneRingSize[i] = DynamicMesh.GetVtxEdgeCount(VertId);
		}
	}


	// Compute the total number of entries in the sparse matrix
	int32 NumMatrixEntries = 0;
	{
		for (int32 i = 0; i < NumVerts; ++i)
		{
			NumMatrixEntries += 1 + OneRingSize[i]; // myself plus my neighbors
		}

	}
	LaplacianAsTripletList.reserve(NumMatrixEntries);



	// Construct Laplacian Matrix: loop over verts constructing the corresponding matrix row.

	for (int32 i = 0; i < NumInteriorVerts; ++i)
	{
		const int32 VertId       = ToMeshV[i];
		const int32 IOneRingSize = OneRingSize[i];

		ScalarT CenterWeight = ScalarT(0); // equal and opposite the sum of the neighbor weights
		for (int NeighborVertId : DynamicMesh.VtxVerticesItr(VertId))
		{
			const int32 j = ToIndex[NeighborVertId];
			const int32 JOneRingSize = OneRingSize[j];


			ScalarT NeighborWeight = ScalarT(1) / std::sqrt(IOneRingSize + JOneRingSize);
			CenterWeight += NeighborWeight;

			if (j < NumInteriorVerts)
			{
				// add the neighbor 
				LaplacianAsTripletList.push_back(MatrixTripletT(i, j, NeighborWeight));
			}
			else
			{
				int32 jBoundary = j - NumInteriorVerts;
				BoundaryAsTripletList.push_back(MatrixTripletT(i, jBoundary, NeighborWeight));
			}
		}
		// add the center
		LaplacianAsTripletList.push_back(MatrixTripletT(i, i, -CenterWeight));

	}


	// populate the Laplacian for the interior.
	{
		// [djh] - can we construct with (0,0) ? or do we need to check for it?
		FSparseMatrixD InteriorMatrix(NumInteriorVerts, NumInteriorVerts);
		InteriorMatrix.setFromTriplets(LaplacianAsTripletList.begin(), LaplacianAsTripletList.end());
		LaplacianInterior.swap(InteriorMatrix);
		LaplacianInterior.makeCompressed();
	}

	// populate the boundary matrix
	{
		FSparseMatrixD BoundaryMatrix(NumInteriorVerts, NumBoundaryVerts);
		BoundaryMatrix.setFromTriplets(BoundaryAsTripletList.begin(), BoundaryAsTripletList.end());
		LaplacianBoundary.swap(BoundaryMatrix);
		LaplacianBoundary.makeCompressed();
	}

}


void ConstructCotangentLaplacian(const FDynamicMesh3& DynamicMesh, FVertexLinearization& VertexMap, FSparseMatrixD& AreaMatrix, FSparseMatrixD& LaplacianInterior, FSparseMatrixD& LaplacianBoundary)
{


	typedef FSparseMatrixD::Scalar  ScalarT;
	typedef Eigen::Triplet<ScalarT> MatrixTripletT;


	// Sync the mapping between the mesh vertex ids and their offsets in a nomimal linear array.
	VertexMap.Reset(DynamicMesh);

	const TArray<int32>& ToMeshV = VertexMap.ToId();
	const TArray<int32>& ToIndex = VertexMap.ToIndex();
	const int32 NumVerts = VertexMap.NumVerts();

	const int32 NumBoundaryVerts = VertexMap.NumBoundaryVerts();
	const int32 NumInteriorVerts = NumVerts - NumBoundaryVerts;

	// Eigen constructs a sparse matrix from a linearized array of matrix entries.
	// pre-allocate space for the temporary triplet list 
	
	std::vector<MatrixTripletT> LaplacianAsTripletList;
	std::vector<MatrixTripletT> BoundaryAsTripletList;
	
	// Eigen constructs a sparse matrix from a linearized array of matrix entries.
	{
		int32 NumMatrixEntries = ComputeNumMatrixElements(DynamicMesh, ToMeshV);
		LaplacianAsTripletList.reserve(NumMatrixEntries);
	}

	// Create the mapping of triangles

	FTriangleLinearization TriangleMap(DynamicMesh);

	const TArray<int32>& ToMeshTri = TriangleMap.ToId();
	const TArray<int32>& ToTriIdx = TriangleMap.ToIndex();
	const int32 NumTris = TriangleMap.NumTris();


	// Clear space for the areas
	std::vector<MatrixTripletT> DiagonalTriplets;
	DiagonalTriplets.reserve(NumVerts);

	FSparseMatrixD Diagonals(NumInteriorVerts, NumInteriorVerts);
	Diagonals.reserve(NumInteriorVerts);


	// Create an array that holds all the geometric information we need for each triangle.

	TArray<CotanTriangleData>  CotangentTriangleDataArray = ConstructTriangleDataArray<CotanTriangleData>(DynamicMesh, TriangleMap);



	// Construct Laplacian Matrix: loop over verts constructing the corresponding matrix row.
	//                             store the id of the boundary verts for later use.

	for (int32 i = 0; i < NumInteriorVerts; ++i)
	{
		const int32 IVertId = ToMeshV[i]; // I - the row


		// Compute the Voronoi area for this vertex.
		double WeightArea = 0.;
		for (int32 TriId : DynamicMesh.VtxTrianglesItr(IVertId))
		{
			const int32 TriIdx = ToTriIdx[TriId];
			const CotanTriangleData& TriData = CotangentTriangleDataArray[TriIdx];


			// The three VertIds for this triangle.
			const FIndex3i TriVertIds = DynamicMesh.GetTriangle(TriId);

			// Which of the corners is IVertId?
			int32 Offset = 0;
			while (TriVertIds[Offset] != IVertId)
			{
				Offset++;
				checkSlow(Offset < 3);
			}

			WeightArea += TriData.VoronoiArea[Offset];
		}


		double WeightII = 0.; // accumulate to equal and opposite the sum of the neighbor weights

		// for each connecting edge

		for (int32 EdgeId : DynamicMesh.VtxEdgesItr(IVertId))
		{
			// [v0, v1, t0, t1]:  NB: both t0 & t1 exist since IVert isn't a boundary vert.
			FIndex4i Edge = DynamicMesh.GetEdge(EdgeId);


			// the other vert in the edge - identifies the matrix column
			const int32 JVertId = (Edge[0] == IVertId) ? Edge[1] : Edge[0];  // J - the column

			checkSlow(JVertId != IVertId);

			// Get the cotangents for this edge.

			const int32 Tri0Idx = ToTriIdx[Edge[2]];
			const CotanTriangleData& Tri0Data = CotangentTriangleDataArray[Tri0Idx];
			const double CotanAlpha = Tri0Data.GetOpposingCotangent(EdgeId);


			// The second triangle will be invalid if this is an edge!

			const double CotanBeta = (Edge[3] != FDynamicMesh3::InvalidID) ? CotangentTriangleDataArray[ToTriIdx[Edge[3]]].GetOpposingCotangent(EdgeId) : 0.;

			double WeightIJ = 0.5 * (CotanAlpha + CotanBeta);
			WeightII += WeightIJ;

			const int32 j = ToIndex[JVertId];

			if (j < NumInteriorVerts)
			{
				LaplacianAsTripletList.push_back(MatrixTripletT(i, j, WeightIJ));
			}
			else
			{
				int32 jBoundary = j - NumInteriorVerts;
				BoundaryAsTripletList.push_back(MatrixTripletT(i, jBoundary, WeightIJ));
			}

		}

		LaplacianAsTripletList.push_back(MatrixTripletT(i, i, -WeightII));

		DiagonalTriplets.push_back(MatrixTripletT(i, i, WeightArea));


	}

	Diagonals.setFromTriplets(DiagonalTriplets.begin(), DiagonalTriplets.end());
	AreaMatrix.swap(Diagonals);
	AreaMatrix.makeCompressed();

	// populate the Laplacian for the interior.
	{
		// [djh] - can we construct with (0,0) ? or do we need to check for it?
		FSparseMatrixD InteriorMatrix(NumInteriorVerts, NumInteriorVerts);
		InteriorMatrix.setFromTriplets(LaplacianAsTripletList.begin(), LaplacianAsTripletList.end());
		LaplacianInterior.swap(InteriorMatrix);
		LaplacianInterior.makeCompressed();
	}

	// populate the boundary matrix
	{
		FSparseMatrixD BoundaryMatrix(NumInteriorVerts, NumBoundaryVerts);
		BoundaryMatrix.setFromTriplets(BoundaryAsTripletList.begin(), BoundaryAsTripletList.end());
		LaplacianBoundary.swap(BoundaryMatrix);
		LaplacianBoundary.makeCompressed();
	}

}

double ConstructScaledCotangentLaplacian(const FDynamicMesh3& DynamicMesh, FVertexLinearization& VertexMap, FSparseMatrixD& LaplacianInterior, FSparseMatrixD& LaplacianBoundary, const bool bClampAreas)
{
	typedef FSparseMatrixD::Scalar  ScalarT;
	typedef Eigen::Triplet<ScalarT> MatrixTripletT;

	// diagonal mass matrix.
	FSparseMatrixD AreaMatrix;
	FSparseMatrixD CotangentInterior;
	FSparseMatrixD CotangentBoundary;
	ConstructCotangentLaplacian(DynamicMesh, VertexMap, AreaMatrix, CotangentInterior, CotangentBoundary);

	// Find average entry in the area matrix
	const int32 Rank = AreaMatrix.cols();
	double AveArea = 0.;
	for (int32 i = 0; i < Rank; ++i)
	{
		double Area = AreaMatrix.coeff(i, i);
		checkSlow(Area > 0.);  // Area must be positive.
		AveArea += Area;
	}
	AveArea /= Rank;
	
	std::vector<MatrixTripletT> ScaledInvAreaTriplets;
	ScaledInvAreaTriplets.reserve(Rank);
	for (int32 i = 0; i < Rank; ++i)
	{
		double Area = AreaMatrix.coeff(i, i);
		double ScaledInvArea = AveArea / Area;
		if (bClampAreas)
		{
			ScaledInvArea = FMath::Clamp(ScaledInvArea, 0.5, 5.); // when  squared this gives largest scales 100 x smallest
		}

		ScaledInvAreaTriplets.push_back(MatrixTripletT(i, i, ScaledInvArea));
	}

	FSparseMatrixD ScaledInvAreaMatrix(Rank, Rank);
	ScaledInvAreaMatrix.setFromTriplets(ScaledInvAreaTriplets.begin(), ScaledInvAreaTriplets.end());
	ScaledInvAreaMatrix.makeCompressed();

	LaplacianBoundary = ScaledInvAreaMatrix * CotangentBoundary;
	LaplacianBoundary.makeCompressed();
	LaplacianInterior = ScaledInvAreaMatrix * CotangentInterior;
	LaplacianInterior.makeCompressed();	

	return AveArea;
}

void ConstructCotangentLaplacian(const FDynamicMesh3& DynamicMesh, FVertexLinearization& VertexMap, FSparseMatrixD& LaplacianInterior, FSparseMatrixD& LaplacianBoundary, const bool bClampWeights)
{


	typedef FSparseMatrixD::Scalar  ScalarT;
	typedef Eigen::Triplet<ScalarT> MatrixTripletT;


	
	// Sync the mapping between the mesh vertex ids and their offsets in a nominal linear array.
	VertexMap.Reset(DynamicMesh);

	const TArray<int32>& ToMeshV = VertexMap.ToId();
	const TArray<int32>& ToIndex = VertexMap.ToIndex();
	const int32 NumVerts = VertexMap.NumVerts();

	const int32 NumBoundaryVerts = VertexMap.NumBoundaryVerts();
	const int32 NumInteriorVerts = NumVerts - NumBoundaryVerts;

	// Eigen constructs a sparse matrix from a linearized array of matrix entries.
	// pre-allocate space for the temporary triplet list 

	std::vector<MatrixTripletT> LaplacianAsTripletList;
	std::vector<MatrixTripletT> BoundaryAsTripletList;
	{
		int32 NumMatrixEntries = ComputeNumMatrixElements(DynamicMesh, ToMeshV);
		LaplacianAsTripletList.reserve(NumMatrixEntries);
	}

	// Map the triangles.

	FTriangleLinearization TriangleMap(DynamicMesh);

	const TArray<int32>& ToMeshTri = TriangleMap.ToId();
	const TArray<int32>& ToTriIdx = TriangleMap.ToIndex();
	const int32 NumTris = TriangleMap.NumTris();

	// Create an array that holds all the geometric information we need for each triangle.

	TArray<CotanTriangleData>  CotangentTriangleDataArray = ConstructTriangleDataArray<CotanTriangleData>(DynamicMesh, TriangleMap);


	// Construct Laplacian Matrix: loop over verts constructing the corresponding matrix row.
	//                             skipping the boundary verts for later use.

	for (int32 i = 0; i < NumInteriorVerts; ++i)
	{
		const int32 IVertId = ToMeshV[i]; // I - the row


		// Compute the Voronoi area for this vertex.
		double WeightArea = 0.;
		for (int32 TriId : DynamicMesh.VtxTrianglesItr(IVertId))
		{
			const int32 TriIdx = ToTriIdx[TriId];
			const CotanTriangleData& TriData = CotangentTriangleDataArray[TriIdx];


			// The three VertIds for this triangle.
			const FIndex3i TriVertIds = DynamicMesh.GetTriangle(TriId);

			// Which of the corners is IVertId?
			int32 Offset = 0;
			while (TriVertIds[Offset] != IVertId)
			{
				Offset++;
				checkSlow(Offset < 3);
			}

			WeightArea += TriData.VoronoiArea[Offset];
		}


		double WeightII = 0.; // accumulate to equal and opposite the sum of the neighbor weights

		// for each connecting edge

		for (int32 EdgeId : DynamicMesh.VtxEdgesItr(IVertId))
		{
			// [v0, v1, t0, t1]:  NB: both t0 & t1 exist since IVert isn't a boundary vert.
			FIndex4i Edge = DynamicMesh.GetEdge(EdgeId);


			// the other vert in the edge - identifies the matrix column
			const int32 JVertId = (Edge[0] == IVertId) ? Edge[1] : Edge[0];  // J - the column

			checkSlow(JVertId != IVertId);

			// Get the cotangents for this edge.

			const int32 Tri0Idx = ToTriIdx[Edge[2]];
			const CotanTriangleData& Tri0Data = CotangentTriangleDataArray[Tri0Idx];
			const double CotanAlpha = Tri0Data.GetOpposingCotangent(EdgeId);

			// The second triangle will be invalid if this is an edge!

			const double CotanBeta = (Edge[3] != FDynamicMesh3::InvalidID) ? CotangentTriangleDataArray[ToTriIdx[Edge[3]]].GetOpposingCotangent(EdgeId) : 0.;

			double WeightIJ = 0.5 * (CotanAlpha + CotanBeta);

			// clamp the weight
			if (bClampWeights)
			{
				WeightIJ = FMath::Clamp(WeightIJ, -1.e5 * WeightArea, 1.e5 * WeightArea);
			}

			WeightII += WeightIJ;

			const int32 j = ToIndex[JVertId];

			if (j < NumInteriorVerts)
			{ 
				LaplacianAsTripletList.push_back(MatrixTripletT(i, j, WeightIJ / WeightArea));
			}
			else
			{
				int32 jBoundary = j - NumInteriorVerts;
				BoundaryAsTripletList.push_back(MatrixTripletT(i, jBoundary, WeightIJ / WeightArea));
			}
			

		}

		LaplacianAsTripletList.push_back(MatrixTripletT(i, i, -WeightII / WeightArea));

	}

	// populate the Laplacian for the interior.
	{
		// [djh] - can we construct with (0,0) ? or do we need to check for it?
		FSparseMatrixD InteriorMatrix(NumInteriorVerts, NumInteriorVerts);
		InteriorMatrix.setFromTriplets(LaplacianAsTripletList.begin(), LaplacianAsTripletList.end());
		LaplacianInterior.swap(InteriorMatrix);
		LaplacianInterior.makeCompressed();
	}

	// populate the boundary matrix
	{
		FSparseMatrixD BoundaryMatrix(NumInteriorVerts, NumBoundaryVerts);
		BoundaryMatrix.setFromTriplets(BoundaryAsTripletList.begin(), BoundaryAsTripletList.end());
		LaplacianBoundary.swap(BoundaryMatrix);
		LaplacianBoundary.makeCompressed();
	}

}


void ConstructMeanValueWeightLaplacian(const FDynamicMesh3& DynamicMesh, FVertexLinearization& VertexMap, FSparseMatrixD& LaplacianInterior, FSparseMatrixD& LaplacianBoundary)
{

	typedef FSparseMatrixD::Scalar  ScalarT;
	typedef Eigen::Triplet<ScalarT> MatrixTripletT;


	// Sync the mapping between the mesh vertex ids and their offsets in a nominal linear array.
	VertexMap.Reset(DynamicMesh);

	const TArray<int32>& ToMeshV = VertexMap.ToId();
	const TArray<int32>& ToIndex = VertexMap.ToIndex();
	const int32 NumVerts = VertexMap.NumVerts();

	const int32 NumBoundaryVerts = VertexMap.NumBoundaryVerts();
	const int32 NumInteriorVerts = NumVerts - NumBoundaryVerts;

	// Eigen constructs a sparse matrix from a linearized array of matrix entries.
	// pre-allocate space for the temporary triplet list 

	std::vector<MatrixTripletT> LaplacianAsTripletList;
	std::vector<MatrixTripletT> BoundaryAsTripletList;
	{
		int32 NumMatrixEntries = ComputeNumMatrixElements(DynamicMesh, ToMeshV);
		LaplacianAsTripletList.reserve(NumMatrixEntries);
	}

	// Map the triangles.

	FTriangleLinearization TriangleMap(DynamicMesh);

	const TArray<int32>& ToMeshTri = TriangleMap.ToId();
	const TArray<int32>& ToTriIdx = TriangleMap.ToIndex();
	const int32 NumTris = TriangleMap.NumTris();


	// Create an array that holds all the geometric information we need for each triangle.

	TArray<MeanValueTriangleData>  TriangleDataArray = ConstructTriangleDataArray<MeanValueTriangleData>(DynamicMesh, TriangleMap);


	// Construct Laplacian Matrix: loop over verts constructing the corresponding matrix row.
	//                             skipping the boundary verts for later use.

	for (int32 i = 0; i < NumInteriorVerts; ++i)
	{
		const int32 IVertId = ToMeshV[i]; // I - the row


		double WeightII = 0.; // accumulate to equal and opposite the sum of the neighbor weights

		// for each connecting edge

		for (int32 EdgeId : DynamicMesh.VtxEdgesItr(IVertId))
		{
			// [v0, v1, t0, t1]:  NB: both t0 & t1 exist since IVert isn't a boundary vert.
			FIndex4i Edge = DynamicMesh.GetEdge(EdgeId);

			// the other vert in the edge - identifies the matrix column
			const int32 JVertId = (Edge[0] == IVertId) ? Edge[1] : Edge[0];  // J - the column

			// Get the cotangents for this edge.

			const int32 Tri0Idx = ToTriIdx[Edge[2]];
			const auto& Tri0Data = TriangleDataArray[Tri0Idx];
			double TanHalfAngleSum = Tri0Data.GetTanHalfAngle(IVertId);
			double EdgeLength = FMath::Max(1.e-5, Tri0Data.GetEdgeLenght(EdgeId)); // Clamp the length

			// The second triangle will be invalid if this is an edge!

			TanHalfAngleSum += (Edge[3] != FDynamicMesh3::InvalidID) ? TriangleDataArray[ToTriIdx[Edge[3]]].GetTanHalfAngle(IVertId) : 0.;

			double WeightIJ = TanHalfAngleSum / EdgeLength;
			WeightII += WeightIJ;

			const int32 j = ToIndex[JVertId];

			
			if (j < NumInteriorVerts)
			{
				LaplacianAsTripletList.push_back(MatrixTripletT(i, j, WeightIJ));
			}
			else
			{
				int32 jBoundary = j - NumInteriorVerts;
				BoundaryAsTripletList.push_back(MatrixTripletT(i, jBoundary, WeightIJ));
			}

		}

		LaplacianAsTripletList.push_back(MatrixTripletT(i, i, -WeightII));


	}

	// populate the Laplacian for the interior.
	{
		// [djh] - can we construct with (0,0) ? or do we need to check for it?
		FSparseMatrixD InteriorMatrix(NumInteriorVerts, NumInteriorVerts);
		InteriorMatrix.setFromTriplets(LaplacianAsTripletList.begin(), LaplacianAsTripletList.end());
		LaplacianInterior.swap(InteriorMatrix);
		LaplacianInterior.makeCompressed();
	}

	// populate the boundary matrix
	{
		FSparseMatrixD BoundaryMatrix(NumInteriorVerts, NumBoundaryVerts);
		BoundaryMatrix.setFromTriplets(BoundaryAsTripletList.begin(), BoundaryAsTripletList.end());
		LaplacianBoundary.swap(BoundaryMatrix);
		LaplacianBoundary.makeCompressed();
	}

}


void ConstructLaplacian(const ELaplacianWeightScheme Scheme, const FDynamicMesh3& DynamicMesh, FVertexLinearization& VertexMap, FSparseMatrixD& LaplacianInterior, FSparseMatrixD& LaplacianBoundary)
{

	switch (Scheme)
	{
	default:
	case ELaplacianWeightScheme::Uniform:
		ConstructUniformLaplacian(DynamicMesh, VertexMap, LaplacianInterior, LaplacianBoundary);
		break;
	case ELaplacianWeightScheme::Umbrella:
		ConstructUmbrellaLaplacian(DynamicMesh, VertexMap, LaplacianInterior, LaplacianBoundary);
		break;
	case ELaplacianWeightScheme::Valence:
		ConstructValenceWeightedLaplacian(DynamicMesh, VertexMap, LaplacianInterior, LaplacianBoundary);
		break;
	case ELaplacianWeightScheme::Cotangent:
	{
		bool bClampWeights = false;
		ConstructScaledCotangentLaplacian(DynamicMesh, VertexMap, LaplacianInterior, LaplacianBoundary, bClampWeights);
		break;
	}
	case ELaplacianWeightScheme::ClampedCotangent:
	{
		bool bClampWeights = true;
		ConstructScaledCotangentLaplacian(DynamicMesh, VertexMap, LaplacianInterior, LaplacianBoundary, bClampWeights);
		break;
	}
	case ELaplacianWeightScheme::MeanValue:
		ConstructMeanValueWeightLaplacian(DynamicMesh, VertexMap, LaplacianInterior, LaplacianBoundary);
		break;
	}
}

static void ExtractBoundaryVerts(const FVertexLinearization& VertexMap, TArray<int32>& BoundaryVerts)
{
	int32 NumBoundaryVerts = VertexMap.NumBoundaryVerts();
	int32 NumInternalVerts = VertexMap.NumVerts() - NumBoundaryVerts;
	BoundaryVerts.Empty(NumBoundaryVerts);

	const auto& ToId = VertexMap.ToId();
	for (int32 i = NumInternalVerts; i < VertexMap.NumVerts(); ++i)
	{
		int32 VtxId = ToId[i];
		BoundaryVerts.Add(VtxId);
	}
}

TUniquePtr<FSparseMatrixD> ConstructUniformLaplacian(const FDynamicMesh3& DynamicMesh, FVertexLinearization& VertexMap, TArray<int32>* BoundaryVerts)
{

	TUniquePtr<FSparseMatrixD> LaplacianMatrix(new FSparseMatrixD);
	FSparseMatrixD BoundaryMatrix;

	ConstructUniformLaplacian(DynamicMesh, VertexMap, *LaplacianMatrix, BoundaryMatrix);

	if (BoundaryVerts)
	{
		ExtractBoundaryVerts(VertexMap, *BoundaryVerts);
	}

	return LaplacianMatrix;
}
	


TUniquePtr<FSparseMatrixD> ConstructUmbrellaLaplacian(const FDynamicMesh3& DynamicMesh, FVertexLinearization& VertexMap, TArray<int32>* BoundaryVerts)
{
	TUniquePtr<FSparseMatrixD> LaplacianMatrix(new FSparseMatrixD);
	FSparseMatrixD BoundaryMatrix;

	ConstructUmbrellaLaplacian(DynamicMesh, VertexMap, *LaplacianMatrix, BoundaryMatrix);

	if (BoundaryVerts)
	{
		ExtractBoundaryVerts(VertexMap, *BoundaryVerts);
	}

	return LaplacianMatrix;
}
	

TUniquePtr<FSparseMatrixD> ConstructValenceWeightedLaplacian(const FDynamicMesh3& DynamicMesh, FVertexLinearization& VertexMap, TArray<int32>* BoundaryVerts)
{

	TUniquePtr<FSparseMatrixD> LaplacianMatrix(new FSparseMatrixD);
	FSparseMatrixD BoundaryMatrix;

	ConstructValenceWeightedLaplacian(DynamicMesh, VertexMap, *LaplacianMatrix, BoundaryMatrix);

	if (BoundaryVerts)
	{
		ExtractBoundaryVerts(VertexMap, *BoundaryVerts);
	}
	
	return LaplacianMatrix;

}


TUniquePtr<FSparseMatrixD> ConstructCotangentLaplacian(const FDynamicMesh3& DynamicMesh, FVertexLinearization& VertexMap, FSparseMatrixD& AreaMatrix, TArray<int32>* BoundaryVerts)
{
	TUniquePtr<FSparseMatrixD> LaplacianMatrix(new FSparseMatrixD);
	FSparseMatrixD BoundaryMatrix;

	ConstructCotangentLaplacian(DynamicMesh, VertexMap, AreaMatrix, *LaplacianMatrix, BoundaryMatrix);

	if (BoundaryVerts)
	{
		ExtractBoundaryVerts(VertexMap, *BoundaryVerts);
	}

	return LaplacianMatrix;
}



TUniquePtr<FSparseMatrixD> ConstructCotangentLaplacian(const FDynamicMesh3& DynamicMesh, FVertexLinearization& VertexMap, const bool bClampWeights, TArray<int32>* BoundaryVerts)
{

	TUniquePtr<FSparseMatrixD> LaplacianMatrix(new FSparseMatrixD);
	FSparseMatrixD BoundaryMatrix;

	ConstructCotangentLaplacian(DynamicMesh, VertexMap, *LaplacianMatrix, BoundaryMatrix, bClampWeights);

	if (BoundaryVerts)
	{
		ExtractBoundaryVerts(VertexMap, *BoundaryVerts);
	}

	return LaplacianMatrix;
}



TUniquePtr<FSparseMatrixD> ConstructMeanValueWeightLaplacian(const FDynamicMesh3& DynamicMesh, FVertexLinearization& VertexMap, TArray<int32>* BoundaryVerts)
{

	TUniquePtr<FSparseMatrixD> LaplacianMatrix(new FSparseMatrixD);
	FSparseMatrixD BoundaryMatrix;

	ConstructMeanValueWeightLaplacian(DynamicMesh, VertexMap, *LaplacianMatrix, BoundaryMatrix);

	if (BoundaryVerts)
	{
		ExtractBoundaryVerts(VertexMap, *BoundaryVerts);
	}

	return LaplacianMatrix;
}

	


TUniquePtr<FSparseMatrixD> ConstructLaplacian(const ELaplacianWeightScheme Scheme, const FDynamicMesh3& DynamicMesh, FVertexLinearization& VertexMap, TArray<int32>* BoundaryVerts)
{

	switch (Scheme)
	{
	default:
	case ELaplacianWeightScheme::Uniform:
		return ConstructUniformLaplacian(DynamicMesh, VertexMap, BoundaryVerts);
		break;
	case ELaplacianWeightScheme::Umbrella:
		return ConstructUmbrellaLaplacian(DynamicMesh, VertexMap, BoundaryVerts);
		break;
	case ELaplacianWeightScheme::Valence:
		return ConstructValenceWeightedLaplacian(DynamicMesh, VertexMap, BoundaryVerts);
		break;
	case ELaplacianWeightScheme::Cotangent:
	{
		bool bClampWeights = false;
		return ConstructCotangentLaplacian(DynamicMesh, VertexMap, bClampWeights, BoundaryVerts);
		break;
	}
	case ELaplacianWeightScheme::ClampedCotangent:
	{
		bool bClampWeights = true;
		return ConstructCotangentLaplacian(DynamicMesh, VertexMap, bClampWeights, BoundaryVerts);
		break;
	}
	case ELaplacianWeightScheme::MeanValue:
		return ConstructMeanValueWeightLaplacian(DynamicMesh, VertexMap, BoundaryVerts);
		break;
	}
}
