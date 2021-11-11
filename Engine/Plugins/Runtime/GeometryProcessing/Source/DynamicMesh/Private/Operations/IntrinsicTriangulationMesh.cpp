// Copyright Epic Games, Inc. All Rights Reserved.

#include "Operations/IntrinsicTriangulationMesh.h"
#include "Operations/MeshGeodesicSurfaceTracer.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Util/IndexUtil.h"
#include "Algo/Reverse.h"
#include "Containers/BitArray.h"
#include "Containers/Queue.h"
#include "MathUtil.h"
#include "Async/ParallelFor.h"

using namespace UE::Geometry;


/**
 *  Helper functions that use triangle edge lengths instead of vertex positions to do some euclidean triangle-based calculations.
 *  Many of these are based on the "law of cosines" 
 */
namespace
{
	/**
	* Given a flat triangle with edges L0, L1, L2 in CCW order, compute the angle between
	* the L0 and L2 edge in radians [0, Pi] range.
	* 
	* used by Sharp, Soliman and Crane [2019, ACM Transactions on Graphics] section 3.2
	*
	* but can be understood as trivial application of "the law of cosines"
	*/
	double InteriorAngle(const double L0, const double L1, const double L2)
	{
		// V1 = V2 - V0.   L1^2 = ||V1||^2 = ||V2||^2 + ||V0||^2 - 2 Dot(V2, V0)  =  L2^2 + L0^2 - 2 L2 L0 CosTheta
		// CosTheta = (L0^2 + L1^2 - L2^2) / (2 L0 L1)
		double CosTheta = (L0 * L0 + L2 * L2 - L1 * L1) / (2. * L0 * L2);

		// account for roundoff, theoretically this would already be in the [-1,1] range
		CosTheta = FMath::Clamp(CosTheta, -1., 1.);

		return FMath::Acos(CosTheta);
	}

	/**
	* For a triangle, use the law of cos to compute the third edge length
	*  - Given interior angle in radians AngleR, and adjacent edge lengths L0 and L1, this computes the opposing edge length. 
	*/
	double LawOfCosLength(const double AngleR, const double L0, const double L1)
	{
		double L2sqr = L0 * L0 + L1 * L1 - 2.*L0*L1*FMath::Cos(AngleR);
		// theoretically this is always positive, but protect round-off.   Alternately, could compute (L0-L1)^2 + 4 L0 L1 Sin^2 ( AngleR/2)  
		L2sqr = FMath::Max(L2sqr, 0.);
		return FMath::Sqrt(L2sqr);
	}

	/**
	* Compute the unsigned distance between two points, described as barycentric coordinates
	* relative to a triangle with side lengths L0, L1, L2 is CCW order.
	* @params L0, L1, L2 - triangle side lengths in CCW order
	* @param BCPoint0  - Barycentric coordinates for the first point
	* @param BCPoint1  - Barycentric coordinates for the second point
	*
	* NB: it is assumed that the points are within the triangle, and the triangle is valid (i.e. lengths satisfy triangle inequality)
	*/
	double DistanceBetweenBarycentricPoints(const double L0, const double L1, const double L2, const FVector3d& BCPoint0, const FVector3d& BCPoint1)
	{
		// see Sharp, Soliman and Crane [2019, ACM Transactions on Graphics] or  Schindler and Chen [2012, Section 3.2]
		using Vector3Type = FVector3d;
		using ScalarType = double;
		const ScalarType Zero(0);

		const Vector3Type u = BCPoint1 - BCPoint0;
		const ScalarType dsqr = -(L0 * L0 * u[0] * u[1] + L1 * L1 * u[1] * u[2] + L2 * L2 * u[2] * u[0]);
		const ScalarType d = (dsqr < Zero) ? Zero : sqrt(dsqr);
		return d;
	}


	/**
	* Compute a vector (in polar form) from triangle corner to a point within the triangle.
	* @params L0, L1, L2 - triangle side lengths in CCW order
	* @param BarycetricPoint - interior point in triangle
	*
	* @return, a polar vector from the vertex shared by L0, L2 to the BarycentricPoint in the form
	*          Vector[0] = radial distance
	*          Vector[1] = angle measured from the L0 side of the triangle
	*/
	FVector2d VectorToPoint(const double L0, const double L1, const double L2, const FVector3d& BarycentricPoint)
	{
		FVector3d BCpi(1., 0., 0.);
		FVector3d BCpj(0., 1., 0.);
		double rpi = DistanceBetweenBarycentricPoints(L0, L1, L2, BCpi, BarycentricPoint);
		double rpj = DistanceBetweenBarycentricPoints(L0, L1, L2, BCpj, BarycentricPoint);
		double angle = InteriorAngle(L0, rpj, rpi);
		return FVector2d(rpi, angle);
	}

	/**
	* Heron's formula for computing the area of a triangle given the lengths of all three sides.  
	*/
	double TriangleArea(const double L0, const double L1, const double L2)
	{
		// note: since this is a triangle, the lengths should satisfy triangle inequality, meaning this should be positive
		double AreaSqrTimes16 = FMath::Max(0., (L0 + L1 + L2) * (-L0 + L1 + L2 ) * (L0 -L1 + L2) * (L0 + L1 -L2));
		return FMath::Sqrt(AreaSqrTimes16) * 0.25;
	}
	double TriangleArea(const FVector3d& Ls)
	{
		return TriangleArea(Ls[0], Ls[1], Ls[2]);
	}
	/**
	* Compute the cotangent of the interior angle opposite the edge L0, give a triangle with edges {L0, L1, L2} in CCW order
	*/
	double ComputeCotangent(const double L0, const double L1, const double L2)
	{
		const double Area = TriangleArea(L0, L1, L2); 
		const double CT = 0.25 * (-L0 * L0 + L1 * L1 + L2 * L2) / Area;
		return CT;
	}

	/**
	* Return the location of third vertex in a triangle in R2 with prescribed lengths
	* such that the resulting triangle could be constructed as { (0,0),  (L1, 0), Result}.
	* 
	* NB: this assumes, but does not check, that the lengths satisfy the triangle inequality
	*/
	FVector2d ComputeOpposingVert2d(const double L0, const double L1, const double L2)
	{
		const double Area = TriangleArea(L0, L1, L2);
		const double Y = 2. * Area / L0;
		double X = FMath::Sqrt(FMath::Max(0., L2 * L2 - Y * Y));
		FVector2d ResultPos(X, Y);
		// angle between L0 and L2 edges can be computed form 2 * L0 * L2 * Cos(theta) = L0^2 + L2^2 - L1^2
		if (L0 * L0 + L2 * L2 < L1 * L1)
		{
			ResultPos.X = -X; // angle > 90-degrees.
		}
		return ResultPos;
	}

	/**
	* Given three side lengths that satisfy the triangle inequality,
	* this updates the 2d positions to be the vertices of a triangle, 
	* the edge lengths of which ( when in CCW order)  match the prescribed lengths.  
	* with vertices { (0,0),  (L1, 0), (X, Y)} where Y > 0
	*/
	void TriangleFromLengths(const double L0, const double L1, const double L2, FVector2d& p0, FVector2d& p1, FVector2d& p3)
	{
		p0 = FVector2d(0., 0.);
		p1 = FVector2d(L1, 0.);
		p3 = ComputeOpposingVert2d(L0, L1, L2);
	}

	
};


/**
 *  Utilities and generic implementation for flipping a mesh to Delaunay  
 */
namespace
{
	/**
	* LIFO for unique DynamicMesh IDs, can be constructed from any FRefCountVector::IndexEnumerable
	* and supports a filtering function on construction.  If the filtering function is absent, all
	* unique elements will be added, otherwise only those selected by the filter.
	*    
	*	for example: 
	* 
	*	 auto AddToQueueFilter = [](int ID)->bool{...};
	* 
	*    FIndexLIFO EdgeQueueLIFO(Mesh.EdgeIndicesItr(), AddToQueueFilter);
	*/
	class FIndexLIFO
	{
	public:
		// constructor that adds all unique IDs from IndexEnumerable
		FIndexLIFO(FRefCountVector::IndexEnumerable IDEnumerable)
			: IsEnqueued(false, (int32)IDEnumerable.Vector->GetMaxIndex())
		{
			const int32 NumIDs = (int32)IDEnumerable.Vector->GetCount();
			IDs.Reserve(NumIDs);
			for (int32 ID : IDEnumerable)
			{
				Enqueue(ID);
			}
		}
		// filtering constructor that only adds IDs for which Filter(ID) is true
		FIndexLIFO(FRefCountVector::IndexEnumerable IDEnumerable, const TFunctionRef<bool(int32)>& Filter)
			: IsEnqueued(false, (int32)IDEnumerable.Vector->GetMaxIndex())
		{
			const int32 NumIDs = (int32)IDEnumerable.Vector->GetCount();
			IDs.Reserve(NumIDs);
			for (int32 ID : IDEnumerable)
			{
				if (Filter(ID))
				{
					Enqueue(ID);
				}
			}
		}
		bool Dequeue(int32& IDOut)
		{
			if (IDs.Num())
			{
				IDOut = IDs.Pop(false);
				IsEnqueued[IDOut] = false;
				return true;
			}
			IDOut = -1;
			return false;
		}
		void Enqueue(int32 ID)
		{
			if (!IsEnqueued[ID])
			{
				IDs.Add(ID);
				IsEnqueued[ID] = true;
			}
		}
	private:
		FIndexLIFO();
		TBitArray<FDefaultBitArrayAllocator> IsEnqueued;
		TArray<int32> IDs;
	};

	/**
	* FIFO for unique Dynamic Mesh IDs - can be constructed from any FRefCountVector::IndexEnumerable
	* and supports a filtering function on construction.  If the filtering function is absent, all
	* unique elements will be added, otherwise only those selected by the filter.
	*
	*	for example:
	*
	*	 auto AddToQueueFilter = [](int ID)->bool{...};
	*
	*    FIndexFIFO EdgeQueueFIFO(Mesh.EdgeIndicesItr(), AddToQueueFilter);
	*/
	class FIndexFIFO
	{
	public:
		// constructor that adds all unique IDs from IndexEnumerable
		FIndexFIFO(FRefCountVector::IndexEnumerable IDEnumerable)
			: IsEnqueued(false, (int32)IDEnumerable.Vector->GetMaxIndex())
		{
			const int32 NumEdges = (int32)IDEnumerable.Vector->GetMaxIndex();
			for (int32 ID : IDEnumerable)
			{
				Enqueue(ID);
			}
		}

		// filtering constructor that only adds IDs for which Filter(ID) is true  Note: Filter is called in parallel.
		FIndexFIFO(FRefCountVector::IndexEnumerable IDEnumerable, const TFunctionRef<bool(int32)>& Filter)
			: IsEnqueued(false, (int32)IDEnumerable.Vector->GetMaxIndex())
		{
			// parallel evaluation that assumes Filter is expensive
			
			const int32 MaxID = (int32)IDEnumerable.Vector->GetMaxIndex();
			TArray<int32> ToInclude;
			ToInclude.AddZeroed(MaxID);
			ParallelFor(MaxID, [&ToInclude, &Filter](int32 ID)
			{
				if (Filter(ID))
				{
					ToInclude[ID] = 1;
				}
			});
			for (int32 ID = 0; ID < MaxID; ++ID)
			{
				if (ToInclude[ID] == 1)
				{
					Enqueue(ID);
				}
			}
		}

		bool Dequeue(int32& IDOut)
		{
			if (IDs.Dequeue(IDOut))
			{
				IsEnqueued[IDOut] = false;
				return true;
			}
			IDOut = -1;
			return false;
		}
		void Enqueue(int32 ID)
		{
			if (!IsEnqueued[ID])
			{
				IDs.Enqueue(ID);
				IsEnqueued[ID] = true;
			}
		}
	private:
		FIndexFIFO();
		TBitArray<FDefaultBitArrayAllocator> IsEnqueued;
		TQueue<int32> IDs;
	};


	/**
	* Carry out edge flips on intrinsic mesh until either the MaxFlipCount is reached or the mesh is fully Delaunay and returns
	* the number of flips.
	* Note: There can be cases where an edge can not flip (e.g. if the resulting edge already exists)
	*       - for this reason, there may be some "uncorrected" edges in the resulting mesh.
	* 
	* @param IntrinsicMesh - The mesh to be operated on.
	* @param Uncorrected   - contains on return, all the edges that could not be flipped.
	* @param Threshold     - edges with cotan weight less than threshold should be flipped.
	*/
	template <typename MeshType>
	int32 FlipToDelaunayImpl(MeshType& Mesh, TSet<int32>& Uncorrected, const int32 MaxFlipCount, double Threshold = -TMathUtilConstants<double>::ZeroTolerance)
	{
	Uncorrected.Empty();

	// returns true if an edge should be flipped.
	auto EdgeShouldFlipFilter = [&Mesh, Threshold](int32 EID)->bool
	{
		if (!Mesh.IsEdge(EID) || Mesh.IsBoundaryEdge(EID))
		{
			return false;
		}
	
		const double CotanWeightValue = Mesh.EdgeCotanWeight(EID);
		
		// Delaunay test
		return CotanWeightValue < Threshold;
	};

	// Enqueue the "bad" edges.
	FIndexFIFO EdgeQueue(Mesh.EdgeIndicesItr(), EdgeShouldFlipFilter);

	// flip away the bad edges
	int32 FlipCount = 0;
	int32 EID;
	while (EdgeQueue.Dequeue(EID) && FlipCount < MaxFlipCount)
	{
		if (EdgeShouldFlipFilter(EID))
		{
			FIntrinsicTriangulation::FEdgeFlipInfo EdgeFlipInfo;
			const EMeshResult Result = Mesh.FlipEdge(EID, EdgeFlipInfo);

			if (Result == EMeshResult::Ok)
			{
				FlipCount++;
				for (int32 t = 0; t < 2; ++t)
				{
					const FIndex3i TriEIDs = Mesh.GetTriEdges(EdgeFlipInfo.Triangles[t]);
					int32 IndexOf = TriEIDs.IndexOf(EID);
					EdgeQueue.Enqueue(TriEIDs[(IndexOf + 1) % 3]);
					EdgeQueue.Enqueue(TriEIDs[(IndexOf + 2) % 3]);
				}
			}
			else
			{
				Uncorrected.Add(EID);
			}
		}
	}

	return FlipCount;
	}

};

