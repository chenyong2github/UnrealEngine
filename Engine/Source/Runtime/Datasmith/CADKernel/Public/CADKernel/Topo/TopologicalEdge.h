// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Geo/Curves/Curve.h"
#include "CADKernel/Geo/Curves/RestrictionCurve.h"
#include "CADKernel/Geo/GeoEnum.h"
#include "CADKernel/Math/Boundary.h"
#include "CADKernel/Topo/Linkable.h"
#include "CADKernel/Topo/TopologicalVertex.h"
#include "CADKernel/Utils/Cache.h"

namespace CADKernel
{
	typedef TTopologicalLink<FTopologicalEdge> FEdgeLink;

	enum ECoordinateType : uint8
	{
		VertexCoordinate,
		ImposedCoordinate,
		IsoUCoordinate,
		IsoVCoordinate,
		IsoUVCoordinate,
		OtherCoordinate
	};

	template<typename FCuttingPointType>
	void GetCuttingPointCoordinates(const TArray<FCuttingPointType>& CuttingPoints, TArray<double>& CuttingPointCoordinates);

	struct FEdge2DProperties;
	struct FCuttingPoint;
	struct FImposedCuttingPoint;

	class FModelMesh;
	class FRestrictionCurve;
	class FTopologicalLoop;
	class FTopologicalVertex;

	class CADKERNEL_API FTopologicalEdge : public TLinkable<FTopologicalEdge, FEdgeLink>
	{
		friend class FEntity;
		friend class FTopologicalLoop;
		friend class FTopologicalFace;

	protected:

		TSharedPtr<FTopologicalVertex> StartVertex;
		TSharedPtr<FTopologicalVertex> EndVertex;

		/**
		 * The edge is oriented in the curve orientation i.e. StartCoordinate < EndCoordinate
		 */
		FLinearBoundary Boundary;

		TSharedPtr<FRestrictionCurve> Curve;
 		mutable double Length3D = -1.;

		TWeakPtr<FTopologicalLoop> Loop;

		TSharedPtr<FEdgeMesh> Mesh;

		/**
		 * Final U coordinates of the edge's mesh nodes
		 */
		TArray<FCuttingPoint> CuttingPointUs;

		/**
		 * U coordinates of the edge's mesh nodes for thin zone purpose
		 */
		TArray<FImposedCuttingPoint> ImposedCuttingPointUs;
		TArray<FLinearBoundary> ThinZoneBounds;

		/**
		 * Temporary discretization of the edge used to compute the mesh of the edge
		 */
		TArray<double> CrossingPointUs;

		/**
		 * Min delta U at the crossing points to respect meshing criteria
		 */
		TArray<double> CrossingPointDeltaUMins;

		/**
		 * Max delta U at the crossing points to respect meshing criteria
		 */
		TArray<double> CrossingPointDeltaUMaxs;

	private:

		/**
		 * Constructors of FEdge cannot be used directly,use FEdge::Make to create an FEdge object.
		 */
		FTopologicalEdge(const TSharedRef<FRestrictionCurve>& InCurve, const TSharedRef<FTopologicalVertex>& InVertex1, const TSharedRef<FTopologicalVertex>& InVertex2, const FLinearBoundary& InBoundary);
		FTopologicalEdge(const TSharedRef<FRestrictionCurve>& InCurve, const TSharedRef<FTopologicalVertex>& InVertex1, const TSharedRef<FTopologicalVertex>& InVertex2);
		FTopologicalEdge(const TSharedRef<FRestrictionCurve>& InCurve, const FLinearBoundary& InBoundary);
		FTopologicalEdge(const TSharedRef<FRestrictionCurve>& InCurve);

		FTopologicalEdge(FCADKernelArchive& Archive)
			: TLinkable<FTopologicalEdge, FEdgeLink>()
		{
			Serialize(Archive);
		}

		void SetBoundary(const TSharedRef<FTopologicalLoop>& NewBoundary)
		{
			Loop = NewBoundary;
		}

		void RemoveBoundary()
		{
			Loop.Reset();
		}

		/**
		 * Used by FEdge::Make
		 * Sort vertex coordinate to ensure that start coordinate < end coordinate
		 * Swap vertices if needed i.e. to ensure that 3D point at start (and end) coordinate is near to start (and end) vertex 3d coordinates
		 * @return false if one vertex is too far to the associated curve 3d point
		 */
		bool CheckVertices();