namespace SignpostSufaceTraceUtil
{
	using namespace IntrinsicCorrespondenceUtils;
	/**
	* helper code that uses FSignpost to trace from a specified intrinsic mesh vertex in a given direction.
	* 
	*Note: this doesn't check the validity of the start point - the calling code should do that
	*/
	UE::Geometry::FMeshGeodesicSurfaceTracer TraceFromIntrinsicVert( const FSignpost& SignpostData, const int32 TraceStartVID, 
		                                                             const double TracePolarAngle, const double TraceDist )
	{
		const FDynamicMesh3* SurfaceMesh                            = SignpostData.SurfaceMesh; 
		const FSurfacePoint& StartSurfacePoint                      = SignpostData.IntrinsicVertexPositions[TraceStartVID];
		const FSurfacePoint::FSurfacePositionUnion& SurfacePosition = StartSurfacePoint.Position;

		double ActualDist = 0.;
		UE::Geometry::FMeshGeodesicSurfaceTracer SurfaceTracer(*SurfaceMesh);
		switch (StartSurfacePoint.PositionType)
		{
		case FSurfacePoint::EPositionType::Vertex:
		{
			// convert vertex location
			const int32 ExtrinsicVID    = StartSurfacePoint.Position.VertexPosition.VID;
			const int32 ExtrinsicRefEID = SignpostData.VIDToReferenceEID[ExtrinsicVID];

			const FMeshGeodesicSurfaceTracer::FMeshTangentDirection TangentDirection = { ExtrinsicVID, ExtrinsicRefEID, TracePolarAngle };
			ActualDist = SurfaceTracer.TraceMeshFromVertex(TangentDirection, TraceDist);
		}
		break;
		case FSurfacePoint::EPositionType::Edge:
		{
			// convert edge location -  Not used, and might have bugs
			const int32 RefSurfaceEID   = SurfacePosition.EdgePosition.EdgeID;
			double Alpha                = SurfacePosition.EdgePosition.Alpha;
			// convert to BC
			const int32 SurfaceTID      = SurfaceMesh->GetEdgeT(RefSurfaceEID).A;
			const FIndex2i SurfaceEdgeV = SurfaceMesh->GetEdgeV(RefSurfaceEID);
			const int32 IndexOf         = SurfaceMesh->GetTriEdges(SurfaceTID).IndexOf(RefSurfaceEID);
			const bool bSameOrientation = (SurfaceMesh->GetTriangle(SurfaceTID)[IndexOf] == SurfaceEdgeV.A);
			FVector3d BaryPoint(0., 0., 0.);
			if (!bSameOrientation)
			{
				Alpha = 1. - Alpha;
			}
			BaryPoint[IndexOf] = Alpha;
			BaryPoint[(IndexOf + 1) % 3] = 1. - Alpha;

			FMeshGeodesicSurfaceTracer::FMeshSurfaceDirection DirectionOnSurface(RefSurfaceEID, TracePolarAngle);
			ActualDist = SurfaceTracer.TraceMeshFromBaryPoint(SurfaceTID, BaryPoint, DirectionOnSurface, TraceDist);
		}
		break;
		case FSurfacePoint::EPositionType::Triangle:
		{
			// trace from BC point in triangle
			const int32 SurfaceTID = SurfacePosition.TriPosition.TriID;

			const FVector3d BaryPoint( SurfacePosition.TriPosition.BarycentricCoords[0],
									   SurfacePosition.TriPosition.BarycentricCoords[1],
				                       SurfacePosition.TriPosition.BarycentricCoords[2] );
			const int32 RefSurfaceEID = SignpostData.TIDToReferenceEID[SurfaceTID];

			FMeshGeodesicSurfaceTracer::FMeshSurfaceDirection DirectionOnSurface(RefSurfaceEID, TracePolarAngle);
			SurfaceTracer.TraceMeshFromBaryPoint(SurfaceTID, BaryPoint, DirectionOnSurface, TraceDist);
		}
		break;
		default:
		{
			// not possible
			check(0);
		}
		}

		// RVO..
		return SurfaceTracer;
	}
};

namespace FNormalCoordSurfaceTraceImpl
{
	using namespace IntrinsicCorrespondenceUtils;
	/**
	* low-level code that uses normal coordinates to continues tracing a surface edge across an FIntrinsicEdgeFlipMesh until it terminates at a vertex.
	*
	* In order to compute the full crossing history for a surface curve, this function could be called directly after 
	* the first intrinsic edge crossing at which time the 'Crossings' result array will already hold the start vertex and the first edge crossing.
	* 
	* 
	* This function will populate the rest of the sequence of edge crossings for the surface mesh edge
	* as a list of the edges crossed, and not the location on the individual edges.  
	* To compute the those locations the resulting triangle strip should be unfolded and each location can be solved for
	* using TraceEdgeOverHost below.
	*/
	void  ContinueTraceSurfaceEdge( const FIntrinsicEdgeFlipMesh& IntrinsicMesh, const int32 StartTID, const int32 StartEID, const int32 StartP,
		                            TArray<FNormalCoordinates::FEdgeAndCrossingIdx>& Crossings )
	{
		const FNormalCoordinates& NCoords = IntrinsicMesh.GetNormalCoordinates();

		if (!IntrinsicMesh.IsTriangle(StartTID))
		{
			return;
		}

		const FIndex3i StartTriEIDs = IntrinsicMesh.GetTriEdges(StartTID);
		checkSlow(StartTriEIDs.IndexOf(StartEID) != -1);

		const int32 NumXStartEID = NCoords.NormalCoord[StartEID];
		if (StartP > NumXStartEID || StartP < 1) // note the actual permitted range of P is smaller.. 
		{
			checkSlow(0); // this shouldn't happen
			return;
		}

		// This is adapted from Algorithm 2 of  Gillespi et al, 2020
		auto GetNextCrossing = [&IntrinsicMesh, &NCoords](int32& TriID, int32& EdgeID, int32& P)
		{
			// get next tri
			TriID = [&]
				{
					const FIndex2i EdgeT = IntrinsicMesh.GetEdgeT(EdgeID);
					return (EdgeT.A == TriID) ? EdgeT.B : EdgeT.A;
				}();
			checkSlow(TriID != -1);

			const FIndex3i TriEIDs = IntrinsicMesh.GetTriEdges(TriID);
			const int32 IndexOf    = TriEIDs.IndexOf(EdgeID);

			const int32 Edge_ji = EdgeID;
			const int32 Edge_il = TriEIDs[(IndexOf + 1) % 3];
			const int32 Edge_lj = TriEIDs[(IndexOf + 2) % 3];

			const int32 N_ji = NCoords.NormalCoord[Edge_ji];
			const int32 N_il = NCoords.NormalCoord[Edge_il];
			const int32 N_lj = NCoords.NormalCoord[Edge_lj];

			const int32 Pin = P;

			if (N_ji > N_lj + N_il)  // case 1
			{
				// some of the N_ji edges that cross edge IJ must connect with the vertex at L 
				if (P <= N_il)
				{
					// exits side il
					EdgeID = Edge_il;
					P      = Pin;
				}
				else if ((N_il < P) && (P <= N_ji - N_lj))
				{
					// this path terminates at vertex
					// this is a slight abuse.  The convention, when CrossingIndex = 0, we specify the vertex by the edge that originates there.
					EdgeID = Edge_lj; // i = GetTriangle()[TriEIDs.IndexOf(EdgeID_lj)] will be the vertex.
					P      = 0;
				}
				else
				{
					EdgeID = Edge_lj;
					P      = Pin - (N_ji - N_lj);
				}

			}
			else if (N_lj > N_ji + N_il)  // case 2
			{
				EdgeID = Edge_lj;
				P      = Pin + (N_lj - N_ji);

			}
			else if (N_il > N_ji + N_lj) // case 3
			{
				EdgeID = Edge_il;
				P      = Pin;

			}
			else  // case 4
			{
				const int32 Cij_l = (N_lj + N_il - N_ji) / 2;
				if (P <= N_il - Cij_l)
				{
					EdgeID = Edge_il;
					P      = Pin;

				}
				else
				{
					const int32 Clj_i = (N_il + N_ji - N_lj) / 2;
					EdgeID = Edge_lj;
					P      = Pin - Clj_i + Cij_l;
				}
			}
		};

		int32 P = StartP;
		int32 EID = StartEID;
		int32 TID = StartTID;
		// record first crossing
		auto& FirstXing = Crossings.AddZeroed_GetRef();
		FirstXing.TID  = TID;
		FirstXing.EID  = EID;
		FirstXing.CIdx = P;
		do
		{
			GetNextCrossing(TID, EID, P);
			auto& Xing = Crossings.AddZeroed_GetRef();
			Xing.TID   = TID;
			Xing.EID   = EID;
			Xing.CIdx  = P;
			checkSlow(P > -1);
		} while (P != 0); // P = 0 when the path terminates ( paths terminate at vertices only)


	}

	// trace the surface edge - note a surface edge along some surface mesh boundaries can't be specified by this API, but those edges will
	// be the same in the intrinsic mesh since they can't flip.
	TArray<FNormalCoordinates::FEdgeAndCrossingIdx> TraceSurfaceEdge( const FIntrinsicEdgeFlipMesh& IntrinsicMesh,
		                                                              const int32 SurfaceTID, const int32 IndexOf )
	{
		const FNormalCoordinates& NCoords = IntrinsicMesh.GetNormalCoordinates();
		const FDynamicMesh3& SurfaceMesh  = *NCoords.SurfaceMesh;

		if (!SurfaceMesh.IsTriangle(SurfaceTID) || IndexOf > 2 || IndexOf < 0)
		{
			TArray< FNormalCoordinates::FEdgeAndCrossingIdx > EmptyCrossings;
			return EmptyCrossings;
		}


		// the origin vertex
		const int32 StartVID = SurfaceMesh.GetTriangle(SurfaceTID)[IndexOf];
		// the surface edge we are tracing
		const int32 TraceEID = SurfaceMesh.GetTriEdge(SurfaceTID, IndexOf);

		checkSlow(!SurfaceMesh.IsBoundaryEdge(TraceEID));
		const int32 RefEID = NCoords.VIDToReferenceEID[StartVID];
		const int32 OrderOfTraceEID = NCoords.GetEdgeOrder(StartVID, TraceEID);
		const int32 ValenceOfStartVID = NCoords.RefVertDegree[StartVID];
		checkSlow(OrderOfTraceEID != -1);

		// data to gather about the intrinsic triangle that where the trace starts.
		struct
		{
			bool bIsAlsoSurfaceEdge = false;
			int32 TID               = -1;
			int32 EID               = -1;
			int32 IdxOf             = -1;
			int32 FirstRoundabout   = -1;
			int32 SecondRoundabout  = -1;
		} FinderInfo;

		// Identify the target intrinsic triangle where this edge trace starts.
		{

			// where to start when visiting the intrinsic triangles that are adjacent to the startVID
			const int32 VistorStartEID =[&] 
				{	
					if (SurfaceMesh.IsBoundaryVertex(StartVID))
					{
						// due to the way the intrinsic mesh is constructed we know the boundary edges will have the same EID in both meshes.
						// recall, these can't flip, so they are fixed in the intrinsic mesh.
						return RefEID;
					}
					else
					{
						for (int32 NbrEID : IntrinsicMesh.VtxEdgesItr(StartVID))
						{
							// get the first
							return NbrEID;
						}
						return -1; // shouldn't reach here
					}
				}();

			// when visiting each adjacent triangle (in CCW order) test if the edge we trace is inside this triangle.
			auto EdgeFinder = [&](int32 IntrinsicTID, int32 IntrinsicEID, int32 IdxOf)->bool
			{


				const FIndex3i IntrinsicTriEIDs = IntrinsicMesh.GetTriEdges(IntrinsicTID);
				const int32 ThisRoundabout      = NCoords.RoundaboutOrder[IntrinsicTID][IdxOf];
				const bool bEquivalentToSurface = (NCoords.NormalCoord[IntrinsicEID] == 0) && (ThisRoundabout == OrderOfTraceEID);
				int32 NextRoundabout = -1;

				bool bShouldBreak = false;

				if (bEquivalentToSurface) // test if the current intrinsic edge is the same as the surface mesh trace edge
				{
					bShouldBreak = true;
				}
				else // test if the edge starts in this intrinsic triangle
				{
					const int32 NextIntrinsicEID      = IntrinsicTriEIDs[(IdxOf + 2) % 3];
					const FIndex2i NextIntrinsicEdgeT = IntrinsicMesh.GetEdgeT(NextIntrinsicEID);
					const int32 NextIntrinsicTID      = (NextIntrinsicEdgeT.A == IntrinsicTID) ? NextIntrinsicEdgeT.B : NextIntrinsicEdgeT.A;

					if (NextIntrinsicTID != -1)
					{
						const int32 NextIndexOf = IntrinsicMesh.GetTriEdges(NextIntrinsicTID).IndexOf(NextIntrinsicEID);
						NextRoundabout          = NCoords.RoundaboutOrder[NextIntrinsicTID][NextIndexOf];
						if (NextRoundabout < ThisRoundabout)
						{
							// we have crossed the zero roundabout cut
							NextRoundabout += ValenceOfStartVID;
						}
						checkSlow(IntrinsicMesh.GetTriangle(NextIntrinsicTID)[NextIndexOf] == StartVID);
						//checkSlow(NextRoundabout != 0 || ThisRoundabout != 0)
						if ((NextRoundabout > OrderOfTraceEID) && (OrderOfTraceEID >= ThisRoundabout))
						{
							bShouldBreak = true;
						}
					}
					else // mesh boundary case and we made it to the last triangle w/o finding this edge. it must be in this one.
					{
						bShouldBreak = true;
					}
				}

				if (bShouldBreak)
				{
					FinderInfo.bIsAlsoSurfaceEdge = bEquivalentToSurface;
					FinderInfo.TID                = IntrinsicTID;
					FinderInfo.EID                = IntrinsicEID;
					FinderInfo.IdxOf              = IdxOf;
					FinderInfo.FirstRoundabout    = ThisRoundabout;
					FinderInfo.SecondRoundabout   = NextRoundabout;
				}

				return bShouldBreak;
			};
			VisitVertexAdjacentElements(IntrinsicMesh, StartVID, VistorStartEID, EdgeFinder);

			checkSlow(FinderInfo.TID != -1); // should have found something!
		}


		TArray<FNormalCoordinates::FEdgeAndCrossingIdx > Crossings;
		// add start vertex location
		auto& StartXing = Crossings.AddZeroed_GetRef();
		StartXing.TID   = FinderInfo.TID;
		StartXing.EID   = FinderInfo.IdxOf; // TriVIDs(IdxOf) = StartVID
		StartXing.CIdx  = 0;
		if (FinderInfo.bIsAlsoSurfaceEdge)
		{
			// only need to add end vertex location
			auto& EndXing = Crossings.AddZeroed_GetRef();
			EndXing.TID   = FinderInfo.TID;
			EndXing.EID   = (FinderInfo.IdxOf + 1) % 3;  // TriVIDs( (IdxOf +1)%3) = EndVID
			EndXing.CIdx  = 0;
		}
		else
		{
			// edge trace starts at StartVID and exits the opposite edge of intrinsic Tri TID (but it isn't an edge of the intrinsic tri).
			const FIndex3i TriEIDs  = IntrinsicMesh.GetTriEdges(FinderInfo.TID);
			const int32 OppEID      = TriEIDs[(FinderInfo.IdxOf + 1) % 3];
			const int32 NormalCoord = NCoords.NormalCoord[FinderInfo.EID];
			const int32 CrossingIdx = [&]
				{
					if (NormalCoord == 0)
					{
						return OrderOfTraceEID - FinderInfo.FirstRoundabout;
					}
					else
					{
						return OrderOfTraceEID - FinderInfo.FirstRoundabout + 1 + NormalCoord;
					}
				}();

			checkSlow(CrossingIdx > 0); // would be zero if the edge was also a surface edge, but that case is handled above

			ContinueTraceSurfaceEdge(IntrinsicMesh, FinderInfo.TID, OppEID, CrossingIdx, Crossings);
		}

		return MoveTemp(Crossings);
	}



	TArray<FNormalCoordinates::FEdgeAndCrossingIdx> TraceSurfaceEdge( const FIntrinsicEdgeFlipMesh& IntrinsicMesh,
																	  const int32 SurfaceEID, const bool bReverse )
	{
		const FNormalCoordinates& NCoords = IntrinsicMesh.GetNormalCoordinates();
		const FDynamicMesh3& SurfaceMesh  = *NCoords.SurfaceMesh;
		if (!SurfaceMesh.IsEdge(SurfaceEID))
		{
			TArray< FNormalCoordinates::FEdgeAndCrossingIdx > Crossings;
			return MoveTemp(Crossings);
		}

		const FIndex2i EdgeV = SurfaceMesh.GetEdgeV(SurfaceEID);
		const int32 StartVID = (bReverse) ? EdgeV.B : EdgeV.A;

		// Special case of tracing a surface edge on the mesh boundary.  This will be the same as an intrinsic edge.
		if (SurfaceMesh.IsBoundaryEdge(SurfaceEID))
		{
			// boundary edges don't flip.  Will be the same as the intrinsic edge.

			const int32 IntrinsicEID      = SurfaceEID;
			const int32 IntrinsicTID      = IntrinsicMesh.GetEdgeT(IntrinsicEID).A;
			const int32 IndexOf           = IntrinsicMesh.GetTriEdges(IntrinsicTID).IndexOf(IntrinsicEID);
			const bool bRerverseIntrinsic = !(IntrinsicMesh.GetTriangle(IntrinsicTID)[IndexOf] == StartVID);


			TArray< FNormalCoordinates::FEdgeAndCrossingIdx > Crossings;
			Crossings.SetNumUninitialized(2);
			auto& XingStart = Crossings[0];
			XingStart.TID   = IntrinsicTID;
			XingStart.EID   = (bRerverseIntrinsic) ? (IndexOf + 1) % 3 : IndexOf;
			XingStart.CIdx  = 0;

			auto& XingEnd = Crossings[1];
			XingEnd.TID   = IntrinsicTID;
			XingEnd.EID   = (bRerverseIntrinsic) ? IndexOf : (IndexOf + 1) % 3;
			XingEnd.CIdx  = 0;

			return MoveTemp(Crossings);
		}
		else  // the edge isn't a surface edge:  encode its direction by identifying it as an edge of a triangle and do the trace
		{

			const FIndex2i EdgeT = SurfaceMesh.GetEdgeT(SurfaceEID);
			int32 TID            = EdgeT.A;
			int32 IndexOf        = SurfaceMesh.GetTriEdges(TID).IndexOf(SurfaceEID);
			if (SurfaceMesh.GetTriangle(TID)[IndexOf] != StartVID)
			{
				TID = EdgeT.B;
				IndexOf = SurfaceMesh.GetTriEdges(TID).IndexOf(SurfaceEID);
				checkSlow(SurfaceMesh.GetTriangle(TID)[IndexOf] == StartVID);
			}
			return TraceSurfaceEdge(IntrinsicMesh, TID, IndexOf);
		}
	}


	/**
	* Utility to trace an edge defined (by TraceEID) on the TraceMesh across the HostMesh, given a list of host mesh edges intersected by the trace mesh.
	* note this assumes that the TraceMesh and HostMesh share the same vertex set
	*/
	template<typename HostMeshType, typename TraceMeshType>
	TArray<FIntrinsicEdgeFlipMesh::FSurfacePoint> TraceEdgeOverHost( const int32 TraceEID, const TArray<int32>& HostXings, const HostMeshType& HostMesh,
		                                                             const TraceMeshType& TraceMesh, double CoalesceThreshold, bool bReverse )
	{
		using FSurfacePoint = FIntrinsicEdgeFlipMesh::FSurfacePoint;
		TArray<FSurfacePoint> EdgeTrace;

		if (!TraceMesh.IsEdge(TraceEID))
		{
			// empty array..
			return MoveTemp(EdgeTrace);
		}

		// utility to keep from adding duplicate VIDs.  Duplicate VIDs could result when Coalescing..
		auto AddVIDToTrace = [&EdgeTrace](const int32 VID)
		{
			if (EdgeTrace.Num() == 0)
			{
				EdgeTrace.Emplace(VID);
			}
			else
			{
				const FSurfacePoint& LastPoint = EdgeTrace.Last();
				const bool bSameAsLast         = ( LastPoint.PositionType == FSurfacePoint::EPositionType::Vertex 
				                                && LastPoint.Position.VertexPosition.VID == VID );
				if (!bSameAsLast)
				{
					EdgeTrace.Emplace(VID);
				}
			}
		};


		// an edge  w/o crossings is the same as the host mesh edge. the trace in this case requires only the start and edge vertex 
		if (HostXings.Num() == 0)
		{
			const int32 AdjTID     = TraceMesh.GetEdgeT(TraceEID).A;
			const int32 IndexOf    = TraceMesh.GetTriEdges(AdjTID).IndexOf(TraceEID);
			const FIndex3i TriVIDs = TraceMesh.GetTriangle(AdjTID);

			const int32 StartVID = TriVIDs[IndexOf];
			const int32 EndVID   = TriVIDs[(IndexOf + 1) % 3];


			EdgeTrace.Emplace(StartVID);
			EdgeTrace.Emplace(EndVID);
		}
		else
		{
			// knowing the host mesh edges this trace edge crosses, we make a 2d triangle strip by unfolding the 3d triangles the edge crosses
			// and solve a 2x2 problem for each 2d edge crossed.

			// struct holds bare-bones 2d triangle strip and ability to map vertexIDs from the src mesh
			struct
			{
				TMap<int32, int32> ToStripIndexMap;    // maps from mesh VID to triangle strip VID.
				TArray<FVector2d> StripVertexBuffer;
			} TriangleStrip;
			TriangleStrip.StripVertexBuffer.Reserve(3 + HostXings.Num());


			// Functor to add a triangle to the triangle strip. This assumes that at least one shared edge of this triangle already exits in the strip
			auto AddTriangleToStrip = [&HostMesh, &TriangleStrip](const int32 HostTID)
			{

				FIndex3i TriVIDs = HostMesh.GetTriangle(HostTID);

				// two of the tri vertices are already in the buffer.  Identify the new one.
				int32 IndexOf = -1;
				for (int32 i = 0; i < 3; ++i)
				{
					const int32 VID       = TriVIDs[i];
					const int32* StripVID = TriangleStrip.ToStripIndexMap.Find(VID);
					if (!StripVID)
					{
						IndexOf = i;
						break;
					}
				}

				// no need to do anything if all vertices had previously been added, 
				if (IndexOf == -1)
				{
					return;
				}


				const FVector3d Verts[3] = { HostMesh.GetVertex(TriVIDs[0]),  HostMesh.GetVertex(TriVIDs[1]),  HostMesh.GetVertex(TriVIDs[2]) };
				// with new vert last (i.e. V2).
				const FIndex3i Reordered((IndexOf + 1) % 3, (IndexOf + 2) % 3, IndexOf);
				// the spanning vectors 
				const FVector3d E1 = Verts[Reordered[1]] - Verts[Reordered[0]];
				const FVector3d E2 = Verts[Reordered[2]] - Verts[Reordered[0]];

				// coordinates of V2 relative to the direction of E1, and its orthogonal complement.
				const double E1DotE2       = E2.Dot(E1);
				const double E1LengthSqr   = FMath::Max(E1.SizeSquared(), TMathUtilConstants<double>::ZeroTolerance);
				const double E1Length      = FMath::Sqrt(E1LengthSqr);
				const double E1Dist        = E2.Dot(E1) / E1Length;
				const double OrthE1DistSqr = FMath::Max(0., E2.SizeSquared() - E1DotE2 * E1DotE2 / E1LengthSqr);
				const double OrthE1Dist    = FMath::Sqrt(OrthE1DistSqr);

				// 2d version of E1
				const int32* StripTriV0 = TriangleStrip.ToStripIndexMap.Find(TriVIDs[Reordered[0]]);
				const int32* StripTriV1 = TriangleStrip.ToStripIndexMap.Find(TriVIDs[Reordered[1]]);
				checkSlow(StripTriV0); checkSlow(StripTriV1);

				const FVector2d& StripTriVert0 = TriangleStrip.StripVertexBuffer[*StripTriV0];
				const FVector2d& StripTriVert1 = TriangleStrip.StripVertexBuffer[*StripTriV1];
				const FVector2d StripE1        = (StripTriVert1 - StripTriVert0);

				const FVector2d StripE1Perp(-StripE1[1], StripE1[0]); // Rotate StripE1 90 CCW.

																	  // new vertex 2d position
				const FVector2d StripTriVert2 = StripTriVert0 + (StripE1)*E1DotE2 / E1LengthSqr + (StripE1Perp / E1Length) * OrthE1Dist;
				TriangleStrip.StripVertexBuffer.Add(StripTriVert2);
				const int32 StripVID          = TriangleStrip.StripVertexBuffer.Num() - 1;
				const int32 SurfaceVID        = TriVIDs[IndexOf];
				TriangleStrip.ToStripIndexMap.Add(SurfaceVID, StripVID);
			};

			// find the VID where we start the edge trace and add it to the trace.
			const int32 StartVID = [&]
				{
					// by convention the intrinsic edge direction will be defined by the first adj triangle
					const int32 AdjTID     = TraceMesh.GetEdgeT(TraceEID).A;
					const int32 IndexOf    = TraceMesh.GetTriEdges(AdjTID).IndexOf(TraceEID);
					const FIndex3i TriVIDs = TraceMesh.GetTriangle(AdjTID);
					return TriVIDs[IndexOf];
				}();
			EdgeTrace.Emplace(StartVID);

			// find host triangle that contains both the StartVID and the first host edge we cross.
			int32 HostTID = [&]
				{
					const int32 HostEID      = HostXings[0];
					const FIndex2i HostEdgeT = HostMesh.GetEdgeT(HostEID);
					const FIndex3i TriAVIDs  = HostMesh.GetTriangle(HostEdgeT.A);
					const FIndex3i TriBVIDs  = HostMesh.GetTriangle(HostEdgeT.B);

					return (TriAVIDs.IndexOf(StartVID) != -1) ? HostEdgeT.A : HostEdgeT.B;
				}();

			// jump-start the process of making the triangle strip by adding the first 2 verts.
			{
				const FIndex3i TriVIDs = HostMesh.GetTriangle(HostTID);
				const int32 IndexOf    = TriVIDs.IndexOf(StartVID);
				checkSlow(IndexOf != -1);
				const int32 NextVID    = TriVIDs[(IndexOf + 1) % 3];

				const FVector3d Vert0 = HostMesh.GetVertex(StartVID);
				const FVector3d Vert1 = HostMesh.GetVertex(NextVID);

				const double LSqr = FMath::Max((Vert0 - Vert1).SizeSquared(), TMathUtilConstants<double>::ZeroTolerance);
				const double L    = FMath::Sqrt(LSqr);

				const FVector2d StripVert0(0., 0.);
				const FVector2d StripVert1(L, 0.);

				TriangleStrip.StripVertexBuffer.Add(StripVert0);
				TriangleStrip.ToStripIndexMap.Add(StartVID, 0);

				TriangleStrip.StripVertexBuffer.Add(StripVert1);
				TriangleStrip.ToStripIndexMap.Add(NextVID, 1);
			}

			// unfold the triangle strip, add the first triangle and then all subsequent ones.
			AddTriangleToStrip(HostTID);
			for (int32 HostEID : HostXings)
			{
				const FIndex2i EdgeT = HostMesh.GetEdgeT(HostEID);
				HostTID              = (EdgeT.A == HostTID) ? EdgeT.B : EdgeT.A;
				AddTriangleToStrip(HostTID);
			}

			// get the 2d location of the end of the trace edge (i.e. at EndVID ). Note: the start location is (0,0) in 2d 
			const FVector2d StripEndVertex = [&]
				{
					const FIndex2i TraceEdgeV = TraceMesh.GetEdgeV(TraceEID);
					const int32 EndVID        = (TraceEdgeV.A == StartVID) ? TraceEdgeV.B : TraceEdgeV.A;
					const int32* StripEndVID  = TriangleStrip.ToStripIndexMap.Find(EndVID);
					checkSlow(StripEndVID);
					return TriangleStrip.StripVertexBuffer[*StripEndVID];
				}();

			// loop over the two-d versions of the host edges the path crosses and find the intersection.
			for (int32 HostEID : HostXings)
			{
				const FIndex2i EdgeV = HostMesh.GetEdgeV(HostEID);
				const int32* StripA  = TriangleStrip.ToStripIndexMap.Find(EdgeV.A);
				const int32* StripB  = TriangleStrip.ToStripIndexMap.Find(EdgeV.B);
				checkSlow(StripA); checkSlow(StripB);
				const FIndex2i StripEdgeV(*StripA, *StripB);

				const FVector2d StripVertA = TriangleStrip.StripVertexBuffer[StripEdgeV.A];
				const FVector2d StripVertB = TriangleStrip.StripVertexBuffer[StripEdgeV.B];

				// solve Alpha*StripVertA + (1-Alpha)StripVertB = Gamma Start + (1-Gamma)End.
				// i.e. 
				// Alpha * (StripVertA - StripVertB) + Gamma * (End - Start) =  End - StripVertB.

				// write this as 2x2 matrix problem M.x = b solving for unknown vector x=(alpha, gamma).

				// matrix
				double m[2][2];
				m[0][0] = (StripVertA - StripVertB)[0];  m[0][1] = StripEndVertex[0];
				m[1][0] = (StripVertA - StripVertB)[1];  m[1][1] = StripEndVertex[1];

				// b-vector
				const FVector2d b = StripEndVertex - StripVertB;

				const double Det = m[0][0] * m[1][1] - m[0][1] * m[1][0];
				// inverse: not yet scaled by 1/det
				double invm[2][2];
				invm[0][0] =  m[1][1];   invm[0][1] = -m[0][1];
				invm[1][0] = -m[1][0];   invm[1][1] =  m[0][0];

				// solve for alpha only ( don't care about gamma ) 
				double alpha = invm[0][0] * b[0] + invm[0][1] * b[1];

				if (FMath::Abs(Det) < TMathUtilConstants<double>::ZeroTolerance)
				{
					// this surface edge and the intrinsic edge that intersects it are nearly parallel.  
					// Assume the crossing happens at the farther vertex.
					const double dA = StripVertA.SquaredLength();
					const double dB = StripVertB.SquaredLength();

					alpha = (dA > dB) ? 1. : 0.;
				}
				else
				{
					alpha = FMath::Clamp(alpha / Det, 0., 1.);
				}

				// may want to convert this edge crossing to a vertex crossing, if it is close enough.
				int32 CoalesceVID = -1;
				bool bSnapToVert = false;

				if (alpha < CoalesceThreshold)
				{
					CoalesceVID = EdgeV.B;
					bSnapToVert = true;
				}
				else if ((1. - alpha) < CoalesceThreshold)
				{
					CoalesceVID = EdgeV.A;
					bSnapToVert = true;
				}

				if (bSnapToVert)
				{
					AddVIDToTrace(CoalesceVID);
				}
				else
				{
					EdgeTrace.Emplace(HostEID, alpha);
				}
			}

			// Add the end vertex to the EdgeTrace
			{
				const FIndex2i TraceEdgeV = TraceMesh.GetEdgeV(TraceEID);
				const int32 EndVID        = (TraceEdgeV.A == StartVID) ? TraceEdgeV.B : TraceEdgeV.A;
				AddVIDToTrace(EndVID);
			}

		}

		// correct trace results order so it is either EdgeV order or reversed as requested
		const bool bHasEdgeVOrder = (TraceMesh.GetEdgeV(TraceEID).A == EdgeTrace[0].Position.VertexPosition.VID);
		const bool bNeedToReverse = (bHasEdgeVOrder && bReverse) || (!bHasEdgeVOrder && !bReverse);

		if (bNeedToReverse)
		{
			Algo::Reverse(EdgeTrace);
		}

		return MoveTemp(EdgeTrace);
	}
};