	public:

		static TSharedPtr<FTopologicalEdge> Make(const TSharedRef<FRestrictionCurve>& InCurve, const TSharedRef<FTopologicalVertex>& InVertex1, const TSharedRef<FTopologicalVertex>& InVertex2, const FLinearBoundary& InBoundary);
		static TSharedPtr<FTopologicalEdge> Make(const TSharedRef<FRestrictionCurve>& InCurve, const TSharedRef<FTopologicalVertex>& InVertex1, const TSharedRef<FTopologicalVertex>& InVertex2);
		static TSharedPtr<FTopologicalEdge> Make(const TSharedRef<FRestrictionCurve>& InCurve, const FLinearBoundary& InBoundary);
		static TSharedPtr<FTopologicalEdge> Make(const TSharedRef<FRestrictionCurve>& InCurve);

		virtual ~FTopologicalEdge() = default;

		void Delete();

		virtual void Serialize(FCADKernelArchive& Ar) override
		{
			TLinkable<FTopologicalEdge, FEdgeLink>::Serialize(Ar);
			SerializeIdent(Ar, StartVertex);
			SerializeIdent(Ar, EndVertex);
			SerializeIdent(Ar, Curve);
			Ar << Boundary;
			SerializeIdent(Ar, Loop);
			Ar << Length3D;
		}

		virtual void SpawnIdent(FDatabase& Database) override;

		virtual void ResetMarkersRecursively() override
		{
			ResetMarkers();
			TLinkable<FTopologicalEdge, FEdgeLink>::ResetMarkers();
			StartVertex->ResetMarkersRecursively();
			EndVertex->ResetMarkersRecursively();

			Curve->ResetMarkersRecursively();;
		}

#ifdef CADKERNEL_DEV
		virtual FInfoEntity& GetInfo(FInfoEntity&) const override;
#endif

		double GetTolerance3D();

		virtual EEntity GetEntityType() const override
		{
			return EEntity::TopologicalEdge;
		}

		virtual TSharedPtr<FEntityGeom> ApplyMatrix(const FMatrixH& InMatrix) const override;

		// ======   Topological Function   ======

		void LinkVertex();

		/**
		 * Checks if the carrier curve is degenerated i.e. the 2d length of the curve is nearly zero
		 * If the 3d length is nearly zero, the edge is flag as degenerated
		 */
		bool CheckIfDegenerated();

		void Link(const TSharedRef<FTopologicalEdge>& OtherEdge);

		TSharedRef<const FTopologicalEdge> GetLinkActiveEdge() const
		{
			return StaticCastSharedRef<const FTopologicalEdge>(GetLinkActiveEntity());
		}

		TSharedRef<FTopologicalEdge> GetLinkActiveEdge()
		{
			return StaticCastSharedRef<FTopologicalEdge>(GetLinkActiveEntity());
		}

		TSharedPtr<FTopologicalEdge> GetFirstTwinEdge() const
		{
			if (!TopologicalLink)
			{
				return nullptr;
			}

			if (TopologicalLink->GetTwinsEntities().Num() < 2)
			{
				return nullptr;
			}

			TWeakPtr<FTopologicalEdge> FirstTwinEdge = (TopologicalLink->GetTwinsEntities()[0].Pin().Get() == this) ? TopologicalLink->GetTwinsEntities()[1] : TopologicalLink->GetTwinsEntities()[0];
			return FirstTwinEdge.Pin();
		}

		/**
		 * @return true if the twin edge is in the same direction as this
		 */
		bool IsSameDirection(const TSharedPtr<FTopologicalEdge>& Edge) const;

		/**
		 * @return true if the edge is self connected at its extremities
		 */
		bool IsClosed() const
		{
			return StartVertex->GetLinkActiveEntity() == EndVertex->GetLinkActiveEntity();
		}

		/**
		 * @return the containing boundary
		 */
		const TSharedPtr<FTopologicalLoop> GetLoop() const
		{
			return Loop.Pin();
		}

		/**
		 * @return the containing boundary
		 */
		TSharedPtr<FTopologicalLoop> GetLoop()
		{
			return Loop.Pin();
		}

		/**
		 * @return the carrier topological face
		 */
		TSharedRef<FTopologicalFace> GetFace() const;

		// ======   Vertex Functions (Get, Set, ...)   ======

		const TSharedRef<FTopologicalVertex> GetStartVertex(EOrientation Forward) const
		{
			return (Forward == EOrientation::Front ? StartVertex.ToSharedRef() : EndVertex.ToSharedRef());
		}

		const TSharedRef<FTopologicalVertex> GetEndVertex(EOrientation Forward) const
		{
			return (Forward == EOrientation::Front ? EndVertex.ToSharedRef() : StartVertex.ToSharedRef());
		}

		const TSharedRef<FTopologicalVertex> GetStartVertex(bool Forward) const
		{
			return (Forward ? StartVertex.ToSharedRef() : EndVertex.ToSharedRef());
		}

		const TSharedRef<FTopologicalVertex> GetEndVertex(bool Forward) const
		{
			return (Forward ? EndVertex.ToSharedRef() : StartVertex.ToSharedRef());
		}

		const TSharedRef<FTopologicalVertex> GetStartVertex() const
		{
			return StartVertex.ToSharedRef();
		}

		const TSharedRef< FTopologicalVertex> GetEndVertex() const
		{
			return EndVertex.ToSharedRef();
		}

		TSharedRef<FTopologicalVertex> GetStartVertex()
		{
			return StartVertex.ToSharedRef();
		}

		TSharedRef<FTopologicalVertex> GetEndVertex()
		{
			return EndVertex.ToSharedRef();
		}

		TSharedPtr<FTopologicalVertex> GetOtherVertex(const TSharedRef<FTopologicalVertex>& Vertex) const
		{
			return (Vertex->GetLink() == StartVertex->GetLink() ? EndVertex : (Vertex->GetLink() == EndVertex->GetLink() ? StartVertex : TSharedPtr<FTopologicalVertex>()));
		}

		TSharedPtr<FTopologicalVertex> GetOtherVertex(const TSharedRef<const FTopologicalVertex>& Vertex) const
		{
			return (Vertex->GetLink() == StartVertex->GetLink() ? EndVertex : (Vertex->GetLink() == EndVertex->GetLink() ? StartVertex : TSharedPtr<FTopologicalVertex>()));
		}

		void SetStartVertex(const double NewCoordinate);
		void SetEndVertex(const double NewCoordinate);

		void SetStartVertex(const double NewCoordinate, const FPoint& NewPoint3D);
		void SetEndVertex(const double NewCoordinate, const FPoint& NewPoint3D);

		void DeleteStartVertex()
		{
			StartVertex.Reset();
		}

		void DeleteEndVertex()
		{
			EndVertex.Reset();
		}

		const FLinearBoundary& GetBoundary() const
		{
			return Boundary;
		}

		double GetStartCurvilinearCoordinates() const
		{
			return Boundary.GetMin();
		}

		double GetEndCurvilinearCoordinates() const
		{
			return Boundary.GetMax();
		}

		/**
		 * @return the 3d coordinate of the start vertex (the barycenter of the twin vertices)
		 */
		FPoint GetStartBarycenter()
		{
			return StartVertex->GetBarycenter();
		}

		/**
		 * @return the 3d coordinate of the end vertex (the barycenter of the twin vertices)
		 */
		FPoint GetEndBarycenter()
		{
			return EndVertex->GetBarycenter();
		}

		/**
		 * @return the 3d coordinate of the start vertex (prefer GetStartBarycenter)
		 */
		FPoint GetStartCoordinate()
		{
			return StartVertex->GetCoordinates();
		}

		/**
		 * @return the 3d coordinate of the end vertex (prefer GetEndBarycenter)
		 */
		FPoint GetEndCoordinate()
		{
			return EndVertex->GetCoordinates();
		}

		void GetTangentsAtExtremities(FPoint& StartTangent, FPoint& EndTangent, bool bForward) const;

		// ======   Meshing Function   ======

		const TSharedRef<FEdgeMesh> GetMesh() const
		{
			if (GetLinkActiveEntity() != AsShared())
			{
				return GetLinkActiveEdge()->GetMesh();
			}
			ensureCADKernel(Mesh.IsValid());
			return Mesh.ToSharedRef();
		}

		TSharedRef<FEdgeMesh> GetOrCreateMesh(const TSharedRef<FModelMesh>& MeshModel);

		/**
		 * Generate a sampling of the curve.
		 * This sampling is used by apply meshing criteria function to defined the optimal mesh of the edge.
		 * This sampling is saved in CrossingPointUs TArray.
		 */
		void ComputeCrossingPointCoordinates();
		