int32 UE::Geometry::FlipToDelaunay(FIntrinsicTriangulation& IntrinsicMesh, TSet<int32>& Uncorrected, const int32 MaxFlipCount)
{
	return FlipToDelaunayImpl(IntrinsicMesh, Uncorrected, MaxFlipCount);
}

int32 UE::Geometry::FlipToDelaunay(FSimpleIntrinsicEdgeFlipMesh& IntrinsicMesh, TSet<int32>& Uncorrected, const int32 MaxFlipCount)
{
	return FlipToDelaunayImpl(IntrinsicMesh, Uncorrected, MaxFlipCount);
}

int32 UE::Geometry::FlipToDelaunay(FIntrinsicEdgeFlipMesh& IntrinsicMesh, TSet<int32>& Uncorrected, const int32 MaxFlipCount)
{
	return FlipToDelaunayImpl(IntrinsicMesh, Uncorrected, MaxFlipCount);
}



/**------------------------------------------------------------------------------
* FSimpleIntrinsicEdgeFlipMesh Methods
*------------------------------------------------------------------------------ */

FSimpleIntrinsicEdgeFlipMesh::FSimpleIntrinsicEdgeFlipMesh(const FDynamicMesh3& SrcMesh) :
	Vertices(SrcMesh.GetVerticesBuffer()),
	VertexRefCounts{ SrcMesh.GetVerticesRefCounts() },
	VertexEdgeLists{ SrcMesh.GetVertexEdges() },

	Triangles{ SrcMesh.GetTrianglesBuffer() },
	TriangleRefCounts{ SrcMesh.GetTrianglesRefCounts() },
	TriangleEdges{ SrcMesh.GetTriangleEdges() },

	Edges{ SrcMesh.GetEdgesBuffer() },
	EdgeRefCounts{ SrcMesh.GetEdgesRefCounts() }
{
	
	const int32 MaxEID = MaxEdgeID();
	EdgeLengths.SetNum(MaxEID);
	for (int32 EID = 0; EID < MaxEID; ++EID)
	{
		if (!IsEdge(EID))
		{
			continue;
		}
		const FIndex2i EdgeV = GetEdgeV(EID);
		const FVector3d Pos[2] = { GetVertex(EdgeV.A), GetVertex(EdgeV.B) };
		EdgeLengths[EID] = (Pos[1] - Pos[0]).Length();
	}

	const int32 MaxTriID = MaxTriangleID();
	InternalAngles.SetNum(MaxTriID);
	for (int32 TID = 0; TID < MaxTriID; ++TID)
	{
		if (!IsTriangle(TID))
		{
			continue;
		}
		// angles at v0, v1, v2, in that order
		InternalAngles[TID] = ComputeTriInternalAnglesR(TID);
	}
}


FIndex2i FSimpleIntrinsicEdgeFlipMesh::GetEdgeOpposingV(int32 EID) const
{
	const FEdge& Edge = Edges[EID];
	FIndex2i Result(InvalidID, InvalidID);

	for (int32 i = 0; i < 2; ++i)
	{
		int32 TriID = Edge.Tri[i];
		if (TriID == InvalidID) continue;

		const FIndex3i TriEIDs = GetTriEdges(TriID);
		const int32 IndexOf    = TriEIDs.IndexOf(EID);
		const FIndex3i TriVIDs = GetTriangle(TriID);
		Result[i] = TriVIDs[AddTwoModThree[IndexOf]];
	}

	return Result;
}


FIndex2i FSimpleIntrinsicEdgeFlipMesh::GetOrientedEdgeV(int32 EID, int32 TID) const
{
	int32 IndexOf = GetTriEdges(TID).IndexOf(EID);
	checkSlow(IndexOf != InvalidID);
	FIndex3i TriVIDs = GetTriangle(TID);
	return FIndex2i(TriVIDs[IndexOf], TriVIDs[AddOneModThree[IndexOf]]);
}


int32 FSimpleIntrinsicEdgeFlipMesh::ReplaceEdgeTriangle(int32 eID, int32 tOld, int32 tNew)
{
	FIndex2i& Tris = Edges[eID].Tri;
	int32 a = Tris[0], b = Tris[1];
	if (a == tOld) {
		if (tNew == InvalidID)
		{
			Tris[0] = b;
			Tris[1] = InvalidID;
		}
		else
		{
			Tris[0] = tNew;
		}
		return 0;
	}
	else if (b == tOld)
	{
		Tris[1] = tNew;
		return 1;
	}
	else
	{
		return -1;
	}
}


EMeshResult FSimpleIntrinsicEdgeFlipMesh::FlipEdgeTopology(int32 eab, FEdgeFlipInfo& FlipInfo)
{
	if (!IsEdge(eab))
	{
		return EMeshResult::Failed_NotAnEdge;
	}
	if (IsBoundaryEdge(eab))
	{
		return EMeshResult::Failed_IsBoundaryEdge;
	}

	// find oriented edge [a,b], tris t0,t1, and other verts c in t0, d in t1
	const FEdge Edge = Edges[eab];
	int32 t0 = Edge.Tri[0], t1 = Edge.Tri[1];

	FIndex2i oppV = GetEdgeOpposingV(eab);
	FIndex2i orientedV = GetOrientedEdgeV(eab, t0);
	int32 a = orientedV.A, b = orientedV.B;

	if (oppV[0] == InvalidID || oppV[1] == InvalidID)
	{
		return EMeshResult::Failed_BrokenTopology;
	}
	int32 c = oppV[0];
	int32 d = oppV[1];

	const FIndex3i T0te = GetTriEdges(t0);
	const FIndex3i T1te = GetTriEdges(t1);

	const int32 T0IndexOf = T0te.IndexOf(eab);
	const int32 T1IndexOf = T1te.IndexOf(eab);
	// find edges bc, ca, ad, db
	const int32 ebc = T0te[AddOneModThree[T0IndexOf]];
	const int32 eca = T0te[AddTwoModThree[T0IndexOf]];

	const int32 ead = T1te[AddOneModThree[T1IndexOf]];
	const int32 edb = T1te[AddTwoModThree[T1IndexOf]];

	// update triangles
	Triangles[t0] = FIndex3i(c, d, b);
	Triangles[t1] = FIndex3i(d, c, a);

	// update edge AB, which becomes flipped edge CD
	SetEdgeVerticesInternal(eab, c, d);
	SetEdgeTrianglesInternal(eab, t0, t1);
	const int32 ecd = eab;

	// update the two other edges whose triangle nbrs have changed
	if (ReplaceEdgeTriangle(eca, t0, t1) == -1)
	{
		checkfSlow(false, TEXT("FSimpleIntrinsicEdgeFlipMesh.FlipEdge: first ReplaceEdgeTriangle failed"));
		return EMeshResult::Failed_UnrecoverableError;
	}
	if (ReplaceEdgeTriangle(edb, t1, t0) == -1)
	{
		checkfSlow(false, TEXT("FSimpleIntrinsicEdgeFlipMesh.FlipEdge: second ReplaceEdgeTriangle failed"));
		return EMeshResult::Failed_UnrecoverableError;
	}

	// update triangle nbr lists (these are edges)
	TriangleEdges[t0] = FIndex3i(ecd, edb, ebc);
	TriangleEdges[t1] = FIndex3i(ecd, eca, ead);

	// remove old eab from verts a and b, and Decrement ref counts
	if (VertexEdgeLists.Remove(a, eab) == false)
	{
		checkfSlow(false, TEXT("FSimpleIntrinsicEdgeFlipMesh.FlipEdge: first edge list remove failed"));
		return EMeshResult::Failed_UnrecoverableError;
	}
	VertexRefCounts.Decrement(a);
	if (a != b)
	{
		if (VertexEdgeLists.Remove(b, eab) == false)
		{
			checkfSlow(false, TEXT("FSimpleIntrinsicEdgeFlipMesh.FlipEdge: second edge list remove failed"));
			return EMeshResult::Failed_UnrecoverableError;
		}
		VertexRefCounts.Decrement(b);
	}
	if (IsVertex(a) == false || IsVertex(b) == false)
	{
		checkfSlow(false, TEXT("FSimpleIntrinsicEdgeFlipMesh.FlipEdge: either a or b is not a vertex?"));
		return EMeshResult::Failed_UnrecoverableError;
	}

	// add edge ecd to verts c and d, and increment ref counts
	VertexEdgeLists.Insert(c, ecd);
	VertexRefCounts.Increment(c);
	if (c != d)
	{
		VertexEdgeLists.Insert(d, ecd);
		VertexRefCounts.Increment(d);
	}

	// success! collect up results
	FlipInfo.EdgeID = eab;
	FlipInfo.OriginalVerts = FIndex2i(a, b);
	FlipInfo.OpposingVerts = FIndex2i(c, d);
	FlipInfo.Triangles = FIndex2i(t0, t1);
	return EMeshResult::Ok;
}


FVector3d FSimpleIntrinsicEdgeFlipMesh::ComputeTriInternalAnglesR(const int32 TID) const
{
	const FVector3d Lengths = GetTriEdgeLengths(TID);
	FVector3d Angles;
	for (int32 v = 0; v < 3; ++v)
	{
		Angles[v] = InteriorAngle(Lengths[v], Lengths[AddOneModThree[v]], Lengths[AddTwoModThree[v]]);
	}

	return Angles;
}