		int32 EvaluateCuttingPointNum();

		void InitDeltaUs()
		{
			int32 Size = CrossingPointUs.Num();
			ensureCADKernel(Size >= 2);
			CrossingPointUs.SetNum(Size);
			CrossingPointDeltaUMins.Init(SMALL_NUMBER, Size - 1);
			CrossingPointDeltaUMaxs.Init(2.0 * (GetEndCurvilinearCoordinates() - GetStartCurvilinearCoordinates()), Size - 1);
		}

		void ChooseFinalDeltaUs();

		const TArray<double>& GetCrossingPointUs() const
		{
			return CrossingPointUs;
		}

		TArray<double>& GetCrossingPointUs()
		{
			return CrossingPointUs;
		}

		TArray<double>& GetDeltaUMins()
		{
			return CrossingPointDeltaUMins;
		}

		TArray<double>& GetDeltaUMaxs()
		{
			return CrossingPointDeltaUMaxs;
		}

		const TArray<double>& GetDeltaUMaxs() const
		{
			return CrossingPointDeltaUMaxs;
		}

		TArray<FCuttingPoint>& GetCuttingPoints()
		{
			return CuttingPointUs;
		};

		const TArray<FCuttingPoint>& GetCuttingPoints() const
		{
			return CuttingPointUs;
		}

		// For thin zone purpose

		const TArray<FImposedCuttingPoint>& GetImposedCuttingPoints()
		{
			return ImposedCuttingPointUs;
		}

		void AddThinZone(const FLinearBoundary& InThinZoneBounds)
		{
			ThinZoneBounds.Add(InThinZoneBounds);
		}

		const TArray<FLinearBoundary>& GetThinZoneBounds() const
		{
			return ThinZoneBounds;
		}

		void AddImposedCuttingPointU(const double ImposedCuttingPointU, int32 OppositeNodeIndex);


		// ======   Curve Functions   ======

		TSharedRef<FRestrictionCurve> GetCurve() const
		{
			return Curve.ToSharedRef();
		}

		TSharedRef<FRestrictionCurve> GetCurve()
		{
			return Curve.ToSharedRef();
		}

		double Length() const;

		/**
		 * Samples the curve with segments of a desired length
		 */
		void Sample(const double DesiredSegmentLength, TArray<double>& OutCoordinates) const;

		/**
		 * Exact evaluation of point on the 3D curve
		 * According to derivativeOrder Gradient of the point (DerivativeOrder = 1) and Laplacian (DerivativeOrder = 1) can also be return
		 */
		void EvaluatePoint(double InCoordinate, int32 Derivative, FCurvePoint& Point) const
		{
			Curve->EvaluatePoint(InCoordinate, Point, Derivative);
		}

		/**
		 * Exact evaluation of points on the 3D curve
		 * According to derivativeOrder Gradient of the point (DerivativeOrder = 1) and Laplacian (DerivativeOrder = 1) can also be return
		 */
		void EvaluatePoints(const TArray<double>& InCoordinates, int32 DerivativeOrder, TArray<FCurvePoint>& OutPoints) const
		{
			Curve->EvaluatePoints(InCoordinates, OutPoints, DerivativeOrder);
		}

		/**
		 * Approximation of 3D points compute with carrier surface 3D polyline
		 */
		void ApproximatePoints(const TArray<double>& InCoordinates, TArray<FPoint>& OutPoints) const
		{
			Curve->Approximate3DPoints(InCoordinates, OutPoints);
		}

		/**
		 * Approximation of 2D point defined by its coordinate compute with carrier surface 2D polyline
		 */
		FPoint Approximate2DPoint(const double InCoordinate) const
		{
			return Curve->Approximate2DPoint(InCoordinate);
		}

		/**
		 * Approximation of 2D points defined by its coordinates compute with carrier surface 2D polyline
		 */
		void Approximate2DPoints(const TArray<double>& InCoordinates, TArray<FPoint2D>& OutPoints) const	
		{
			Curve->Approximate2DPoints(InCoordinates, OutPoints);
		}

		/**
		 * Approximation of surfacic polyline (points 2d, 3d, normals, tangents) defined by its coordinates compute with carrier surface polyline
		 */
		void ApproximatePolyline(FSurfacicPolyline& Polyline) const
		{
			Curve->ApproximatePolyline(Polyline);
		}