double FSimpleIntrinsicEdgeFlipMesh::GetOpposingVerticesDistance(int32 EID) const
{
	const double OrgLength  = GetEdgeLength(EID);
	const FIndex2i EdgeTris = GetEdgeT(EID);
	checkSlow(EdgeTris[1] != FDynamicMesh3::InvalidID);

	// compute 2d locations of the verts opposite the EID edge.
	FVector2d Opp2dVerts[2];
	for (int32 i = 0; i < 2; ++i)
	{
		const FIndex3i TriEIDs = GetTriEdges(EdgeTris[i]);
		const FVector3d Ls = GetEdgeLengthTriple(TriEIDs);
		const int32 IndexOf = TriEIDs.IndexOf(EID);
		const FVector3d PermutedLs = Permute(IndexOf, Ls);
		Opp2dVerts[i] = ComputeOpposingVert2d(PermutedLs[0], PermutedLs[1], PermutedLs[2]);
	}
	// rotate second tri so the shared edge aligns
	Opp2dVerts[1].Y = -Opp2dVerts[1].Y;
	Opp2dVerts[1].X = OrgLength - Opp2dVerts[1].X;

	return (Opp2dVerts[0] - Opp2dVerts[1]).Length();
}


EMeshResult FSimpleIntrinsicEdgeFlipMesh::FlipEdge(int32 EID, FEdgeFlipInfo& EdgeFlipInfo)
{
	if (!IsEdge(EID))
	{
		return EMeshResult::Failed_NotAnEdge;
	}

	if (IsBoundaryEdge(EID))
	{
		return EMeshResult::Failed_IsBoundaryEdge;
	}

	// prohibit case where the edge is shared by a non-convex pair of triangles.
	{
		// original triangles
		FDynamicMesh3::FEdge OrgEdge = GetEdge(EID);

		// Assumes both triangles have same orientation
		double TotalAngleAtOrgVert[2] = { 0., 0. };
		for (int32 i = 0; i < 2; ++i)
		{	
			int32 TriID = OrgEdge.Tri[i];
			int32 IndexOf = GetTriEdges(TriID).IndexOf(EID);
			TotalAngleAtOrgVert[i] += InternalAngles[TriID][IndexOf];
			TotalAngleAtOrgVert[(i+1) % 2] += InternalAngles[TriID][AddOneModThree[IndexOf]];
		}

		if (TotalAngleAtOrgVert[0] > TMathUtilConstants<double>::Pi - TMathUtilConstants<double>::ZeroTolerance || TotalAngleAtOrgVert[1] > TMathUtilConstants<double>::Pi - TMathUtilConstants<double>::ZeroTolerance)
		{
			return EMeshResult::Failed_Unsupported;
		}
	}

	// compute the length of edge after flip
	const double PostFlipLength = GetOpposingVerticesDistance(EID);

	// flip edge in the underlying mesh
	const EMeshResult MeshFlipResult = FlipEdgeTopology(EID, EdgeFlipInfo);

	if (MeshFlipResult != EMeshResult::Ok)
	{
		// could fail for many reasons, e.g. a boundary edge or the new edge already exists.
		return MeshFlipResult;
	}

	// update the intrinsic edge length
	EdgeLengths[EID] = PostFlipLength;

	// update interior angles for the tris
	for (int32 t = 0; t < 2; ++t)
	{
		const int32 TID = EdgeFlipInfo.Triangles[t];
		InternalAngles[TID] = ComputeTriInternalAnglesR(TID);	
	}

	return MeshFlipResult;
}


FVector2d FSimpleIntrinsicEdgeFlipMesh::EdgeOpposingAngles(int32 EID) const
{
	FVector2d Result;
	FDynamicMesh3::FEdge Edge = GetEdge(EID);
	{
		const int32 IndexOf    = GetTriEdges(Edge.Tri.A).IndexOf(EID);
		const int32 IndexOfOpp = AddTwoModThree[IndexOf];
		const double Angle     = GetTriInternalAngleR(Edge.Tri.A, IndexOfOpp);
		Result[0] = Angle;
	}
	if (Edge.Tri.B != FDynamicMesh3::InvalidID)
	{
		const int32 IndexOf    = GetTriEdges(Edge.Tri.B).IndexOf(EID);
		const int32 IndexOfOpp = AddTwoModThree[IndexOf];
		const double Angle     = GetTriInternalAngleR(Edge.Tri.B, IndexOfOpp);
		Result[1] = Angle;
	}
	else
	{
		Result[1] = -TMathUtilConstants<double>::MaxReal;
	}

	return Result;
}


double FSimpleIntrinsicEdgeFlipMesh::EdgeCotanWeight(int32 EID) const
{
	auto ComputeCotanOppAngle = [this](int32 EID, int32 TID)->double
	{
		const FIndex3i TriEIDs = GetTriEdges(TID);
		const int32 IOf        = TriEIDs.IndexOf(EID);
		const FVector3d TriLs  = GetEdgeLengthTriple(TriEIDs);

		const FVector3d Ls(TriLs[IOf], TriLs[AddOneModThree[IOf]], TriLs[AddTwoModThree[IOf]]);
		return ComputeCotangent(Ls[0], Ls[1], Ls[2]);
	};

	FDynamicMesh3::FEdge Edge = GetEdge(EID);
	double Result = ComputeCotanOppAngle(EID, Edge.Tri.A);
	if (Edge.Tri.B != FDynamicMesh3::InvalidID)
	{
		Result += ComputeCotanOppAngle(EID, Edge.Tri.B);
		Result /= 2;
	}

	return Result;
}


/**------------------------------------------------------------------------------
*  FIntrinsicTriangulation Methods
*-------------------------------------------------------------------------------*/


UE::Geometry::FIntrinsicTriangulation::FIntrinsicTriangulation(const FDynamicMesh3& SrcMesh)
	: MyBase(SrcMesh)
	, SignpostData(SrcMesh)
{
}

TArray<FIntrinsicTriangulation::FSurfacePoint> UE::Geometry::FIntrinsicTriangulation::TraceEdge(int32 EID, double CoalesceThreshold, bool bReverse) const
{
	TArray<FIntrinsicTriangulation::FSurfacePoint> SurfacePoints;
	if (!IsEdge(EID))
	{
		return SurfacePoints;
	}


	const FDynamicMesh3* SurfaceMesh = GetExtrinsicMesh();
	const double IntrinsicEdgeLength = GetEdgeLength(EID);
	const FIndex2i EdgeV = GetEdgeV(EID);
	int32 StartVID = EdgeV.A;
	int32 EndVID = EdgeV.B;
	if (bReverse)
	{
		StartVID = EdgeV.B;
		EndVID = EdgeV.A;
	}


	FIndex2i AdjTIDs = GetEdgeT(EID);

	// look-up the polar angle of this edge as it leaves StartVID ( this angle is relative to a specified reference mesh edge )
	const double PolarAngle = 
		[&]{
			const int32 AdjTID = AdjTIDs.A;
			const int32 IndexOf = GetTriEdges(AdjTID).IndexOf(EID);
			checkSlow(IndexOf > -1);
			if (GetTriangle(AdjTID)[IndexOf] == StartVID)
			{
				// IntrinsicEdgeAngles hold the local angle of each out-going edge relative to the vertex the edge exits
				return SignpostData.IntrinsicEdgeAngles[AdjTID][IndexOf];
			}
			else
			{
				const double ToRadians = SignpostData.GeometricVertexInfo[StartVID].ToRadians;
				// polar angle of prev edge just before (clockwise) the edge we want
				const double PrevPolarAngle = SignpostData.IntrinsicEdgeAngles[AdjTID][(IndexOf + 1) % 3];
				// angle between previous edge and this one
				const double InternalAngle = InternalAngles[AdjTID][(IndexOf + 1) % 3];
				// add the internal angle to rotate from Prev Edge to this edge
				return ToRadians * InternalAngle + PrevPolarAngle;
			}
		}();

	// Trace the surface mesh from the intrinsic StartVID, in the PolarAngle direction, a distance of IntrinsicEdgeLength.
	// 
	// Note: this intrinsic vertex may or may not correspond to a vertex in the surface mesh if vertices were added to the intrinsic mesh
	// by doing an edge split or a triangle poke.
	FMeshGeodesicSurfaceTracer SurfaceTracer =  SignpostSufaceTraceUtil::TraceFromIntrinsicVert(SignpostData, StartVID, PolarAngle, IntrinsicEdgeLength);

	// util to convert the trace result to a surface point, potentially snapping to a vertex if within the coalesce threshold 
	auto TraceResultToSurfacePoint = [CoalesceThreshold, SurfaceMesh](const FMeshGeodesicSurfaceTracer::FTraceResult& TraceResult)->FSurfacePoint
	{

	
		if (TraceResult.bIsEdgePoint)
		{
			// TraceResult alpha is defined as EdgeV.A * (1-Alpha) + EdgeV.B Alpha;  This is the complement of what we want
			const double Alpha = FMath::Clamp((1. - TraceResult.EdgeAlpha), 0., 1.);
			const int32 EID = TraceResult.EdgeID;

			if (Alpha <= 0.5 && Alpha < CoalesceThreshold)
			{
				return FSurfacePoint(SurfaceMesh->GetEdgeV(EID).B);
			}
			else if (Alpha > 0.5 && (1. - Alpha) < CoalesceThreshold)
			{
				return FSurfacePoint(SurfaceMesh->GetEdgeV(EID).A);
			}

			return FSurfacePoint(EID, Alpha);
		}
		else
		{
			// TODO - should snap to vertex / edge if close?
			const int32 TID = TraceResult.TriID;
			const FVector3d BC = TraceResult.Barycentric;
			return FSurfacePoint(TID, BC);
		}
	};

	// Add surface point to the outgoing array, but don't allow for duplicate vertex points
	auto AddSurfacePoint = [&SurfacePoints](const FSurfacePoint& PointA)
	{
		if (SurfacePoints.Num() == 0)
		{
			SurfacePoints.Add(PointA);
		}
		else
		{
			const FSurfacePoint& PointB = SurfacePoints.Last();
			const bool bAreSameVertexPoint = ( PointA.PositionType == FSurfacePoint::EPositionType::Vertex &&
				                               PointB.PositionType == FSurfacePoint::EPositionType::Vertex &&
				                               PointA.Position.VertexPosition.VID == PointB.Position.VertexPosition.VID );
			if (!bAreSameVertexPoint)
			{
				SurfacePoints.Add(PointA);
			}
		}
	};

	// package the surface trace results as series of surface points.  Note, because this is a trace along an intrinsic edge
	// the first and last result will be an intrinsic vertex (StartVID and EndVID)
	TArray<FMeshGeodesicSurfaceTracer::FTraceResult>& TraceResults = SurfaceTracer.GetTraceResults();
	{
		int32 NumTraceResults = TraceResults.Num();
		AddSurfacePoint(GetVertexSurfacePoint(StartVID));
		for (int32 i = 1; i < NumTraceResults - 1; ++i)
		{
			FMeshGeodesicSurfaceTracer::FTraceResult& TraceResult = TraceResults[i];
			FSurfacePoint SurfacePoint = TraceResultToSurfacePoint(TraceResult);
			AddSurfacePoint(SurfacePoint);
		}
		AddSurfacePoint(GetVertexSurfacePoint(EndVID));
	}


	return SurfacePoints;
}

EMeshResult UE::Geometry::FIntrinsicTriangulation::FlipEdge(int32 EID, FEdgeFlipInfo& EdgeFlipInfo)
{

	if (IsBoundaryEdge(EID))
	{
		return EMeshResult::Failed_IsBoundaryEdge;
	}
	// capture the state of the triangles before the flip.
	const FIndex2i Tris = GetEdgeT(EID);
	const FIndex2i PreFlipIndexOf(GetTriEdges(Tris[0]).IndexOf(EID), GetTriEdges(Tris[1]).IndexOf(EID));


	// flip edge in the underlying mesh
	// this updates the edge lengths and the interior angles.
	const EMeshResult MeshFlipResult = MyBase::FlipEdge(EID, EdgeFlipInfo);

	if (MeshFlipResult != EMeshResult::Ok)
	{
		// could fail for many reasons, e.g. a boundary edge 
		return MeshFlipResult;
	}

	// internal angles at the verts that are now connected by the flipped edge
	const double NewAngleAtC = InternalAngles[Tris[1]][1];
	const double NewAngleAtD = InternalAngles[Tris[0]][1];

	// update signpost
	SignpostData.OnFlipEdge(EID, Tris, EdgeFlipInfo.OpposingVerts, PreFlipIndexOf, NewAngleAtC, NewAngleAtD);

	return EMeshResult::Ok;
}