		/**
		 * Project Point on the 3D polyline and return the coordinate of the projected point
		 */
		double ProjectPoint(const FPoint& InPointToProject, FPoint& OutProjectedPoint) const
		{
			return Curve->GetCoordinateOfProjectedPoint(Boundary, InPointToProject, OutProjectedPoint);
		}

		/**
		 * Project a set of points on the 3D polyline and return the coordinate of the projected point
		 */
		void ProjectPoints(const TArray<FPoint>& InPointsToProject, TArray<double>& OutProjectedPointCoords, TArray<FPoint>& OutProjectedPoints) const
		{
			Curve->ProjectPoints(Boundary, InPointsToProject, OutProjectedPointCoords, OutProjectedPoints);
		}

		/**
		 * Project a set of points of a twin edge on the 3D polyline and return the coordinate of the projected point
		 */
		void ProjectTwinEdgePoints(const TArray<FPoint>& InPointsToProject, bool bSameOrientation, TArray<double>& OutProjectedPointCoords) const
		{
			Curve->ProjectTwinCurvePoints(InPointsToProject, bSameOrientation, OutProjectedPointCoords);
		}

		/**
		 * Compute 2D points of the edge coincident the points of the twin edge defined by their coordinates
		 */
		void ProjectTwinEdgePointsOn2DCurve(const TSharedRef<FTopologicalEdge>& InTwinEdge, const TArray<double>& InTwinEdgePointCoords, TArray<FPoint2D>& OutPoints2D);

		/**
		 * Get the discretization points of the edge and add them to the outpoints TArray
		 */
		template<class PointType>
		void GetDiscretization2DPoints(EOrientation Orientation, TArray<PointType>& OutPoints) const
		{
			Curve->GetDiscretizationPoints(Boundary, Orientation, OutPoints);
		}

		double TransformLocalCoordinateToActiveEdgeCoordinate(const double LocalCoordinate);
		double TransformActiveEdgeCoordinateToLocalCoordinate(const double ActiveEdgeCoordinate);

		void TransformActiveEdgeCoordinatesToLocalCoordinates(const TArray<double>& InActiveEdgeCoordinate, TArray<double>& OutLocalCoordinate);
		void TransformLocalCoordinatesToActiveEdgeCoordinates(const TArray<double>& InLocalCoordinate, TArray<double>& OutActiveEdgeCoordinate);

		/**
		 * Compute the edge 2D properties i.e. the mean and standard deviation of the slop of the edge in the parametric space of the carrier surface
		 */
		void ComputeEdge2DProperties(FEdge2DProperties& SlopCharacteristics);

		// ======   State Functions   ======

		/**
		 * Important note: A Degenerated Edge is used to close 2D boundary in case of degenerated surface to ensureCADKernel a closed boundary
		 * Specific process is done for the mesh of this kind of surface
		 */
		virtual bool IsDegenerated() const override
		{
			return FHaveStates::IsDegenerated();
		}

		bool IsThinPeak() const
		{
			return ((States & EHaveStates::ThinPeak) == EHaveStates::ThinPeak);
		}

		virtual void SetThinPeak() const
		{
			States |= EHaveStates::ThinPeak;
		}

		virtual void ResetThinPeak() const
		{
			States &= ~EHaveStates::ThinPeak;
		}

		/**
		 * @return true if the edge is adjacent to only one surface (its carrier surface)
		 */
		bool IsBorder()
		{
			return GetTwinsEntityCount() == 1;
		}

		static TSharedPtr<FTopologicalEdge> CreateEdgeToMerge2Edges(const TArray<TWeakPtr<FTopologicalEdge>>& Edges, const TArray<EOrientation>& edgeDirections);
	};

	struct CADKERNEL_API FEdge2DProperties
	{
		double StandardDeviation = 0;
		double MediumSlop = 0;
		double Length3D = 0;
		EIso IsoType = EIso::UndefinedIso;
		bool bIsMesh = false;
		double MeshedLength = 0;

		void Add(double InSlop, double InLength)
		{
			double Temp = InSlop * InLength;
			MediumSlop += Temp;
			Temp *= InSlop;
			StandardDeviation += Temp;
			Length3D += InLength;
		}