double UE::Geometry::FIntrinsicTriangulation::UpdateVertexByEdgeTrace(const int32 NewVID, const int32 TraceStartVID, const double TracePolarAngle, const double TraceDist)
{
	using FSurfaceTraceResult = FSignpost::FSurfaceTraceResult;

	FMeshGeodesicSurfaceTracer SurfaceTracer = SignpostSufaceTraceUtil::TraceFromIntrinsicVert(SignpostData, TraceStartVID, TracePolarAngle, TraceDist);

	TArray<FMeshGeodesicSurfaceTracer::FTraceResult>& TraceResultArray = SurfaceTracer.GetTraceResults();
	// need to convert the result of the trace into the correct form

	const FMeshGeodesicSurfaceTracer::FTraceResult& TraceResult = TraceResultArray.Last();
	const FSurfacePoint TraceResultPosition(TraceResult.TriID, TraceResult.Barycentric);
	// fix directions relative to local reference edge on the extrinsic mesh 
	// by finding direction of the TraceEID edge indecent on NewVID
	const double AngleOffset = [&]
	{
		FVector2d Dir = TraceResult.SurfaceDirection.Dir;

		// translate to Dir about the reference edge for this triangle
		const int32 EndRefEID = SignpostData.TIDToReferenceEID[TraceResult.TriID];

		const FMeshGeodesicSurfaceTracer::FTangentTri2& TangentTri2 = SurfaceTracer.GetLastTri();
		// convert to local basis for first edge of tri2
		if (TangentTri2.EdgeOrientationSign[0] == -1)
		{
			Dir = -Dir;
		}
		const int32 IndexOfEndRefEID = TangentTri2.PermutedTriEIDs.IndexOf(EndRefEID);
		FVector2d DirRelToRefEID = TangentTri2.ChangeBasis(Dir, IndexOfEndRefEID);
		if (TangentTri2.EdgeOrientationSign[IndexOfEndRefEID] == -1)
		{
			DirRelToRefEID = -DirRelToRefEID;
		}
		// angle of (new edge) path to NewVert relative to Ref edge
		const double AngleToNewVert = FMath::Atan2(DirRelToRefEID.Y, DirRelToRefEID.X);
		// reverse direction of path because we NewVert is the local origin for polar angles around new vert
		const double AngleFromNewVert = AngleToNewVert + TMathUtilConstants<double>::Pi;
		return AsZeroToTwoPi(AngleFromNewVert);
	}();

	// Trace result as position and angle.
	FSurfaceTraceResult SurfaceTraceResult = { TraceResultPosition, AngleOffset };

	// As R3
	bool bTmpIsValid;
	const FVector3d TraceResultPos = AsR3Position(SurfaceTraceResult.SurfacePoint, *SignpostData.SurfaceMesh, bTmpIsValid);

	// update the R3 for NewVID in the intrinsic mesh
	Vertices[NewVID] = TraceResultPos;

	// store the surface position for the intrinsic vertex in the signpost data
	// Currently we are storing every new intrinsic surface position as a point on a surface tri
	//  TODO: is there any advantage to classify as "Edge" position if very close to edge ?  
	SignpostData.IntrinsicVertexPositions.InsertAt(SurfaceTraceResult.SurfacePoint, NewVID);

	// return relative angle of trace
	return SurfaceTraceResult.Angle;
}


UE::Geometry::EMeshResult UE::Geometry::FIntrinsicTriangulation::PokeTriangleTopology(int32 TriangleID, FPokeTriangleInfo& PokeResult)
{
	PokeResult = FPokeTriangleInfo();

	if (!IsTriangle(TriangleID))
	{
		return EMeshResult::Failed_NotATriangle;
	}

	FIndex3i tv = GetTriangle(TriangleID);
	FIndex3i te = GetTriEdges(TriangleID);

	// create vertex with averaged position.. 
	FVector3d vPos = (1./3.)*(GetVertex(tv[0]) + GetVertex(tv[1]) + GetVertex(tv[2]));

	int32 newVertID = AppendVertex(vPos);


	// add in edges to center vtx, do not connect to triangles yet
	int32 eAN = AddEdgeInternal(tv[0], newVertID, -1, -1);
	int32 eBN = AddEdgeInternal(tv[1], newVertID, -1, -1);
	int32 eCN = AddEdgeInternal(tv[2], newVertID, -1, -1);
	VertexRefCounts.Increment(tv[0]);
	VertexRefCounts.Increment(tv[1]);
	VertexRefCounts.Increment(tv[2]);
	VertexRefCounts.Increment(newVertID, 3);

	// old triangle becomes tri along first edge
	Triangles[TriangleID] = FIndex3i(tv[0], tv[1], newVertID);
	TriangleEdges[TriangleID] = FIndex3i(te[0], eBN, eAN);

	// add two triangles
	int32 t1 = AddTriangleInternal(tv[1], tv[2], newVertID, te[1], eCN, eBN);
	int32 t2 = AddTriangleInternal(tv[2], tv[0], newVertID, te[2], eAN, eCN);

	// second and third edges of original tri have neighbors
	ReplaceEdgeTriangle(te[1], TriangleID, t1);
	ReplaceEdgeTriangle(te[2], TriangleID, t2);

	// set the triangles for the edges we created above
	SetEdgeTrianglesInternal(eAN, TriangleID, t2);
	SetEdgeTrianglesInternal(eBN, TriangleID, t1);
	SetEdgeTrianglesInternal(eCN, t1, t2);


	PokeResult.OriginalTriangle = TriangleID;
	PokeResult.TriVertices = tv;
	PokeResult.NewVertex = newVertID;
	PokeResult.NewTriangles = FIndex2i(t1, t2);
	PokeResult.NewEdges = FIndex3i(eAN, eBN, eCN);
	PokeResult.BaryCoords = FVector3d(1./3., 1./3., 1./3.);
	
	return EMeshResult::Ok;
}

UE::Geometry::EMeshResult UE::Geometry::FIntrinsicTriangulation::PokeTriangle(int32 TID, const FVector3d& BaryCoordinates, FPokeTriangleInfo& PokeInfo)
{
	if (!IsTriangle(TID))
	{
		return EMeshResult::Failed_NotATriangle;
	}

	// state before the poke
	const FIndex3i OriginalVIDs = GetTriangle(TID);
	const FIndex3i OriginalEdges = GetTriEdges(TID);
	const FVector3d OriginalTriEdgeLengths = GetTriEdgeLengths(TID);
	const FVector3d OriginalEdgeDirs = SignpostData.IntrinsicEdgeAngles[TID];
	
	// Add a new vertex and faces to the IntrinsicMesh.   
	// Note: the r3 position will be wrong initially since this poke will just interpolate the corners of the tri.
	//       we fix this position as the last step in this function
	EMeshResult PokeResult = PokeTriangleTopology(TID, PokeInfo);
	if (PokeResult != EMeshResult::Ok)
	{
		return PokeResult;
	}
	

	FIndex3i NewTris(TID, PokeInfo.NewTriangles[0], PokeInfo.NewTriangles[1]);
	const int32 NewVID = PokeInfo.NewVertex;

	// Need to update intrinsic information for the 3 triangles that resulted from the poke
	// 1) update the edge lengths for the triangles
	// 2) update the internal angles for the triangles
	// 3) update the edge directions for the triangles. 
	// 4) update the surface point for the new vertex by tracing one of the new edges. 
	//    also update the edge directions leaving the new vertex to be relative to the direction defined on the extrinsic mesh


	// (1) compute intrinsic edge lengths for the 3 new edges
	{
		FVector3d DistancesFromOldCorner;
		for (int32 v = 0; v < 3; ++v)
		{
			FVector3d BaryCoordVertex(0., 0., 0.); BaryCoordVertex[v] = 1.;
			const double Distance = DistanceBetweenBarycentricPoints( OriginalTriEdgeLengths[0],
							  										  OriginalTriEdgeLengths[1],
																	  OriginalTriEdgeLengths[2],
																	  BaryCoordVertex, BaryCoordinates);
			DistancesFromOldCorner[v] = Distance;
		}
	
		{
			// original tri is and verts (a, b,c) and edges (ab, bc, ca)
			// the updated tri has verts (a, b, new) and edges (ab, bnew, newa)
			FIndex3i T0EIDs = GetTriEdges(NewTris[0]);
			EdgeLengths.InsertAt(DistancesFromOldCorner[1], T0EIDs[1]);
			EdgeLengths.InsertAt(DistancesFromOldCorner[0], T0EIDs[2]);

			// new tri[0] has verts (b, c, new) and edges (bc, cnew, newb)
			FIndex3i T1EIDs = GetTriEdges(PokeInfo.NewTriangles[0]);
			EdgeLengths.InsertAt(DistancesFromOldCorner[2], T1EIDs[1]);
		}
	}
	// (2) update internal angles for the triangles.
	{
		InternalAngles.InsertAt(ComputeTriInternalAnglesR(NewTris[2]), NewTris[2]);
		InternalAngles.InsertAt(ComputeTriInternalAnglesR(NewTris[1]), NewTris[1]);
		InternalAngles[NewTris[0]] = ComputeTriInternalAnglesR(NewTris[0]);
	}
	
	// (3) update the edge directions 
	FVector3d TriEdgeDir[3]; // one for each tri in order (TID, NewTris[0], NewTris[1] )
	// edges around the boundary of the original tri
	TriEdgeDir[0][0] = OriginalEdgeDirs[0]; // AtoB dir
	TriEdgeDir[1][0] = OriginalEdgeDirs[1]; // BtoC dir
	TriEdgeDir[2][0] = OriginalEdgeDirs[2]; // CtoA dir
	
	// edges from the corners of the original tri towards the new vertex at the "center"
	const double ToRadiansAtA = SignpostData.GeometricVertexInfo[OriginalVIDs[0]].ToRadians;
	const double ToRadiansAtB = SignpostData.GeometricVertexInfo[OriginalVIDs[1]].ToRadians;
	const double ToRadiansAtC = SignpostData. GeometricVertexInfo[OriginalVIDs[2]].ToRadians;
	TriEdgeDir[0][1] = AsZeroToTwoPi( OriginalEdgeDirs[1] + ToRadiansAtB * InternalAngles[NewTris[1]][0]);  // BtoNew dir
	TriEdgeDir[1][1] = AsZeroToTwoPi( OriginalEdgeDirs[2] + ToRadiansAtC * InternalAngles[NewTris[2]][0]);  // CtoNew dir
	TriEdgeDir[2][1] = AsZeroToTwoPi( OriginalEdgeDirs[0] + ToRadiansAtA * InternalAngles[NewTris[0]][0]);  // AtoNew dir   

	// edges from new vertex to a, to b, and to c, using new-to-A as the zero direction.  These will be updated later when we learn the correct angle for new-to-A
	TriEdgeDir[0][2] = 0.;                                                                // NewToA dir 
	TriEdgeDir[1][2] = InternalAngles[NewTris[0]][2];                                     // NewToB dir
	TriEdgeDir[2][2] = InternalAngles[NewTris[0]][2] + InternalAngles[NewTris[1]][2];     // NewToC dir
	SignpostData.GeometricVertexInfo.InsertAt(FSignpost::FGeometricInfo(), NewVID); // we want the default of false, and 1.

	// (4) update the surface point for the new vertex by tracing one of the new edges. 
	// 
	// --- fix the r3 position of the new vertex and compute its SurfacePoint Attributes by doing a trace on the Extrinsic Mesh along one of the edges incident on NewVID

	// Use first incident edge and find the distance and direction (angle) to trace
	// Q: would picking the shortest of the new edges be better than just the first?
	const int32 AtoNewEID = PokeInfo.NewEdges[0]; // a-to-new edge
	const double AtoNewDist = EdgeLengths[AtoNewEID];

	// trace from A vertex in the direction toward the new vertex the edge length distance. This updates the surface position entry for the new vert.
	const int32 AVID = OriginalVIDs[0];
	const double AtoNewDir = TriEdgeDir[2][1];
	const double NewToAAngle = UpdateVertexByEdgeTrace(NewVID, AVID, AtoNewDir, AtoNewDist);

	// update the Newto{A,B,C}  directions such that NewToA has the angle LocalEIDAngle.
	for (int32 e = 0; e < 3; ++e)
	{ 
		TriEdgeDir[e][2] = AsZeroToTwoPi(TriEdgeDir[e][2] + NewToAAngle);
	}
	
	// record the new edge directions.
	SignpostData.IntrinsicEdgeAngles.InsertAt(TriEdgeDir[2], NewTris[2]);
	SignpostData.IntrinsicEdgeAngles.InsertAt(TriEdgeDir[1], NewTris[1]);
	SignpostData.IntrinsicEdgeAngles[NewTris[0]] = TriEdgeDir[0];
	
	// record the coordinate actually used. 
	PokeInfo.BaryCoords = BaryCoordinates;

	return EMeshResult::Ok;
}