		// Finalize has been done on each Property
		void Add2(FEdge2DProperties& Property)
		{
			StandardDeviation = (FMath::Square(StandardDeviation) + FMath::Square(MediumSlop)) * Length3D + (FMath::Square(Property.StandardDeviation) + FMath::Square(Property.MediumSlop)) * Property.Length3D;
			MediumSlop = MediumSlop * Length3D + Property.MediumSlop * Property.Length3D;
			Length3D += Property.Length3D;

			MediumSlop /= Length3D;
			StandardDeviation /= Length3D;
			StandardDeviation -= FMath::Square(MediumSlop);
			StandardDeviation = sqrt(StandardDeviation);
		}

		// Finalize has not been done on each Property
		void Add(FEdge2DProperties& Property)
		{
			StandardDeviation += Property.StandardDeviation;
			MediumSlop += Property.MediumSlop;
			Length3D += Property.Length3D;
		}

		void Finalize()
		{
			MediumSlop /= Length3D;
			StandardDeviation /= Length3D;
			StandardDeviation -= FMath::Square(MediumSlop);
			if (StandardDeviation < 0)
			{
				StandardDeviation = 0;
			}
			else
			{
				StandardDeviation = sqrt(StandardDeviation);
			}
			IsoType = EIso::UndefinedIso;

			if (MediumSlop < 0.2)
			{
				if (StandardDeviation < 0.1)
				{
					IsoType = EIso::IsoU;
				}
			}
			else if (MediumSlop > 1.8)
			{
				if (StandardDeviation < 0.1)
				{
					IsoType = EIso::IsoV;
				}
			}
		}

	};

	/**
	 * Cutting point used for thin zone purpose
	 */
	struct CADKERNEL_API FImposedCuttingPoint
	{
		/**
		 * coordinate of the edge's mesh nodes
		 */
		double Coordinate;
		int32 OppositNodeIndex = -1;

		FImposedCuttingPoint()
		{
		}

		FImposedCuttingPoint(double InCoordinate, int32 NodeIndex1)
			: Coordinate(InCoordinate)
			, OppositNodeIndex(NodeIndex1)
		{
		};
	};

	/**
	 * Cutting point used for meshing purpose
	 */
	struct CADKERNEL_API FCuttingPoint
	{
		/**
		 * coordinate of the edge's mesh nodes
		 */
		double Coordinate;
		ECoordinateType Type;
		int32 OppositNodeIndex = -1;
		int32 OppositNodeIndex2 = -1;
		double IsoDeltaU;

		FCuttingPoint()
		{
		}

		FCuttingPoint(double InCoordinate, ECoordinateType InType)
			: Coordinate(InCoordinate)
			, Type(InType)
			, OppositNodeIndex(-1)
			, OppositNodeIndex2(-1)
			, IsoDeltaU(HUGE_VAL)
		{
		}

		FCuttingPoint(double InCoordinate, ECoordinateType InType, int32 NodeIndex1, int32 NodeIndex2)
			: Coordinate(InCoordinate)
			, Type(InType)
			, OppositNodeIndex(NodeIndex1)
			, OppositNodeIndex2(NodeIndex2)
			, IsoDeltaU(HUGE_VAL)
		{
		}

		FCuttingPoint(double InCoordinate, ECoordinateType InType, int32 InOppositeNodeId, double DeltaU)
			: Coordinate(InCoordinate)
			, Type(InType)
			, OppositNodeIndex(InOppositeNodeId)
			, OppositNodeIndex2(-1)
			, IsoDeltaU(DeltaU)
		{
		}
	};

	struct CADKERNEL_API FCuttingGrid
	{
		TArray<FCuttingPoint> Coordinates[2];

		constexpr TArray<FCuttingPoint>& operator[](EIso Iso)
		{
			ensureCADKernel(Iso == 0 || Iso == 1);
			return Coordinates[Iso];
		}

		constexpr const TArray<FCuttingPoint>& operator[](EIso Iso) const
		{
			ensureCADKernel(Iso == 0 || Iso == 1);
			return Coordinates[Iso];
		}
	};


	template<typename FCuttingPointType>
	void GetCuttingPointCoordinates(const TArray<FCuttingPointType>& CuttingPoints, TArray<double>& CuttingPointCoordinates)
	{
		CuttingPointCoordinates.Empty(CuttingPoints.Num());
		for (const FCuttingPointType& CuttingPoint : CuttingPoints)
		{
			CuttingPointCoordinates.Add(CuttingPoint.Coordinate);
		}
	};


} // namespace CADKernel