EMeshResult UE::Geometry::FIntrinsicTriangulation::SplitEdgeTopology(int32 eab, FEdgeSplitInfo& SplitInfo)
{
	SplitInfo = FEdgeSplitInfo();

	if (!IsEdge(eab))
	{
		return EMeshResult::Failed_NotAnEdge;
	}

	// look up primary edge & triangle
	const FEdge Edge = Edges[eab];
	const int32 t0 = Edge.Tri[0];
	if (t0 == InvalidID)
	{
		return EMeshResult::Failed_BrokenTopology;
	}
	const FIndex3i T0tv = GetTriangle(t0);
	const FIndex3i T0te = GetTriEdges(t0);
	const int32 IndexOfe = T0te.IndexOf(eab);
	const int32 a = T0tv[IndexOfe];
	const int32 b = T0tv[AddOneModThree[IndexOfe]];
	const int32 c = T0tv[AddTwoModThree[IndexOfe]];

	// look up edge bc, which needs to be modified
	const int32 ebc = T0te[AddOneModThree[IndexOfe]];

	// RefCount overflow check. Conservatively leave room for
	// extra increments from other operations.
	if (VertexRefCounts.GetRawRefCount(c) > FRefCountVector::INVALID_REF_COUNT - 3)
	{
		return EMeshResult::Failed_HitValenceLimit;
	}
	

	SplitInfo.OriginalEdge = eab;
	SplitInfo.OriginalVertices = FIndex2i(a, b);   // this is the oriented a,b
	SplitInfo.OriginalTriangles = FIndex2i(t0, InvalidID);
	SplitInfo.SplitT = 0.5;

	// quite a bit of code is duplicated between boundary and non-boundary case, but it
	//  is too hard to follow later if we factor it out...
	if (IsBoundaryEdge(eab))
	{
		// create vertex
		const FVector3d vNew = 0.5 * (GetVertex(a) + GetVertex(b));
		const int32 f = AppendVertex(vNew);
		

		// rewrite existing triangle
		Triangles[t0][AddOneModThree[IndexOfe]] =  f;

		// add second triangle
		const int32 t2 = AddTriangleInternal(f, b, c, InvalidID, InvalidID, InvalidID);
	

		// rewrite edge bc, create edge af
		ReplaceEdgeTriangle(ebc, t0, t2);
		const int32 eaf = eab;
		Edges[eaf].Vert = FIndex2i(FMath::Min(a,f), FMath::Max(a,f));
		//ReplaceEdgeVertex(eaf, b, f);
		if (a != b)
		{ 
			VertexEdgeLists.Remove(b, eab);
		}
		VertexEdgeLists.Insert(f, eaf);

		// create edges fb and fc
		const int32 efb = AddEdgeInternal(f, b, t2);
		const int32 efc = AddEdgeInternal(f, c, t0, t2);

		// update triangle edge-nbrs
		ReplaceTriangleEdge(t0, ebc, efc);
		TriangleEdges[t2] = FIndex3i(efb, ebc, efc);

		// update vertex refcounts
		VertexRefCounts.Increment(c);
		VertexRefCounts.Increment(f, 2);

		SplitInfo.bIsBoundary = true;
		SplitInfo.OtherVertices = FIndex2i(c, InvalidID);
		SplitInfo.NewVertex = f;
		SplitInfo.NewEdges = FIndex3i(efb, efc, InvalidID);
		SplitInfo.NewTriangles = FIndex2i(t2, InvalidID);

		return EMeshResult::Ok;

	}
	else 		// interior triangle branch
	{
		// look up other triangle
		const int32 t1 = Edges[eab].Tri[1];
		SplitInfo.OriginalTriangles.B = t1;
		const FIndex3i T1tv = GetTriangle(t1);
		const FIndex3i T1te = GetTriEdges(t1);
		const int32 T1IndexOfe = T1te.IndexOf(eab);
		checkSlow(T1tv[T1IndexOfe] == b);

		const int32 d = T1tv[AddTwoModThree[T1IndexOfe]];
		const int32 edb = T1te[AddTwoModThree[T1IndexOfe]];

		// RefCount overflow check. Conservatively leave room for
		// extra increments from other operations.
		if (VertexRefCounts.GetRawRefCount(d) > FRefCountVector::INVALID_REF_COUNT - 3)
		{
			return EMeshResult::Failed_HitValenceLimit;
		}

		// create vertex
		FVector3d vNew = 0.5 * (GetVertex(a) + GetVertex(b));
		int32 f = AppendVertex(vNew);


		// rewrite existing triangles, replacing b with f
		Triangles[t0][AddOneModThree[IndexOfe]] = f;
		Triangles[t1][T1IndexOfe] = f;
	

		// add two triangles to close holes we just created
		int32 t2 = AddTriangleInternal(f, b, c, InvalidID, InvalidID, InvalidID);
		int32 t3 = AddTriangleInternal(f, d, b, InvalidID, InvalidID, InvalidID);
		

		// update the edges we found above, to point to triangles
		ReplaceEdgeTriangle(ebc, t0, t2);
		ReplaceEdgeTriangle(edb, t1, t3);

		// edge eab became eaf
		int32 eaf = eab; //Edge * eAF = eAB;
		Edges[eaf].Vert = FIndex2i(FMath::Min(a, f), FMath::Max(a, f));

		// update a/b/f vertex-edges
		if (a != b)
		{ 
			VertexEdgeLists.Remove(b, eab);
		}
		VertexEdgeLists.Insert(f, eaf);

		// create edges connected to f  (also updates vertex-edges)
		int32 efb = AddEdgeInternal(f, b, t2, t3);
		int32 efc = AddEdgeInternal(f, c, t0, t2);
		int32 edf = AddEdgeInternal(d, f, t1, t3);

		// update triangle edge-nbrs
		ReplaceTriangleEdge(t0, ebc, efc);
		ReplaceTriangleEdge(t1, edb, edf);
		TriangleEdges[t2] = FIndex3i(efb, ebc, efc);
		TriangleEdges[t3] = FIndex3i(edf, edb, efb);

		// update vertex refcounts
		VertexRefCounts.Increment(c);
		VertexRefCounts.Increment(d);
		VertexRefCounts.Increment(f, 4);

		SplitInfo.bIsBoundary = false;
		SplitInfo.OtherVertices = FIndex2i(c, d);
		SplitInfo.NewVertex = f;
		SplitInfo.NewEdges = FIndex3i(efb, efc, edf);
		SplitInfo.NewTriangles = FIndex2i(t2, t3);

		return EMeshResult::Ok;
	}

}


UE::Geometry::EMeshResult  UE::Geometry::FIntrinsicTriangulation::SplitEdge(int32 EdgeAB, FEdgeSplitInfo& SplitInfo, double SplitParameterT)
{
	SplitParameterT = FMath::Clamp(SplitParameterT, 0., 1.);

	if (!IsEdge(EdgeAB))
	{
		return EMeshResult::Failed_NotAnEdge;
	}

	
	const FEdge OriginalEdge = GetEdge(EdgeAB);

	if (IsBoundaryEdge(EdgeAB))
	{
		// state before the split
		const int32 TID = OriginalEdge.Tri[0];
		const int32 IndexOfe = GetTriEdges(TID).IndexOf(EdgeAB);

		// Info about the original T0 tri, reordered to make the split edge the first edge..
		const FIndex3i OriginalT0VIDs  = Permute(IndexOfe, GetTriangle(TID));  // as (a, b, c)
		const FIndex3i OriginalT0Edges = Permute(IndexOfe, GetTriEdges(TID));  // as (ab, bc, ca)
		
		const FVector3d OriginalT0EdgeLengths = Permute(IndexOfe, GetTriEdgeLengths(TID));    // as (|ab|, |bc|, |ca|)
		const FVector3d OriginalT0EdgeDirs    = Permute(IndexOfe, SignpostData.IntrinsicEdgeAngles[TID]);  // as ( aTob, bToc, cToa)

		
		// Update the connectivity with the edge split
		EMeshResult Result = SplitEdgeTopology(EdgeAB, SplitInfo);

		if (Result != EMeshResult::Ok)
		{
			return Result;
		}
		const int32 NewVID = SplitInfo.NewVertex;
		const int32 NewTID = SplitInfo.NewTriangles[0]; // edges {fb, bc, cf}
		const int32 NewEdgeFB = SplitInfo.NewEdges[0];
		const int32 NewEdgeFC = SplitInfo.NewEdges[1];

		const double DistAF = SplitParameterT * OriginalT0EdgeLengths[0];
		const double DistFB = FMath::Max(0., OriginalT0EdgeLengths[0] - DistAF);
		// new edge length
		const double DistFC = DistanceBetweenBarycentricPoints( OriginalT0EdgeLengths[0],
			                                                    OriginalT0EdgeLengths[1],
																OriginalT0EdgeLengths[2],
			                                                    FVector3d(SplitParameterT, 1. - SplitParameterT, 0.), 
																FVector3d(0., 0., 1.));
		
		EdgeLengths.InsertAt(DistFC, NewEdgeFC);
		EdgeLengths.InsertAt(DistFB, NewEdgeFB);
		EdgeLengths[EdgeAB] = DistAF;
		
		// update the internal angles
		InternalAngles.InsertAt(ComputeTriInternalAnglesR(NewTID), NewTID);
		InternalAngles[TID] = ComputeTriInternalAnglesR(TID);
		const FVector3d TIDInternalAngles = Permute(IndexOfe, InternalAngles[TID]);
		
		// update the directions.  Say DirFtoB is zero
		const double ToRadiansAtC = SignpostData.GeometricVertexInfo[OriginalT0VIDs[2]].ToRadians;
		SignpostData.GeometricVertexInfo.InsertAt(FSignpost::FGeometricInfo(), NewVID); // we want the default of false, and 1.

		FVector3d TriEdgeDir[2]; // one for each tri in order (TID, NewTID )
		// edges around the boundary of the original tri
		TriEdgeDir[0][0] = OriginalT0EdgeDirs[0];    // AtoF dir
		TriEdgeDir[1][1] = OriginalT0EdgeDirs[1];    // BtoC dir
		TriEdgeDir[0][2] = OriginalT0EdgeDirs[2];    // CtoA dir  

		TriEdgeDir[1][0] = 0.;  // FtoB dir
		TriEdgeDir[1][2] = AsZeroToTwoPi(OriginalT0EdgeDirs[2] + ToRadiansAtC * TIDInternalAngles[2]);  // CtoF dir
		TriEdgeDir[0][1] = AsZeroToTwoPi(InternalAngles[NewTID][0]);                                    // FtoC dir relative to FtoB
		const double FtoADir = AsZeroToTwoPi(InternalAngles[NewTID][0] + TIDInternalAngles[1]);         // FtoA dir relative to FtoB
		// update the surface position of the new vertex and the relative directions from it.

		// trace from A vertex in the direction toward the new vertex the edge length distance. This updates the surface position entry for the new vert.
		const int32 AVID = OriginalT0VIDs[0];
		const double AtoFDir = TriEdgeDir[0][0];
		const double FToAAngle = UpdateVertexByEdgeTrace(NewVID, AVID, AtoFDir, DistAF);
		const double ToLocalDir = (FToAAngle - FtoADir); 
		// update the Fto{B,C}  directions such that the direction from F to A would agree with FtoAAngle
		TriEdgeDir[0][1] = AsZeroToTwoPi(TriEdgeDir[0][1] + ToLocalDir);
		TriEdgeDir[1][0] = AsZeroToTwoPi(TriEdgeDir[1][0] + ToLocalDir);
		
		// store angle results
		SignpostData.IntrinsicEdgeAngles.InsertAt(TriEdgeDir[1], NewTID);
		SignpostData.IntrinsicEdgeAngles[TID] = Permute((3 - IndexOfe)%3,  TriEdgeDir[0]);


		return Result;
	}
	else
	{
		// state before the split
		const int32 TID = OriginalEdge.Tri[0];
		const int32 IndexOfe = GetTriEdges(TID).IndexOf(EdgeAB);

		// Info about the original T0 tri, reordered to make the split edge the first edge..
		const FIndex3i OriginalT0VIDs  = Permute(IndexOfe, GetTriangle(TID));  // as (a, b, c)
		const FIndex3i OriginalT0Edges = Permute(IndexOfe, GetTriEdges(TID));  // as (ab, bc, ca)

		const FVector3d OriginalT0EdgeLengths = Permute(IndexOfe, GetTriEdgeLengths(TID));    // as (|ab|, |bc|, |ca|)
		const FVector3d OriginalT0EdgeDirs    = Permute(IndexOfe, SignpostData.IntrinsicEdgeAngles[TID]);  // as ( aTob, bToc, cToa)
		
		// Info about the original T0 tri, reordered to make the split edge the first edge..
		const int32 TID1 = OriginalEdge.Tri[1];
		const int32 IndexOfT1e = GetTriEdges(TID1).IndexOf(EdgeAB);

		const FIndex3i OriginalT1VIDs  = Permute(IndexOfT1e, GetTriangle(TID1));  // as (b, a, d)
		const FIndex3i OriginalT1Edges = Permute(IndexOfT1e, GetTriEdges(TID1));  // as (ba, ad, db)
		
		const FVector3d OriginalT1EdgeLengths = Permute(IndexOfT1e, GetTriEdgeLengths(TID1));   // as (|ba|, |ad|, |db})
		const FVector3d OriginalT1EdgeDirs    = Permute(IndexOfT1e, SignpostData.IntrinsicEdgeAngles[TID1]); // as (bToa, aTod, dTob) 
		

		// Update the connectivity with the edge split
		EMeshResult Result = SplitEdgeTopology(EdgeAB, SplitInfo);

		if (Result != EMeshResult::Ok)
		{
			return Result;
		}
		const int32 NewVID = SplitInfo.NewVertex;
		const int32 NewTID0 = SplitInfo.NewTriangles[0]; // edges {fb, bc, cf}
		const int32 NewTID1 = SplitInfo.NewTriangles[1]; // edges {fd, db, bc}
		const int32 EdgeAF = EdgeAB;
		const int32 NewEdgeFB = SplitInfo.NewEdges[0];
		const int32 NewEdgeFC = SplitInfo.NewEdges[1];
		const int32 NewEdgeFD = SplitInfo.NewEdges[2];

		const double DistAF = SplitParameterT * OriginalT0EdgeLengths[0];
		const double DistFB = FMath::Max(0., OriginalT0EdgeLengths[0] - DistAF);
		// new edge length
		const double DistFC = DistanceBetweenBarycentricPoints( OriginalT0EdgeLengths[0],
			                                                    OriginalT0EdgeLengths[1],
			                                                    OriginalT0EdgeLengths[2],
			                                                    FVector3d(SplitParameterT, 1. - SplitParameterT, 0.),
			                                                    FVector3d(0., 0., 1.));
		
		// new edge length
		const double DistFD = DistanceBetweenBarycentricPoints( OriginalT1EdgeLengths[0],
			                                                    OriginalT1EdgeLengths[1],
			                                                    OriginalT1EdgeLengths[2],
			                                                    FVector3d(1. - SplitParameterT, SplitParameterT, 0.),
			                                                    FVector3d(0., 0., 1.));

		EdgeLengths.InsertAt(DistFD, NewEdgeFD);
		EdgeLengths.InsertAt(DistFC, NewEdgeFC);
		EdgeLengths.InsertAt(DistFB, NewEdgeFB);
		EdgeLengths[EdgeAF] = DistAF;

		// update the internal angles (this uses edge lengths)
		InternalAngles.InsertAt(ComputeTriInternalAnglesR(NewTID1), NewTID1);
		InternalAngles.InsertAt(ComputeTriInternalAnglesR(NewTID0), NewTID0);
		InternalAngles[TID] = ComputeTriInternalAnglesR(TID);
		InternalAngles[TID1] = ComputeTriInternalAnglesR(TID1);

		// update the directions.  Say DirFtoB is zero
		const double ToRadiansAtC = SignpostData.GeometricVertexInfo[OriginalT0VIDs[2]].ToRadians;
		const double ToRadiansAtD = SignpostData.GeometricVertexInfo[OriginalT1VIDs[2]].ToRadians;
		SignpostData.GeometricVertexInfo.InsertAt(FSignpost::FGeometricInfo(), NewVID); // we want the default of false, and 1.

		const FVector3d TIDInternalAngles  = Permute(IndexOfe, InternalAngles[TID]);
		const FVector3d TID1InternalAngles = Permute(IndexOfT1e, InternalAngles[TID1]);

		FVector3d TriEdgeDir[4]; // one for each tri in order (TID, NewTID0, TID1,  NewTID1 )
		// edges around the boundary of the original tri
		TriEdgeDir[0][0] = OriginalT0EdgeDirs[0];    // AtoF dir
		TriEdgeDir[1][1] = OriginalT0EdgeDirs[1];    // BtoC dir
		TriEdgeDir[0][2] = OriginalT0EdgeDirs[2];    // CtoA dir  

		TriEdgeDir[3][2] = OriginalT1EdgeDirs[0];    // BtoF dir
		TriEdgeDir[2][1] = OriginalT1EdgeDirs[1];    // AtoD dir
		TriEdgeDir[3][1] = OriginalT1EdgeDirs[2];    // DtoB dir

		
		TriEdgeDir[1][2] = AsZeroToTwoPi(OriginalT0EdgeDirs[2] + ToRadiansAtC * TIDInternalAngles[2]);       // CtoF dir
		TriEdgeDir[2][2] = AsZeroToTwoPi(OriginalT1EdgeDirs[2] + ToRadiansAtD * InternalAngles[NewTID1][1]); // Dtof dir 

		TriEdgeDir[1][0] = 0.;                                                                          // FtoB dir
		TriEdgeDir[0][1] = AsZeroToTwoPi(InternalAngles[NewTID0][0]);                                   // FtoC dir relative to FtoB
		TriEdgeDir[2][0] = AsZeroToTwoPi(InternalAngles[NewTID0][0] + TIDInternalAngles[1]);            // FtoA dir relative to FtoB
		
		TriEdgeDir[3][0] = AsZeroToTwoPi(InternalAngles[NewTID0][0] + TIDInternalAngles[1] + TID1InternalAngles[0]); // FtoD dir relative to FtoB

		// trace from A vertex in the direction toward the new vertex the edge length distance. This updates the surface position entry for the new vert.
		const int32 AVID = OriginalT0VIDs[0];
		const double AtoFDir = TriEdgeDir[0][0];
		const double FToAAngle = UpdateVertexByEdgeTrace(NewVID, AVID, AtoFDir, DistAF);
		const double ToLocalDir = (FToAAngle - TriEdgeDir[2][0]);

		// fix angles relative to local dir.
		TriEdgeDir[1][0] = AsZeroToTwoPi(ToLocalDir);
		TriEdgeDir[0][1] = AsZeroToTwoPi(TriEdgeDir[0][1] + ToLocalDir);
		TriEdgeDir[2][0] = AsZeroToTwoPi(TriEdgeDir[2][0] + ToLocalDir);
		TriEdgeDir[3][0] = AsZeroToTwoPi(TriEdgeDir[3][0] + ToLocalDir);

		// store angle results
		SignpostData.IntrinsicEdgeAngles.InsertAt(TriEdgeDir[3], NewTID1);
		SignpostData.IntrinsicEdgeAngles.InsertAt(TriEdgeDir[1], NewTID0);
		SignpostData.IntrinsicEdgeAngles[TID]  = Permute((3 - IndexOfe) % 3, TriEdgeDir[0]);
		SignpostData.IntrinsicEdgeAngles[TID1] = Permute((3 - IndexOfT1e) % 3, TriEdgeDir[2]);

		return Result;
	}
}

/**------------------------------------------------------------------------------
*  FIntrinsicEdgeFlipMesh Methods
*-------------------------------------------------------------------------------*/
FIntrinsicEdgeFlipMesh::FIntrinsicEdgeFlipMesh(const FDynamicMesh3& SurfaceMesh)
	: MyBase(SurfaceMesh)
	, NormalCoordinates(SurfaceMesh)
{
}

EMeshResult FIntrinsicEdgeFlipMesh::FlipEdge(int32 EID, FEdgeFlipInfo& EdgeFlipInfo)
{
	if (!IsEdge(EID))
	{
		return EMeshResult::Failed_NotAnEdge;
	}

	if (IsBoundaryEdge(EID))
	{
		return EMeshResult::Failed_IsBoundaryEdge;
	}

	// state before flip, needed when updating the normal coords after the flip
	const FIndex2i EdgeT  = GetEdgeT(EID);
	const FIndex3i TAEIDs = GetTriEdges(EdgeT.A);
	const FIndex3i TBEIDs = GetTriEdges(EdgeT.B);
	const FIndex2i OppVs  = GetEdgeOpposingV(EID);

	EMeshResult FlipResult = MyBase::FlipEdge(EID, EdgeFlipInfo);

	if (FlipResult == EMeshResult::Ok)
	{
		// update the normal coords
		NormalCoordinates.OnFlipEdge(EdgeT.A, TAEIDs, OppVs.A, EdgeT.B, TBEIDs, OppVs.B, EID);
	}

	return FlipResult;
}

TArray<FIntrinsicEdgeFlipMesh::FSurfacePoint> FIntrinsicEdgeFlipMesh::TraceEdge(int32 IntrinsicEID, double CoalesceThreshold, bool bReverse) const
{
	TArray<FIntrinsicEdgeFlipMesh::FSurfacePoint> Result;
	
	if (!IsEdge(IntrinsicEID))
	{
		return MoveTemp(Result);
	}

	TArray<int32> SurfXings;
	const int32 NumSurfXings = NormalCoordinates.NormalCoord[IntrinsicEID];
	if (NumSurfXings > 0)
	{
		SurfXings.Reserve(NumSurfXings);
	}

	const FDynamicMesh3& SurfaceMesh = *GetExtrinsicMesh();
	const FIntrinsicEdgeFlipMesh& IntrinsicMesh = *this;

	const FIndex2i IntrinsicEdgeT = GetEdgeT(IntrinsicEID);

	// need to create a list of surface mesh edges that cross this edge.  To do this we need to follow
	// each curve that crosses this intrinsic edge to the vertices where it terminates ( thus identifying the surface edge )
	for (int32 p = 1; p < NumSurfXings + 1; ++p)
	{

		const FIndex2i SurfaceEdgeV = [&]
			{
		
				TArray<FNormalCoordinates::FEdgeAndCrossingIdx> Crossings;
				FIndex2i Verts;
				// follow surface curve (i.e. surface edge) forward to the end and identify the vertex
				{
					FNormalCoordSurfaceTraceImpl::ContinueTraceSurfaceEdge(IntrinsicMesh,  IntrinsicEdgeT.A, IntrinsicEID, p, Crossings);
					const FNormalCoordinates::FEdgeAndCrossingIdx& LastXing = Crossings.Last();

					const FIndex3i TriEIDs = GetTriEdges(LastXing.TID);
					Verts.A                = GetTriangle(LastXing.TID)[TriEIDs.IndexOf(LastXing.EID)];
				}
				Crossings.Reset();

				// follow surface curve (i.e. surface edge) backward to other end and identify the vertex
				{
					FNormalCoordSurfaceTraceImpl::ContinueTraceSurfaceEdge(IntrinsicMesh, IntrinsicEdgeT.B, IntrinsicEID, NumSurfXings + 1 - p, Crossings);
					const FNormalCoordinates::FEdgeAndCrossingIdx& LastXing = Crossings.Last();

					const FIndex3i TriEIDs = GetTriEdges(LastXing.TID);
					Verts.B                = GetTriangle(LastXing.TID)[TriEIDs.IndexOf(LastXing.EID)];
				}
				return Verts;
			}();

		// identify the surface edge from its endpoints. 
		const int32 SurfaceEID = SurfaceMesh.FindEdge(SurfaceEdgeV.A, SurfaceEdgeV.B);
		checkSlow(SurfaceEID != InvalidID);
		
		SurfXings.Add(SurfaceEID);
	}
	
	// do the actual trace - this identifies the surface mesh triangles crossed by the intrinsic edge and unfolds them into a triangle strip where the trace is performed
	Result =  FNormalCoordSurfaceTraceImpl::TraceEdgeOverHost(IntrinsicEID, SurfXings, SurfaceMesh, IntrinsicMesh, CoalesceThreshold, bReverse);

	return MoveTemp(Result);
}

TArray<FIntrinsicEdgeFlipMesh::FEdgeAndCrossingIdx> FIntrinsicEdgeFlipMesh::GetImplicitEdgeCrossings(const int32 SurfaceEID, const bool bReverse) const
{
	return FNormalCoordSurfaceTraceImpl::TraceSurfaceEdge(*this, SurfaceEID, bReverse);
}

FIntrinsicEdgeFlipMesh::FEdgeCorrespondence::FEdgeCorrespondence(const FIntrinsicEdgeFlipMesh& Mesh)
	: IntrinsicMesh(&Mesh)
	, SurfaceMesh(Mesh.GetNormalCoordinates().SurfaceMesh)
{
	const int32 IntrinsicMaxEID = IntrinsicMesh->MaxEdgeID();
	const int32 SurfaceMaxEID   = SurfaceMesh->MaxEdgeID();

	SurfaceEdgesCrossed.SetNum(IntrinsicMaxEID);
	const IntrinsicCorrespondenceUtils::FNormalCoordinates& NormalCoords = IntrinsicMesh->GetNormalCoordinates();

	// allocate the SurfaceEdgesCrossed. From the normal coordinates we know how many surface edge crossings each intrinsic edge sees
	for (int32 IntrinsicEID = 0; IntrinsicEID < IntrinsicMaxEID; ++IntrinsicEID)
	{
		if (!IntrinsicMesh->IsEdge(IntrinsicEID))
		{
			continue;
		}
		// number of times a surface edge crosses this intrinsic edge
		int32 NumXings = NormalCoords.NormalCoord[IntrinsicEID];

		if (NumXings > 0)
		{
			SurfaceEdgesCrossed[IntrinsicEID].SetNum(NumXings);

		} // else don't bother making an entry when NumXings == 0 since that means the edges are the same on both meshes.
	}

	// trace each surface edge across the intrinsic mesh and construct an ordered list of surface edges crossing each intrinsic edge.
	// the order of the crossings should be consistent with the edge direction relative to the first adjacent tri 
	// (i.e. starting at the corner Mesh.GetTriEdges(GetEdgeT(EID).A).IndexOf(EID) ) 

	for (int32 SurfaceEID = 0; SurfaceEID < SurfaceMaxEID; ++SurfaceEID)
	{
		if (!SurfaceMesh->IsEdge(SurfaceEID))
		{
			continue;
		}
		const TArray<FEdgeAndCrossingIdx> IntrinsicEdgeXings = IntrinsicMesh->GetImplicitEdgeCrossings(SurfaceEID, false /* = bReverseTrace*/);

		for (int32 i = 0; i < IntrinsicEdgeXings.Num(); ++i)
		{
			const FEdgeAndCrossingIdx& EdgeXing = IntrinsicEdgeXings[i];
			bool bIsEndVertex                   = (EdgeXing.CIdx == 0);

			if (!bIsEndVertex)
			{
				const int32 IntrinsicEID      = EdgeXing.EID;
				const FIndex2i IntrinsicEdgeT = IntrinsicMesh->GetEdgeT(IntrinsicEID);
				checkSlow(IntrinsicEdgeT.A == EdgeXing.TID || IntrinsicEdgeT.B == EdgeXing.TID);

				// array of surface edges this intrinsic edge crosses, these should be ordered relative to the direction of TriA.
				TArray<int32>& XingSurfaceEdges = SurfaceEdgesCrossed[IntrinsicEID];

				// crossings count from the bottom of the edge to the top relative to EdgeXing.TID 
				const int32 XingID = (IntrinsicEdgeT.A == EdgeXing.TID) ? EdgeXing.CIdx - 1 : XingSurfaceEdges.Num() - EdgeXing.CIdx;

				checkSlow(XingID > -1);
				XingSurfaceEdges[XingID] = SurfaceEID;
			}
		}
	}
}

TArray<FIntrinsicEdgeFlipMesh::FSurfacePoint>
FIntrinsicEdgeFlipMesh::TraceSurfaceEdge(int32 SurfaceEID, double CoalesceThreshold, bool bReverse) const
{
	const FDynamicMesh3& HostMesh                   = *this->GetExtrinsicMesh();
	const FIntrinsicEdgeFlipMesh& TraceMesh         = *this;
	TArray<FEdgeAndCrossingIdx> EdgeAndCrossingIdxs = this->GetImplicitEdgeCrossings(SurfaceEID, bReverse);

	TArray<int32> HostEdgeCrossings;
	HostEdgeCrossings.Reserve(EdgeAndCrossingIdxs.Num() - 2); // EdgeAndCrossingsIdx include the start and end vertex.  don't need them.
	for (FEdgeAndCrossingIdx EdgeAndCrossing : EdgeAndCrossingIdxs)
	{
		if (EdgeAndCrossing.CIdx != 0) // skip the start and end vertex
		{
			HostEdgeCrossings.Add(EdgeAndCrossing.EID);
		}
	}

	return FNormalCoordSurfaceTraceImpl::TraceEdgeOverHost(SurfaceEID, HostEdgeCrossings, HostMesh, TraceMesh, CoalesceThreshold, bReverse);
}

TArray<FIntrinsicEdgeFlipMesh::FSurfacePoint>
FIntrinsicEdgeFlipMesh::FEdgeCorrespondence::TraceEdge(int32 IntrinsicEID, double CoalesceThreshold, bool bReverse) const
{
	const FDynamicMesh3& HostMesh           = *SurfaceMesh;
	const FIntrinsicEdgeFlipMesh& TraceMesh = *IntrinsicMesh;
	const TArray<int32>& HostEdgeCrossings  = SurfaceEdgesCrossed[IntrinsicEID];

	return FNormalCoordSurfaceTraceImpl::TraceEdgeOverHost(IntrinsicEID, HostEdgeCrossings, HostMesh, TraceMesh, CoalesceThreshold, bReverse);
}

